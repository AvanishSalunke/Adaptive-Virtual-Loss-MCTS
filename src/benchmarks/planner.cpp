// planner.cpp - the attack-path planner: the same search engine, a different game.
// A DEFENSIVE planning model on simulated, made-up networks (see attack_graph.hpp).
//
//   planner demo  <hosts> <seed> [lambda]
//       One network: the exact ranking of the easiest attack paths, what the
//       search finds (V5, 8 threads, 0.1 s), and the links to fix first.
//
//   planner tune
//       Tries several virtual-loss settings on TUNING networks (seeds 1000+).
//
//   planner final <lambda> <fixed_v>
//       With the settings frozen, compares V0, V4 + fixed and V5 on separate
//       TEST networks (seeds 1+): 15 networks of 30, 50 and 100 hosts.
//
// For every network the exact solver gives the true best path and the true
// top 3. A "run" is one search of a given time budget from the start host.
// The plan a run recommends is the path of most-visited moves from the root.
//   * found best  : that plan has the best path's success chance
//   * regret      : best chance minus the plan's chance (0 if it never reaches the target)
//   * top-3 agreement : how many of the true top 3 paths are among the 3 most
//                       likely complete paths the search discovered
// Results are averaged per network, then over networks; "SE" is the standard
// error over networks. The exploration constant is set by the PLANNER_C
// environment variable (default 0.3: rewards here are small success chances,
// so the Connect-4 value of 1.41 would over-explore).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "attack_graph.hpp"
#include "csv_log.hpp"
#include "stats.hpp"
#include "uct_tree_lockfree.hpp"
#include "uct_v0.hpp"
#include "virtual_loss.hpp"

using Seq = UctSearcher<AttackGame>;
using Par = LockFreeTreeUct<AttackGame>;

static double g_c = 0.3;  // exploration constant (see the header comment)
static const int kThreads = 8;
static const int kRepeats = 10;

struct Case {
    Network net;
    double best_prob = 0.0;
    std::vector<PathInfo> top3;
};

static std::vector<Case> make_cases(uint64_t seed_base) {
    const int sizes[] = {30, 50, 100};
    std::vector<Case> cases;
    cases.reserve(15);
    for (int i = 0; i < 15; ++i) {
        Case c;
        c.net = make_network(sizes[i % 3], seed_base + static_cast<uint64_t>(i));
        const ExactSolver solver(c.net);
        c.best_prob = solver.best_probability();
        c.top3 = solver.top_paths(3);
        cases.push_back(std::move(c));
    }
    return cases;
}

static void print_cases(const std::vector<Case>& cases) {
    std::printf("| Network | Hosts | Links | Budget | Best path chance | 2nd | 3rd |\n");
    std::printf("|---------|-------|-------|--------|------------------|-----|-----|\n");
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        std::printf("| %7zu | %5d | %5d | %6d | %16.4f | %.4f | %.4f |\n", i + 1, c.net.hosts,
                    c.net.links, c.net.budget, c.best_prob, c.top3.size() > 1 ? c.top3[1].prob : 0.0,
                    c.top3.size() > 2 ? c.top3[2].prob : 0.0);
    }
}

struct Found {
    double prob;
    std::vector<int> hosts;  // from the source
};

// Keeps the k most likely complete paths seen so far, best first.
static void keep_top(std::vector<Found>& top, std::size_t k, double prob, const std::vector<int>& path,
                     int source) {
    if (top.size() == k && prob <= top.back().prob) return;
    Found f;
    f.prob = prob;
    f.hosts.push_back(source);
    f.hosts.insert(f.hosts.end(), path.begin(), path.end());
    top.push_back(f);
    std::sort(top.begin(), top.end(), [](const Found& a, const Found& b) { return a.prob > b.prob; });
    if (top.size() > k) top.pop_back();
}

