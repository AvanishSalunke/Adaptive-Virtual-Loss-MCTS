# PROGRESS

Project: Adaptive Virtual Loss for Lock-Free Parallel MCTS (our adaptive formula is an UNTESTED hypothesis).

## Machine
- Intel Core Ultra 5 125H, 14 cores / 18 logical processors, 16 GB RAM, Windows 11 + WSL2 (Ubuntu)
- Hybrid CPU (P-cores, E-cores, low-power E-cores): threads are not all equal; 8-thread results are noisy.

## Viva folder
- `project files/`: a self-contained COPY of everything needed to run the final adaptive virtual loss comparison on Connect-4 (compare.cpp, game/, search/, support/, Makefile, README.txt, saved results). Run `make` then `make run` inside it: it plays ONLY V5 adaptive vs V4 fixed (20 games, about a minute; `make run-full` = 400 games, about 15 minutes). Its compare.cpp is a trimmed version of src/benchmarks/compare.cpp. It is a snapshot: the master code is in src/. Not compile-tested yet (user asked not to run anything); all #include targets were checked to exist.
- Known open item: the graph planner's final comparison was not re-run after fixing the fixed-point reward bug in uct_tree_lockfree.hpp (planner_final.csv does not exist); do not quote planner comparison numbers. Connect-4 results are unaffected by that bug.

