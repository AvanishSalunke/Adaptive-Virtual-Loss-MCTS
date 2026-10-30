// stats.hpp - small statistics helpers used when reporting results.
#pragma once
#include <cmath>
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
