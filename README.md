# Adaptive Virtual Loss for Lock-Free Parallel MCTS

Making Monte Carlo Tree Search faster on many cores without making it play worse.

We proposed an adaptive virtual-loss penalty and tested it against a fixed one. In our tests it did **not** give a detectable improvement. The results, and how we measured them, are below.

## What is MCTS?

MCTS is a way for a program to pick a good move without checking every possibility. It runs lots of quick random games, keeps score, and slowly focuses on the moves that keep winning.

Each round does four things:

1. Walk down the tree to a promising spot.
2. Add a new node there.
3. Play a random game from it to the end.
4. Send the result back up the tree.

The move that ends up with the most visits is the one it plays.

To decide where to walk, it uses the UCT score:

```
wins/visits + C * sqrt( ln(parent visits) / visits )
```

The first part favours moves that have been winning. The second part favours moves that haven't been tried much.

Think of it as choosing between restaurants with only a few dinners to spend: try one, write down how it went, go back to the good ones more often, and now and then try a place you've barely visited.

## Why use many threads?

More rounds per second means better moves, so running MCTS on several threads sounds like an easy win. It isn't, because every thread reads the same scores, sees the same "best" branch, and runs off to the same place. Most of that work is repeated and teaches us almost nothing.

The classic study (Chaslot et al., 2008) reports a shared tree that ran about 8 times faster on 16 threads but was only about 3.3 times as strong as one thread (numbers as quoted in our proposal; check against the paper). The speed is real, but a lot of it is wasted.

## Virtual loss

The usual fix is called virtual loss. When a thread goes down a branch, we pretend that branch has lost a few games. It looks worse for a moment, so the next thread goes somewhere else. Once the real result comes back, the pretend losses are removed.

A small example from our test program: branch A has 60 visits and 39 wins, branch B has 40 visits and 22 wins. With no penalty, the UCT score of A is 1.0418 and B is 1.0299, so a second thread also picks A. With a virtual loss of 1 on A the scores become 1.0283 and 1.0304, so the second thread picks B.

The catch is that the size of the penalty is a fixed number picked by hand. Too small and the threads still bunch up. Too big and they get pushed away from moves that really are good.

## Our idea

The right penalty depends on how much we already know about a branch.

- New branch, few visits: we know nothing, so push the threads apart.
- Well-visited branch: we already trust it, so a small penalty is enough.

So we let the penalty shrink as the visit count grows:

```
penalty = max(1, round( lambda * threads / sqrt(visits + 1) ))
```

`lambda` is a constant we tune. The formula lives in `project files/search/virtual_loss.hpp`.

## Locks vs lock-free

Threads editing the same node can mess each other up, so shared trees usually use locks, which make threads wait. We built both and measured them:

- **Locks (spin lock on each node):** no speedup on our machine. At 8 threads it ran slightly slower than the single-thread search, and about one lock request in four found the lock already taken.
- **Lock-free (atomic add):** about 2x the playouts per second of the single-thread search at 8 threads, with no lost updates. This is the version used in the final comparison.
- **A racy variant (read, then write):** we tried it too. It lost about 1 to 2% of the visit counts at 2 threads, about 4 to 7% at 4 threads and about 16 to 17% at 8 threads. That is not rare, so we did not use it.

## What we measured

All matches are played on Connect-4 (6x7). Both players get the same wall-clock time per move (0.1 s) and the same number of threads (8). Colours alternate, and each pair of games shares the same 4 random opening moves, so a lucky opening helps both sides equally. A draw counts as half a win. We report a 95% confidence interval (Wilson), and we say "no detectable difference" when it contains 50%.

Settings were chosen on a separate tuning run (different openings) and then frozen before the final games: adaptive `lambda = 0.5`, fixed `v = 5`.

### 1. Adaptive vs fixed virtual loss (400 games each)

| Match | Wins | Draws | Losses | Win rate | 95% interval |
|---|---|---|---|---|---|
| Adaptive vs fixed | 163 | 45 | 192 | 46.4% | 41.5% to 51.3% |
| Adaptive vs single thread | 202 | 37 | 161 | 55.1% | 50.2% to 59.9% |
| Fixed vs single thread | 213 | 39 | 148 | 58.1% | 53.2% to 62.9% |

