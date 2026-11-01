// uct_tree_locked.hpp - V3: tree parallelisation with a lock on each node.
//
// All threads search ONE shared tree at the same time. Each thread runs the
// normal UCT iteration (select, expand, play out, back up) but the tree is
// now shared, so every read or write of shared tree data happens while
// holding a lock.
//
// Locking rule: the lock stored in node P protects
//   * P's list of children (first_child, n_children), and
//   * the statistics (visits, wins) of all of P's children.
// So while a thread is choosing among P's children it holds P's lock, which
// makes the children's numbers consistent; and when it backs a result up
// through child C, it holds the lock of C's parent. The root's own statistics
// are never needed, so the root is not locked as a "child".
//
// The random playout, the expensive part, runs with NO lock held.
//
// There is no virtual loss here: threads that reach the same node at the
// same time pick the same child. That repeated work (search overhead) is
// exactly what the later versions try to reduce.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "game.hpp"
#include "rng.hpp"
#include "spinlock.hpp"
#include "team.hpp"
#include "timing.hpp"
#include "uct_v0.hpp"  // for the shared Limit and RootChild types

template <class G>
class LockedTreeUct {
public:
    using Move = typename G::Move;
    using Limit = typename UctSearcher<G>::Limit;
    using RootChild = typename UctSearcher<G>::RootChild;

    LockedTreeUct(int threads, uint64_t seed, std::size_t max_nodes = (1u << 24),
                  double exploration = 1.4142135623730951)
        : threads_(threads), exploration_(exploration), capacity_(max_nodes),
          pool_(new Node[max_nodes]) {
        data_.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
            data_.push_back(ThreadData{Rng(seed + static_cast<uint64_t>(t)), 0, 0, 0});
    }

    void reseed(uint64_t seed) {
        for (int t = 0; t < threads_; ++t)
            data_[static_cast<std::size_t>(t)].rng = Rng(seed + static_cast<uint64_t>(t));
    }

    Move search(const G& root_state, Limit limit) {
        if (root_state.is_over()) return Move();
        if (limit.seconds <= 0.0 && limit.iterations == 0) limit.iterations = 1;

        next_free_.store(1, std::memory_order_relaxed);
        pool_full_.store(false, std::memory_order_relaxed);
        init_node(0, -1, Move(), -1);
        for (ThreadData& td : data_) {
            td.iterations = 0;
            td.lock_acquires = 0;
            td.lock_waits = 0;
        }
        std::atomic<uint64_t> claimed(0);  // iterations handed out so far (iteration-limited runs only)

        Timer timer;
        // Every thread runs this block: its own loop of UCT iterations on the shared tree.
        run_team(threads_, [&](int thread_index) {
            ThreadData& td = data_[static_cast<std::size_t>(thread_index)];
            for (;;) {
                if (limit.iterations != 0 &&
                    claimed.fetch_add(1, std::memory_order_relaxed) >= limit.iterations)
                    break;
                run_iteration(root_state, td);
                ++td.iterations;
                // Read the clock only every 16 iterations; every thread stops by itself.
                if (limit.seconds > 0.0 && (td.iterations & 15) == 0 &&
                    timer.elapsed_sec() >= limit.seconds)
                    break;
            }
        });  // returns when every thread has finished

        elapsed_ = timer.elapsed_sec();
        iterations_ = 0;
        lock_acquires_ = 0;
        lock_waits_ = 0;
        for (const ThreadData& td : data_) {
            iterations_ += td.iterations;
            lock_acquires_ += td.lock_acquires;
            lock_waits_ += td.lock_waits;
        }
        return most_visited_root_move(root_state);
    }

    // Statistics of the last search.
    uint64_t iterations() const { return iterations_; }
    double elapsed_sec() const { return elapsed_; }
    std::size_t nodes_used() const {
        return static_cast<std::size_t>(
            std::min<int64_t>(next_free_.load(std::memory_order_relaxed), static_cast<int64_t>(capacity_)));
    }
    bool pool_full() const { return pool_full_.load(std::memory_order_relaxed); }
    uint64_t lock_acquires() const { return lock_acquires_; }
    uint64_t lock_waits() const { return lock_waits_; }  // acquisitions that found the lock taken

    std::vector<RootChild> root_stats() const {
        std::vector<RootChild> out;
        const Node& root = pool_[0];
        for (int i = 0; i < root.n_children; ++i) {
            const Node& c = pool_[root.first_child + i];
            out.push_back({c.move, c.visits, c.visits > 0 ? c.wins / c.visits : 0.0});
        }
        return out;
    }

