// timing.hpp - a tiny stopwatch used by every benchmark in this project.
#pragma once
#include <chrono>

class Timer {
public:
    Timer() { reset(); }

    // Restart the stopwatch from zero.
    void reset() { start_ = Clock::now(); }

    // Seconds since the last reset (wall-clock time, not CPU time).
    double elapsed_sec() const {
        return std::chrono::duration<double>(Clock::now() - start_).count();
    }

    double elapsed_ms() const { return elapsed_sec() * 1000.0; }

private:
    // steady_clock never jumps backwards (system_clock can, e.g. when the
    // OS corrects the time), so it is the right clock for measuring durations.
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
};
