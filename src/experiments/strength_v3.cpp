// strength_v3.cpp - does V3 get as much STRONGER as it gets FASTER?
//
// usage: strength_v3 [games=100] [thread counts...=2 4 8]
//
// For each thread count T it plays two matches, V3 (T threads, 0.1 s per
// move) against the sequential V0:
//   A. equal time:      V0 also gets 0.1 s per move.
//   B. equal playouts:  V0 gets as much time as it needs to make the same
//                       number of playouts as V3 makes in 0.1 s, i.e.
//                       0.1 s * (V3 playouts/sec at T threads) / (V0 playouts/sec).
// If V3's playouts were exactly as useful as V0's, B would come out around
// 50%. A clearly lower score means V3 wastes work (search overhead).
//
// Colours alternate and every pair of games shares a random opening.
// Each match is logged to results/strength_v3.csv as soon as it finishes.
// Run it from the project root. It takes a while: try "strength_v3 20" first.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "arena.hpp"
#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_locked.hpp"
#include "uct_v0.hpp"

using V0 = UctSearcher<Connect4>;
using V3 = LockedTreeUct<Connect4>;

// Mean playouts/sec over `repeats` timed searches of `seconds` each (after one warm-up).
template <class RunOnce>
static double mean_rate(int repeats, RunOnce run_once) {
    run_once(999);
    std::vector<double> rates;
    for (int r = 0; r < repeats; ++r) rates.push_back(run_once(static_cast<uint64_t>(r + 1)));
    double mean = 0.0, sd = 0.0;
    mean_std(rates, mean, sd);
    return mean;
}

int main(int argc, char** argv) {
    int games = (argc > 1) ? std::atoi(argv[1]) : 100;
    if (games < 2) games = 2;
    if (games % 2 != 0) ++games;  // colours must balance
    std::vector<int> thread_counts;
    for (int i = 2; i < argc; ++i) thread_counts.push_back(std::atoi(argv[i]));
    if (thread_counts.empty()) thread_counts = {2, 4, 8};

    const double kBudget = 0.1;
    const int kOpeningPlies = 4;
    const uint64_t kOpeningSeed = 1;
    const uint64_t kV0Seed = 4000;
    const uint64_t kV3Seed = 3000;

    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n\n", info.compiler.c_str(), info.git_commit.c_str());
    std::printf("Settings: game=Connect-4 6x7, games per match=%d (colours alternate, %d random "
                "opening plies shared by each pair), V3 time per move=%.2f s, V0 seed=%llu, "
                "V3 seed=%llu+T, opening seed=%llu\n\n",
                games, kOpeningPlies, kBudget, static_cast<unsigned long long>(kV0Seed),
                static_cast<unsigned long long>(kV3Seed),
                static_cast<unsigned long long>(kOpeningSeed));

    // ---- Phase 1: playouts/sec of V0 and of V3 at each thread count ----
    V0::Limit speed_limit;
    speed_limit.seconds = kBudget;
    double v0_rate = 0.0;
    {
        V0 s(1);
        v0_rate = mean_rate(5, [&](uint64_t seed) {
            s.reseed(seed);
            s.search(Connect4(), speed_limit);
            return static_cast<double>(s.iterations()) / s.elapsed_sec();
        });
    }
    std::vector<double> v3_rate;
    for (int T : thread_counts) {
        V3 s(T, 1);
        v3_rate.push_back(mean_rate(5, [&](uint64_t seed) {
            s.reseed(seed);
            s.search(Connect4(), speed_limit);
            return static_cast<double>(s.iterations()) / s.elapsed_sec();
        }));
    }
    std::printf("Playouts/sec measured now (5 repeats each, 0.1 s): V0 = %.0f\n", v0_rate);
    for (std::size_t i = 0; i < thread_counts.size(); ++i)
        std::printf("  V3 with %d threads = %.0f (%.2f x V0)\n", thread_counts[i], v3_rate[i],
                    v3_rate[i] / v0_rate);

    // ---- Phase 2: the matches ----
    std::printf("\n| Pairing | Games | Wins | Draws | Losses | Win rate | 95%% CI | Time per move | Threads |\n");
    std::printf("|---------|-------|------|-------|--------|----------|--------|---------------|---------|\n");

    auto run_pairing = [&](const char* kind, int T, double v0_time) {
        V3 v3(T, kV3Seed + static_cast<uint64_t>(T));
        V0 v0(kV0Seed);
        MoveFn<Connect4> v3_move = [&](const Connect4& s) {
            V3::Limit limit;
            limit.seconds = kBudget;
            return v3.search(s, limit);
        };
        MoveFn<Connect4> v0_move = [&](const Connect4& s) {
            V0::Limit limit;
            limit.seconds = v0_time;
            return v0.search(s, limit);
        };
        char label[64];
        std::snprintf(label, sizeof label, "%s, V3 with %d threads", kind, T);
        const MatchResult m = play_match<Connect4>(v3_move, v0_move, games, kOpeningPlies,
                                                   kOpeningSeed, label);
        double lo = 0.0, hi = 1.0;
        wilson_ci95(m.score(), m.games(), lo, hi);

        char pairing[96], times[64];
        std::snprintf(pairing, sizeof pairing, "V3 vs V0 (%s)", kind);
        std::snprintf(times, sizeof times, "V3 %.2f s / V0 %.2f s", kBudget, v0_time);
        std::printf("| %-26s | %5d | %4d | %5d | %6d | %7.1f%% | %5.1f%% - %5.1f%% | %-21s | %7d |\n",
                    pairing, m.games(), m.wins, m.draws, m.losses, 100.0 * m.score(), 100.0 * lo,
                    100.0 * hi, times, T);
        std::fflush(stdout);

        append_csv("results/strength_v3.csv", info,
                   {"pairing", "v3_threads", "v3_time_s", "v0_time_s", "games", "wins", "draws",
                    "losses", "win_rate", "ci95_low", "ci95_high", "opening_plies", "v3_seed",
                    "v0_seed", "opening_seed"},
                   {pairing, std::to_string(T), fmt(kBudget, 3), fmt(v0_time, 3),
                    std::to_string(m.games()), std::to_string(m.wins), std::to_string(m.draws),
                    std::to_string(m.losses), fmt(m.score(), 4), fmt(lo, 4), fmt(hi, 4),
                    std::to_string(kOpeningPlies), std::to_string(kV3Seed + static_cast<uint64_t>(T)),
                    std::to_string(kV0Seed), std::to_string(kOpeningSeed)});
    };

    for (std::size_t i = 0; i < thread_counts.size(); ++i) {
        run_pairing("equal time", thread_counts[i], kBudget);
        run_pairing("equal playouts", thread_counts[i], kBudget * v3_rate[i] / v0_rate);
    }

    std::printf("\n(win rate = V3's (wins + 0.5 * draws) / games; CI = Wilson interval, "
                "approximate because draws count as half)\n");
    return 0;
}
