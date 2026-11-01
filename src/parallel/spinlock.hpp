// spinlock.hpp - a minimal mutual-exclusion lock built from one atomic flag.
//
// Only one thread at a time can hold the lock; the others "spin" (keep
// checking) until it is released. That is fine for very short critical
// sections, such as updating a few counters. Memory orders:
//   acquire (on lock)   : everything the previous holder wrote before
//                         unlocking is visible to us once we hold the lock.
//   release (on unlock) : everything we wrote while holding the lock becomes
//                         visible to the next holder.
#pragma once
#include <atomic>
#include <cstdint>

// Tells the CPU "I am in a busy-wait loop": saves power and lets the
// sibling hyper-thread use the core while we wait.
inline void cpu_pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

class SpinLock {
public:
    // The flag is left uninitialised on purpose (see the node pool in
    // uct_tree_locked.hpp, which must not touch memory it does not use).
    // Call reset() once before the first use.
    void reset() { flag_.store(false, std::memory_order_relaxed); }

    void lock() {
        uint64_t unused = 0;
        lock(unused);
    }

    // Same, but counts in `contended` every time the lock was already taken.
    void lock(uint64_t& contended) {
        // exchange(true) sets the flag and returns the OLD value in one
        // indivisible step: if the old value was false, the lock was free and is now ours.
        if (!flag_.exchange(true, std::memory_order_acquire)) return;
        ++contended;
        for (;;) {
            // Wait with plain reads first: they hit the local cache and do not
            // fight over the cache line the way repeated exchanges would.
            while (flag_.load(std::memory_order_relaxed)) cpu_pause();
            if (!flag_.exchange(true, std::memory_order_acquire)) return;
        }
    }

    void unlock() { flag_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> flag_;
};