struct RunResult {
    double rate = 0.0;
    double regret = 0.0;
    bool found = false;
    double top3 = 0.0;
    bool pool_full = false;
};

template <class Searcher>
static RunResult run_once(Searcher& s, const Case& c, double seconds) {
    const AttackGame start(&c.net);
    Seq::Limit limit;
    limit.seconds = seconds;
    s.search(start, limit);

    RunResult r;
    r.rate = static_cast<double>(s.iterations()) / s.elapsed_sec();
    AttackGame plan = start;
    for (int host : s.principal_path()) plan.play(host);
    const double p = plan.success_probability();
    r.regret = std::max(0.0, c.best_prob - p);
    r.found = p >= c.best_prob - 1e-12;

    std::vector<Found> top;
    s.for_each_finished(start, [&](const std::vector<int>& path, const AttackGame& end) {
        const double q = end.success_probability();
        if (q > 0.0) keep_top(top, 3, q, path, c.net.source);
    });
    int match = 0;
    for (const Found& f : top)
        for (const PathInfo& e : c.top3)
            if (f.hosts == e.hosts) {
                ++match;
                break;
            }
    r.top3 = c.top3.empty() ? 0.0 : static_cast<double>(match) / static_cast<double>(c.top3.size());
    r.pool_full = s.pool_full();
    return r;
}

struct Aggregate {
    double rate = 0.0;
    std::vector<double> found, regret, top3;  // one value per network (mean over the repeats)
    bool pool_full = false;
};

template <class Searcher>
static Aggregate evaluate(Searcher& s, const std::vector<Case>& cases, double seconds, uint64_t seed_base) {
    Aggregate agg;
    run_once(s, cases[0], seconds);  // warm-up, discarded
    double rate_sum = 0.0;
    int runs = 0;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        double found = 0.0, regret = 0.0, top3 = 0.0;
        for (int r = 0; r < kRepeats; ++r) {
            s.reseed(seed_base + 1000 * c + static_cast<uint64_t>(r));
            const RunResult rr = run_once(s, cases[c], seconds);
            found += rr.found ? 1.0 : 0.0;
            regret += rr.regret;
            top3 += rr.top3;
            rate_sum += rr.rate;
            ++runs;
            agg.pool_full = agg.pool_full || rr.pool_full;
        }
        agg.found.push_back(found / kRepeats);
        agg.regret.push_back(regret / kRepeats);
        agg.top3.push_back(top3 / kRepeats);
    }
    agg.rate = rate_sum / runs;
    return agg;
}

static void mean_se(const std::vector<double>& v, double& mean, double& se) {
    double sd = 0.0;
    mean_std(v, mean, sd);
    se = v.size() > 1 ? sd / std::sqrt(static_cast<double>(v.size())) : 0.0;
}

static void print_header() {
    std::printf("| Version | Threads | Time budget | Playouts/sec | %% runs finding the true best path (+/- SE) | Mean regret vs exact (+/- SE) | Top-3 path agreement (+/- SE) |\n");
    std::printf("|---------|---------|-------------|--------------|-------------------------------------------|-------------------------------|-------------------------------|\n");
}

static void report(const char* mode, const std::string& version, int threads, double seconds,
                   const Aggregate& a, uint64_t net_seed_base, const RunInfo& info, const char* csv) {
    double f, fse, rg, rse, t3, t3se;
    mean_se(a.found, f, fse);
    mean_se(a.regret, rg, rse);
    mean_se(a.top3, t3, t3se);
    std::printf("| %-34s | %7d | %9.3f s | %12.0f | %27.1f%% +/- %5.1f | %16.4f +/- %8.4f | %14.2f +/- %6.2f |\n",
                version.c_str(), threads, seconds, a.rate, 100.0 * f, 100.0 * fse, rg, rse, t3, t3se);
    std::fflush(stdout);
    append_csv(csv, info,
               {"mode", "version", "threads", "time_budget_s", "networks", "repeats", "playouts_per_sec",
                "found_best_pct", "found_best_se_pct", "mean_regret", "mean_regret_se",
                "top3_agreement", "top3_agreement_se", "exploration_c", "network_seed_base",
                "pool_exhausted"},
               {mode, version, std::to_string(threads), fmt(seconds, 3), std::to_string(a.found.size()),
                std::to_string(kRepeats), fmt(a.rate, 0), fmt(100.0 * f, 2), fmt(100.0 * fse, 2),
                fmt(rg, 5), fmt(rse, 5), fmt(t3, 4), fmt(t3se, 4), fmt(g_c, 3),
                std::to_string(net_seed_base), a.pool_full ? "yes" : "no"});
}

