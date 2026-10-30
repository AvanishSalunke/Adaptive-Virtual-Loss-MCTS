// test_connect4.cpp - checks that the Connect-4 rules are implemented
// correctly and measures how fast random games run on one thread.
// Run it from the project root so the results/ files are found.
//
//   1. Known positions: hand-built wins in every direction, a full column.
//   2. One sample random game, printed.
//   3. 1000 random games, each move checked against a slow, obviously
//      correct reference implementation of the rules (a 2D array).
//   4. Speed of random games (random playouts per second), 5 repeats.

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "connect4.hpp"
#include "csv_log.hpp"
#include "game.hpp"
#include "rng.hpp"
#include "stats.hpp"
#include "timing.hpp"

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

// Plays `setup` (game must still be running), then `last`, and checks that
// player `winner` has just won.
static void expect_win(const char* name, std::initializer_list<int> setup, int last,
                       int winner) {
    const int before = g_failures;
    Connect4 g = play_cols(setup);
    CHECK(!g.is_over());
    g.play(last);
    CHECK(g.is_over());
    CHECK(g.won());
    CHECK(g.result(winner) == 1.0);
    CHECK(g.result(1 - winner) == 0.0);
    if (g_failures != before) std::printf("  ^ in case: %s\n", name);
}

static void test_known_positions() {
    const int before = g_failures;

    expect_win("vertical, first player", {0, 1, 0, 1, 0, 1}, 0, 0);
    expect_win("horizontal, first player", {0, 0, 1, 1, 2, 2}, 3, 0);
    expect_win("diagonal up-right, first player", {0, 1, 2, 3, 1, 2, 3, 3, 2, 6}, 3, 0);
    expect_win("diagonal down-right, first player", {6, 5, 4, 3, 5, 4, 3, 3, 4, 0}, 3, 0);
    expect_win("vertical, second player", {0, 1, 0, 1, 0, 1, 6}, 1, 1);

    {   // a full column stops being a legal move (and nobody has won yet)
        Connect4 g = play_cols({0, 0, 0, 0, 0, 0});
        Connect4::Move moves[Connect4::kMaxMoves];
        const int n = g.legal_moves(moves);
        CHECK(!g.is_over());
        CHECK(n == 6);
        bool has_col0 = false;
        for (int i = 0; i < n; ++i) has_col0 = has_col0 || (moves[i] == 0);
        CHECK(!has_col0);
    }

    std::printf("  %s\n", g_failures == before ? "PASS" : "FAIL");
}

// Slow reference: does `who` have four in a row anywhere on the grid?
static bool naive_four(const int grid[Connect4::kRows][Connect4::kCols], int who) {
    static const int dr[4] = {0, 1, 1, 1};   // right, up, up-right, up-left
    static const int dc[4] = {1, 0, 1, -1};
    for (int r = 0; r < Connect4::kRows; ++r)
        for (int c = 0; c < Connect4::kCols; ++c)
            for (int d = 0; d < 4; ++d) {
                int k = 0;
                for (; k < 4; ++k) {
                    const int rr = r + k * dr[d];
                    const int cc = c + k * dc[d];
                    if (rr < 0 || rr >= Connect4::kRows || cc < 0 || cc >= Connect4::kCols ||
                        grid[rr][cc] != who)
                        break;
                }
                if (k == 4) return true;
            }
    return false;
}

struct Outcomes {
    int games = 0;
    int p1_wins = 0;   // first player (X)
    int p2_wins = 0;   // second player (O)
    int draws = 0;
    int min_moves = 1000;
    int max_moves = 0;
    double total_moves = 0.0;
    int rule_mismatches = 0;  // times the bitboard disagreed with the reference
};