## Source layout (src/)
- util/        timing.hpp, rng.hpp, stats.hpp, run_info.hpp, csv_log.hpp
- parallel/    team.hpp (OpenMP or std::thread launcher)
- games/       game.hpp (interface), connect4.hpp, arena.hpp (match play)
- search/      only what the final comparison needs: uct_v0.hpp (V0), uct_tree_lockfree.hpp (V4; with fixed or adaptive virtual loss = V4 + fixed / V5), virtual_loss.hpp (penalty rule)
- tests/       test_connect4, test_uct (basic correctness)
- benchmarks/  compare.cpp: the final comparison (tuning run, then frozen final run)
- experiments/ everything not needed by the final comparison: V1 leaf parallel (uct_leaf_parallel.hpp), V2 root parallel (uct_root_parallel.hpp), V3 locks (uct_tree_locked.hpp), spinlock.hpp, and the finished measurements kept as evidence for the paper: hello_threads, race_demo, test_parallel, test_locked, test_lockfree, test_virtual_loss, strength_v3, tsan_check (`make experiments` builds them). V2 comes back into play only if the graph planner comparison (V0, V2, V4, V5 in the proposal) needs it.
Headers are found through -I flags in the Makefile, so sources include them by bare name. Results go to results/*.csv.

## Done
- Step 0: setup, timer, hello_threads. WSL2 project linked as ~/mcts.
- Step 1: Game interface, Connect-4 bitboard, per-thread RNG, helpers (test-connect4 PASS, 0 rule mismatches).
- Step 2: V0 sequential UCT (test-uct PASS).
- Step 3: V1 leaf parallel, V2 root parallel (test-parallel PASS).
- Step 4: V3 shared tree with spin locks, race demo, lock contention, 20-game strength run for T=2,4 (race-demo, test-locked PASS).
- Step 5: V4 lock-free (exact and racy modes); TSan: control reported the planted race, V3 and V4 reported no warnings (4 threads x 20000 iterations). test-lockfree PASS.
- Source files regrouped into topic folders; clean rebuild OK.
- Step 6: virtual loss added to V4 as an option: None / Fixed(v) / Adaptive(lambda). test-virtual-loss PASS (tactics, 0 lost updates, tiny pool OK).

## Next
- FOCUS (user request): stop adding experiments. The remaining job is to compare the final V5 against V4 + fixed (and V0). `compare.cpp` is written and smoke-tested (2 games). Plan: `make compare-tune ARGS="8 100"` (tuning run: DONE, see results), freeze lambda and fixed v (FROZEN by the rule "highest tuning score": lambda=0.5, fixed v=5; user approved; final run started: `compare final 8 400 0.5 5`, results/compare_final.csv), then `make compare-final ARGS="8 400 <lambda> <v>"` (~20 min). Then plots/tables, graph planner, paper.
- FINAL RUN DONE (see results). Next: graph planner demo (Step 8), then combined tables and plots, then the paper.
- Step 7 (reduced): only what the final comparison needs. The strength-speedup table (V0 at 0.1/0.2/0.4/0.8 s) and plots come after the final run, if time allows. V4 racy vs exact strength is optional.
- Step 8: graph model, exact solver, MCTS on the graph, ranked weak paths, graph-planner table.
- Step 9: lambda sweep on a SEPARATE tuning run, freeze it, tune the fixed baseline too, full experiment, final tables and plots, honest discussion.
- Step 10: LaTeX paper, README, GitHub-ready repo. User must verify reference [4].

## Results so far
- hello_threads, 4 runs (400M steps): 1 thread ~373-380 ms; 2 threads speedup 1.96-2.01; 4 threads 3.84-3.92; 8 threads 5.62-6.67 (noisy: depends on which cores the OS picks on this hybrid CPU).
- Connect-4 random games (1000 games, seed 42, 1 thread): P1 563 / P2 434 / draws 3; P1 win rate 56.3% +/- 3.1% (normal-approx 95% CI); mean game length 21.21 moves; 0 rule mismatches vs reference.
- Random playout speed, 1 thread (5 repeats x 100000 games): 5.91M +/- 0.19M playouts/sec (no tree; real MCTS will be much slower per playout).
- V0 tactics: win-in-1 and block-in-1 found 20/20 seeds in all 4 cases (20000 iterations).
- V0 vs Random (100 games, colours alternate, 0.02 s/move, seeds 2024/777): 100 W / 0 D / 0 L, Wilson 95% CI 96.3%-100%.
- V0 speed (empty board, 0.1 s/move, 20 repeats, 1 thread): 2.19M +/- 0.06M playouts/sec; latency 100.01 ms mean and p95; ~457k tree nodes per search; node pool never exhausted. This is the 1-thread baseline for all speedups.
- Empty-board search (seed 1, 0.1 s): centre column got 153429 of 175936 root visits (87%).
- Step 3 speed (0.1 s/move, 10 repeats, playouts/sec; speedup vs own 1 thread; rate vs V0 1.00 = 2.15M): V1 leaf 1 playout/thread: 1.62M (T=1) -> 3.28M (T=8), 2.02x own, 1.52x V0. V1 leaf 16 playouts/thread: 6.09M -> 16.7M, 2.74x own, 7.77x V0 (but tree only ~28k nodes at T=8: playouts are less informative). V2 root: 1.82M (T=1, 0.85x V0, std 7%, unexplained) -> 9.17M (T=8), 5.04x own, 4.27x V0. V2 T=4: one repeat took 108 ms (p95 of 10 runs = the max). All tactics passed 10/10 for V1 and V2 with 4 threads.
- Step 4 race demo (200000 increments/thread, 5 repeats): unprotected lost 549k-595k of 800k at 4 threads and 407k-823k of 1.6M at 8 threads; spinlock and atomic lost 0. Mean time at 8 threads: unprotected 3.8 ms, atomic 42.5 ms, spinlock 186.3 ms (1 thread: 0.2 / 0.9 / 1.2 ms).
- Step 4 V3 (locks) correctness: 0 lost updates (8 threads x 200000 iterations; also with a tiny 4096-node pool); tactics 10/10 x4.
- Step 4 V3 speed (0.1 s, 10 repeats, playouts/sec): V0 2.08M; V3 T=1 1.85M (0.89x V0), T=2 1.80M, T=4 1.87M, T=8 1.50M (0.72x V0). V3 gives NO speedup on this machine. Lock acquisitions ~15-16 per iteration; contended: 4.6% (T=2), 14.9% (T=4), 25.2% (T=8).
- Step 4 strength (20 games per match only, so CIs about +/-20 points): V3 vs V0 equal time: T=2 57.5% [36.4, 76.2], T=4 52.5% [32.0, 72.1]; equal playouts (V0 0.09 s): T=2 52.5%, T=4 62.5% [40.9, 80.0]. All consistent with 50%. The 8-thread match and the full 100-game runs are NOT done; do them in the Step 7 harness.
- Finding to report honestly: lock-based shared tree does not scale here (cheap ~0.17 us playouts, so tree/lock work dominates). Hypothesis (untested): cache-line bouncing on hot locks.
- Step 5 correctness: V4 exact 0 lost updates (8 threads x 200000 iterations, also with a 4096-node pool); tactics 10/10 x4. V4 racy lost 14775-17984 root-level visits per 100000 iterations at 8 threads (5 runs).
- Step 5 speed (0.1 s, 10 repeats, playouts/sec; V0 = 2.05M): V3 locks T=1/2/4/8: 1.90M / 1.84M / 1.83M / 1.80M (0.88x V0 at T=8; an earlier run gave 1.50M at T=8, so cross-run variation exceeds the within-run std). V4 atomic add: 1.80M / 2.30M / 3.29M / 4.05M (2.26x own, 1.98x V0 at T=8, efficiency 0.28). V4 racy: 1.94M / 2.48M / 3.78M / 5.48M (2.82x own, 2.68x V0 at T=8). A rerun after the folder move gave the same picture (V4 exact 4.02M, racy 5.36M at T=8).
- Step 5 V4 bookkeeping at T=8 (per 0.1 s search): exact mode ~8% of iterations hit a failed expansion; about a third of allocated node slots wasted. Racy mode lost 16-17% of root visits at T=8 (about 7% at T=4, 1-2% at T=2): NOT rare at 8 threads; whether it hurts strength is untested.
- Step 6 duplicated work at T=8 (V4, 0.1 s, 10 repeats; failed expansions % of iterations / wasted slots % / useful nodes per iteration; 1 thread has 2.10): no virtual loss 5.50 / 37.0 / 0.65; fixed v=1 2.07 / 6.64 / 2.03; fixed v=3 1.71 / 5.54 / 2.04; adaptive lambda=1 (UNTUNED placeholder) 2.81 / 8.94 / 1.99. Virtual loss restores tree growth per iteration to roughly the sequential value. Strength NOT measured yet.
- Step 6 speed at T=8 (playouts/sec): none 4.04M, fixed v=1 3.85M, fixed v=3 3.98M, adaptive lambda=1 3.78M; efficiency about 0.27 for all. Virtual loss does not raise raw speed (slightly lower, about 2-6%).
- Step 6 worked example (A: 60 visits, 39 wins; B: 40 visits, 22 wins): without penalty the second thread picks A (A 1.0418 vs B 1.0299); with v=1 it picks B (1.0283 vs 1.0304); with v=3 it picks B (1.0026 vs 1.0314).
- TUNING RUN (results/compare_tune.csv; 8 threads vs V0 1 thread, 100 games each, 0.1 s/move, tuning openings seed 100000; NOT the final result). Win rate [95% CI]: fixed v=1 63.5% [53.7, 72.3]; v=2 55.5% [45.7, 64.9]; v=3 57.0% [47.2, 66.3]; v=5 65.0% [55.3, 73.6]; adaptive lambda=0.5 67.5% [57.8, 75.9]; lambda=1 64.0% [54.2, 72.7]; lambda=2 63.0% [53.2, 71.8]; lambda=4 64.5% [54.7, 73.2]. All intervals overlap: no setting is distinguishable from another; the fixed results are not monotonic in v (63.5, 55.5, 57.0, 65.0), a sign of noise. Mean over the 8 candidates 62.5%. Highest scores: fixed v=5, adaptive lambda=0.5 (both at the edge of their sweep). Note lambda=0.5 at T=8 gives penalty 4, 2, 1 for n=0, 3, >=8, so it behaves almost like fixed v=1 for most nodes.
- FINAL COMPARISON (results/compare_final.csv; frozen lambda=0.5 and fixed v=5; 8 threads for V4/V5, V0 1 thread; 400 games per pairing; 0.1 s/move; openings seed 1, different from tuning; commit f923ce0-dirty). V5 vs V4 + fixed: 163 W / 45 D / 192 L = 46.4% [95% CI 41.5, 51.3]: NO detectable difference (point estimate slightly below 50%). V5 vs V0: 202/37/161 = 55.1% [50.2, 59.9]. V4 + fixed vs V0: 213/39/148 = 58.1% [53.2, 62.9]. The adaptive hypothesis is NOT supported by this measurement (a valid negative/null result). Tuning-run scores for the same two settings vs V0 (67.5% and 65.0%) were higher than in the final run (55.1% and 58.1%): winner's curse from picking the best of 4 noisy candidates. Not tested: plain V4 without virtual loss, other thread counts, other time budgets, other games.

## Notes
- Claude may run the make build/test commands itself (WSL: `wsl bash -lc 'cd ~/mcts && make ...'`); nothing else without asking. The user commits.
- Only claim results for thread counts the machine really has; report hyper-threads / E-cores separately.
- make may print "Clock skew detected": WSL and Windows file times differ by milliseconds; harmless (run `make clean` if a rebuild seems skipped).
