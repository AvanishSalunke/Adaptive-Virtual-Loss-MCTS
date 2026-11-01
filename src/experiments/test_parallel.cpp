// test_parallel.cpp - tests and speed measurements for V1 (leaf parallel)
// and V2 (root parallel), compared with V0.
//   1. Tactics: each parallel version still takes wins and blocks losses.
//   2. Root parallelism up close: each thread's tree and the merged totals.
//   3. Speed: playouts per second for 1, 2, 4, 8 threads.
// Run it from the project root so the results/ files are found.

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_leaf_parallel.hpp"
#include "uct_root_parallel.hpp"
#include "uct_v0.hpp"

using V0 = UctSearcher<Connect4>;
using V1 = LeafParallelUct<Connect4>;
using V2 = RootParallelUct<Connect4>;

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

// ---------------- tactics ----------------
struct Tactic {
    const char* name;
    Connect4 position;
    int expected;
};

// Runs a fresh search for seeds 1..seeds; counts how often `expected` is chosen.
template <class SearchFn>
static void tactic_case(const Tactic& t, int seeds, SearchFn search_with_seed) {
    int hits = 0;
    for (int s = 1; s <= seeds; ++s)
        if (search_with_seed(t.position, static_cast<uint64_t>(s)) == t.expected) ++hits;
    std::printf("  %-34s expected column %d, chosen %2d / %d times  %s\n", t.name, t.expected,
                hits, seeds, hits == seeds ? "PASS" : "FAIL");
    CHECK(hits == seeds);
}

// ---------------- speed measurement ----------------
struct Sample {
    double playouts;
    double seconds;
    double nodes;
    bool pool_full;
};

struct SpeedRow {
    std::string version;
    int threads = 0;
    double rate_mean = 0.0, rate_sd = 0.0;
    double lat_mean = 0.0, lat_p95 = 0.0;
    double nodes_mean = 0.0;
    bool pool_full = false;
};

