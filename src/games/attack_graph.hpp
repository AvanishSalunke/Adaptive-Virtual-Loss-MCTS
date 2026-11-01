// attack_graph.hpp - a simulated company network, an attacker who tries to
// cross it, and an exact solver to check the search against.
//
// This is a DEFENSIVE planning model on made-up data. It never touches a real
// system. The question it answers is "which attack paths are easiest, so the
// defender knows which links to fix first?".
//
// Model:
//   * hosts 0 .. n-1 joined by undirected links. Each link has a success
//     probability p (how likely the attacker is to get through it) and an
//     integer cost (effort, time or noise).
//   * the attacker starts at host 0 and wants to reach host n-1 (the critical
//     server) without spending more than `budget` in total cost, and without
//     visiting a host twice.
//   * a path's value is its success chance: the product of the p of its links
//     (the probability that every step works). The best path is the one with
//     the highest value within the budget. A playout needs no coin flips,
//     because the value is already that probability.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rng.hpp"

struct Network {
    int hosts = 0;
    int source = 0;
    int target = 0;
    int budget = 0;
    int links = 0;  // number of undirected links
    // Compressed adjacency lists: the links of host h are the entries
    // start[h] .. start[h + 1] - 1 of to / cost / prob.
    std::vector<int> start;
    std::vector<int> to;
    std::vector<int> cost;
    std::vector<double> prob;
    // Cheapest cost from each host to the target. The game uses it to skip
    // moves after which the target can no longer be reached within the budget.
    std::vector<int> to_target;

    // Index of the link a -> b, or -1.
    int link_index(int a, int b) const {
        for (int i = start[a]; i < start[a + 1]; ++i)
            if (to[i] == b) return i;
        return -1;
    }
};

// Cheapest total cost from host `from` to every host (Dijkstra, O(n^2): fine for <= 128 hosts).
inline std::vector<int> cost_distances(const Network& net, int from) {
    const int kFar = 1 << 29;
    std::vector<int> dist(static_cast<std::size_t>(net.hosts), kFar);
    std::vector<char> done(static_cast<std::size_t>(net.hosts), 0);
    dist[static_cast<std::size_t>(from)] = 0;
    for (int round = 0; round < net.hosts; ++round) {
        int u = -1;
        for (int v = 0; v < net.hosts; ++v)
            if (!done[static_cast<std::size_t>(v)] && (u < 0 || dist[static_cast<std::size_t>(v)] < dist[static_cast<std::size_t>(u)])) u = v;
        if (u < 0 || dist[static_cast<std::size_t>(u)] >= kFar) break;
        done[static_cast<std::size_t>(u)] = 1;
        for (int i = net.start[u]; i < net.start[u + 1]; ++i) {
            int& d = dist[static_cast<std::size_t>(net.to[i])];
            d = std::min(d, dist[static_cast<std::size_t>(u)] + net.cost[i]);
        }
    }
    return dist;
}

