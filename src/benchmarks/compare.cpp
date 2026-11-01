// compare.cpp - the final comparison: does adaptive virtual loss (V5) play
// better than fixed virtual loss (V4 + fixed)?
//
//   compare tune  <threads> <games>
//       Plays every candidate against the sequential V0: fixed virtual loss
//       with v = 1, 2, 3, 5 and adaptive virtual loss with lambda = 0.5, 1, 2, 4.
//       This is the tuning run: it uses its own random openings, different from
//       the final comparison, so the final games are never used for tuning.
//
//   compare final <threads> <games> <lambda> <fixed_v>
//       With the settings frozen, plays V5 against V4 + fixed head to head,
//       and each of them against V0.
//
// Every player gets the same wall-clock time per move (0.1 s). V0 always uses
// one thread. Colours alternate and every pair of games shares a random opening.
// Rows are printed as they finish and appended to results/compare_*.csv.
// Run it from the project root. Long runs: start with a small game count.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "arena.hpp"
#include "connect4.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_lockfree.hpp"
#include "uct_v0.hpp"
#include "virtual_loss.hpp"

using V0 = UctSearcher<Connect4>;
using V4 = LockFreeTreeUct<Connect4>;

static const double kC = 1.4142135623730951;  // UCT exploration constant, same for everyone
static const double kBudget = 0.1;            // seconds per move, same for everyone
static const int kOpeningPlies = 4;

static MoveFn<Connect4> make_v0(uint64_t seed) {
    auto searcher = std::make_shared<V0>(seed);
    return [searcher](const Connect4& position) {
        V0::Limit limit;
        limit.seconds = kBudget;
        return searcher->search(position, limit);
    };
}

// V4 with a virtual-loss rule: None = plain V4, Fixed = V4 + fixed, Adaptive = V5.
static MoveFn<Connect4> make_v4(int threads, uint64_t seed, VirtualLoss vl) {
    auto searcher = std::make_shared<V4>(threads, seed, true, 1u << 24, kC, vl);
    return [searcher](const Connect4& position) {
        V4::Limit limit;
        limit.seconds = kBudget;
        return searcher->search(position, limit);
    };
}

struct Outcome {
    MatchResult m;
    double lo = 0.0;
    double hi = 1.0;
};

static void print_header() {
    std::printf("| Pairing | Games | Wins | Draws | Losses | Win rate | 95%% CI | Time per move | Threads |\n");
    std::printf("|---------|-------|------|-------|--------|----------|--------|---------------|---------|\n");
}

// Plays the match, prints one table row and appends it to the CSV file.
static Outcome play_and_report(const char* mode, const std::string& pairing,
                               const MoveFn<Connect4>& a, const MoveFn<Connect4>& b, int games,
                               uint64_t opening_seed, int threads, const RunInfo& info,
                               const char* csv_path) {
    Outcome o;
    o.m = play_match<Connect4>(a, b, games, kOpeningPlies, opening_seed, pairing.c_str());
    wilson_ci95(o.m.score(), o.m.games(), o.lo, o.hi);
    std::printf("| %-52s | %5d | %4d | %5d | %6d | %7.1f%% | %5.1f%% - %5.1f%% | %9.2f s | %7d |\n",
                pairing.c_str(), o.m.games(), o.m.wins, o.m.draws, o.m.losses, 100.0 * o.m.score(),
                100.0 * o.lo, 100.0 * o.hi, kBudget, threads);
    std::fflush(stdout);
    append_csv(csv_path, info,
               {"mode", "pairing", "games", "wins", "draws", "losses", "win_rate", "ci95_low",
                "ci95_high", "time_per_move_s", "threads", "opening_seed"},
               {mode, pairing, std::to_string(o.m.games()), std::to_string(o.m.wins),
                std::to_string(o.m.draws), std::to_string(o.m.losses), fmt(o.m.score(), 4),
                fmt(o.lo, 4), fmt(o.hi, 4), fmt(kBudget, 3), std::to_string(threads),
                std::to_string(opening_seed)});
    return o;
}

struct Candidate {
    std::string label;
    VirtualLoss vl;
    bool adaptive;
};