static std::string v5_name(double lambda) { return "V5 adaptive (lambda=" + fmt(lambda, 1) + ")"; }
static std::string v4_name(int v) { return "V4 + fixed virtual loss (v=" + std::to_string(v) + ")"; }

// Paired comparison over networks: a - b, with its 95% interval.
static void print_paired(const char* what, const std::vector<double>& a, const std::vector<double>& b,
                         bool lower_is_better) {
    std::vector<double> diff;
    for (std::size_t i = 0; i < a.size(); ++i) diff.push_back(a[i] - b[i]);
    double mean, se;
    mean_se(diff, mean, se);
    const double lo = mean - 1.96 * se, hi = mean + 1.96 * se;
    const char* verdict = "no detectable difference";
    if (lower_is_better) {
        if (hi < 0.0) verdict = "V5 better";
        else if (lo > 0.0) verdict = "V5 worse";
    } else {
        if (lo > 0.0) verdict = "V5 better";
        else if (hi < 0.0) verdict = "V5 worse";
    }
    std::printf("  %-34s V5 - V4 = %+.4f (95%% interval %+.4f to %+.4f): %s\n", what, mean, lo, hi, verdict);
}

static void run_tune(const RunInfo& info) {
    const uint64_t kNetSeed = 1000;  // tuning networks; the final run uses different ones
    const double kBudget = 0.05;
    const std::vector<Case> cases = make_cases(kNetSeed);
    std::printf("TUNING RUN (not the final result). %zu tuning networks (30, 50, 100 hosts), %d runs per network, "
                "time budget %.2f s, %d threads, exploration constant %.2f, network seeds %llu+\n\n",
                cases.size(), kRepeats, kBudget, kThreads, g_c, static_cast<unsigned long long>(kNetSeed));
    print_header();

    {
        Seq s(7000, 1u << 23, g_c);
        report("tune", "V0 sequential UCT (reference)", 1, kBudget, evaluate(s, cases, kBudget, 777), kNetSeed,
               info, "results/planner_tune.csv");
    }
    double best_fixed = 1e9, best_adaptive = 1e9;
    std::string best_fixed_name, best_adaptive_name;
    int idx = 0;
    for (int v : {1, 3, 5, 10}) {
        Par s(kThreads, 7100 + static_cast<uint64_t>(idx++), true, 1u << 23, g_c, VirtualLoss::Fixed(v));
        const Aggregate a = evaluate(s, cases, kBudget, 777);
        report("tune", v4_name(v), kThreads, kBudget, a, kNetSeed, info, "results/planner_tune.csv");
        double rg, se;
        mean_se(a.regret, rg, se);
        if (rg < best_fixed) {
            best_fixed = rg;
            best_fixed_name = v4_name(v);
        }
    }
    for (double lambda : {0.5, 1.0, 2.0, 4.0}) {
        Par s(kThreads, 7100 + static_cast<uint64_t>(idx++), true, 1u << 23, g_c, VirtualLoss::Adaptive(lambda));
        const Aggregate a = evaluate(s, cases, kBudget, 777);
        report("tune", v5_name(lambda), kThreads, kBudget, a, kNetSeed, info, "results/planner_tune.csv");
        double rg, se;
        mean_se(a.regret, rg, se);
        if (rg < best_adaptive) {
            best_adaptive = rg;
            best_adaptive_name = v5_name(lambda);
        }
    }
    std::printf("\nLowest mean regret among the fixed settings:    %s (%.4f)\n", best_fixed_name.c_str(), best_fixed);
    std::printf("Lowest mean regret among the adaptive settings: %s (%.4f)\n", best_adaptive_name.c_str(), best_adaptive);
    std::printf("Check the SE values before freezing a setting: a 'best' can be noise.\n");
}

