# Range Sum Queries

You are given an array of `n` integers and `q` queries. Each query gives two indices `l` and `r`
(1-based, inclusive). For every query, print the sum of the array elements from position `l` to
position `r`.

## Input

```
n q
a1 a2 ... an
l1 r1
...
lq rq
```

- `1 <= n, q <= 200000`
- `-10^9 <= ai <= 10^9`
- `1 <= l <= r <= n`

## Output

`q` lines, the answer for each query.

## Example

Input:

```
5 2
3 1 4 1 5
1 3
2 5
```

Output:

```
8
11
```

## What this task teaches

Adding the `q` ranges one element at a time costs `O(n*q)`, which is too slow at the limits above.
A prefix-sum array answers each query in `O(1)` after `O(n)` preparation. The point of the task is
to notice that the cost moved out of the query and into the preparation.
