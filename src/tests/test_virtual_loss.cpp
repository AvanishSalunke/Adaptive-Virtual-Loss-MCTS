// test_virtual_loss.cpp - virtual loss: the penalty rule, a worked example,
// correctness checks, and its effect on duplicated work in the lock-free tree.
//   1. The adaptive penalty for several thread counts, lambdas and visit counts.
//   2. Two children of one node, with and without a virtual loss.
//   3. Correctness: tactics, and every temporary penalty is removed again.
//   4. Speed and duplicated work for: no penalty, fixed penalties, adaptive.
// Run it from the project root so the results/ files are found.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_lockfree.hpp"
#include "uct_v0.hpp"
#include "virtual_loss.hpp"

using V0 = UctSearcher<Connect4>;
using V4 = LockFreeTreeUct<Connect4>;

static const double kC = 1.4142135623730951;  // the UCT exploration constant used everywhere

static int g_failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            ++g_failures;                                            \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);  \
        }                                                            \
    } while (0)

static Connect4 play_cols(std::initializer_list<int> cols) {
    Connect4 g;
    for (int c : cols) g.play(c);
    return g;
}

// ---------------- 1. the penalty rule ----------------
static void print_penalty_table(int threads) {
    const double lambdas[] = {0.5, 1.0, 2.0, 4.0};
    const int visit_counts[] = {0, 1, 3, 8, 15, 24, 63, 99, 999};
    std::printf("T = %d threads: v = max(1, round(lambda * T / sqrt(n + 1)))\n", threads);
    std::printf("| n (visits) | lambda = 0.5 | lambda = 1 | lambda = 2 | lambda = 4 |\n");
    std::printf("|------------|--------------|------------|------------|------------|\n");
    for (int n : visit_counts) {
        std::printf("| %10d |", n);
        for (double lambda : lambdas)
            std::printf(" %*d |", lambda == 0.5 ? 12 : 10, virtual_loss_penalty(VirtualLoss::Adaptive(lambda), threads, n));
        std::printf("\n");
    }
}

// ---------------- 2. worked example ----------------
static double uct_score(double wins, double visits, double parent_visits) {
    return wins / visits + kC * std::sqrt(std::log(parent_visits) / visits);
}

static void worked_example() {
    std::printf("One node, two children, C = %.3f. A: 60 visits, 39 wins. B: 40 visits, 22 wins.\n", kC);
    std::printf("A thread has just chosen A; a second thread arrives before the first has finished.\n\n");
    std::printf("| Situation for the second thread | A visits | A average | A score | B score | Second thread picks |\n");
    std::printf("|---------------------------------|----------|-----------|---------|---------|---------------------|\n");
    struct Row {
        const char* label;
        int penalty;
    };
    const Row rows[] = {{"no virtual loss", 0}, {"virtual loss 1 on A", 1}, {"virtual loss 3 on A", 3}};
    for (const Row& r : rows) {
        const double na = 60.0 + r.penalty;       // the pretend games count as visits with no wins
        const double parent = na + 40.0;
        const double a = uct_score(39.0, na, parent);
        const double b = uct_score(22.0, 40.0, parent);
        std::printf("| %-31s | %8.0f | %9.4f | %7.4f | %7.4f | %19s |\n", r.label, na, 39.0 / na, a, b,
                    a >= b ? "A (same as thread 1)" : "B");
    }
}

// ---------------- 4. speed and duplicated work ----------------
struct Tactic {
    const char* name;
    Connect4 position;
    int expected;
};

struct Config {
    std::string label;
    VirtualLoss vl;
};

struct Sample {
    double playouts;
    double seconds;
    double nodes;
    double wasted;
    double iterations;
    double cas_failures;
    double lost;
    bool pool_full;
};

struct SpeedRow {
    std::string version;
    int threads = 0;
    double rate_mean = 0.0, rate_sd = 0.0;
    double lat_mean = 0.0, lat_p95 = 0.0;
    double nodes_mean = 0.0;
    double lost_max = 0.0;
    double failed_pct = 0.0;       // failed expansions as % of iterations
    double wasted_pct = 0.0;       // wasted slots as % of allocated slots
    double useful_per_iter = 0.0;  // nodes that stayed in the tree per iteration
    bool pool_full = false;
};

