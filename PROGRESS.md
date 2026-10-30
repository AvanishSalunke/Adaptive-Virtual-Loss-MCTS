# PROGRESS

Project: Adaptive Virtual Loss for Lock-Free Parallel MCTS (our adaptive formula is an UNTESTED hypothesis).

## Machine
- Intel Core Ultra 5 125H, 14 cores / 18 logical processors, 16 GB RAM, Windows 11 + WSL2 (Ubuntu)
- Hybrid CPU (P-cores, E-cores, low-power E-cores): threads are not all equal. See notes below.

## Done
- Step 0: folder layout, Makefile, timing helper, hello_threads. Built and run in WSL2 (project linked as ~/mcts).
- Step 1 (built; test-connect4 PASS, 0 rule mismatches): Game interface (game.hpp), Connect-4 bitboard (connect4.hpp), per-thread RNG (rng.hpp), stats / run-info / CSV helpers, test_connect4.cpp.

## Next
- Step 2 (code written, waiting for user to build and run `make test-uct`): V0 sequential UCT (uct_v0.hpp), arena.hpp, test_uct.cpp (tactics, V0 vs random, first speed table).
- Step 3: V1 leaf parallel + V2 root parallel.

## Results so far
- hello_threads, 4 runs (400M steps): 1 thread ~373-380 ms; 2 threads speedup 1.96-2.01; 4 threads 3.84-3.92; 8 threads 5.62-6.67 (noisy: depends on which cores the OS picks on this hybrid CPU).
- Connect-4 random games (1000 games, seed 42, 1 thread): P1 563 / P2 434 / draws 3; P1 win rate 56.3% +/- 3.1% (normal-approx 95% CI); mean game length 21.21 moves; 0 rule mismatches vs reference.
- Random playout speed, 1 thread (5 repeats x 100000 games): 5.91M +/- 0.19M playouts/sec (no tree; real MCTS will be much slower per playout).

## Notes
- User runs all builds/commands and pastes output; Claude edits code and gives instructions.
- Only claim results for thread counts the machine really has; report hyper-threads / E-cores separately.