Adaptive and fixed cannot be told apart. Both parallel versions beat the single-thread search.

### 2. Virtual loss vs plain parallel search (400 games each)

Each version plays the same parallel search with no virtual loss.

| Version | Wins | Draws | Losses | Win rate | 95% interval | Verdict |
|---|---|---|---|---|---|---|
| Fixed virtual loss (v=5) | 195 | 48 | 157 | 54.8% | 49.9% to 59.6% | no detectable difference |
| Adaptive virtual loss (lambda=0.5) | 202 | 33 | 165 | 54.6% | 49.7% to 59.4% | no detectable difference |

Both finished about 4.7 points ahead of plain parallel search, but the lower ends of the intervals (49.9% and 49.7%) just miss 50%. So virtual loss looks like a small help, which this test does not confirm. Adaptive did exactly as well as fixed, with no extra gain.

### Supporting measurements

- Virtual loss does reduce repeated work: at 8 threads the tree grew about 0.65 new nodes per search step without it and about 2.0 with it (the single-thread search gets about 2.1).
- It does not make the search faster: playouts per second are about the same, or a few percent lower.

### Limits of these results

One game (Connect-4), one thread count (8), one time per move (0.1 s), four values of lambda and four fixed values tried. The honest summary is "no benefit of the adaptive formula found in this setup", not "adaptive virtual loss never helps". The tuning-run scores for the chosen settings (65% to 67% against the single-thread search) were higher than in the final games (55% to 58%), which is why the final run uses different openings.

The simulated network attack-path planner (a made-up network, an exact solver to check against, and a ranked list of the easiest attack paths) is built and has a demo (`make planner-demo ARGS="50 7"`), but its final comparison is not finished, so we report no planner results.

## How to run

You need `g++` with OpenMP and `make` (we used g++ 13 on Ubuntu under WSL2).

The final adaptive vs fixed comparison is self-contained in `project files/`:

```
cd "project files"
make
make run          # quick run, 20 games (about a minute)
make run-full     # 400 games (about 15 minutes)
```

Virtual loss against plain parallel search, from the repository root:

```
make vl-effect        # 20 games per match
make vl-effect-full   # 400 games per match (about 30 minutes)
```

Results are appended to CSV files in `results/` with a timestamp, the machine and the git commit.

## Repository layout

- `project files/` - the files needed for the final adaptive vs fixed comparison, with their own Makefile and README.
- `src/util`, `src/parallel`, `src/games`, `src/search` - the engine: Connect-4, sequential and lock-free MCTS, and the virtual-loss rule.
- `src/tests`, `src/benchmarks` - correctness tests and the comparison programs.
- `src/experiments` - earlier parallel versions (leaf, root, locks) and the finished measurements, kept as evidence.
- `results/` - saved results.
- `PROGRESS.md` - a running log of what was done and measured.

## References

1. G. M. J.-B. Chaslot, M. H. M. Winands, H. J. van den Herik. *Parallel Monte-Carlo Tree Search.* Computers and Games (CG 2008), LNCS 5131, Springer, 2008, pp. 60-71.
2. S. A. Mirsoleimani, A. Plaat, J. van den Herik, J. Vermaseren. *An Analysis of Virtual Loss in Parallel MCTS.* Proc. ICAART 2017, pp. 648-652. DOI 10.5220/0006205806480652.
3. X. Yang, T. K. Aasawat, K. Yoshizoe. *Practical Massively Parallel Monte-Carlo Tree Search Applied to Molecular Design.* Proc. ICLR 2021. arXiv:2006.10504.
4. A. Nayak, P. C. Nissimagoudar, R. Revankar, P. Giraddi, S. Hosamani, N. C. Iyer, S. V. Budihal, Baswaraj. *Accelerating Decision-Making in AI: Parallelizing Monte Carlo Tree Search for Connect 4 Using CPU and GPU.* Procedia Computer Science, vol. 263, 2025, pp. 82-89. DOI 10.1016/j.procs.2025.07.011. (Not yet verified by the authors of this repository.)
