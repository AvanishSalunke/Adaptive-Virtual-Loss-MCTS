// race_demo.cpp - what goes wrong when threads update shared data unprotected.
//
// T threads each add 1 to one shared counter kN times, so the right answer is
// T * kN. Three ways to do the update:
//   unprotected : plain read-modify-write -> updates are LOST
//   spinlock    : one thread at a time inside the update -> exact, threads queue
//   atomic      : the hardware does read-modify-write as one indivisible step
// The unprotected version is undefined behaviour in C++. It is here only to
// SEE the problem; nothing in the search engine does this.

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <omp.h>

#include "csv_log.hpp"
#include "spinlock.hpp"
#include "stats.hpp"
#include "timing.hpp"

static const long long kN = 200000;  // increments per thread
static const int kRepeats = 5;

struct Outcome {
    long long expected;
    long long lost;
    double ms;
};

static Outcome run_unprotected(int T) {
    volatile long long counter = 0;  // volatile: a real load and store on every step
    int actual = 0;
    Timer tm;
    #pragma omp parallel num_threads(T)
    {
        #pragma omp master
        actual = omp_get_num_threads();
        for (long long i = 0; i < kN; ++i) counter = counter + 1;
    }
    const double ms = tm.elapsed_ms();
    const long long expected = static_cast<long long>(actual) * kN;
    const long long result = counter;
    return {expected, expected - result, ms};
}

static Outcome run_spinlock(int T) {
    SpinLock lock;
    lock.reset();
    long long counter = 0;
    int actual = 0;
    Timer tm;
    #pragma omp parallel num_threads(T)
    {
        #pragma omp master
        actual = omp_get_num_threads();
        for (long long i = 0; i < kN; ++i) {
            lock.lock();
            ++counter;
            lock.unlock();
        }
    }
    const double ms = tm.elapsed_ms();
    const long long expected = static_cast<long long>(actual) * kN;
    return {expected, expected - counter, ms};
}

static Outcome run_atomic(int T) {
    std::atomic<long long> counter(0);
    int actual = 0;
    Timer tm;
    #pragma omp parallel num_threads(T)
    {
        #pragma omp master
        actual = omp_get_num_threads();
        for (long long i = 0; i < kN; ++i) counter.fetch_add(1, std::memory_order_relaxed);
    }
    const double ms = tm.elapsed_ms();
    const long long expected = static_cast<long long>(actual) * kN;
    return {expected, expected - counter.load(), ms};
}

template <class Fn>
static void row(const char* method, int T, Fn fn, const RunInfo& info) {
    long long min_lost = -1, max_lost = 0, expected = 0;
    double ms_sum = 0.0;
    for (int r = 0; r < kRepeats; ++r) {
        const Outcome o = fn(T);
        expected = o.expected;
        if (min_lost < 0 || o.lost < min_lost) min_lost = o.lost;
        if (o.lost > max_lost) max_lost = o.lost;
        ms_sum += o.ms;
    }
    const double ms_mean = ms_sum / kRepeats;
    std::printf("| %-12s | %7d | %9lld | %10lld .. %-10lld | %12.1f |\n", method, T, expected,
                min_lost, max_lost, ms_mean);
    append_csv("results/race_demo.csv", info,
               {"method", "threads", "increments_per_thread", "repeats", "expected",
                "lost_min", "lost_max", "time_ms_mean"},
               {method, std::to_string(T), std::to_string(kN), std::to_string(kRepeats),
                std::to_string(expected), std::to_string(min_lost), std::to_string(max_lost),
                fmt(ms_mean, 1)});
}

int main() {
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(),
                info.logical_cpus, info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n\n", info.compiler.c_str(), info.git_commit.c_str());
    std::printf("Settings: %lld increments per thread, %d repeats per row\n\n", kN, kRepeats);

    std::printf("| Method | Threads | Expected | Lost updates (min .. max of %d runs) | Mean time ms |\n", kRepeats);
    std::printf("|--------|---------|----------|---------------------------------|--------------|\n");
    const int thread_counts[] = {1, 4, 8};
    for (int T : thread_counts) {
        row("unprotected", T, run_unprotected, info);
        row("spinlock", T, run_spinlock, info);
        row("atomic", T, run_atomic, info);
    }
    std::printf("\nWith 1 thread there is nobody to race with, so even the unprotected row is exact.\n");
    return 0;
}
