// uct_v0.hpp - V0: sequential UCT, i.e. Monte Carlo Tree Search on one thread.
//
// One search = many iterations. Each iteration has four phases:
//   1. Selection       walk down the tree, at each node following the child
//                      with the best UCT score, until a node with an
//                      unvisited child is reached.
//   2. Expansion       create the children (one per legal move) of the node.
//   3. Simulation      from the new leaf, play random moves to the end of
//                      the game (a "playout").
//   4. Backpropagation walk back up to the root, adding the playout result
//                      to the visit and win counters of every node on the way.
// When time is up, the move whose root child has the MOST VISITS is played.
//
// Statistics convention: a node's `wins` are from the point of view of the
// player who made the move INTO that node. A parent therefore simply prefers
// the child with the highest wins/visits.
//
// Memory: all nodes live in one pre-allocated array (the node pool) and refer
// to each other by index. The children of a node are stored side by side, so
// a node only needs the index of its first child and a child count.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "game.hpp"
#include "rng.hpp"
#include "timing.hpp"

template <class G>
class UctSearcher {
public:
    using Move = typename G::Move;

    // Stop when either limit is reached; a limit of 0 means "no limit".
    // If both are 0 the search runs a single iteration.
    struct Limit {
        double seconds = 0.0;
        uint64_t iterations = 0;
    };

    struct RootChild {
        Move move;
        int visits;
        double win_rate;  // for the player to move at the root
    };

    // exploration = 1.41421... (sqrt 2) is the textbook constant for rewards
    // in [0, 1] (UCB1 / UCT; check against the original papers). It is NOT
    // tuned for Connect-4.
    explicit UctSearcher(uint64_t seed, std::size_t max_nodes = (1u << 24),
                         double exploration = 1.4142135623730951)
        : rng_(seed), exploration_(exploration), capacity_(max_nodes),
          pool_(new Node[max_nodes]) {}

    void reseed(uint64_t seed) { rng_ = Rng(seed); }

    // Returns the chosen move for `root_state`, which must not be a finished game.
    // Every call starts with a fresh tree (no reuse between moves).
    Move search(const G& root_state, Limit limit) {
        if (root_state.is_over()) return Move();
        Timer timer;
        next_free_ = 1;  // slot 0 is the root
        pool_full_ = false;
        iterations_ = 0;
        init_node(0, -1, Move(), -1);

        if (limit.seconds <= 0.0 && limit.iterations == 0) limit.iterations = 1;
        for (;;) {
            run_iteration(root_state);
            ++iterations_;
            if (limit.iterations != 0 && iterations_ >= limit.iterations) break;
            // Reading the clock costs time, so look at it only every 16 iterations.
            if (limit.seconds > 0.0 && (iterations_ & 15) == 0 &&
                timer.elapsed_sec() >= limit.seconds)
                break;
        }
        elapsed_ = timer.elapsed_sec();
        return most_visited_root_move(root_state);
    }

    // Statistics of the last search.
    uint64_t iterations() const { return iterations_; }
    double elapsed_sec() const { return elapsed_; }
    std::size_t nodes_used() const { return next_free_; }
    bool pool_full() const { return pool_full_; }  // true if the tree stopped growing

    std::vector<RootChild> root_stats() const {
        std::vector<RootChild> out;
        const Node& root = pool_[0];
        for (int i = 0; i < root.n_children; ++i) {
            const Node& c = pool_[root.first_child + i];
            out.push_back({c.move, c.visits, c.visits > 0 ? c.wins / c.visits : 0.0});
        }
        return out;
    }

    // The plan the search recommends: from the root, repeatedly follow the
    // child with the most visits, as far as the tree goes.
    std::vector<Move> principal_path() const {
        std::vector<Move> path;
        int node = 0;
        while (pool_[node].n_children > 0) {
            const int first = pool_[node].first_child;
            int best = -1;
            int best_visits = 0;
            for (int i = 0; i < pool_[node].n_children; ++i) {
                if (pool_[first + i].visits > best_visits) {
                    best_visits = pool_[first + i].visits;
                    best = first + i;
                }
            }
            if (best < 0) break;
            path.push_back(pool_[best].move);
            node = best;
        }
        return path;
    }

