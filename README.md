# Adaptive Virtual Loss for Lock-Free Parallel MCTS

Making Monte Carlo Tree Search faster on many cores without making it play worse.

Work in progress. The idea below is something we want to test, not something we've proven.

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

## Why use many threads?

More rounds per second means better moves, so running MCTS on several threads sounds like an easy win. It isn't, because every thread reads the same scores, sees the same "best" branch, and runs off to the same place. Most of that work is repeated and teaches us almost nothing.

So the program can get 8x more games per second and still only play about 3x better. The speed is real, but a lot of it is wasted.

## Virtual loss

The usual fix is called virtual loss. When a thread goes down a branch, we pretend that branch has lost a few games. It looks worse for a moment, so the next thread goes somewhere else. Once the real result comes back, the pretend losses are removed.

For example, branch A scores 1.35 and branch B scores 1.21. Normally every thread picks A. If we add a virtual loss to A, its score drops below B and the next thread tries B.

The catch is that the size of the penalty is a fixed number picked by hand. Too small and the threads still bunch up. Too big and they get pushed away from moves that really are good.

## Our idea

The right penalty depends on how much we already know about a branch.

- New branch, few visits: we know nothing, so push the threads apart.
- Well-visited branch: we already trust it, so a small penalty is enough.

So we let the penalty shrink as the visit count grows:

```
penalty = max(1, round( lambda * threads / sqrt(visits + 1) ))
```

`lambda` is just a constant we tune. Whether this actually beats a well-tuned fixed penalty is what the project is meant to find out.

## Locks vs lock-free

Threads editing the same node can mess each other up, so shared trees usually use locks, which make threads wait. We use lock-free updates instead (atomic operations). Now and then an update gets lost, but MCTS is statistical anyway, so the odd lost count doesn't matter much.

## How we'll judge it

Speed alone can be misleading, so we measure both:

- Speed: games per second and how it scales with threads.
- Quality: how often it beats a plain single-thread player given the same time per move.

We'll test on Connect-4, and on a simulated computer network where the search finds the weakest attack paths so defenders know what to fix first.

## Reference

- Chaslot, Winands, van den Herik. *Parallel Monte-Carlo Tree Search.* Computers and Games, 2008.