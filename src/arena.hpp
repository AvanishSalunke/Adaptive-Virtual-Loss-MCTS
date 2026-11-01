// arena.hpp - lets two "players" play a game against each other.
// A player is any function that looks at a state and returns a move.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>

#include "rng.hpp"

template <class G>
using MoveFn = std::function<typename G::Move(const G&)>;

// Plays one game from `start` and returns the result from the point of view
// of the player who moves first: 1 win, 0.5 draw, 0 loss.
template <class G>
double play_game(const MoveFn<G>& first, const MoveFn<G>& second, G start = G()) {
    G state = start;
    while (!state.is_over()) {
        const MoveFn<G>& mover = (state.current_player() == 0) ? first : second;
        state.play(mover(state));
    }
    return state.result(0);
}

// Result of a match, counted from the point of view of player `a`.
struct MatchResult {
    int wins = 0;
    int draws = 0;
    int losses = 0;
    int games() const { return wins + draws + losses; }
    // A draw counts as half a win.
    double score() const { return games() > 0 ? (wins + 0.5 * draws) / games() : 0.0; }
};

// Plays `games` games between a and b and counts the results for a.
// To make the comparison fair:
//   * colours alternate: game 0 has a first, game 1 has a second, ...
//   * games come in pairs that share the same random opening
//     (`opening_plies` random moves), played once with each colour assignment,
//     so a lucky or lopsided opening helps both sides equally.
// If progress_label is given, a running count is shown on stderr.
template <class G>
MatchResult play_match(const MoveFn<G>& a, const MoveFn<G>& b, int games, int opening_plies = 4,
                       uint64_t opening_seed = 1, const char* progress_label = nullptr) {
    MatchResult res;
    for (int g = 0; g < games; ++g) {
        Rng rng(opening_seed + static_cast<uint64_t>(g / 2));
        G start;
        for (int p = 0; p < opening_plies && !start.is_over(); ++p) {
            typename G::Move moves[G::kMaxMoves];
            const int n = start.legal_moves(moves);
            start.play(moves[rng.next_below(static_cast<uint32_t>(n))]);
        }
        const bool a_first = (g % 2 == 0);
        const double score = a_first ? play_game<G>(a, b, start) : 1.0 - play_game<G>(b, a, start);
        if (score == 1.0) ++res.wins;
        else if (score == 0.5) ++res.draws;
        else ++res.losses;
        if (progress_label) {
            std::fprintf(stderr, "\r  %s: %d/%d games", progress_label, g + 1, games);
            std::fflush(stderr);
        }
    }
    if (progress_label) std::fprintf(stderr, "\r%60s\r", "");
    return res;
}
