// tsan_check.cpp - runs the shared-tree searchers under ThreadSanitizer (TSan).
// TSan watches every memory access and reports two threads touching the same
// memory without proper synchronisation, if at least one of them writes.
//
// Built by `make tsan` / `make tsan-control` with -fsanitize=thread and
// -DMCTS_STD_THREADS (see team.hpp for why).
//
//   tsan_check          runs V3 (locks) and V4 (atomics, both modes)
//   tsan_check control  runs a deliberately racy counter that TSan MUST report,
//                       which proves the tool is working in this setup
//
// TSan only sees the interleavings that actually happen in a run, so "no
// report" is evidence, not proof.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "connect4.hpp"
#include "uct_tree_lockfree.hpp"
#include "uct_tree_locked.hpp"
#include "uct_v0.hpp"

using V0 = UctSearcher<Connect4>;
using V3 = LockedTreeUct<Connect4>;
using V4 = LockFreeTreeUct<Connect4>;

static void control() {
    long long counter = 0;  // plain and unprotected on purpose
    std::thread a([&] { for (int i = 0; i < 100000; ++i) ++counter; });
    std::thread b([&] { for (int i = 0; i < 100000; ++i) ++counter; });
    a.join();
    b.join();
    std::printf("control: counter = %lld (200000 would mean no update was lost)\n", counter);
    std::printf("TSan should have printed 'WARNING: ThreadSanitizer: data race' above.\n");
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "control") == 0) {
        control();
        return 0;
    }

    const int kThreads = 4;
    V0::Limit limit;
    limit.iterations = 20000;

    {
        V3 s(kThreads, 1, 1u << 18);
        s.search(Connect4(), limit);
        std::printf("V3 locks:                %d threads, %llu iterations, lost updates = %lld\n",
                    kThreads, static_cast<unsigned long long>(s.iterations()), s.lost_updates());
    }
    {
        V4 s(kThreads, 2, true, 1u << 18);
        s.search(Connect4(), limit);
        std::printf("V4 atomics (exact):      %d threads, %llu iterations, lost updates = %lld\n",
                    kThreads, static_cast<unsigned long long>(s.iterations()), s.lost_updates());
    }
    {
        V4 s(kThreads, 3, false, 1u << 18);
        s.search(Connect4(), limit);
        std::printf("V4 atomics (racy mode):  %d threads, %llu iterations, lost updates = %lld\n",
                    kThreads, static_cast<unsigned long long>(s.iterations()), s.lost_updates());
    }
    std::printf("\nDone. If no 'WARNING: ThreadSanitizer' appeared above, no data race was observed in this run.\n");
    std::printf("Note: the racy mode loses updates but is NOT a data race in C++ terms, because every\n");
    std::printf("access is an atomic operation; TSan reports data races, not lost updates.\n");
    return 0;
}