// run_once(seed) performs one timed search and returns its Sample.
template <class RunOnce>
static SpeedRow measure(const std::string& version, int threads, int repeats, uint64_t base_seed,
                        RunOnce run_once) {
    run_once(base_seed + 999);  // warm-up, discarded
    std::vector<double> rates, lat_ms, nodes;
    bool full = false;
    for (int r = 0; r < repeats; ++r) {
        const Sample s = run_once(base_seed + static_cast<uint64_t>(r));
        rates.push_back(s.playouts / s.seconds);
        lat_ms.push_back(s.seconds * 1000.0);
        nodes.push_back(s.nodes);
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
    row.pool_full = full;
    return row;
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n", info.compiler.c_str(),
                info.git_commit.c_str());

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
    std::printf("V1 leaf parallel:\n");
    for (const Tactic& t : tactics)
        tactic_case(t, kSeeds, [&](const Connect4& pos, uint64_t seed) {
            V1 s(kTacticThreads, seed, 1, 1u << 20);
            V0::Limit limit;
            limit.seconds = kTacticTime;
            return s.search(pos, limit);
        });
    std::printf("V2 root parallel:\n");
    for (const Tactic& t : tactics)
        tactic_case(t, kSeeds, [&](const Connect4& pos, uint64_t seed) {
            V2 s(kTacticThreads, seed, 1u << 20);
            V0::Limit limit;
            limit.seconds = kTacticTime;
            return s.search(pos, limit);
        });

    // ---------------- 2. root parallelism up close ----------------
    std::printf("\n== 2. V2 up close: empty board, 4 threads, 0.1 s, seed 1 ==\n");
    {
        V2 s(4, 1);
        V0::Limit limit;
        limit.seconds = 0.1;
        const int move = s.search(Connect4(), limit);
        std::printf("Root visits per move in each thread's own tree, and the merged total:\n");
        std::printf("  column | tree 0 | tree 1 | tree 2 | tree 3 |  total\n");
        for (std::size_t i = 0; i < s.merged_moves().size(); ++i) {
            std::printf("  %6d |", s.merged_moves()[i]);
            for (int t = 0; t < s.threads(); ++t)
                std::printf(" %6d |", s.tree_root_stats(t)[i].visits);
            std::printf(" %7lld\n", s.merged_visits()[i]);
        }
        std::printf("Chosen column: %d | playouts in all trees: %llu | nodes in all trees: %zu\n",
                    move, static_cast<unsigned long long>(s.iterations()), s.nodes_used());
    }

    // ---------------- 3. speed ----------------
    const int kRepeats = 10;
    const double kBudget = 0.1;
    const uint64_t kBaseSeed = 7000;
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
                          static_cast<double>(s.nodes_used()), s.pool_full()};
        }));
    }
    for (int T : kThreadCounts) {
        V1 s(T, 1, 1);
        rows.push_back(measure("V1 leaf (1 playout/thread)", T, kRepeats, kBaseSeed,
                               [&](uint64_t seed) {
                                   s.reseed(seed);
                                   s.search(Connect4(), limit);
                                   return Sample{static_cast<double>(s.playouts()), s.elapsed_sec(),
                                                 static_cast<double>(s.nodes_used()), s.pool_full()};
                               }));
    }
    for (int T : kThreadCounts) {
        V1 s(T, 1, 16);
        rows.push_back(measure("V1 leaf (16 playouts/thread)", T, kRepeats, kBaseSeed,
                               [&](uint64_t seed) {
                                   s.reseed(seed);
                                   s.search(Connect4(), limit);
                                   return Sample{static_cast<double>(s.playouts()), s.elapsed_sec(),
                                                 static_cast<double>(s.nodes_used()), s.pool_full()};
                               }));
    }
    for (int T : kThreadCounts) {
        V2 s(T, 1);
        rows.push_back(measure("V2 root parallel", T, kRepeats, kBaseSeed, [&](uint64_t seed) {
            s.reseed(seed);
            s.search(Connect4(), limit);
            return Sample{static_cast<double>(s.iterations()), s.elapsed_sec(),
                          static_cast<double>(s.nodes_used()), s.pool_full()};
        }));
    }

    // Speedup and efficiency are measured against the SAME version at 1 thread.
    auto base_rate = [&](const std::string& version) {
        for (const SpeedRow& r : rows)
            if (r.version == version && r.threads == 1) return r.rate_mean;
        return 1.0;
    };
    const double v0_rate = rows[0].rate_mean;

    std::printf("| Version | Threads | Playouts/sec (mean +/- std) | Speedup vs 1 thread | Efficiency | Latency mean ms | Latency p95 ms | Tree nodes (mean) | Lost updates |\n");
    std::printf("|---------|---------|-----------------------------|---------------------|------------|-----------------|----------------|-------------------|--------------|\n");
    bool any_pool_full = false;
    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        std::printf("| %-28s | %7d | %9.0f +/- %-9.0f | %19.2f | %10.2f | %15.2f | %14.2f | %17.0f | %12s |\n",
                    r.version.c_str(), r.threads, r.rate_mean, r.rate_sd, speedup,
                    speedup / r.threads, r.lat_mean, r.lat_p95, r.nodes_mean, "n/a");
        any_pool_full = any_pool_full || r.pool_full;
    }
    std::printf("(Speedup and efficiency compare each version with ITS OWN 1-thread rate. "
                "V1 and V2 tree nodes are the total over all threads.)\n");
    std::printf("Node pool exhausted in any run: %s\n", any_pool_full ? "YES (a tree stopped growing)" : "no");

    std::printf("\nPlayouts/sec relative to V0 (1 thread), %.0f per second:\n", v0_rate);
    std::printf("| Version | Threads | Rate relative to V0 |\n|---------|---------|---------------------|\n");
    for (const SpeedRow& r : rows)
        std::printf("| %-28s | %7d | %19.2f |\n", r.version.c_str(), r.threads, r.rate_mean / v0_rate);

    for (const SpeedRow& r : rows) {
        const double speedup = r.rate_mean / base_rate(r.version);
        append_csv("results/speed_v0_v1_v2.csv", info,
                   {"version", "threads", "time_per_move_s", "repeats", "base_seed",
                    "playouts_per_sec_mean", "playouts_per_sec_std", "speedup_vs_1",
                    "efficiency", "latency_mean_ms", "latency_p95_ms", "tree_nodes_mean",
                    "lost_updates", "pool_exhausted"},
                   {r.version, std::to_string(r.threads), fmt(kBudget, 3),
                    std::to_string(kRepeats), std::to_string(kBaseSeed), fmt(r.rate_mean, 0),
                    fmt(r.rate_sd, 0), fmt(speedup, 3), fmt(speedup / r.threads, 3),
                    fmt(r.lat_mean, 2), fmt(r.lat_p95, 2), fmt(r.nodes_mean, 0), "n/a",
                    r.pool_full ? "yes" : "no"});
    }

    std::printf("\nRESULT: %s (%d failed checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
