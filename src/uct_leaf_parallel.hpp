// uct_leaf_parallel.hpp - V1: leaf parallelisation.
//
// ONE thread walks the tree (selection and expansion), exactly like V0.
// When it reaches a leaf, ALL threads play random games from that same leaf
// at the same time. Their results are added up and backed up the tree
// together, so one iteration adds (threads x playouts_per_thread) visits.
//
// The tree is only touched between the parallel regions, so no locks or
// atomics are needed. The price: every iteration starts and ends a parallel
// region, and all threads wait for the slowest playout before moving on.
#pragma once
#include <omp.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "game.hpp"
#include "rng.hpp"
#include "timing.hpp"
#include "uct_v0.hpp"  // only for the shared Limit type

template <class G>
class LeafParallelUct {
public:
    using Move = typename G::Move;
    using Limit = typename UctSearcher<G>::Limit;

    // playouts_per_thread = 1 is the standard scheme. Larger values make each
    // thread play several games per iteration, so the parallel-region cost is
    // shared over more playouts.
    LeafParallelUct(int threads, uint64_t seed, int playouts_per_thread = 1,
                    std::size_t max_nodes = (1u << 24), double exploration = 1.4142135623730951)
        : threads_(threads), batch_(playouts_per_thread), exploration_(exploration),
          capacity_(max_nodes), pool_(new Node[max_nodes]) {
        rngs_.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
            rngs_.push_back(PaddedRng{Rng(seed + static_cast<uint64_t>(t))});
    }

    void reseed(uint64_t seed) {
        for (int t = 0; t < threads_; ++t) rngs_[static_cast<std::size_t>(t)].rng = Rng(seed + static_cast<uint64_t>(t));
    }

    Move search(const G& root_state, Limit limit) {
        if (root_state.is_over()) return Move();
        Timer timer;
        next_free_ = 1;
        pool_full_ = false;
        iterations_ = 0;
        playouts_ = 0;
        init_node(0, -1, Move(), -1);

        if (limit.seconds <= 0.0 && limit.iterations == 0) limit.iterations = 1;
        for (;;) {
            run_iteration(root_state);
            ++iterations_;
            if (limit.iterations != 0 && iterations_ >= limit.iterations) break;
            if (limit.seconds > 0.0 && (iterations_ & 15) == 0 &&
                timer.elapsed_sec() >= limit.seconds)
                break;
        }
        elapsed_ = timer.elapsed_sec();
        return most_visited_root_move(root_state);
    }

    uint64_t iterations() const { return iterations_; }  // tree iterations
    uint64_t playouts() const { return playouts_; }      // random games played in total
    double elapsed_sec() const { return elapsed_; }
    std::size_t nodes_used() const { return next_free_; }
    bool pool_full() const { return pool_full_; }

private:
    // Same node layout as V0 (see uct_v0.hpp for the field meanings).
    struct Node {
        int32_t parent;
        int32_t first_child;
        int32_t visits;
        Move move;
        int16_t n_children;
        int8_t mover;
        double wins;
    };

    // alignas(64): each thread's generator gets its own 64-byte cache line.
    // Without it, neighbouring generators would share a line, and every
    // random number one thread draws would force the other cores to reload
    // that line ("false sharing"), even though they never touch each
    // other's data.
    struct alignas(64) PaddedRng {
        Rng rng;
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
        // Selection and expansion: one thread, like V0.
        G state = root_state;
        int node = 0;
        while (!state.is_over()) {
            if (pool_[node].n_children == 0 && !expand(node, state)) break;
            node = select_child(node);
            state.play(pool_[node].move);
            if (pool_[node].visits == 0) break;
        }

        // Simulation: every thread plays from the same leaf.
        double sum0 = 0.0;  // total result of all playouts for player 0
        double sum1 = 0.0;  // ... and for player 1
        long long done = 0; // number of playouts actually played
        if (state.is_over()) {
            // The leaf is a finished game: all playouts would give the same answer.
            done = static_cast<long long>(threads_) * batch_;
            sum0 = static_cast<double>(done) * state.result(0);
            sum1 = static_cast<double>(done) * state.result(1);
        } else {
            // reduction(+ : ...): every thread gets private copies of the three
            // totals (starting at 0); OpenMP adds the copies together at the
            // end of the region, so no thread ever writes a shared total.
            #pragma omp parallel num_threads(threads_) reduction(+ : sum0, sum1, done)
            {
                Rng& rng = rngs_[static_cast<std::size_t>(omp_get_thread_num())].rng;
                for (int k = 0; k < batch_; ++k) {
                    const G end = play_random_to_end(state, rng);
                    sum0 += end.result(0);
                    sum1 += end.result(1);
                    ++done;
                }
            }  // implicit barrier: all threads finish before the code below runs
        }
        playouts_ += static_cast<uint64_t>(done);

        // Backpropagation: one thread again.
        for (int n = node; n != -1; n = pool_[n].parent) {
            Node& nd = pool_[n];
            nd.visits += static_cast<int32_t>(done);
            if (nd.parent != -1) nd.wins += (nd.mover == 0) ? sum0 : sum1;
        }
    }

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

    int select_child(int node_idx) const {
        const Node& parent = pool_[node_idx];
        const int first = parent.first_child;
        const int last = first + parent.n_children;
        const double log_parent = std::log(static_cast<double>(parent.visits > 0 ? parent.visits : 1));
        int best = first;
        double best_score = -1.0;
        for (int c = first; c < last; ++c) {
            const Node& ch = pool_[c];
            if (ch.visits == 0) return c;
            const double score = ch.wins / ch.visits + exploration_ * std::sqrt(log_parent / ch.visits);
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        return best;
    }

    Move most_visited_root_move(const G& root_state) const {
        const Node& root = pool_[0];
        if (root.n_children == 0) {
            Move moves[G::kMaxMoves];
            root_state.legal_moves(moves);
            return moves[0];
        }
        int best = root.first_child;
        for (int c = best + 1; c < root.first_child + root.n_children; ++c)
            if (pool_[c].visits > pool_[best].visits) best = c;
        return pool_[best].move;
    }

    int threads_;
    int batch_;
    double exploration_;
    std::size_t capacity_;
    std::unique_ptr<Node[]> pool_;
    std::vector<PaddedRng> rngs_;
    std::size_t next_free_ = 1;
    bool pool_full_ = false;
    uint64_t iterations_ = 0;
    uint64_t playouts_ = 0;
    double elapsed_ = 0.0;
};