template <class RunOnce>
static SpeedRow measure(const std::string& version, int threads, int repeats, uint64_t base_seed,
                        RunOnce run_once) {
    run_once(base_seed + 999);  // warm-up, discarded
    std::vector<double> rates, lat_ms, nodes;
    double sum_iter = 0.0, sum_cas = 0.0, sum_wasted = 0.0, sum_nodes = 0.0, lost_max = 0.0;
    bool full = false;
    for (int r = 0; r < repeats; ++r) {
        const Sample s = run_once(base_seed + static_cast<uint64_t>(r));
        rates.push_back(s.playouts / s.seconds);
        lat_ms.push_back(s.seconds * 1000.0);
        nodes.push_back(s.nodes);
        sum_iter += s.iterations;
        sum_cas += s.cas_failures;
        sum_wasted += s.wasted;
        sum_nodes += s.nodes;
        if (s.lost > lost_max) lost_max = s.lost;
        full = full || s.pool_full;
    }
    SpeedRow row;
    row.version = version;
    row.threads = threads;
    double sd = 0.0;
    mean_std(rates, row.rate_mean, row.rate_sd);
    mean_std(lat_ms, row.lat_mean, sd);
    mean_std(nodes, row.nodes_mean, sd);
    row.lat_p95 = percentile(lat_ms, 95.0);
    row.lost_max = lost_max;
    row.failed_pct = sum_iter > 0.0 ? 100.0 * sum_cas / sum_iter : 0.0;
    row.wasted_pct = sum_nodes > 0.0 ? 100.0 * sum_wasted / sum_nodes : 0.0;
    row.useful_per_iter = sum_iter > 0.0 ? (sum_nodes - sum_wasted) / sum_iter : 0.0;
    row.pool_full = full;
    return row;
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n", info.compiler.c_str(), info.git_commit.c_str());

    std::printf("\n== 1. The penalty rule ==\n");
    print_penalty_table(4);
    std::printf("\n");
    print_penalty_table(8);

    std::printf("\n== 2. Why a penalty changes the next thread's choice ==\n");
    worked_example();

    // ---------------- 3. correctness ----------------
    const Config configs[] = {
        {"V4 (no virtual loss)", VirtualLoss::None()},
        {"V4 + fixed virtual loss (v=1)", VirtualLoss::Fixed(1)},
        {"V4 + fixed virtual loss (v=3)", VirtualLoss::Fixed(3)},
        {"V5 adaptive virtual loss (lambda=1, untuned)", VirtualLoss::Adaptive(1.0)},
    };

    std::printf("\n== 3. Correctness ==\n");
    const int kSeeds = 10;
    const double kTacticTime = 0.05;
    const Tactic tactics[] = {
        {"win in 1 (first player)", play_cols({0, 1, 0, 1, 0, 1}), 0},
        {"win in 1 (second player)", play_cols({0, 1, 0, 1, 0, 1, 6}), 1},
        {"block loss in 1 (second player)", play_cols({0, 1, 0, 1, 0}), 0},
        {"block loss in 1 (first player)", play_cols({2, 0, 4, 0, 6, 0}), 0},
    };
    for (const Config& cfg : {configs[2], configs[3]}) {
        std::printf("Tactics for %s (%d seeds, 4 threads, %.2f s):\n", cfg.label.c_str(), kSeeds,
                    kTacticTime);
        for (const Tactic& t : tactics) {
            int hits = 0;
            for (int s = 1; s <= kSeeds; ++s) {
                V4 searcher(4, static_cast<uint64_t>(s), true, 1u << 20, kC, cfg.vl);
                V0::Limit limit;
                limit.seconds = kTacticTime;
                if (searcher.search(t.position, limit) == t.expected) ++hits;
            }
            std::printf("  %-34s expected column %d, chosen %2d / %d times  %s\n", t.name, t.expected,
                        hits, kSeeds, hits == kSeeds ? "PASS" : "FAIL");
            CHECK(hits == kSeeds);
        }
    }
    std::printf("Every temporary penalty must be removed again: the root children's visits must add up\n"
                "to the iteration count exactly (lost updates = 0), also with a tiny node pool.\n");
    for (const Config& cfg : configs) {
        V4 s(8, 1, true, 1u << 22, kC, cfg.vl);
        V0::Limit limit;
        limit.iterations = 200000;
        s.search(Connect4(), limit);
        V4 tiny(8, 2, true, 4096, kC, cfg.vl);
        V0::Limit small_limit;
        small_limit.iterations = 100000;
        tiny.search(Connect4(), small_limit);
        std::printf("  %-46s 8 threads: lost updates = %lld | tiny pool: lost updates = %lld, pool full = %s\n",
                    cfg.label.c_str(), s.lost_updates(), tiny.lost_updates(),
                    tiny.pool_full() ? "yes" : "no");
        CHECK(s.iterations() == 200000);
        CHECK(s.lost_updates() == 0);
        CHECK(tiny.lost_updates() == 0);
        CHECK(tiny.pool_full());
    }

    // ---------------- 4. speed and duplicated work ----------------
    const int kRepeats = 10;
    const double kBudget = 0.1;
    const uint64_t kBaseSeed = 13000;
    const int kThreadCounts[] = {1, 2, 4, 8};
    std::printf("\n== 4. Speed and duplicated work ==\n");
    std::printf("Settings: game=Connect-4 6x7, position=empty board, time per move=%.2f s, "
                "repeats=%d (plus 1 discarded warm-up), base seed=%llu\n"
                "The adaptive lambda=1 is a placeholder, NOT tuned. Playing strength is not measured here.\n\n",
                kBudget, kRepeats, static_cast<unsigned long long>(kBaseSeed));

    V0::Limit limit;
    limit.seconds = kBudget;
    std::vector<SpeedRow> rows;
    for (const Config& cfg : configs) {
        for (int T : kThreadCounts) {
            V4 s(T, 1, true, 1u << 24, kC, cfg.vl);
            rows.push_back(measure(cfg.label, T, kRepeats, kBaseSeed, [&](uint64_t seed) {
                s.reseed(seed);
                s.search(Connect4(), limit);
                return Sample{static_cast<double>(s.iterations()), s.elapsed_sec(),
                              static_cast<double>(s.nodes_used()),
                              static_cast<double>(s.wasted_nodes()),
                              static_cast<double>(s.iterations()),
                              static_cast<double>(s.cas_failures()),
                              static_cast<double>(s.lost_updates()), s.pool_full()};
            }));
        }
    }

    auto base_rate = [&](const std::string& version) {
        for (const SpeedRow& r : rows)
            if (r.version == version && r.threads == 1) return r.rate_mean;
        return 1.0;
    };

    std::printf("| Version | Threads | Playouts/sec (mean +/- std) | Speedup vs 1 thread | Efficiency | Latency mean ms | Latency p95 ms | Tree nodes (mean) | Lost updates (max) |\n");
    std::printf("|---------|---------|-----------------------------|---------------------|------------|-----------------|----------------|-------------------|--------------------|\n");
    bool any_pool_full = false;
    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        std::printf("| %-44s | %7d | %9.0f +/- %-9.0f | %19.2f | %10.2f | %15.2f | %14.2f | %17.0f | %18.0f |\n",
                    r.version.c_str(), r.threads, r.rate_mean, r.rate_sd, speedup, speedup / r.threads,
                    r.lat_mean, r.lat_p95, r.nodes_mean, r.lost_max);
        any_pool_full = any_pool_full || r.pool_full;
    }
    std::printf("(Speedup and efficiency compare each version with ITS OWN 1-thread rate.)\n");
    std::printf("Node pool exhausted in any run: %s\n", any_pool_full ? "YES (the tree stopped growing)" : "no");

    std::printf("\nDuplicated work (smaller failed/wasted percentages and a larger 'useful nodes per iteration' mean threads spread out more):\n");
    std::printf("| Version | Threads | Failed expansions, %% of iterations | Wasted slots, %% of allocated | Useful nodes per iteration |\n");
    std::printf("|---------|---------|-----------------------------------|-----------------------------|----------------------------|\n");
    for (const SpeedRow& r : rows)
        std::printf("| %-44s | %7d | %33.2f | %27.2f | %26.2f |\n", r.version.c_str(), r.threads,
                    r.failed_pct, r.wasted_pct, r.useful_per_iter);

    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        append_csv("results/virtual_loss_speed.csv", info,
                   {"version", "threads", "time_per_move_s", "repeats", "base_seed",
                    "playouts_per_sec_mean", "playouts_per_sec_std", "speedup_vs_1", "efficiency",
                    "latency_mean_ms", "latency_p95_ms", "tree_nodes_mean", "lost_updates_max",
                    "failed_expansions_pct", "wasted_slots_pct", "useful_nodes_per_iteration",
                    "pool_exhausted"},
                   {r.version, std::to_string(r.threads), fmt(kBudget, 3), std::to_string(kRepeats),
                    std::to_string(kBaseSeed), fmt(r.rate_mean, 0), fmt(r.rate_sd, 0),
                    fmt(speedup, 3), fmt(speedup / r.threads, 3), fmt(r.lat_mean, 2),
                    fmt(r.lat_p95, 2), fmt(r.nodes_mean, 0), fmt(r.lost_max, 0),
                    fmt(r.failed_pct, 3), fmt(r.wasted_pct, 3), fmt(r.useful_per_iter, 3),
                    r.pool_full ? "yes" : "no"});
    }

    std::printf("\nRESULT: %s (%d failed checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