    // Every finished iteration adds exactly 1 to exactly one root child, so
    // the root children's visits must add up to the number of iterations.
    // With correct locking the difference ("lost updates") is always 0.
    long long lost_updates() const {
        long long sum = 0;
        for (const RootChild& c : root_stats()) sum += c.visits;
        return static_cast<long long>(iterations_) - sum;
    }

private:
    // 32 bytes. No default initialisers, so `new Node[n]` does not touch the memory.
    struct Node {
        SpinLock lock;        // protects first_child, n_children and the CHILDREN's visits/wins
        int32_t parent;       // never changes after creation
        int32_t first_child;  // written once, under this node's lock
        int32_t visits;       // protected by the PARENT's lock
        Move move;            // never changes after creation
        int16_t n_children;   // written once, under this node's lock
        int8_t mover;         // never changes after creation
        double wins;          // protected by the PARENT's lock
    };

    // Per-thread data, one cache line each, so threads that update their own
    // counters do not slow each other down through false sharing.
    struct alignas(64) ThreadData {
        Rng rng;
        uint64_t iterations;
        uint64_t lock_acquires;
        uint64_t lock_waits;
    };

    void init_node(int idx, int parent, Move move, int mover) {
        Node& nd = pool_[idx];
        nd.lock.reset();
        nd.parent = parent;
        nd.first_child = -1;
        nd.visits = 0;
        nd.move = move;
        nd.n_children = 0;
        nd.mover = static_cast<int8_t>(mover);
        nd.wins = 0.0;
    }

    void acquire(Node& node, ThreadData& td) {
        ++td.lock_acquires;
        node.lock.lock(td.lock_waits);
    }

    void run_iteration(const G& root_state, ThreadData& td) {
        G state = root_state;
        int node = 0;

        // Selection and expansion. Lock a node only while looking at its children.
        while (!state.is_over()) {
            Node& parent = pool_[node];
            acquire(parent, td);
            if (parent.n_children == 0 && !expand_locked(node, state)) {
                parent.lock.unlock();  // pool full: stop growing, play out from here
                break;
            }
            bool fresh = false;
            const int child = select_child_locked(parent, fresh);
            parent.lock.unlock();
            node = child;
            state.play(pool_[child].move);
            if (fresh) break;
        }

        // Simulation: no lock held.
        const G final_state = play_random_to_end(state, td.rng);

        // Backpropagation: update each node while holding its PARENT's lock.
        for (int n = node; pool_[n].parent != -1; n = pool_[n].parent) {
            Node& p = pool_[pool_[n].parent];
            acquire(p, td);
            ++pool_[n].visits;
            pool_[n].wins += final_state.result(pool_[n].mover);
            p.lock.unlock();
        }
    }

    // Caller holds the lock of node_idx. Creates its children.
    bool expand_locked(int node_idx, const G& state) {
        Move moves[G::kMaxMoves];
        const int n = state.legal_moves(moves);
        // Reserve n slots in one atomic step: no two threads can get the same slots.
        const int64_t first = next_free_.fetch_add(n, std::memory_order_relaxed);
        if (first + n > static_cast<int64_t>(capacity_)) {
            pool_full_.store(true, std::memory_order_relaxed);
            return false;
        }
        const int mover = state.current_player();
        for (int i = 0; i < n; ++i) init_node(static_cast<int>(first) + i, node_idx, moves[i], mover);
        pool_[node_idx].first_child = static_cast<int>(first);
        pool_[node_idx].n_children = static_cast<int16_t>(n);
        return true;
    }

    // Caller holds parent's lock. Same rule as V0: an unvisited child first,
    // otherwise the best UCT score. The parent's visit count is taken as the
    // sum of its children's visits (differs from V0's by at most 1).
    int select_child_locked(const Node& parent, bool& fresh) const {
        const int first = parent.first_child;
        const int last = first + parent.n_children;
        for (int c = first; c < last; ++c) {
            if (pool_[c].visits == 0) {
                fresh = true;
                return c;
            }
        }
        long long total = 0;
        for (int c = first; c < last; ++c) total += pool_[c].visits;
        const double log_parent = std::log(static_cast<double>(total > 0 ? total : 1));
        int best = first;
        double best_score = -1.0;
        for (int c = first; c < last; ++c) {
            const Node& ch = pool_[c];
            const double score = ch.wins / ch.visits + exploration_ * std::sqrt(log_parent / ch.visits);
            if (score > best_score) {
                best_score = score;
                best = c;
            }
        }
        fresh = false;
        return best;
    }

    // Called after all threads have finished, so no locking is needed.
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
    double exploration_;
    std::size_t capacity_;
    std::unique_ptr<Node[]> pool_;
    std::vector<ThreadData> data_;
    // The shared counter that all threads hit when expanding gets its own cache line.
    alignas(64) std::atomic<int64_t> next_free_{1};
    alignas(64) std::atomic<bool> pool_full_{false};
    uint64_t iterations_ = 0;
    uint64_t lock_acquires_ = 0;
    uint64_t lock_waits_ = 0;
    double elapsed_ = 0.0;
};
