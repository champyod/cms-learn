# Shortest Path in a Grid

You are given a grid of `n` rows and `m` columns. Each cell is either free (`.`) or blocked (`#`).
You start at cell `(sr, sc)` and want to reach `(tr, tc)`. In one step you may move up, down, left
or right into a free cell. Print the minimum number of steps, or `-1` if the target is unreachable.

## Input

```
n m
grid rows, each of length m
sr sc
tr tc
```

- `1 <= n, m <= 1000`
- start and target cells are free

## Output

One integer: the minimum number of steps, or `-1`.

## Example

Input:

```
3 4
....
.#..
....
1 1
3 4
```

Output:

```
5
```

## What this task teaches

Every move costs the same, so breadth-first search visits cells in non-decreasing distance order and
the first time it reaches the target is the answer. Adding the start cell to the queue before the
loop, and marking a cell visited when it is *enqueued* rather than when it is dequeued, are the two
mistakes this task is built to expose.
