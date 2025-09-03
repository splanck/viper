# mem2reg (v3)

Promotes stack slots to SSA registers across branches and loops by introducing
block parameters and passing values along edges.

## Scope

* Handles integer (`i64`), float (`f64`), and boolean (`i1`) slots.
* The address of the alloca must not escape (only used by `load`/`store`).

## Algorithm (seal & rename)

1. Collect promotable allocas.
2. Walk blocks in depth-first order, maintaining the current SSA value for each
   variable per block.
3. Loads are replaced with the current value; stores update it.
4. If a block reads a variable before all predecessors are seen, create a
   placeholder block parameter. When the block becomes *sealed* (all preds
   known), resolve placeholders to real parameters with the correct incoming
   arguments.
5. Values are propagated along edges by updating predecessor terminators with
   branch arguments.
6. After processing, remove the original allocas and stores.

## Example (diamond)

Input IL:

```il
il 0.1.2
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  store i64 %t0, 2
  br Join
F:
  store i64 %t0, 3
  br Join
Join:
  %t2 = load i64 %t0
  ret %t2
}
```

After `mem2reg`:

```il
il 0.1.2
func @main() -> i64 {
entry:
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  br Join(2)
F:
  br Join(3)
Join(%a0:i64):
  ret %a0
}
```

The alloca, loads, and stores are removed. A block parameter on `Join` receives
the value from each predecessor via branch arguments.

## Example (loop)

Input IL:

```il
il 0.1.2
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store i64 %t0, 0
  br L1
L1:
  %t1 = load i64 %t0
  %t2 = add %t1, 1
  store i64 %t0, %t2
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1, Exit
Exit:
  %t4 = load i64 %t0
  ret %t4
}
```

After `mem2reg`:

```il
il 0.1.2
func @main() -> i64 {
entry:
  br L1(0)
L1(%a0:i64):
  %t2 = add %a0, 1
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1(%t2), Exit(%t2)
Exit(%a1:i64):
  ret %a1
}
```

The loop header `L1` has a block parameter `%a0` representing the running value,
fed by both the entry edge and the back-edge. The exit block receives the final
value via parameter `%a1`.

