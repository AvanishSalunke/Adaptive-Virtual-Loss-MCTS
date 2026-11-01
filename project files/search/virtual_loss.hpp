// virtual_loss.hpp - the penalty rule for "virtual loss".
//
// Several threads search the same tree at the same time. Without any
// countermeasure they all look at the same statistics, so they all choose the
// same child and repeat each other's work. Virtual loss fixes this: when a
// thread passes through a node it temporarily pretends that `penalty` games
// were played there and lost. The node's average drops, so the next thread
// is nudged towards another branch. When the thread's real result arrives,
// the pretend games are removed and the real result is added.
//
// The only thing that differs between the variants is the size of the
// penalty:
//   None      0 (no virtual loss)
//   Fixed     the same constant everywhere
//   Adaptive  v = max(1, round(lambda * T / sqrt(n + 1)))
//             T = number of threads, n = visits of the node being passed,
//             lambda = a tuning constant. Many visits make the penalty small.
//             THIS FORMULA IS OUR OWN UNTESTED HYPOTHESIS.
#pragma once
#include <algorithm>
#include <cmath>

struct VirtualLoss {
    enum class Mode { None, Fixed, Adaptive };
    Mode mode = Mode::None;
    int fixed = 1;        // used by Mode::Fixed
    double lambda = 1.0;  // used by Mode::Adaptive

    static VirtualLoss None() { return VirtualLoss(); }
    static VirtualLoss Fixed(int v) {
        VirtualLoss vl;
        vl.mode = Mode::Fixed;
        vl.fixed = v;
        return vl;
    }
    static VirtualLoss Adaptive(double lambda) {
        VirtualLoss vl;
        vl.mode = Mode::Adaptive;
        vl.lambda = lambda;
        return vl;
    }
};

// Number of pretend lost games to add to a node that has `visits` visits
// right now, when `threads` threads are searching. std::lround rounds halves
// away from zero (2.5 becomes 3).
inline int virtual_loss_penalty(const VirtualLoss& vl, int threads, int visits) {
    switch (vl.mode) {
        case VirtualLoss::Mode::Fixed:
            return vl.fixed;  // <- the fixed rule
        case VirtualLoss::Mode::Adaptive:
            // <- the adaptive rule: the same place, a different formula
            return std::max(1, static_cast<int>(std::lround(vl.lambda * threads / std::sqrt(visits + 1.0))));
        case VirtualLoss::Mode::None:
            break;
    }
    return 0;
}
