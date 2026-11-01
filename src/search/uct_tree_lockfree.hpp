// uct_tree_lockfree.hpp - V4: tree parallelisation without locks.
//
// All threads search one shared tree, like V3, but nobody ever waits for
// anybody: every shared number is a std::atomic and is changed with a single
// indivisible hardware operation.
//
// * Statistics. visits and wins2 (twice the summed result, so that it is a
//   whole number: results are 0, 0.5 or 1) are atomic integers.
//   - exact mode:  fetch_add, one indivisible "read, add, write". No update is lost.
//   - racy mode:   load, then store(value + 1). Each step is atomic but the
//                  pair is not, so two threads can both load 100 and both
//                  store 101: one visit is LOST. It is here to measure how
//                  often that happens and what it costs.
//   Statistics are read with memory_order_relaxed: they are only heuristics
//   (UCT scores) and no other data is reached through them, so we do not
//   need them ordered with respect to anything else.
//
// * Expansion. A node's children are published with ONE atomic word,
//   `children` = (index of first child << 16) | number of children, where 0
//   means "not expanded yet". A thread that finds 0 prepares a block of
//   children on its own (nobody can see it yet) and then tries
//       compare_exchange(children, 0 -> block)
//   with release semantics. If it wins, the block is visible to everybody.
//   If another thread won first, the CAS fails and our block is simply left
//   unused. Readers load `children` with acquire semantics, which guarantees
//   they also see the fully prepared children behind it.
//
// Virtual loss (optional, see virtual_loss.hpp). When a thread chooses a child
// it immediately adds `penalty` extra visits (with no wins) to it, so threads
// arriving a moment later see a worse average and choose differently. When
// the playout is finished, the penalty is taken off again and the real
// result is added. The temporary visits must be added and removed exactly, so
// whenever a penalty is used the updates are exact (fetch_add), whatever the
// exact_updates setting says.
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
#include "team.hpp"
#include "timing.hpp"
#include "uct_v0.hpp"  // for the shared Limit and RootChild types
#include "virtual_loss.hpp"

template <class G>
class LockFreeTreeUct {
public:
    using Move = typename G::Move;
    using Limit = typename UctSearcher<G>::Limit;
    using RootChild = typename UctSearcher<G>::RootChild;

    // exact_updates = true: fetch_add (no lost updates). false: racy load+store.
    LockFreeTreeUct(int threads, uint64_t seed, bool exact_updates = true,
                    std::size_t max_nodes = (1u << 24), double exploration = 1.4142135623730951,
                    VirtualLoss virtual_loss = VirtualLoss())
        : threads_(threads), exact_updates_(exact_updates), exploration_(exploration),
          virtual_loss_(virtual_loss), capacity_(max_nodes), pool_(new Node[max_nodes]) {
        data_.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
            data_.push_back(ThreadData{Rng(seed + static_cast<uint64_t>(t)), 0, 0, 0, {}});
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
            td.cas_failures = 0;
            td.wasted_nodes = 0;
        }
        std::atomic<uint64_t> claimed(0);  // iterations handed out (iteration-limited runs only)

        Timer timer;
        run_team(threads_, [&](int thread_index) {
            ThreadData& td = data_[static_cast<std::size_t>(thread_index)];
            for (;;) {
                if (limit.iterations != 0 &&
                    claimed.fetch_add(1, std::memory_order_relaxed) >= limit.iterations)
                    break;
                run_iteration(root_state, td);
                ++td.iterations;
                if (limit.seconds > 0.0 && (td.iterations & 15) == 0 &&
                    timer.elapsed_sec() >= limit.seconds)
                    break;
            }
        });

