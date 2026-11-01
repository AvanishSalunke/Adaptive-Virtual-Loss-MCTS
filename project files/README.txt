ADAPTIVE VIRTUAL LOSS FOR PARALLEL MONTE CARLO TREE SEARCH - THE FILES THAT MATTER

This folder is self-contained. It holds only what is needed to run the final
adaptive virtual loss search against the fixed version and against the
sequential base, on Connect-4. (The rest of the repository holds earlier
experiments and is not needed here.)

HOW TO RUN (from this folder, in the WSL / Ubuntu terminal)
    make             build
    make run         quick demonstration, about 3 minutes (20 games per match)
    make run-full    the full comparison, 400 games per match, about an hour

WHAT IT COMPARES  (8 threads, 0.1 s per move for everyone, colours alternate)
    V5  adaptive virtual loss  lambda = 0.5     <- the idea of this project
    V4  fixed virtual loss     v = 5            <- the usual method (baseline)
    V0  sequential UCT, 1 thread                <- the base
The program plays V5 against V4, V5 against V0 and V4 against V0, prints a
table with win rates and 95% confidence intervals, and says in words whether
V5 and V4 can be told apart. A 20-game quick run has wide intervals.

THE FILES
    compare.cpp                    the program: sets up the matches and prints the table

    search/virtual_loss.hpp        THE ADAPTIVE FORMULA:  v = max(1, round(lambda * T / sqrt(n + 1)))
                                   (the fixed rule is the line above it)
    search/uct_tree_lockfree.hpp   the parallel search tree (V4/V5): where the penalty is added when
                                   a thread picks a child and removed again when the result arrives
    search/uct_v0.hpp              the sequential search (V0), the base
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
    V5 adaptive vs V0 base  : 55.1%  (95% interval 50.2% - 59.9%)
    V4 fixed    vs V0 base  : 58.1%  (95% interval 53.2% - 62.9%)
Both parallel versions beat the sequential base. In this test the adaptive
formula gave no detectable improvement over fixed virtual loss: an honest
negative result.
