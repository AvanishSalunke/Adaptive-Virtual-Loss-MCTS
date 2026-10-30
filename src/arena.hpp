// arena.hpp - lets two "players" play a game against each other.
// A player is any function that looks at a state and returns a move.
#pragma once
#include <functional>

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
