// game.hpp - the contract every game or planning problem must satisfy, so the
// search code can be written once and reused for all of them.
//
// A game type G must provide:
//
//   G::Move                          small copyable type naming one move
//   G::kMaxMoves                     upper bound on legal moves in any position
//   int    current_player() const   side to move: 0 or 1
//   int    legal_moves(Move* out) const
//                                    writes the legal moves to out[0..n-1], returns n
//   void   play(Move m)              applies a legal move to this state
//   bool   is_over() const           true when no further move may be played
//   double result(int player) const  final score for `player`: 1 win, 0.5 draw,
//                                    0 loss (only meaningful when is_over())
//
// Design notes:
//  * Code that uses a game is a template over G ("compile-time polymorphism")
//    instead of using virtual functions. Playouts call these functions
//    millions of times; templates let the compiler inline them, and a virtual
//    call per move would be slower.
//  * A state is small and cheap to copy. Branching ("what if I play here?")
//    is just copying the state and calling play() on the copy.
//  * legal_moves() fills a caller-provided array, so no memory is allocated
//    inside the hot loop.
#pragma once
#include <cstdint>

#include "rng.hpp"

// Plays uniformly random legal moves from `state` until the game ends and
// returns the final state. This is exactly what an MCTS "playout" does.
// `state` is taken by value, so the caller's copy is untouched.
template <class G>
G play_random_to_end(G state, Rng& rng) {
    typename G::Move moves[G::kMaxMoves];
    while (!state.is_over()) {
        const int n = state.legal_moves(moves);
        state.play(moves[rng.next_below(static_cast<uint32_t>(n))]);
    }
    return state;
}