static void run_tune(int threads, int games, const RunInfo& info) {
    const uint64_t kOpeningSeed = 100000;  // tuning openings; the final run uses different ones
    std::vector<Candidate> candidates;
    for (int v : {1, 2, 3, 5})
        candidates.push_back({"V4 + fixed virtual loss v=" + std::to_string(v), VirtualLoss::Fixed(v), false});
    for (double lambda : {0.5, 1.0, 2.0, 4.0})
        candidates.push_back({"V5 adaptive virtual loss lambda=" + fmt(lambda, 1),
                              VirtualLoss::Adaptive(lambda), true});

    std::printf("TUNING RUN (not the final result). Settings: game=Connect-4 6x7, candidates with %d "
                "threads against V0 (1 thread), %d games per candidate (colours alternate, %d random "
                "opening plies shared by each pair), time per move=%.2f s, opening seed=%llu\n\n",
                threads, games, kOpeningPlies, kBudget, static_cast<unsigned long long>(kOpeningSeed));
    print_header();

    const MoveFn<Connect4> v0 = make_v0(4000);
    double best_fixed = -1.0, best_adaptive = -1.0;
    std::string best_fixed_label, best_adaptive_label;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        const MoveFn<Connect4> player = make_v4(threads, 500 + i, c.vl);
        const Outcome o = play_and_report("tune", c.label + " vs V0", player, v0, games, kOpeningSeed,
                                          threads, info, "results/compare_tune.csv");
        double& best = c.adaptive ? best_adaptive : best_fixed;
        std::string& best_label = c.adaptive ? best_adaptive_label : best_fixed_label;
        if (o.m.score() > best) {
            best = o.m.score();
            best_label = c.label;
        }
    }
    std::printf("\nHighest score among the fixed settings:    %s (%.1f%%)\n", best_fixed_label.c_str(), 100.0 * best_fixed);
    std::printf("Highest score among the adaptive settings: %s (%.1f%%)\n", best_adaptive_label.c_str(), 100.0 * best_adaptive);
    std::printf("With %d games per candidate the intervals are wide, so a 'best' can be noise. Check the "
                "CIs before freezing a setting.\n", games);
}

static void run_final(int threads, int games, double lambda, int fixed_v, const RunInfo& info) {
    const uint64_t kOpeningSeed = 1;  // different from the tuning openings
    const MoveFn<Connect4> v5 = make_v4(threads, 900, VirtualLoss::Adaptive(lambda));
    const MoveFn<Connect4> v4_fixed = make_v4(threads, 901, VirtualLoss::Fixed(fixed_v));
    const MoveFn<Connect4> v0 = make_v0(4100);
    const std::string v5_name = "V5 adaptive (lambda=" + fmt(lambda, 1) + ")";
    const std::string v4_name = "V4 + fixed (v=" + std::to_string(fixed_v) + ")";

    std::printf("FINAL COMPARISON with frozen settings: lambda=%.2f, fixed v=%d. Settings: game=Connect-4 "
                "6x7, %d threads for V4/V5 (V0: 1 thread), %d games per pairing (colours alternate, %d "
                "random opening plies shared by each pair), time per move=%.2f s, opening seed=%llu\n\n",
                lambda, fixed_v, threads, games, kOpeningPlies, kBudget,
                static_cast<unsigned long long>(kOpeningSeed));
    print_header();

    const Outcome head = play_and_report("final", v5_name + " vs " + v4_name, v5, v4_fixed, games,
                                         kOpeningSeed, threads, info, "results/compare_final.csv");
    play_and_report("final", v5_name + " vs V0", v5, v0, games, kOpeningSeed, threads, info,
                    "results/compare_final.csv");
    play_and_report("final", v4_name + " vs V0", v4_fixed, v0, games, kOpeningSeed, threads, info,
                    "results/compare_final.csv");

    std::printf("\nHead to head, V5 against V4 + fixed: %.1f%% (95%% CI %.1f%% - %.1f%%). ", 100.0 * head.m.score(),
                100.0 * head.lo, 100.0 * head.hi);
    if (head.lo > 0.5) std::printf("The interval is above 50%%: V5 is better in this setup.\n");
    else if (head.hi < 0.5) std::printf("The interval is below 50%%: V5 is worse in this setup.\n");
    else std::printf("The interval contains 50%%: no detectable difference at this number of games.\n");
}

int main(int argc, char** argv) {
    const bool tune = (argc >= 4 && std::strcmp(argv[1], "tune") == 0);
    const bool final_run = (argc >= 6 && std::strcmp(argv[1], "final") == 0);
    if (!tune && !final_run) {
        std::printf("usage: compare tune  <threads> <games>\n"
                    "       compare final <threads> <games> <lambda> <fixed_v>\n");
        return 1;
    }
    const int threads = std::atoi(argv[2]);
    int games = std::atoi(argv[3]);
    if (games < 2) games = 2;
    if (games % 2 != 0) ++games;  // colours must balance

    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(), info.logical_cpus,
                info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n\n", info.compiler.c_str(), info.git_commit.c_str());

    if (tune) run_tune(threads, games, info);
    else run_final(threads, games, std::atof(argv[4]), std::atoi(argv[5]), info);
    return 0;
}
