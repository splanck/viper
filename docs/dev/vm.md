# VM Internals

## Block Parameter Slots

On control transfer the interpreter evaluates branch arguments and stores them
into parameter slots within the active frame. Each slot is keyed by the
parameter's value identifier. When a block is entered these slots are copied
into the register file, making block parameters available as normal SSA values.
Blocks with no parameters skip this step to remain fast.

## Tracing hooks

The interpreter can emit a deterministic trace of executed IL instructions. On
each dispatch, `TraceSink::onStep` records the current instruction before it is
executed. Enable this via `--trace=il` in `ilc -run`:

```
[IL] fn=@foo blk=L3 ip=#12 op=add %t1, %t2 -> %t3
```

## Trace format stability

Trace output is identical across platforms. All numbers use the C locale with
booleans printed as `0` or `1`, integers in base‑10, and floating-point values
formatted using `%.17g`. Line endings are normalized to `\n` even on Windows.
