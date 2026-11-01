// team.hpp - starts a team of threads that all run the same function.
//
// Normally the team is an OpenMP parallel region. ThreadSanitizer (a tool
// that detects data races) understands std::thread but not the internal
// synchronisation of OpenMP's library (libgomp), so under OpenMP it would
// report races that do not exist. Building with -DMCTS_STD_THREADS makes
// run_team use std::thread instead, so such a report can be trusted.
#pragma once
#ifdef MCTS_STD_THREADS
#include <thread>
#include <vector>
#else
#include <omp.h>
#endif

// Calls body(thread_index) on `threads` threads (indices 0 .. threads-1) and
// returns when all of them have finished.
template <class Body>
void run_team(int threads, Body body) {
#ifdef MCTS_STD_THREADS
    std::vector<std::thread> helpers;
    for (int t = 1; t < threads; ++t) helpers.emplace_back([&body, t] { body(t); });
    body(0);  // the calling thread takes part as thread 0
    for (std::thread& th : helpers) th.join();
#else
    #pragma omp parallel num_threads(threads)
    {
        body(omp_get_thread_num());
    }  // implicit barrier
#endif
}