static Outcomes verify_random_games(int games, uint64_t seed) {
    Rng rng(seed);
    Outcomes o;
    o.games = games;
    for (int i = 0; i < games; ++i) {
        Connect4 g;
        int grid[Connect4::kRows][Connect4::kCols];
        for (auto& row : grid)
            for (int& cell : row) cell = -1;
        int height[Connect4::kCols] = {0};

        while (!g.is_over()) {
            Connect4::Move moves[Connect4::kMaxMoves];
            const int n = g.legal_moves(moves);

            int expected = 0;  // columns the reference says are not full
            for (int c = 0; c < Connect4::kCols; ++c)
                if (height[c] < Connect4::kRows) ++expected;
            if (n != expected || n == 0) {
                ++o.rule_mismatches;
                break;
            }

            const int col = moves[rng.next_below(static_cast<uint32_t>(n))];
            if (height[col] >= Connect4::kRows) {
                ++o.rule_mismatches;
                break;
            }
            const int mover = g.current_player();
            g.play(col);
            grid[height[col]++][col] = mover;
            if (naive_four(grid, mover) != g.won()) ++o.rule_mismatches;
        }

        if (!g.is_over()) continue;  // broke out because of a mismatch
        const int moves_played = g.moves_played();
        if (g.result(0) + g.result(1) != 1.0) ++o.rule_mismatches;
        if (moves_played < 7 || moves_played > 42) ++o.rule_mismatches;  // earliest win: move 7

        if (g.result(0) == 1.0) ++o.p1_wins;
        else if (g.result(1) == 1.0) ++o.p2_wins;
        else ++o.draws;
        if (moves_played < o.min_moves) o.min_moves = moves_played;
        if (moves_played > o.max_moves) o.max_moves = moves_played;
        o.total_moves += moves_played;
    }
    return o;
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n", info.compiler.c_str(),
                info.git_commit.c_str());

    // ---- 1. known positions ----
    std::printf("\n== 1. Known-position rule tests ==\n");
    test_known_positions();

    // ---- 2. one sample game ----
    std::printf("\n== 2. Sample random game (seed 7), final board ==\n");
    {
        Rng rng(7);
        const Connect4 end = play_random_to_end(Connect4(), rng);
        std::printf("%s", end.to_string().c_str());
        std::printf("moves played: %d, result for first player (X): %.1f\n", end.moves_played(),
                    end.result(0));
    }

    // ---- 3. 1000 random games, checked against the reference ----
    const int kGames = 1000;
    const uint64_t kSeed = 42;
    std::printf("\n== 3. %d random games checked against a slow reference ==\n", kGames);
    std::printf("Settings: game=Connect-4 6x7, both players pick uniformly random legal moves, "
                "games=%d, seed=%llu, threads=1\n\n",
                kGames, static_cast<unsigned long long>(kSeed));
    const Outcomes o = verify_random_games(kGames, kSeed);
    const double p1_rate = static_cast<double>(o.p1_wins) / o.games;
    const double p1_ci = proportion_ci95(p1_rate, o.games);
    const double mean_moves = o.total_moves / o.games;

    std::printf("| Games | P1 wins | P2 wins | Draws | P1 win rate (95%% CI) | Mean moves | Min | Max | Rule mismatches |\n");
    std::printf("|-------|---------|---------|-------|----------------------|------------|-----|-----|-----------------|\n");
    std::printf("| %5d | %7d | %7d | %5d | %6.1f%% +/- %4.1f%%     | %10.2f | %3d | %3d | %15d |\n",
                o.games, o.p1_wins, o.p2_wins, o.draws, 100.0 * p1_rate, 100.0 * p1_ci,
                mean_moves, o.min_moves, o.max_moves, o.rule_mismatches);

    append_csv("results/connect4_random_outcomes.csv", info,
               {"game", "games", "seed", "threads", "p1_wins", "p2_wins", "draws", "p1_win_rate",
                "p1_ci95_halfwidth", "mean_moves", "min_moves", "max_moves", "rule_mismatches"},
               {"connect4_6x7", std::to_string(o.games), std::to_string(kSeed), "1",
                std::to_string(o.p1_wins), std::to_string(o.p2_wins), std::to_string(o.draws),
                fmt(p1_rate, 4), fmt(p1_ci, 4), fmt(mean_moves, 3), std::to_string(o.min_moves),
                std::to_string(o.max_moves), std::to_string(o.rule_mismatches)});

    // ---- 4. speed of random playouts ----
    const int kSpeedGames = 100000;
    const int kSpeedRepeats = 5;
    const uint64_t kSpeedSeed = 1000;
    std::printf("\n== 4. Speed of random playouts (no tree, no checking) ==\n");
    std::printf("Settings: game=Connect-4 6x7, games per repeat=%d, repeats=%d (plus 1 discarded "
                "warm-up), base seed=%llu, threads=1\n\n",
                kSpeedGames, kSpeedRepeats, static_cast<unsigned long long>(kSpeedSeed));

    {   // warm-up: wake up the CPU and caches, result discarded
        Rng warm(1);
        for (int i = 0; i < 20000; ++i) play_random_to_end(Connect4(), warm);
    }
    std::vector<double> rates;
    double moves_per_game_sum = 0.0;
    for (int r = 0; r < kSpeedRepeats; ++r) {
        Rng rng(kSpeedSeed + static_cast<uint64_t>(r));
        uint64_t total_moves = 0;
        Timer tm;
        for (int i = 0; i < kSpeedGames; ++i) {
            const Connect4 end = play_random_to_end(Connect4(), rng);
            total_moves += static_cast<uint64_t>(end.moves_played());
        }
        const double secs = tm.elapsed_sec();
        rates.push_back(kSpeedGames / secs);
        moves_per_game_sum += static_cast<double>(total_moves) / kSpeedGames;
    }
    double rate_mean = 0.0, rate_sd = 0.0;
    mean_std(rates, rate_mean, rate_sd);
    const double speed_mean_moves = moves_per_game_sum / kSpeedRepeats;

    std::printf("| Threads | Games per repeat | Repeats | Playouts/sec (mean +/- std) | Mean moves per game |\n");
    std::printf("|---------|------------------|---------|-----------------------------|---------------------|\n");
    std::printf("| %7d | %16d | %7d | %12.0f +/- %-12.0f | %19.2f |\n", 1, kSpeedGames,
                kSpeedRepeats, rate_mean, rate_sd, speed_mean_moves);
    std::printf("\nAll repeats (playouts/sec):");
    for (double x : rates) std::printf(" %.0f", x);
    std::printf("\n");

    append_csv("results/connect4_random_speed.csv", info,
               {"game", "threads", "games_per_repeat", "repeats", "base_seed",
                "playouts_per_sec_mean", "playouts_per_sec_std", "mean_moves_per_game"},
               {"connect4_6x7", "1", std::to_string(kSpeedGames), std::to_string(kSpeedRepeats),
                std::to_string(kSpeedSeed), fmt(rate_mean, 0), fmt(rate_sd, 0),
                fmt(speed_mean_moves, 3)});

    // ---- verdict ----
    const bool pass = (g_failures == 0 && o.rule_mismatches == 0);
    std::printf("\nRESULT: %s (%d failed checks, %d rule mismatches)\n", pass ? "PASS" : "FAIL",
                g_failures, o.rule_mismatches);
    return pass ? 0 : 1;
}
