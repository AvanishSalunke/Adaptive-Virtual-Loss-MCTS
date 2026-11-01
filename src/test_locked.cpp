// test_locked.cpp - tests and speed measurements for V3 (shared tree with locks).
//   1. Tactics: V3 still takes wins and blocks losses.
//   2. Lock correctness: no lost updates, and a tiny node pool does not crash it.
//   3. Speed for 1, 2, 4, 8 threads, plus how often threads found a lock taken.
// Run it from the project root so the results/ files are found.

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_locked.hpp"
#include "uct_v0.hpp"

using V0 = UctSearcher<Connect4>;
using V3 = LockedTreeUct<Connect4>;

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

struct Tactic {
    const char* name;
    Connect4 position;
    int expected;
};

struct Sample {
    double playouts;
    double seconds;
    double nodes;
    bool pool_full;
    double lost;
    double iterations;
    double lock_acquires;
    double lock_waits;
};

struct SpeedRow {
    std::string version;
    int threads = 0;
    double rate_mean = 0.0, rate_sd = 0.0;
    double lat_mean = 0.0, lat_p95 = 0.0;
    double nodes_mean = 0.0;
    bool pool_full = false;
    double lost_max = 0.0;
    double acquires_per_iter = 0.0;
    double contended_pct = 0.0;
};

