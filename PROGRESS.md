# PROGRESS

Project: Adaptive Virtual Loss for Lock-Free Parallel MCTS (our adaptive formula is an UNTESTED hypothesis).

## Machine
- Intel Core Ultra 5 125H, 14 cores / 18 logical processors, 16 GB RAM, Windows 11 + WSL2 (Ubuntu)
- Hybrid CPU (P-cores, E-cores, low-power E-cores): threads are not all equal. See notes below.

## Done
- Step 0: folder layout, Makefile, timing helper, hello_threads. Built and run in WSL2 (project linked as ~/mcts).
- Step 1 (files written, waiting for user to build and run): Game interface (game.hpp), Connect-4 bitboard (connect4.hpp), per-thread RNG (rng.hpp), stats / run-info / CSV helpers, test_connect4.cpp.

## Next
- Step 1: user runs `make clean && make` then `make test-connect4`, pastes output.
- Step 2: teach MCTS, build V0 sequential UCT.

## Results so far
- hello_threads, 4 runs (400M steps): 1 thread ~373-380 ms; 2 threads speedup 1.96-2.01; 4 threads 3.84-3.92; 8 threads 5.62-6.67 (noisy: depends on which cores the OS picks on this hybrid CPU).

## Notes
- User runs all builds/commands and pastes output; Claude edits code and gives instructions.
- Only claim results for thread counts the machine really has; report hyper-threads / E-cores separately.