        elapsed_ = timer.elapsed_sec();
        iterations_ = 0;
        cas_failures_ = 0;
        wasted_nodes_ = 0;
        for (const ThreadData& td : data_) {
            iterations_ += td.iterations;
            cas_failures_ += td.cas_failures;
            wasted_nodes_ += td.wasted_nodes;
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
    uint64_t cas_failures() const { return cas_failures_; }  // expansions that lost the race
    uint64_t wasted_nodes() const { return wasted_nodes_; }  // slots prepared but never used

    std::vector<RootChild> root_stats() const {
        std::vector<RootChild> out;
        const uint64_t packed = pool_[0].children.load(std::memory_order_acquire);
        if (packed == 0) return out;
        const int first = static_cast<int>(packed >> 16);
        const int n = static_cast<int>(packed & 0xFFFF);
        for (int i = 0; i < n; ++i) {
            const Node& c = pool_[first + i];
            const int v = c.visits.load(std::memory_order_relaxed);
            const int w2 = c.wins2.load(std::memory_order_relaxed);
            out.push_back({c.move, v, v > 0 ? 0.5 * w2 / v : 0.0});
        }
        return out;
    }

    // Every finished iteration adds exactly 1 to one root child. The gap
    // between the iteration count and the sum of the root children's visits
    // is the number of visits lost at the root. (Lost updates deeper in the
    // tree are not counted, so this is a lower bound on the total.)
    long long lost_updates() const {
        long long sum = 0;
        for (const RootChild& c : root_stats()) sum += c.visits;
        return static_cast<long long>(iterations_) - sum;
    }

private:
    // 32 bytes. No default initialisers, so `new Node[n]` does not touch the memory.
    struct Node {
        std::atomic<uint64_t> children;  // (first child << 16) | count; 0 = not expanded
        std::atomic<int32_t> visits;
        std::atomic<int32_t> wins2;      // twice the summed result, from `mover`'s point of view
        int32_t parent;                  // plain data: written before the node is published, then never changed
        Move move;
        int8_t mover;
    };

    // One step of the walk down the tree: the node entered and the virtual
    // loss added to it (0 if none), which must be removed again on the way back.
    struct PathEntry {
        int node;
        int penalty;
    };

    struct alignas(64) ThreadData {
        Rng rng;
        uint64_t iterations;
        uint64_t cas_failures;
        uint64_t wasted_nodes;
        std::vector<PathEntry> path;  // reused by every iteration of this thread
    };

    // Only used on nodes that no other thread can see yet (the root before the
    // team starts, or a block of children before it is published).
    void init_node(int idx, int parent, Move move, int mover) {
        Node& nd = pool_[idx];
        nd.children.store(0, std::memory_order_relaxed);
        nd.visits.store(0, std::memory_order_relaxed);
        nd.wins2.store(0, std::memory_order_relaxed);
        nd.parent = parent;
        nd.move = move;
        nd.mover = static_cast<int8_t>(mover);
    }

    void run_iteration(const G& root_state, ThreadData& td) {
        G state = root_state;
        int node = 0;
        td.path.clear();

        while (!state.is_over()) {
            Node& parent = pool_[node];
            // acquire: if the children are published, we also see them fully prepared.
            uint64_t packed = parent.children.load(std::memory_order_acquire);
            if (packed == 0) {
                if (!expand(node, state, td)) break;  // pool full: play out from here
                packed = parent.children.load(std::memory_order_acquire);
            }
            const int first = static_cast<int>(packed >> 16);
            const int n = static_cast<int>(packed & 0xFFFF);
            bool fresh = false;
            int child_visits = 0;
            const int child = select_child(first, n, fresh, child_visits);
            // Virtual loss: make this child look worse to the threads that come next.
            const int penalty = virtual_loss_penalty(virtual_loss_, threads_, child_visits);
            if (penalty > 0) pool_[child].visits.fetch_add(penalty, std::memory_order_relaxed);
            td.path.push_back({child, penalty});
            node = child;
            state.play(pool_[child].move);
            if (fresh) break;
        }

        const G final_state = play_random_to_end(state, td.rng);

        // Back up along the path we walked, swapping each virtual loss for the real result.
        for (const PathEntry& e : td.path) {
            const int r2 = static_cast<int>(2.0 * final_state.result(pool_[e.node].mover) + 0.5);
            update(pool_[e.node], r2, e.penalty);
        }
    }

    void update(Node& nd, int r2, int penalty) {
        if (penalty > 0) {
            // Replace the temporary penalty visits by one real visit with its result.
            nd.visits.fetch_add(1 - penalty, std::memory_order_relaxed);
            nd.wins2.fetch_add(r2, std::memory_order_relaxed);
        } else if (exact_updates_) {
            nd.visits.fetch_add(1, std::memory_order_relaxed);
            nd.wins2.fetch_add(r2, std::memory_order_relaxed);
        } else {
            // Two separate atomic steps: another thread can slip in between them.
            nd.visits.store(nd.visits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            nd.wins2.store(nd.wins2.load(std::memory_order_relaxed) + r2, std::memory_order_relaxed);
        }
    }

    // Returns false only if the pool is full. Otherwise the node has children
    // when this returns (ours, or those of the thread that got there first).
    bool expand(int node_idx, const G& state, ThreadData& td) {
        Move moves[G::kMaxMoves];
        const int n = state.legal_moves(moves);
        // Reserve n slots in one atomic step: no two threads get the same slots.
        const int64_t first = next_free_.fetch_add(n, std::memory_order_relaxed);
        if (first + n > static_cast<int64_t>(capacity_)) {
            pool_full_.store(true, std::memory_order_relaxed);
            return false;
        }
        const int mover = state.current_player();
        for (int i = 0; i < n; ++i) init_node(static_cast<int>(first) + i, node_idx, moves[i], mover);

        // Publish: "if `children` is still 0, set it to my block".
        // On success the release half of acq_rel makes the prepared children
        // visible to any thread that later loads `children` with acquire.
        // On failure, `expected` receives the value another thread stored.
        uint64_t expected = 0;
        const uint64_t desired = (static_cast<uint64_t>(first) << 16) | static_cast<uint64_t>(n);
        if (!pool_[node_idx].children.compare_exchange_strong(
                expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            ++td.cas_failures;
            td.wasted_nodes += static_cast<uint64_t>(n);
        }
        return true;
    }

    // Same rule as V0 and V3 (an unvisited child first, otherwise the best UCT
    // score), computed from one snapshot of the children's numbers.
    // child_visits receives the visit count of the chosen child as seen in the snapshot.
    int select_child(int first, int n, bool& fresh, int& child_visits) const {
        int32_t vis[G::kMaxMoves];
        int32_t w2[G::kMaxMoves];
        for (int i = 0; i < n; ++i) {
            vis[i] = pool_[first + i].visits.load(std::memory_order_relaxed);
            if (vis[i] == 0) {
                fresh = true;
                child_visits = 0;
                return first + i;
            }
            w2[i] = pool_[first + i].wins2.load(std::memory_order_relaxed);
        }
        long long total = 0;
        for (int i = 0; i < n; ++i) total += vis[i];
        const double log_parent = std::log(static_cast<double>(total > 0 ? total : 1));
        int best = 0;
        double best_score = -1.0;
        for (int i = 0; i < n; ++i) {
            const double score = 0.5 * w2[i] / vis[i] + exploration_ * std::sqrt(log_parent / vis[i]);
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
        fresh = false;
        child_visits = vis[best];
        return first + best;
    }

    Move most_visited_root_move(const G& root_state) const {
        const uint64_t packed = pool_[0].children.load(std::memory_order_acquire);
        if (packed == 0) {
            Move moves[G::kMaxMoves];
            root_state.legal_moves(moves);
            return moves[0];
        }
        const int first = static_cast<int>(packed >> 16);
        const int n = static_cast<int>(packed & 0xFFFF);
        int best = first;
        for (int c = first + 1; c < first + n; ++c)
            if (pool_[c].visits.load(std::memory_order_relaxed) >
                pool_[best].visits.load(std::memory_order_relaxed))
                best = c;
        return pool_[best].move;
    }

    int threads_;
    bool exact_updates_;
    double exploration_;
    VirtualLoss virtual_loss_;
    std::size_t capacity_;
    std::unique_ptr<Node[]> pool_;
    std::vector<ThreadData> data_;
    alignas(64) std::atomic<int64_t> next_free_{1};
    alignas(64) std::atomic<bool> pool_full_{false};
    uint64_t iterations_ = 0;
    uint64_t cas_failures_ = 0;
    uint64_t wasted_nodes_ = 0;
    double elapsed_ = 0.0;
};