// Builds a random connected network from a seed (the same seed always gives
// the same network). 4 <= hosts <= 128. The budget is the cheapest possible
// path cost times budget_slack, so some room exists to trade cost for a safer route.
inline Network make_network(int hosts, uint64_t seed, double budget_slack = 1.5) {
    hosts = std::max(4, std::min(128, hosts));
    const int kMaxDegree = 12;  // keeps the number of moves per step small
    Rng rng(seed);
    auto uniform = [&rng]() { return static_cast<double>(rng.next() >> 11) * (1.0 / 9007199254740992.0); };

    struct Edge {
        int a, b, cost;
        double p;
    };
    std::vector<Edge> edges;
    std::vector<int> degree(static_cast<std::size_t>(hosts), 0);
    std::vector<char> exists(static_cast<std::size_t>(hosts) * static_cast<std::size_t>(hosts), 0);
    auto add_link = [&](int a, int b) {
        const int cost = 1 + static_cast<int>(rng.next_below(9));  // 1 .. 9
        const double p = 0.55 + 0.43 * uniform();                  // 0.55 .. 0.98
        edges.push_back({a, b, cost, p});
        exists[static_cast<std::size_t>(a * hosts + b)] = 1;
        exists[static_cast<std::size_t>(b * hosts + a)] = 1;
        ++degree[static_cast<std::size_t>(a)];
        ++degree[static_cast<std::size_t>(b)];
    };

    // A random spanning tree makes the network connected: each host links to an earlier one.
    for (int i = 1; i < hosts; ++i) {
        int j;
        do {
            j = static_cast<int>(rng.next_below(static_cast<uint32_t>(i)));
        } while (degree[static_cast<std::size_t>(j)] >= kMaxDegree);
        add_link(i, j);
    }
    // Extra links until the average number of links per host is about 4.4.
    const int wanted = static_cast<int>(2.2 * hosts);
    for (int attempts = 0; static_cast<int>(edges.size()) < wanted && attempts < 100000; ++attempts) {
        const int a = static_cast<int>(rng.next_below(static_cast<uint32_t>(hosts)));
        const int b = static_cast<int>(rng.next_below(static_cast<uint32_t>(hosts)));
        if (a == b || exists[static_cast<std::size_t>(a * hosts + b)] ||
            degree[static_cast<std::size_t>(a)] >= kMaxDegree || degree[static_cast<std::size_t>(b)] >= kMaxDegree)
            continue;
        add_link(a, b);
    }

    Network net;
    net.hosts = hosts;
    net.source = 0;
    net.target = hosts - 1;
    net.links = static_cast<int>(edges.size());
    net.start.assign(static_cast<std::size_t>(hosts) + 1, 0);
    for (int h = 0; h < hosts; ++h)
        net.start[static_cast<std::size_t>(h) + 1] = net.start[static_cast<std::size_t>(h)] + degree[static_cast<std::size_t>(h)];
    net.to.resize(edges.size() * 2);
    net.cost.resize(edges.size() * 2);
    net.prob.resize(edges.size() * 2);
    std::vector<int> fill(net.start.begin(), net.start.end() - 1);
    auto put = [&](int from, int to, const Edge& e) {
        const int i = fill[static_cast<std::size_t>(from)]++;
        net.to[static_cast<std::size_t>(i)] = to;
        net.cost[static_cast<std::size_t>(i)] = e.cost;
        net.prob[static_cast<std::size_t>(i)] = e.p;
    };
    for (const Edge& e : edges) {
        put(e.a, e.b, e);
        put(e.b, e.a, e);
    }
    // Links are undirected with equal cost both ways, so the cheapest cost
    // from the target to the source is also the cheapest cost from the source to the target.
    net.to_target = cost_distances(net, net.target);
    net.budget = static_cast<int>(std::ceil(net.to_target[static_cast<std::size_t>(net.source)] * budget_slack));
    return net;
}

// The attacker as a one-player game, in the shape the search expects (see
// game.hpp). A move is "go to host h". The state is small and cheap to copy.
class AttackGame {
public:
    using Move = int;  // the host to move to
    static constexpr int kMaxMoves = 16;  // networks have at most 12 links per host

    explicit AttackGame(const Network* net) : net_(net), host_(net->source) {
        visited_[0] = 0;
        visited_[1] = 0;
        mark(host_);
        update_dead();
    }

    int current_player() const { return 0; }  // only one player
    bool is_over() const { return reached_ || dead_; }

    // Links to hosts not visited yet, after which the target can still be
    // reached within the remaining budget (even by the cheapest route). Moves
    // that can no longer succeed are skipped; they could never be part of a
    // path counted by the exact solver either, so the best path does not change.
    int legal_moves(Move* out) const {
        int n = 0;
        for (int i = net_->start[host_]; i < net_->start[host_ + 1]; ++i) {
            const int h = net_->to[i];
            if (!seen(h) && cost_ + net_->cost[i] + net_->to_target[static_cast<std::size_t>(h)] <= net_->budget)
                out[n++] = h;
        }
        return n;
    }

    void play(Move next) {
        const int i = net_->link_index(host_, next);
        cost_ += net_->cost[i];
        prob_ *= net_->prob[i];
        host_ = next;
        mark(next);
        if (host_ == net_->target) reached_ = true;
        else update_dead();
    }

    // 1-player game: the "result" is the path's success chance, or 0 if the
    // target was not reached. For player 1 it is the complement (not used).
    double result(int player) const {
        const double r = success_probability();
        return player == 0 ? r : 1.0 - r;
    }

    double success_probability() const { return reached_ ? prob_ : 0.0; }
    int cost_used() const { return cost_; }
    int host() const { return host_; }

private:
    bool seen(int h) const { return ((visited_[h >> 6] >> (h & 63)) & 1ULL) != 0; }
    void mark(int h) { visited_[h >> 6] |= (1ULL << (h & 63)); }
    void update_dead() {
        Move tmp[kMaxMoves];
        dead_ = (legal_moves(tmp) == 0);
    }

