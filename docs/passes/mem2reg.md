# mem2reg (v1)

Promotes simple stack allocations to SSA registers by replacing loads with the
value from a unique dominating store and removing dead allocas and stores.

## Scope

* Supports integer (`i64`), float (`f64`), and boolean (`i1`) slots.
* The address of the alloca must not escape (only used by `load`/`store`).
* Exactly one store must write to the slot and it must dominate all loads.
* Does not handle loops, phi nodes, or aggregating types yet.

## Example

Input IL:
```il
il 0.1.2
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store i64 %t0, 42
  %t1 = load i64 %t0
  %t2 = load i64 %t0
  %t3 = add %t1, %t2
  ret %t3
}
```

After `mem2reg`:
```il
il 0.1.2
func @main() -> i64 {
entry:
  %t3 = add 42, 42
  ret %t3
}
```

The two loads are replaced with the SSA value `42` from the single store, and
the now unused alloca and store are removed.