static void run_final(double lambda, int fixed_v, const RunInfo& info) {
    const uint64_t kNetSeed = 1;
    const std::vector<Case> cases = make_cases(kNetSeed);
    std::printf("FINAL COMPARISON with frozen settings: lambda=%.2f, fixed v=%d. %zu test networks (30, 50, 100 "
                "hosts, seeds %llu+), %d runs per network, %d threads for V4/V5, exploration constant %.2f\n\n",
                lambda, fixed_v, cases.size(), static_cast<unsigned long long>(kNetSeed), kRepeats, kThreads, g_c);
    print_cases(cases);
    std::printf("\n");
    print_header();

    for (double budget : {0.005, 0.02, 0.1}) {
        Seq s0(8000, 1u << 23, g_c);
        report("final", "V0 sequential UCT", 1, budget, evaluate(s0, cases, budget, 888), kNetSeed, info,
               "results/planner_final.csv");
        // Plain V4 (no virtual loss): 1 thread should behave like V0; 8 threads separates
        // the effect of parallelism from the effect of virtual loss.
        Par plain1(1, 8050, true, 1u << 23, g_c, VirtualLoss::None());
        report("final", "V4 plain, no virtual loss", 1, budget, evaluate(plain1, cases, budget, 888), kNetSeed,
               info, "results/planner_final.csv");
        Par plain8(kThreads, 8060, true, 1u << 23, g_c, VirtualLoss::None());
        report("final", "V4 plain, no virtual loss", kThreads, budget, evaluate(plain8, cases, budget, 888),
               kNetSeed, info, "results/planner_final.csv");
        Par s4(kThreads, 8100, true, 1u << 23, g_c, VirtualLoss::Fixed(fixed_v));
        const Aggregate a4 = evaluate(s4, cases, budget, 888);
        report("final", v4_name(fixed_v), kThreads, budget, a4, kNetSeed, info, "results/planner_final.csv");
        Par s5(kThreads, 8200, true, 1u << 23, g_c, VirtualLoss::Adaptive(lambda));
        const Aggregate a5 = evaluate(s5, cases, budget, 888);
        report("final", v5_name(lambda), kThreads, budget, a5, kNetSeed, info, "results/planner_final.csv");
        std::printf("  Paired over the %zu networks at %.2f s:\n", cases.size(), budget);
        print_paired("mean regret (lower is better)", a5.regret, a4.regret, true);
        print_paired("found best (higher is better)", a5.found, a4.found, false);
        print_paired("top-3 agreement (higher is better)", a5.top3, a4.top3, false);
    }
}

static std::string path_text(const std::vector<int>& hosts) {
    std::string s;
    for (std::size_t i = 0; i < hosts.size(); ++i) s += (i ? " -> " : "") + std::to_string(hosts[i]);
    return s;
}

