// test_uct.cpp - tests and first measurements for V0 (sequential UCT).
//   1. Tactics: takes a win in one move, blocks a loss in one move.
//   2. Visit counts: what the search "thinks" about two positions.
//   3. Strength: V0 against a uniformly random player.
//   4. Speed: playouts per second, move latency and tree size.
// Run it from the project root so the results/ files are found.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "arena.hpp"
#include "connect4.hpp"
#include "csv_log.hpp"
#include "game.hpp"
#include "rng.hpp"
#include "stats.hpp"
#include "timing.hpp"
#include "uct_v0.hpp"

using Searcher = UctSearcher<Connect4>;

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

// Searches `position` once per seed (1..seeds) with a fixed number of
// iterations; returns how many times the search chose `expected`.
static int count_choices(const Connect4& position, int expected, int seeds,
                         uint64_t iterations) {
    int hits = 0;
    for (int s = 1; s <= seeds; ++s) {
        Searcher searcher(static_cast<uint64_t>(s), 1u << 20);
        Searcher::Limit limit;
        limit.iterations = iterations;
        if (searcher.search(position, limit) == expected) ++hits;
    }
    return hits;
}

static void tactic_case(const char* name, const Connect4& position, int expected, int seeds,
                        uint64_t iterations) {
    const int hits = count_choices(position, expected, seeds, iterations);
    std::printf("  %-34s expected column %d, chosen %2d / %d times  %s\n", name, expected, hits,
                seeds, hits == seeds ? "PASS" : "FAIL");
    CHECK(hits == seeds);
}