    const Network* net_;
    int host_;
    int cost_ = 0;
    double prob_ = 1.0;
    uint64_t visited_[2];
    bool reached_ = false;
    bool dead_ = false;
};

// ---------------------------------------------------------------------------
// Exact solver. The best path is found by dynamic programming over (host,
// budget left), and the k best paths by a depth-first search that uses the
// same table to skip every branch that cannot beat the k-th best path.
// ---------------------------------------------------------------------------
struct PathInfo {
    std::vector<int> hosts;  // from the source to the target
    double prob = 0.0;
    int cost = 0;
};

class ExactSolver {
public:
    explicit ExactSolver(const Network& net)
        : net_(net), width_(net.budget + 1),
          up_(static_cast<std::size_t>(net.hosts) * static_cast<std::size_t>(net.budget + 1), 0.0) {
        // up(v, c) = the highest success chance of any route from v to the
        // target that costs at most c (hosts may repeat in this relaxation, which
        // never helps: a loop only adds cost and multiplies in a probability <= 1).
        for (int c = 0; c <= net.budget; ++c) at(net.target, c) = 1.0;
        for (int c = 0; c <= net.budget; ++c) {
            for (int v = 0; v < net.hosts; ++v) {
                if (v == net.target) continue;
                double best = (c > 0) ? at(v, c - 1) : 0.0;
                for (int i = net.start[v]; i < net.start[v + 1]; ++i) {
                    const int w = net.cost[i];
                    if (w <= c) best = std::max(best, net.prob[i] * at(net.to[i], c - w));
                }
                at(v, c) = best;
            }
        }
    }

    // Success chance of the best path within the budget.
    double best_probability() const { return at(net_.source, net_.budget); }

    // The k best simple paths within the budget, best first.
    std::vector<PathInfo> top_paths(int k) const {
        std::vector<PathInfo> best;
        std::vector<int> path{net_.source};
        std::vector<char> seen(static_cast<std::size_t>(net_.hosts), 0);
        seen[static_cast<std::size_t>(net_.source)] = 1;
        dfs(net_.source, 0, 1.0, path, seen, k, best);
        return best;
    }

private:
    double& at(int v, int c) { return up_[static_cast<std::size_t>(v) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(c)]; }
    double at(int v, int c) const { return up_[static_cast<std::size_t>(v) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(c)]; }

    void dfs(int v, int cost, double prob, std::vector<int>& path, std::vector<char>& seen, int k,
             std::vector<PathInfo>& best) const {
        if (v == net_.target) {
            if (static_cast<int>(best.size()) < k || prob > best.back().prob) {
                PathInfo info;
                info.hosts = path;
                info.prob = prob;
                info.cost = cost;
                best.push_back(info);
                std::sort(best.begin(), best.end(), [](const PathInfo& a, const PathInfo& b) { return a.prob > b.prob; });
                if (static_cast<int>(best.size()) > k) best.pop_back();
            }
            return;
        }
        // Neighbours, most promising first. "value" is an upper bound on any path through that link.
        struct Candidate {
            int host;
            int link;
            double value;
        };
        Candidate cands[16];
        int n = 0;
        for (int i = net_.start[v]; i < net_.start[v + 1]; ++i) {
            const int h = net_.to[i];
            if (seen[static_cast<std::size_t>(h)] || cost + net_.cost[i] > net_.budget) continue;
            cands[n++] = {h, i, prob * net_.prob[i] * at(h, net_.budget - cost - net_.cost[i])};
        }
        std::sort(cands, cands + n, [](const Candidate& a, const Candidate& b) { return a.value > b.value; });
        for (int j = 0; j < n; ++j) {
            if (static_cast<int>(best.size()) == k && cands[j].value <= best.back().prob) break;  // the rest are worse
            const int i = cands[j].link;
            seen[static_cast<std::size_t>(cands[j].host)] = 1;
            path.push_back(cands[j].host);
            dfs(cands[j].host, cost + net_.cost[i], prob * net_.prob[i], path, seen, k, best);
            path.pop_back();
            seen[static_cast<std::size_t>(cands[j].host)] = 0;
        }
    }

    const Network& net_;
    int width_;
    std::vector<double> up_;
};
