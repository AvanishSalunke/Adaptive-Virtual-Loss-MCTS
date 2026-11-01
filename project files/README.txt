ADAPTIVE VIRTUAL LOSS FOR PARALLEL MONTE CARLO TREE SEARCH - THE FILES THAT MATTER

This folder is self-contained. It holds only what is needed to run the final
comparison: adaptive virtual loss against fixed (non-adaptive) virtual loss,
on Connect-4. The rest of the repository holds earlier experiments and is not
needed here.

HOW TO RUN (from this folder, in the WSL / Ubuntu terminal)
    make             build
    make run         quick run, 20 games, about a minute
    make run-full    the full run, 400 games, about 15 minutes

The file that runs is compare.cpp (make builds it and runs it).

WHAT IT COMPARES  (8 threads each, 0.1 s per move, colours alternate)
    V5  adaptive virtual loss  lambda = 0.5      <- the idea of this project
    V4  fixed virtual loss     v = 5             <- the usual method
Both are the same parallel search; only the virtual-loss rule differs.
It prints V5's win rate with a 95% confidence interval and says whether the
two can be told apart. A 20-game quick run has wide intervals.

THE FILES
    compare.cpp                    the program: plays V5 against V4 and prints the result

    search/virtual_loss.hpp        THE ADAPTIVE FORMULA:  v = max(1, round(lambda * T / sqrt(n + 1)))
                                   (the fixed rule is the line above it)
    search/uct_tree_lockfree.hpp   the parallel search tree: the penalty is added when a thread picks
                                   a child and removed again when its result arrives
    search/uct_v0.hpp              the basic search that the tree builds on (shared types)
    search/team.hpp                starts the group of threads

    game/connect4.hpp              Connect-4 on bitboards
    game/game.hpp                  the small interface every game must offer to the search
    game/arena.hpp                 plays two players against each other, alternating colours

    support/                       small helpers: timer, random numbers, statistics,
                                   machine info, CSV output

    results/saved_final_400_games.csv    the real final result (400 games per match)
    results/saved_tuning_100_games.csv   the tuning run that chose lambda = 0.5 and v = 5
    results/compare_final.csv            (created when you run it) your new run

RESULT OF THE FULL RUN (saved in results/saved_final_400_games.csv)
    V5 adaptive vs V4 fixed : 46.4%  (95% interval 41.5% - 51.3%)  -> no detectable difference
(The same saved file also has each version against a sequential base: V5 55.1%,
V4 58.1%, so both parallel versions beat the sequential search.)
In this test the adaptive formula gave no detectable improvement over fixed
virtual loss: an honest negative result.
