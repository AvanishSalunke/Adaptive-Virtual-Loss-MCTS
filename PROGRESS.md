# PROGRESS

Project: Adaptive Virtual Loss for Lock-Free Parallel MCTS (our adaptive formula is an UNTESTED hypothesis).

## Machine
- Intel Core Ultra 5 125H, 14 cores / 18 logical processors, 16 GB RAM, Windows 11 + WSL2 (Ubuntu)
- Hybrid CPU (P-cores, E-cores, low-power E-cores): threads are not all equal. See notes below.

## Done
- Step 0 (files written, waiting for user to build and run): folder layout, Makefile, timing helper, hello_threads.

## Next
- Step 0: user installs WSL2 + g++, builds and runs `make run-hello`, pastes output.
- Step 1: Connect-4 engine + Game interface, 1000 random games.

## Results so far
- None yet.

## Notes
- User runs all builds/commands and pastes output; Claude edits code and gives instructions.
- Only claim results for thread counts the machine really has; report hyper-threads / E-cores separately.
