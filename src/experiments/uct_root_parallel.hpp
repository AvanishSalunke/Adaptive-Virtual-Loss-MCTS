// uct_root_parallel.hpp - V2: root parallelisation.
//
// Every thread runs its OWN complete V0 search on its OWN tree, with its own
// random number generator (different seed). Nothing is shared while the
// search runs, so there is nothing to lock. When the time is up, the root
// statistics of all trees are added together (visits per move) and the move
// with the most total visits is played (one common merge rule; check
// against [1] for the exact rules the literature compares).
//
// Costs: T trees need T times the memory, and the trees repeat each other's
// early exploration, because no thread learns from what the others found.
#pragma once
#include <omp.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "game.hpp"
#include "rng.hpp"
#include "timing.hpp"
#include "uct_v0.hpp"

template <class G>
class RootParallelUct {
public:
    using Move = typename G::Move;
    using Single = UctSearcher<G>;
    using Limit = typename Single::Limit;

    // max_nodes_per_tree is the pool size of EACH thread's tree.
    RootParallelUct(int threads, uint64_t seed, std::size_t max_nodes_per_tree = (1u << 23),
                    double exploration = 1.4142135623730951) {
        slots_.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t)
            slots_.emplace_back(seed + static_cast<uint64_t>(t), max_nodes_per_tree, exploration);
    }

    int threads() const { return static_cast<int>(slots_.size()); }

    void reseed(uint64_t seed) {
        for (std::size_t t = 0; t < slots_.size(); ++t)
            slots_[t].searcher.reseed(seed + static_cast<uint64_t>(t));
    }

    // `limit` applies to EACH thread's own search (so the wall-clock time per
    // move stays the same however many threads there are).
    Move search(const G& root_state, Limit limit) {
        if (root_state.is_over()) return Move();
        const int T = threads();
        Timer timer;

        // Thread-private data: iteration t only touches slots_[t], so the
        // threads never write to the same memory. schedule(static, 1) hands
        // iteration t to thread t. The loop ends with an implicit barrier.
        #pragma omp parallel for num_threads(T) schedule(static, 1)
        for (int t = 0; t < T; ++t) slots_[static_cast<std::size_t>(t)].searcher.search(root_state, limit);

        elapsed_ = timer.elapsed_sec();
        return merge(root_state);
    }

    // Statistics of the last search, summed over all trees.
    uint64_t iterations() const { return iterations_; }
    double elapsed_sec() const { return elapsed_; }
    std::size_t nodes_used() const { return nodes_; }
    bool pool_full() const { return pool_full_; }

    // Root statistics of one thread's tree, and the merged visit totals.
    std::vector<typename Single::RootChild> tree_root_stats(int t) const {
        return slots_[static_cast<std::size_t>(t)].searcher.root_stats();
    }
    const std::vector<Move>& merged_moves() const { return merged_moves_; }
    const std::vector<long long>& merged_visits() const { return merged_visits_; }

private:
    // alignas(64): each searcher starts on its own cache line, so one
    // thread's counters (iteration count, next free slot, ...) never share a
    // line with another thread's. That avoids false sharing.
    struct alignas(64) Slot {
        Single searcher;
        Slot(uint64_t seed, std::size_t max_nodes, double exploration)
            : searcher(seed, max_nodes, exploration) {}
    };

    Move merge(const G& root_state) {
        merged_moves_.clear();
        merged_visits_.clear();
        iterations_ = 0;
        nodes_ = 0;
        pool_full_ = false;
        for (const Slot& slot : slots_) {
            const Single& s = slot.searcher;
            iterations_ += s.iterations();
            nodes_ += s.nodes_used();
            pool_full_ = pool_full_ || s.pool_full();
            const auto stats = s.root_stats();
            // All trees expand the root in the same (legal-move) order, so
            // entry i means the same move in every tree.
            if (merged_moves_.empty()) {
                for (const auto& c : stats) {
                    merged_moves_.push_back(c.move);
                    merged_visits_.push_back(0);
                }
            }
            for (std::size_t i = 0; i < stats.size() && i < merged_visits_.size(); ++i)
                merged_visits_[i] += stats[i].visits;
        }
        if (merged_moves_.empty()) {
            Move moves[G::kMaxMoves];
            root_state.legal_moves(moves);
            return moves[0];
        }
        std::size_t best = 0;
        for (std::size_t i = 1; i < merged_visits_.size(); ++i)
            if (merged_visits_[i] > merged_visits_[best]) best = i;
        return merged_moves_[best];
    }

    std::vector<Slot> slots_;
    std::vector<Move> merged_moves_;
    std::vector<long long> merged_visits_;
    uint64_t iterations_ = 0;
    std::size_t nodes_ = 0;
    bool pool_full_ = false;
    double elapsed_ = 0.0;
};
