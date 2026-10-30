// hello_threads.cpp - minimal OpenMP demo.
//   Part A: start several threads and print each thread's id and CPU.
//   Part B: split a fixed workload over 1, 2, 4, 8 threads and time it.
// Speedup is time(1 thread) / time(T threads); efficiency is speedup / T.
// Real scaling is usually below ideal because of shared hardware resources
// (hyper-threads, cache, memory bandwidth) and thread start-up cost.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#ifdef __linux__
#include <sched.h>   // sched_getcpu(): which logical CPU is this thread on?
#endif

#include "timing.hpp"

static int current_cpu() {
#ifdef __linux__
    return sched_getcpu();
#else
    return -1;  // not available on this OS
#endif
}

// A chunk of pure computation. Each step depends on the previous one
// (x is updated from x), so the compiler cannot skip or vectorise it away.
static uint64_t work(uint64_t seed, uint64_t iters) {
    uint64_t x = seed;
    uint64_t acc = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;  // simple LCG step
        acc ^= x >> 33;
    }
    return acc;
}

// Split total_iters into T equal chunks, one per thread, and run them together.
static uint64_t run_once(int T, uint64_t total_iters) {
    uint64_t result = 0;
    const uint64_t chunk = total_iters / T;

    // Loop of exactly T iterations; schedule(static) gives each thread one.
    // reduction(^ : result) gives every thread its own private copy of
    // 'result' and XORs the copies together at the end (no race on 'result').
    #pragma omp parallel for num_threads(T) schedule(static) reduction(^ : result)
    for (int t = 0; t < T; ++t) {
        result ^= work(1000 + t, chunk);
    }
    return result;
}

int main(int argc, char** argv) {
    const uint64_t total_iters =
        (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 400000000ULL;

    std::printf("Logical processors visible to OpenMP: %d\n", omp_get_num_procs());
    std::printf("Default max threads: %d\n", omp_get_max_threads());

    // ---------------- Part A: hello from each thread ----------------
    std::printf("\n--- Part A: 4 threads say hello ---\n");
    #pragma omp parallel num_threads(4)
    {
        const int id = omp_get_thread_num();    // my number: 0..3
        const int n  = omp_get_num_threads();   // how many of us there are
        // 'critical' = one thread at a time, so the lines do not get mixed up.
        #pragma omp critical
        std::printf("Hello from thread %d of %d (running on logical CPU %d)\n",
                    id, n, current_cpu());
    }

    // ---------------- Part B: does more threads = faster? ----------------
    std::printf("\n--- Part B: same total work (%llu steps), different thread counts ---\n",
                static_cast<unsigned long long>(total_iters));
    std::printf("Repeats per row: 5 (one extra warm-up run is thrown away)\n\n");
    std::printf("| Threads | Time ms (mean +/- std) | Speedup vs 1 thread | Efficiency |\n");
    std::printf("|---------|------------------------|---------------------|------------|\n");

    const int thread_counts[] = {1, 2, 4, 8};
    const int repeats = 5;
    double base_ms = 0.0;
    uint64_t sink = 0;  // we print this at the end so the work can't be optimised away

    for (int T : thread_counts) {
        sink ^= run_once(T, total_iters);  // warm-up: thread start-up, cold caches

        double times[repeats];
        for (int r = 0; r < repeats; ++r) {
            Timer tm;
            sink ^= run_once(T, total_iters);
            times[r] = tm.elapsed_ms();
        }

        double mean = 0.0;
        for (int r = 0; r < repeats; ++r) mean += times[r];
        mean /= repeats;
        double var = 0.0;
        for (int r = 0; r < repeats; ++r) var += (times[r] - mean) * (times[r] - mean);
        const double sd = std::sqrt(var / (repeats - 1));

        if (T == 1) base_ms = mean;
        const double speedup = base_ms / mean;  // time(1) / time(T)
        std::printf("| %7d | %9.1f +/- %-8.1f | %19.2f | %10.2f |\n",
                    T, mean, sd, speedup, speedup / T);
    }

    std::printf("\n(checksum, ignore: %llu)\n", static_cast<unsigned long long>(sink));
    return 0;
}
