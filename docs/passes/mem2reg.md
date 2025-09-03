# mem2reg (v2)

Promotes stack slots to SSA registers across branches by introducing block
parameters and passing values along edges.

## Scope

* Works only on acyclic control-flow graphs (functions with loops are skipped).
* Handles integer (`i64`), float (`f64`), and boolean (`i1`) slots.
* The address of the alloca must not escape (only used by `load`/`store`).

## Algorithm (acyclic CFG)

1. Collect promotable allocas: type must be `i64`, `f64`, or `i1`, and the
   address may not escape.
2. For each alloca, process blocks in topological order, tracking the current
   value of the slot within each block.
3. Stores update the current value; loads are replaced with this value and then
   removed.
4. For every edge `B -> S`, ensure `S` has a block parameter for the variable
   and pass the current value from `B` as an argument in its branch
   terminator.
5. After processing all blocks, delete the original alloca and any stores.

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

