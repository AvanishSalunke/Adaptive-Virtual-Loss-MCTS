// stats.hpp - small statistics helpers used when reporting results.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Sample mean and sample standard deviation (divides by n-1, the right choice
// when the values are a small sample of a larger population of possible runs).
inline void mean_std(const std::vector<double>& v, double& mean, double& sd) {
    mean = 0.0;
    sd = 0.0;
    if (v.empty()) return;
    for (double x : v) mean += x;
    mean /= static_cast<double>(v.size());
    if (v.size() < 2) return;
    double ss = 0.0;
    for (double x : v) ss += (x - mean) * (x - mean);
    sd = std::sqrt(ss / static_cast<double>(v.size() - 1));
}

// Half-width of a 95% confidence interval for a proportion p measured over n
// trials, using the normal approximation 1.96 * sqrt(p * (1 - p) / n).
// It is a rough guide: least accurate when p is near 0 or 1, or n is small.
inline double proportion_ci95(double p, int n) {
    return n > 0 ? 1.96 * std::sqrt(p * (1.0 - p) / n) : 0.0;
}

// Wilson 95% interval for a proportion p over n trials. Unlike the simple
// formula above it stays sensible when p is 0 or 1 (e.g. 100 wins in 100 games).
inline void wilson_ci95(double p, int n, double& lo, double& hi) {
    if (n <= 0) {
        lo = 0.0;
        hi = 1.0;
        return;
    }
    const double z = 1.96;
    const double z2 = z * z;
    const double nn = static_cast<double>(n);
    const double denom = 1.0 + z2 / nn;
    const double center = (p + z2 / (2.0 * nn)) / denom;
    const double half = z * std::sqrt(p * (1.0 - p) / nn + z2 / (4.0 * nn * nn)) / denom;
    lo = std::max(0.0, center - half);
    hi = std::min(1.0, center + half);
}

// The pct-th percentile (0..100) by the nearest-rank method. Works on a copy.
inline double percentile(std::vector<double> v, double pct) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    std::size_t rank = static_cast<std::size_t>(std::ceil(pct / 100.0 * static_cast<double>(v.size())));
    if (rank < 1) rank = 1;
    if (rank > v.size()) rank = v.size();
    return v[rank - 1];
}
