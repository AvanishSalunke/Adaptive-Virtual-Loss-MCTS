// compare.cpp - the final comparison: adaptive virtual loss (V5) against
// fixed virtual loss (V4), the non-adaptive baseline.
//
//   compare <threads> <games> <lambda> <fixed_v>
//
// Both players are the same parallel search tree with the same number of
// threads and the same wall-clock time per move (0.1 s); the only difference
// is the virtual-loss rule:
//   V5  adaptive: penalty v = max(1, round(lambda * T / sqrt(n + 1)))
//   V4  fixed:    the same penalty v everywhere
// Colours alternate, and every pair of games shares a random opening, so
// neither side gets a lucky start. The program prints V5's win rate with a 95%
// confidence interval and says whether the two can be told apart.
// Run it from this folder; the row is also appended to results/compare_final.csv.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "arena.hpp"
#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_lockfree.hpp"
#include "virtual_loss.hpp"

using Tree = LockFreeTreeUct<Connect4>;

static const double kC = 1.4142135623730951;  // UCT exploration constant, same for both players
static const double kBudget = 0.1;            // seconds per move, same for both players
static const int kOpeningPlies = 4;
static const uint64_t kOpeningSeed = 1;

// One player: a parallel search tree with the given virtual-loss rule.
static MoveFn<Connect4> make_player(int threads, uint64_t seed, VirtualLoss rule) {
    auto tree = std::make_shared<Tree>(threads, seed, true, 1u << 24, kC, rule);
    return [tree](const Connect4& position) {
        Tree::Limit limit;
        limit.seconds = kBudget;
        return tree->search(position, limit);
    };
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: compare <threads> <games> <lambda> <fixed_v>\n"
                    "example: compare 8 20 0.5 5\n");
        return 1;
    }
    const int threads = std::atoi(argv[1]);
    int games = std::atoi(argv[2]);
    const double lambda = std::atof(argv[3]);
    const int fixed_v = std::atoi(argv[4]);
    if (games < 2) games = 2;
    if (games % 2 != 0) ++games;  // colours must balance

    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(), info.logical_cpus,
                info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n\n", info.compiler.c_str(), info.git_commit.c_str());

    const std::string pairing = "V5 adaptive (lambda=" + fmt(lambda, 1) + ") vs V4 fixed (v=" +
                                std::to_string(fixed_v) + ")";
    std::printf("Settings: game=Connect-4 6x7, %d threads each, %d games (colours alternate, %d random opening "
                "plies shared by each pair), time per move=%.2f s, opening seed=%llu\n\n",
                threads, games, kOpeningPlies, kBudget, static_cast<unsigned long long>(kOpeningSeed));

    const MoveFn<Connect4> v5 = make_player(threads, 900, VirtualLoss::Adaptive(lambda));
    const MoveFn<Connect4> v4 = make_player(threads, 901, VirtualLoss::Fixed(fixed_v));
    const MatchResult m = play_match<Connect4>(v5, v4, games, kOpeningPlies, kOpeningSeed, "playing");

    double lo = 0.0, hi = 1.0;
    wilson_ci95(m.score(), m.games(), lo, hi);
    std::printf("| Pairing | Games | Wins | Draws | Losses | Win rate | 95%% CI | Time per move | Threads |\n");
    std::printf("|---------|-------|------|-------|--------|----------|--------|---------------|---------|\n");
    std::printf("| %-44s | %5d | %4d | %5d | %6d | %7.1f%% | %5.1f%% - %5.1f%% | %9.2f s | %7d |\n",
                pairing.c_str(), m.games(), m.wins, m.draws, m.losses, 100.0 * m.score(), 100.0 * lo,
                100.0 * hi, kBudget, threads);
    std::printf("\n(Win rate is V5's: (wins + 0.5 * draws) / games.)\n");

    if (lo > 0.5) std::printf("Result: the interval is above 50%%, so adaptive virtual loss is better in this test.\n");
    else if (hi < 0.5) std::printf("Result: the interval is below 50%%, so adaptive virtual loss is worse in this test.\n");
    else std::printf("Result: the interval contains 50%%, so no difference between adaptive and fixed can be "
                     "detected at this number of games.\n");

    append_csv("results/compare_final.csv", info,
               {"pairing", "games", "wins", "draws", "losses", "win_rate", "ci95_low", "ci95_high",
                "time_per_move_s", "threads", "opening_seed"},
               {pairing, std::to_string(m.games()), std::to_string(m.wins), std::to_string(m.draws),
                std::to_string(m.losses), fmt(m.score(), 4), fmt(lo, 4), fmt(hi, 4), fmt(kBudget, 3),
                std::to_string(threads), std::to_string(kOpeningSeed)});
    return 0;
}
