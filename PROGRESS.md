# PROGRESS

Project: Adaptive Virtual Loss for Lock-Free Parallel MCTS (our adaptive formula is an UNTESTED hypothesis).

## Machine
- Intel Core Ultra 5 125H, 14 cores / 18 logical processors, 16 GB RAM, Windows 11 + WSL2 (Ubuntu)
- Hybrid CPU (P-cores, E-cores, low-power E-cores): threads are not all equal. See notes below.

## Done
- Step 0: folder layout, Makefile, timing helper, hello_threads. Built and run in WSL2 (project linked as ~/mcts).
- Step 1 (built; test-connect4 PASS, 0 rule mismatches): Game interface (game.hpp), Connect-4 bitboard (connect4.hpp), per-thread RNG (rng.hpp), stats / run-info / CSV helpers, test_connect4.cpp.
- Step 2 (built; test-uct PASS): V0 sequential UCT (uct_v0.hpp), arena.hpp, test_uct.cpp (tactics, V0 vs random, first speed table).
- Step 3 (built; test-parallel PASS): V1 leaf parallel (uct_leaf_parallel.hpp), V2 root parallel (uct_root_parallel.hpp), test_parallel.cpp.

## Next
- Step 4 (built; race-demo and test-locked PASS; 20-game strength run for T=2,4 done): V3 shared tree with spin locks (uct_tree_locked.hpp, spinlock.hpp), race_demo.cpp, test_locked.cpp (speed + lock contention), strength_v3.cpp (V3 vs V0 equal time / equal playouts), arena.hpp play_match. Run: `make race-demo`, `make test-locked`, `make strength-v3 ARGS="20"` first, then the full 100 games.
- Step 5 (done: test-lockfree PASS; TSan control reported the planted race (2 warnings, so the tool works here with setarch -R); TSan on V3, V4 exact and V4 racy reported no warnings, 4 threads x 20000 iterations each): V4 lock-free (uct_tree_lockfree.hpp; exact fetch_add mode and racy load+store mode), team.hpp (OpenMP or std::thread launcher; V3 now uses it), test_lockfree.cpp (V0/V3/V4 speed in one run), tsan_check.cpp. Run: `make test-lockfree`, `make tsan-control`, `make tsan`.
- Step 6: fixed virtual loss, then V5 adaptive.

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
- Finding to report honestly: lock-based shared tree does not scale here (cheap ~0.17 us playouts, so tree/lock work dominates). Hypothesis (untested): cache-line bouncing on hot locks; V4 lock-free should reduce it.
- Step 5 correctness: V4 exact 0 lost updates (8 threads x 200000 iterations, also with a 4096-node pool); tactics 10/10 x4. V4 racy lost 14775-17984 root-level visits per 100000 iterations at 8 threads (5 runs).
- Step 5 speed (0.1 s, 10 repeats, playouts/sec; V0 = 2.05M): V3 locks T=1/2/4/8: 1.90M / 1.84M / 1.83M / 1.80M (0.88x V0 at T=8; an earlier run gave 1.50M at T=8, so cross-run variation exceeds the within-run std). V4 atomic add: 1.80M / 2.30M / 3.29M / 4.05M (2.26x own, 1.98x V0 at T=8, efficiency 0.28). V4 racy: 1.94M / 2.48M / 3.78M / 5.48M (2.82x own, 2.68x V0 at T=8).
- Step 5 V4 bookkeeping at T=8 (per 0.1 s search): exact mode 32.7k failed expansions (about 8% of iterations), 228k wasted nodes (34% of the 662k slots counted in 'tree nodes'; useful tree about 434k nodes for about 405k iterations, versus V0 428k nodes for about 205k iterations, so the tree grows slower per playout = duplicated work). Racy mode lost 17.3% of iterations' root visits at T=8 (6.9% at T=4, 1.7% at T=2): NOT rare at 8 threads; whether it hurts strength is untested.

## Notes
- User runs all builds/commands and pastes output; Claude edits code and gives instructions.
- Only claim results for thread counts the machine really has; report hyper-threads / E-cores separately.