static void run_demo(int hosts, uint64_t seed, double lambda) {
    const Network net = make_network(hosts, seed);
    const ExactSolver solver(net);
    const std::vector<PathInfo> exact = solver.top_paths(5);
    std::printf("Simulated network (made-up data): %d hosts, %d links. Attacker starts at host %d and wants "
                "host %d within a cost budget of %d.\n\n", net.hosts, net.links, net.source, net.target,
                net.budget);

    std::printf("EXACT ranking of the easiest attack paths (highest success chance first):\n");
    for (std::size_t i = 0; i < exact.size(); ++i)
        std::printf("  #%zu  success chance %.4f, cost %2d: %s\n", i + 1, exact[i].prob, exact[i].cost,
                    path_text(exact[i].hosts).c_str());

    Par s(kThreads, 4242, true, 1u << 23, g_c, VirtualLoss::Adaptive(lambda));
    const AttackGame start(&net);
    Seq::Limit limit;
    limit.seconds = 0.1;
    s.search(start, limit);
    std::vector<Found> found;
    s.for_each_finished(start, [&](const std::vector<int>& path, const AttackGame& end) {
        const double q = end.success_probability();
        if (q > 0.0) keep_top(found, 5, q, path, net.source);
    });
    std::printf("\nFOUND by the search (V5, lambda=%.2f, %d threads, 0.1 s):\n", lambda, kThreads);
    for (std::size_t i = 0; i < found.size(); ++i) {
        std::string note = "not in the exact top 5";
        for (std::size_t j = 0; j < exact.size(); ++j)
            if (found[i].hosts == exact[j].hosts) note = "exact rank #" + std::to_string(j + 1);
        std::printf("  #%zu  success chance %.4f (%s): %s\n", i + 1, found[i].prob, note.c_str(),
                    path_text(found[i].hosts).c_str());
    }

    std::map<std::pair<int, int>, int> uses;
    for (const PathInfo& p : exact)
        for (std::size_t i = 0; i + 1 < p.hosts.size(); ++i)
            ++uses[{std::min(p.hosts[i], p.hosts[i + 1]), std::max(p.hosts[i], p.hosts[i + 1])}];
    std::vector<std::pair<std::pair<int, int>, int>> ranked(uses.begin(), uses.end());
    std::sort(ranked.begin(), ranked.end(), [&](const auto& x, const auto& y) {
        if (x.second != y.second) return x.second > y.second;
        return net.prob[static_cast<std::size_t>(net.link_index(x.first.first, x.first.second))] >
               net.prob[static_cast<std::size_t>(net.link_index(y.first.first, y.first.second))];
    });
    std::printf("\nLinks to fix first (used by the most of the %zu easiest paths, then easiest to get through):\n",
                exact.size());
    for (std::size_t i = 0; i < ranked.size() && i < 5; ++i) {
        const int li = net.link_index(ranked[i].first.first, ranked[i].first.second);
        std::printf("  link %d - %d: on %d of %zu paths, success chance %.2f, cost %d\n", ranked[i].first.first,
                    ranked[i].first.second, ranked[i].second, exact.size(), net.prob[static_cast<std::size_t>(li)],
                    net.cost[static_cast<std::size_t>(li)]);
    }
}

int main(int argc, char** argv) {
    if (const char* env = std::getenv("PLANNER_C")) g_c = std::atof(env);
    const RunInfo info = collect_run_info();
    std::printf("Machine: %s | %d logical CPUs | %ld MB RAM\n", info.cpu_model.c_str(), info.logical_cpus,
                info.ram_mb);
    std::printf("Compiler: %s | git commit: %s\n\n", info.compiler.c_str(), info.git_commit.c_str());

    if (argc >= 4 && std::strcmp(argv[1], "demo") == 0)
        run_demo(std::atoi(argv[2]), static_cast<uint64_t>(std::atoll(argv[3])), argc >= 5 ? std::atof(argv[4]) : 0.5);
    else if (argc >= 2 && std::strcmp(argv[1], "tune") == 0)
        run_tune(info);
    else if (argc >= 4 && std::strcmp(argv[1], "final") == 0)
        run_final(std::atof(argv[2]), std::atoi(argv[3]), info);
    else {
        std::printf("usage: planner demo  <hosts> <seed> [lambda]\n"
                    "       planner tune\n"
                    "       planner final <lambda> <fixed_v>\n");
        return 1;
    }
    return 0;
}