    // Calls f(path, final_state) for every finished game that the tree has
    // reached (path = the moves from the root).
    template <class F>
    void for_each_finished(const G& root_state, F f) const {
        std::vector<Move> path;
        walk_finished(0, root_state, path, f);
    }

private:
    template <class F>
    void walk_finished(int node, const G& state, std::vector<Move>& path, F& f) const {
        if (state.is_over()) {
            f(path, state);
            return;
        }
        const int first = pool_[node].first_child;
        for (int i = 0; i < pool_[node].n_children; ++i) {
            const Node& child = pool_[first + i];
            if (child.visits == 0) continue;
            G next = state;
            next.play(child.move);
            path.push_back(child.move);
            walk_finished(first + i, next, path, f);
            path.pop_back();
        }
    }

    // Deliberately no default member initialisers: that keeps `new Node[n]`
    // from touching all the memory up front. Every field is set in init_node().
    // 32 bytes, so two nodes share one 64-byte cache line.
    struct Node {
        int32_t parent;       // index of the parent, -1 for the root
        int32_t first_child;  // index of the first child, -1 if not expanded
        int32_t visits;
        Move move;            // the move that led from the parent to this node
        int16_t n_children;
        int8_t mover;         // player who made `move`, -1 for the root
        double wins;          // sum of results, from `mover`'s point of view
    };

    void init_node(int idx, int parent, Move move, int mover) {
        Node& nd = pool_[idx];
        nd.parent = parent;
        nd.first_child = -1;
        nd.visits = 0;
        nd.move = move;
        nd.n_children = 0;
        nd.mover = static_cast<int8_t>(mover);
        nd.wins = 0.0;
    }

    void run_iteration(const G& root_state) {
        G state = root_state;  // a cheap copy; we play moves on the copy
        int node = 0;
        // 1. Selection and 2. Expansion
        while (!state.is_over()) {
            if (pool_[node].n_children == 0 && !expand(node, state)) break;  // pool full
            node = select_child(node);
            state.play(pool_[node].move);
            if (pool_[node].visits == 0) break;  // a fresh leaf: simulate from here
        }
        // 3. Simulation (if the game is already over this returns `state` unchanged)
        const G final_state = play_random_to_end(state, rng_);
        // 4. Backpropagation
        for (int n = node; n != -1; n = pool_[n].parent) {
            Node& nd = pool_[n];
            ++nd.visits;
            if (nd.parent != -1) nd.wins += final_state.result(nd.mover);
        }
    }

    // Creates one child per legal move. Returns false if the pool is full.
    bool expand(int node_idx, const G& state) {
        Move moves[G::kMaxMoves];
        const int n = state.legal_moves(moves);
        if (next_free_ + static_cast<std::size_t>(n) > capacity_) {
            pool_full_ = true;
            return false;
        }
        const int first = static_cast<int>(next_free_);
        next_free_ += static_cast<std::size_t>(n);
        const int mover = state.current_player();
        for (int i = 0; i < n; ++i) init_node(first + i, node_idx, moves[i], mover);
        pool_[node_idx].first_child = first;
        pool_[node_idx].n_children = static_cast<int16_t>(n);
        return true;
    }

    // UCT: average result + exploration bonus that is larger for rarely tried children.
    //   score = wins/visits + c * sqrt( ln(parent visits) / visits )
    int select_child(int node_idx) const {
        const Node& parent = pool_[node_idx];
        const int first = parent.first_child;
        const int last = first + parent.n_children;
        const double log_parent = std::log(static_cast<double>(parent.visits > 0 ? parent.visits : 1));
        int best = first;
        double best_score = -1.0;
        for (int c = first; c < last; ++c) {
            const Node& ch = pool_[c];
            if (ch.visits == 0) return c;  // try every move once before trusting averages
            const double score = ch.wins / ch.visits + exploration_ * std::sqrt(log_parent / ch.visits);
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        return best;
    }

    // "Robust child": the most visited one. More reliable than the highest
    // average, because a child tried once and won once has a 100% average.
    Move most_visited_root_move(const G& root_state) const {
        const Node& root = pool_[0];
        if (root.n_children == 0) {  // only if the pool had no room at all
            Move moves[G::kMaxMoves];
            root_state.legal_moves(moves);
            return moves[0];
        }
        int best = root.first_child;
        for (int c = best + 1; c < root.first_child + root.n_children; ++c)
            if (pool_[c].visits > pool_[best].visits) best = c;
        return pool_[best].move;
    }

    Rng rng_;
    double exploration_;
    std::size_t capacity_;
    std::unique_ptr<Node[]> pool_;
    std::size_t next_free_ = 1;
    bool pool_full_ = false;
    uint64_t iterations_ = 0;
    double elapsed_ = 0.0;
};
