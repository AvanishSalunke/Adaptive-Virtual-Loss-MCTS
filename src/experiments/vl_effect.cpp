// vl_effect.cpp - does virtual loss help? Plain parallel search against the same
// search with fixed virtual loss, and against the same search with adaptive
// virtual loss.
//
//   vl_effect <threads> <games> <lambda> <fixed_v>
//
// All three players are the same parallel search tree with the same number of
// threads and the same wall-clock time per move (0.1 s). Only the virtual-loss
// rule differs:
//   plain     no virtual loss
//   fixed     the same penalty v everywhere
//   adaptive  penalty v = max(1, round(lambda * T / sqrt(n + 1)))
// Each version plays the PLAIN parallel search. Colours alternate and every
// pair of games shares a random opening, so neither side gets a lucky start.
//
// The deciding factor printed with the result is the LOWER END of the 95%
// confidence interval of the version's win rate:
//   above 50%             -> BETTER than plain parallel search
//   interval contains 50% -> no detectable difference
//   upper end below 50%   -> WORSE than plain parallel search
//
// Run it from the project root; the rows are also appended to results/vl_effect.csv.

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

static const double kC = 1.4142135623730951;  // UCT exploration constant, same for every player
static const double kBudget = 0.1;            // seconds per move, same for every player
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

enum class Verdict { Better, Same, Worse };

// The deciding factor: where the 95% interval lies relative to 50%.
static Verdict judge(double lo, double hi) {
    if (lo > 0.5) return Verdict::Better;
    if (hi < 0.5) return Verdict::Worse;
    return Verdict::Same;
}

static const char* verdict_text(Verdict v) {
    switch (v) {
        case Verdict::Better: return "BETTER than plain parallel";
        case Verdict::Worse: return "WORSE than plain parallel";
        case Verdict::Same: break;
    }
    return "no detectable difference";
}

struct Row {
    std::string name;
    MatchResult m;
    double lo = 0.0;
    double hi = 1.0;
    Verdict verdict = Verdict::Same;
};

static Row play_versus_plain(const std::string& name, const MoveFn<Connect4>& version,
                             const MoveFn<Connect4>& plain, int games) {
    Row r;
    r.name = name;
    r.m = play_match<Connect4>(version, plain, games, kOpeningPlies, kOpeningSeed, name.c_str());
    wilson_ci95(r.m.score(), r.m.games(), r.lo, r.hi);
    r.verdict = judge(r.lo, r.hi);
    return r;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: vl_effect <threads> <games> <lambda> <fixed_v>\n"
                    "example: vl_effect 8 20 0.5 5\n");
        return 1;
    }
    const int threads = std::atoi(argv[1]);
    int games = std::atoi(argv[2]);
    const double lambda = std::atof(argv[3]);
    const int fixed_v = std::atoi(argv[4]);
    if (games < 2) games = 2;
    if (games % 2 != 0) ++games;  // colours must balance

    const MoveFn<Connect4> plain = make_player(threads, 902, VirtualLoss::None());
    const MoveFn<Connect4> fixed = make_player(threads, 901, VirtualLoss::Fixed(fixed_v));
    const MoveFn<Connect4> adaptive = make_player(threads, 900, VirtualLoss::Adaptive(lambda));

    const Row fixed_row = play_versus_plain("Fixed virtual loss (v=" + std::to_string(fixed_v) + ")", fixed, plain, games);
    const Row adaptive_row = play_versus_plain("Adaptive virtual loss (lambda=" + fmt(lambda, 1) + ")", adaptive, plain, games);

    std::printf("Does virtual loss help? Parallel search with and without virtual loss, Connect-4\n");
    std::printf("%d threads each, %.1f s per move, %d games per match, colours alternate, openings shared by each pair of games.\n",
                threads, kBudget, games);
    std::printf("Each version below plays the PLAIN parallel search (the same search with no virtual loss).\n\n");
    std::printf("| Version (plays the plain parallel search) | Wins | Draws | Losses | Win rate | Edge vs plain | 95%% interval    | Verdict                    |\n");
    std::printf("|-------------------------------------------|------|-------|--------|----------|---------------|-----------------|----------------------------|\n");
    for (const Row* r : {&fixed_row, &adaptive_row})
        std::printf("| %-41s | %4d | %5d | %6d | %7.1f%% | %+9.1f pts | %5.1f%% - %5.1f%% | %-26s |\n", r->name.c_str(),
                    r->m.wins, r->m.draws, r->m.losses, 100.0 * r->m.score(), 100.0 * (r->m.score() - 0.5),
                    100.0 * r->lo, 100.0 * r->hi, verdict_text(r->verdict));

    std::printf("\nHow to read the table\n");
    std::printf("  Win rate = (wins + 0.5 x draws) / games for the version in that row; plain parallel gets the rest.\n");
    std::printf("  Edge vs plain = win rate minus 50%%: how far ahead (+) or behind (-) plain parallel search.\n");
    std::printf("  DECIDING FACTOR = the LOWER END of the 95%% interval:\n");
    std::printf("      lower end above 50%%        -> BETTER than plain parallel\n");
    std::printf("      interval contains 50%%      -> no detectable difference\n");
    std::printf("      upper end below 50%%        -> WORSE than plain parallel\n");

    const bool fb = fixed_row.verdict == Verdict::Better, ab = adaptive_row.verdict == Verdict::Better;
    const bool fw = fixed_row.verdict == Verdict::Worse, aw = adaptive_row.verdict == Verdict::Worse;
    std::printf("\nConclusion: ");
    if (fb && ab) std::printf("both virtual-loss versions beat plain parallel search in this test, so virtual loss helps.\n");
    else if (!fb && !ab && !fw && !aw) std::printf("neither virtual-loss version can be told apart from plain parallel search at this number of games.\n");
    else if (fw || aw) std::printf("at least one virtual-loss version is WORSE than plain parallel search in this test; see the rows above.\n");
    else if (fb) std::printf("only fixed virtual loss beats plain parallel search; adaptive cannot be told apart from it.\n");
    else std::printf("only adaptive virtual loss beats plain parallel search; fixed cannot be told apart from it.\n");
    std::printf("(To compare adaptive with fixed directly, use the head-to-head comparison in 'project files'.)\n");

    const RunInfo info = collect_run_info();
    for (const Row* r : {&fixed_row, &adaptive_row})
        append_csv("results/vl_effect.csv", info,
                   {"version", "opponent", "games", "wins", "draws", "losses", "win_rate", "edge_points",
                    "ci95_low", "ci95_high", "verdict", "time_per_move_s", "threads", "opening_seed"},
                   {r->name, "plain parallel", std::to_string(r->m.games()), std::to_string(r->m.wins),
                    std::to_string(r->m.draws), std::to_string(r->m.losses), fmt(r->m.score(), 4),
                    fmt(100.0 * (r->m.score() - 0.5), 2), fmt(r->lo, 4), fmt(r->hi, 4),
                    verdict_text(r->verdict), fmt(kBudget, 3), std::to_string(threads),
                    std::to_string(kOpeningSeed)});
    return 0;
}