static void print_root_stats(const Searcher& s) {
    std::printf("  column | visits | win rate for the side to move\n");
    for (const auto& c : s.root_stats())
        std::printf("  %6d | %6d | %5.1f%%\n", c.move, c.visits, 100.0 * c.win_rate);
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n", info.compiler.c_str(),
                info.git_commit.c_str());

    // ---------------- 1. tactics ----------------
    const int kSeeds = 20;
    const uint64_t kTacticIters = 20000;
    std::printf("\n== 1. Tactics (%d seeds, %llu iterations per search) ==\n", kSeeds,
                static_cast<unsigned long long>(kTacticIters));
    tactic_case("win in 1 (first player, column)", play_cols({0, 1, 0, 1, 0, 1}), 0, kSeeds,
                kTacticIters);
    tactic_case("win in 1 (second player, column)", play_cols({0, 1, 0, 1, 0, 1, 6}), 1, kSeeds,
                kTacticIters);
    tactic_case("block loss in 1 (second player)", play_cols({0, 1, 0, 1, 0}), 0, kSeeds,
                kTacticIters);
    tactic_case("block loss in 1 (first player)", play_cols({2, 0, 4, 0, 6, 0}), 0, kSeeds,
                kTacticIters);

    // ---------------- 2. what the search thinks ----------------
    std::printf("\n== 2. Root visit counts ==\n");
    {
        std::printf("Position: second player must block column 0 (seed 1, %llu iterations)\n",
                    static_cast<unsigned long long>(kTacticIters));
        Searcher s(1, 1u << 20);
        Searcher::Limit limit;
        limit.iterations = kTacticIters;
        s.search(play_cols({0, 1, 0, 1, 0}), limit);
        print_root_stats(s);
    }
    {
        std::printf("\nPosition: empty board (seed 1, 0.1 s)\n");
        Searcher s(1);
        Searcher::Limit limit;
        limit.seconds = 0.1;
        s.search(Connect4(), limit);
        std::printf("  iterations: %llu, tree nodes: %zu\n",
                    static_cast<unsigned long long>(s.iterations()), s.nodes_used());
        print_root_stats(s);
    }

    // ---------------- 3. strength vs a random player ----------------
    const int kGames = 100;
    const double kMoveTime = 0.02;
    std::printf("\n== 3. V0 vs a uniformly random player ==\n");
    std::printf("Settings: game=Connect-4 6x7, games=%d (colours alternate), time per move=%.2f s "
                "for V0, V0 seed=2024, random seed=777, threads=1\n\n",
                kGames, kMoveTime);

    Searcher v0(2024);
    Rng random_rng(777);
    MoveFn<Connect4> v0_move = [&](const Connect4& s) {
        Searcher::Limit limit;
        limit.seconds = kMoveTime;
        return v0.search(s, limit);
    };
    MoveFn<Connect4> random_move = [&](const Connect4& s) {
        Connect4::Move moves[Connect4::kMaxMoves];
        const int n = s.legal_moves(moves);
        return moves[random_rng.next_below(static_cast<uint32_t>(n))];
    };

    int wins = 0, draws = 0, losses = 0;
    for (int g = 0; g < kGames; ++g) {
        const bool v0_first = (g % 2 == 0);
        double score;  // from V0's point of view
        if (v0_first) score = play_game<Connect4>(v0_move, random_move);
        else score = 1.0 - play_game<Connect4>(random_move, v0_move);
        if (score == 1.0) ++wins;
        else if (score == 0.5) ++draws;
        else ++losses;
    }
    const double win_rate = (wins + 0.5 * draws) / kGames;  // a draw counts as half a win
    double ci_lo = 0.0, ci_hi = 1.0;
    wilson_ci95(win_rate, kGames, ci_lo, ci_hi);

    std::printf("| Pairing | Games | Wins | Draws | Losses | Win rate | 95%% CI | Time per move | Threads |\n");
    std::printf("|---------|-------|------|-------|--------|----------|--------|---------------|---------|\n");
    std::printf("| V0 vs Random | %5d | %4d | %5d | %6d | %7.1f%% | %.1f%% - %.1f%% | %9.2f s | %7d |\n",
                kGames, wins, draws, losses, 100.0 * win_rate, 100.0 * ci_lo, 100.0 * ci_hi,
                kMoveTime, 1);
    std::printf("(win rate = (wins + 0.5 * draws) / games; CI = Wilson interval, approximate "
                "because draws count as half)\n");
    CHECK(win_rate >= 0.95);
    std::printf("Target: win rate >= 95%%  ->  %s\n", win_rate >= 0.95 ? "PASS" : "FAIL");

    append_csv("results/v0_vs_random.csv", info,
               {"pairing", "games", "wins", "draws", "losses", "win_rate", "ci95_low", "ci95_high",
                "time_per_move_s", "threads", "v0_seed", "random_seed"},
               {"V0 vs Random", std::to_string(kGames), std::to_string(wins),
                std::to_string(draws), std::to_string(losses), fmt(win_rate, 4), fmt(ci_lo, 4),
                fmt(ci_hi, 4), fmt(kMoveTime, 3), "1", "2024", "777"});

    // ---------------- 4. speed ----------------
    const int kRepeats = 20;
    const double kBudget = 0.1;
    const uint64_t kBaseSeed = 5000;
    std::printf("\n== 4. V0 speed ==\n");
    std::printf("Settings: game=Connect-4 6x7, position=empty board, time per move=%.2f s, "
                "repeats=%d (plus 1 discarded warm-up), base seed=%llu, threads=1\n\n",
                kBudget, kRepeats, static_cast<unsigned long long>(kBaseSeed));

    Searcher bench(1);
    {   // warm-up search, result discarded
        Searcher::Limit limit;
        limit.seconds = kBudget;
        bench.search(Connect4(), limit);
    }
    std::vector<double> rates, latencies_ms, nodes;
    bool any_pool_full = false;
    for (int r = 0; r < kRepeats; ++r) {
        bench.reseed(kBaseSeed + static_cast<uint64_t>(r));
        Searcher::Limit limit;
        limit.seconds = kBudget;
        bench.search(Connect4(), limit);
        rates.push_back(static_cast<double>(bench.iterations()) / bench.elapsed_sec());
        latencies_ms.push_back(bench.elapsed_sec() * 1000.0);
        nodes.push_back(static_cast<double>(bench.nodes_used()));
        any_pool_full = any_pool_full || bench.pool_full();
    }
    double rate_mean = 0.0, rate_sd = 0.0, lat_mean = 0.0, lat_sd = 0.0, nodes_mean = 0.0,
           nodes_sd = 0.0;
    mean_std(rates, rate_mean, rate_sd);
    mean_std(latencies_ms, lat_mean, lat_sd);
    mean_std(nodes, nodes_mean, nodes_sd);
    const double lat_p95 = percentile(latencies_ms, 95.0);

    std::printf("| Version | Threads | Playouts/sec (mean +/- std) | Speedup vs 1 thread | Efficiency | Latency mean ms | Latency p95 ms | Tree nodes (mean) | Lost updates |\n");
    std::printf("|---------|---------|-----------------------------|---------------------|------------|-----------------|----------------|-------------------|--------------|\n");
    std::printf("| V0 sequential UCT | %d | %9.0f +/- %-9.0f | %19.2f | %10.2f | %15.2f | %14.2f | %17.0f | %12s |\n",
                1, rate_mean, rate_sd, 1.0, 1.0, lat_mean, lat_p95, nodes_mean, "n/a");
    std::printf("(V0 is the 1-thread baseline, so its speedup and efficiency are 1 by definition.)\n");
    std::printf("Node pool exhausted in any repeat: %s\n", any_pool_full ? "YES (tree stopped growing)" : "no");
    std::printf("All repeats, playouts/sec:");
    for (double x : rates) std::printf(" %.0f", x);
    std::printf("\n");

    append_csv("results/v0_speed.csv", info,
               {"version", "threads", "time_per_move_s", "repeats", "base_seed",
                "playouts_per_sec_mean", "playouts_per_sec_std", "speedup_vs_1", "efficiency",
                "latency_mean_ms", "latency_p95_ms", "tree_nodes_mean", "lost_updates",
                "pool_exhausted"},
               {"V0", "1", fmt(kBudget, 3), std::to_string(kRepeats), std::to_string(kBaseSeed),
                fmt(rate_mean, 0), fmt(rate_sd, 0), "1.00", "1.00", fmt(lat_mean, 2),
                fmt(lat_p95, 2), fmt(nodes_mean, 0), "n/a", any_pool_full ? "yes" : "no"});

    // ---------------- verdict ----------------
    std::printf("\nRESULT: %s (%d failed checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