template <class RunOnce>
static SpeedRow measure(const std::string& version, int threads, int repeats, uint64_t base_seed,
                        RunOnce run_once) {
    run_once(base_seed + 999);  // warm-up, discarded
    std::vector<double> rates, lat_ms, nodes;
    bool full = false;
    double lost_max = 0.0, sum_iter = 0.0, sum_acq = 0.0, sum_wait = 0.0;
    for (int r = 0; r < repeats; ++r) {
        const Sample s = run_once(base_seed + static_cast<uint64_t>(r));
        rates.push_back(s.playouts / s.seconds);
        lat_ms.push_back(s.seconds * 1000.0);
        nodes.push_back(s.nodes);
        full = full || s.pool_full;
        if (s.lost > lost_max) lost_max = s.lost;
        sum_iter += s.iterations;
        sum_acq += s.lock_acquires;
        sum_wait += s.lock_waits;
    }
    SpeedRow row;
    row.version = version;
    row.threads = threads;
    double sd = 0.0;
    mean_std(rates, row.rate_mean, row.rate_sd);
    mean_std(lat_ms, row.lat_mean, sd);
    mean_std(nodes, row.nodes_mean, sd);
    row.lat_p95 = percentile(lat_ms, 95.0);
    row.pool_full = full;
    row.lost_max = lost_max;
    row.acquires_per_iter = sum_iter > 0.0 ? sum_acq / sum_iter : 0.0;
    row.contended_pct = sum_acq > 0.0 ? 100.0 * sum_wait / sum_acq : 0.0;
    return row;
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n", info.compiler.c_str(), info.git_commit.c_str());

    // ---------------- 1. tactics ----------------
    const int kSeeds = 10;
    const int kTacticThreads = 4;
    const double kTacticTime = 0.05;
    std::printf("\n== 1. Tactics (%d seeds, %d threads, %.2f s per search) ==\n", kSeeds,
                kTacticThreads, kTacticTime);
    const Tactic tactics[] = {
        {"win in 1 (first player)", play_cols({0, 1, 0, 1, 0, 1}), 0},
        {"win in 1 (second player)", play_cols({0, 1, 0, 1, 0, 1, 6}), 1},
        {"block loss in 1 (second player)", play_cols({0, 1, 0, 1, 0}), 0},
        {"block loss in 1 (first player)", play_cols({2, 0, 4, 0, 6, 0}), 0},
    };
    for (const Tactic& t : tactics) {
        int hits = 0;
        for (int s = 1; s <= kSeeds; ++s) {
            V3 searcher(kTacticThreads, static_cast<uint64_t>(s), 1u << 20);
            V0::Limit limit;
            limit.seconds = kTacticTime;
            if (searcher.search(t.position, limit) == t.expected) ++hits;
        }
        std::printf("  %-34s expected column %d, chosen %2d / %d times  %s\n", t.name, t.expected,
                    hits, kSeeds, hits == kSeeds ? "PASS" : "FAIL");
        CHECK(hits == kSeeds);
    }

    // ---------------- 2. lock correctness ----------------
    std::printf("\n== 2. Lock correctness ==\n");
    {
        V3 s(8, 1, 1u << 22);
        V0::Limit limit;
        limit.iterations = 200000;
        s.search(Connect4(), limit);
        std::printf("  8 threads, 200000 iterations: iterations done = %llu, lost updates = %lld\n",
                    static_cast<unsigned long long>(s.iterations()), s.lost_updates());
        CHECK(s.iterations() == 200000);
        CHECK(s.lost_updates() == 0);
    }
    {
        V3 s(8, 2, 4096);  // far too small: the tree must stop growing without crashing
        V0::Limit limit;
        limit.iterations = 100000;
        s.search(Connect4(), limit);
        std::printf("  tiny pool (4096 nodes), 8 threads, 100000 iterations: pool full = %s, "
                    "lost updates = %lld\n",
                    s.pool_full() ? "yes" : "no", s.lost_updates());
        CHECK(s.pool_full());
        CHECK(s.lost_updates() == 0);
    }

    // ---------------- 3. speed ----------------
    const int kRepeats = 10;
    const double kBudget = 0.1;
    const uint64_t kBaseSeed = 9000;
    const int kThreadCounts[] = {1, 2, 4, 8};
    std::printf("\n== 3. Speed ==\n");
    std::printf("Settings: game=Connect-4 6x7, position=empty board, time per move=%.2f s, "
                "repeats=%d (plus 1 discarded warm-up), base seed=%llu\n\n",
                kBudget, kRepeats, static_cast<unsigned long long>(kBaseSeed));

    V0::Limit limit;
    limit.seconds = kBudget;
    std::vector<SpeedRow> rows;
    {
        V0 s(1);
        rows.push_back(measure("V0 sequential UCT", 1, kRepeats, kBaseSeed, [&](uint64_t seed) {
            s.reseed(seed);
            s.search(Connect4(), limit);
            return Sample{static_cast<double>(s.iterations()), s.elapsed_sec(),
                          static_cast<double>(s.nodes_used()), s.pool_full(), 0.0,
                          static_cast<double>(s.iterations()), 0.0, 0.0};
        }));
    }
    for (int T : kThreadCounts) {
        V3 s(T, 1);
        rows.push_back(measure("V3 shared tree + locks", T, kRepeats, kBaseSeed, [&](uint64_t seed) {
            s.reseed(seed);
            s.search(Connect4(), limit);
            return Sample{static_cast<double>(s.iterations()), s.elapsed_sec(),
                          static_cast<double>(s.nodes_used()), s.pool_full(),
                          static_cast<double>(s.lost_updates()),
                          static_cast<double>(s.iterations()),
                          static_cast<double>(s.lock_acquires()),
                          static_cast<double>(s.lock_waits())};
        }));
    }

    auto base_rate = [&](const std::string& version) {
        for (const SpeedRow& r : rows)
            if (r.version == version && r.threads == 1) return r.rate_mean;
        return 1.0;
    };
    const double v0_rate = rows[0].rate_mean;

    std::printf("| Version | Threads | Playouts/sec (mean +/- std) | Speedup vs 1 thread | Efficiency | Latency mean ms | Latency p95 ms | Tree nodes (mean) | Lost updates (max) |\n");
    std::printf("|---------|---------|-----------------------------|---------------------|------------|-----------------|----------------|-------------------|--------------------|\n");
    bool any_pool_full = false;
    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        char lost[32];
        if (r.version == "V0 sequential UCT") std::snprintf(lost, sizeof lost, "n/a");
        else std::snprintf(lost, sizeof lost, "%.0f", r.lost_max);
        std::printf("| %-22s | %7d | %9.0f +/- %-9.0f | %19.2f | %10.2f | %15.2f | %14.2f | %17.0f | %18s |\n",
                    r.version.c_str(), r.threads, r.rate_mean, r.rate_sd, speedup,
                    speedup / r.threads, r.lat_mean, r.lat_p95, r.nodes_mean, lost);
        any_pool_full = any_pool_full || r.pool_full;
    }
    std::printf("(Speedup and efficiency compare each version with ITS OWN 1-thread rate. "
                "Locks should keep lost updates at 0; the column is a check, not a result.)\n");
    std::printf("Node pool exhausted in any run: %s\n", any_pool_full ? "YES (the tree stopped growing)" : "no");

    std::printf("\nPlayouts/sec relative to V0 (1 thread), %.0f per second:\n", v0_rate);
    std::printf("| Version | Threads | Rate relative to V0 |\n|---------|---------|---------------------|\n");
    for (const SpeedRow& r : rows)
        std::printf("| %-22s | %7d | %19.2f |\n", r.version.c_str(), r.threads, r.rate_mean / v0_rate);

    std::printf("\nLock contention in V3 (how often a thread found the lock already taken):\n");
    std::printf("| Threads | Lock acquisitions per iteration | Contended acquisitions |\n");
    std::printf("|---------|---------------------------------|------------------------|\n");
    for (const SpeedRow& r : rows)
        if (r.version != "V0 sequential UCT")
            std::printf("| %7d | %31.2f | %21.2f%% |\n", r.threads, r.acquires_per_iter, r.contended_pct);

    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        const bool is_v0 = (r.version == "V0 sequential UCT");
        append_csv("results/speed_v3.csv", info,
                   {"version", "threads", "time_per_move_s", "repeats", "base_seed",
                    "playouts_per_sec_mean", "playouts_per_sec_std", "speedup_vs_1", "efficiency",
                    "latency_mean_ms", "latency_p95_ms", "tree_nodes_mean", "lost_updates_max",
                    "lock_acquires_per_iter", "contended_pct", "pool_exhausted"},
                   {r.version, std::to_string(r.threads), fmt(kBudget, 3), std::to_string(kRepeats),
                    std::to_string(kBaseSeed), fmt(r.rate_mean, 0), fmt(r.rate_sd, 0),
                    fmt(speedup, 3), fmt(speedup / r.threads, 3), fmt(r.lat_mean, 2),
                    fmt(r.lat_p95, 2), fmt(r.nodes_mean, 0), is_v0 ? "n/a" : fmt(r.lost_max, 0),
                    is_v0 ? "n/a" : fmt(r.acquires_per_iter, 3),
                    is_v0 ? "n/a" : fmt(r.contended_pct, 3), r.pool_full ? "yes" : "no"});
    }

    std::printf("\nRESULT: %s (%d failed checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
