---
status: active
audience: public
last-verified: 2025-09-23
---

# VM Dispatch Optimizations

The IL VM interprets control-flow terminators directly. `switch.i32` used to
scan each case linearly on every dispatch. The interpreter now memoizes dispatch
metadata per instruction and selects the fastest backend for the observed case
values.

## Interpreter dispatch loops

Viper ships three interpreter dispatch loops. They trade off portability and
peak performance and can be toggled without recompiling the VM runtime.

- **Function-pointer table** – maximally portable because it only depends on
  standard C++. The VM keeps an array of opcode handlers and performs an
  indirect call for every instruction retirement.
- **`switch` dispatch** – compiles each opcode handler into a `switch`
  statement. Modern compilers lower this to jump tables for dense opcode
  ranges and fall back to binary searches otherwise.
- **Direct-threaded dispatch** – uses GCC/Clang’s labels-as-values extension to
  compute the next instruction address without a branch. This yields the best
  branch prediction behaviour but requires enabling the extension explicitly.

The interpreter chooses the dispatch loop at process start. Set the
`VIPER_DISPATCH` environment variable to override the default:

| Value      | Selected loop                                | Notes                                                                               |
|------------|----------------------------------------------|-------------------------------------------------------------------------------------|
| _unset_    | `switch` (or direct-threaded when available) | Default at startup.                                                                 |
| `table`    | Function-pointer table                       | Always available.                                                                   |
| `switch`   | `switch` dispatch                            | Portable and optimisation-friendly.                                                 |
| `threaded` | Direct-threaded dispatch                     | Requires building with threaded dispatch support; otherwise falls back to `switch`. |

Direct-threaded dispatch is gated by the `VIPER_VM_THREADED` CMake option. Pass
`-DVIPER_VM_THREADED=ON` when configuring CMake to emit the threaded loop.
Because the implementation relies on labels-as-values, only GCC and Clang can
compile this configuration today. Other compilers automatically retain the
`switch` implementation even if the option is requested.

## Backend selection heuristic

When the VM first executes a `switch.i32`, it collects the scrutinee constants
and their successor indices. The dispatcher computes:

- `range = max_case - min_case + 1`
- `density = case_count / range`

The heuristic chooses among three backends:

| Backend          | When selected                              | Notes                                                          |
|------------------|--------------------------------------------|----------------------------------------------------------------|
| Dense jump table | `range ≤ 4096` **and** `density ≥ 0.60`    | Materialises a contiguous table indexed by `scrutinee - base`. |
| Sorted cases     | default                                    | Keeps cases sorted and uses binary search.                     |
| Hashed cases     | `case_count ≥ 64` **and** `density < 0.15` | Builds an `unordered_map` keyed by case value.                 |

Each entry records the default branch index to fall back on when no case
matches. Duplicate case values are ignored while building metadata so the cache
remains consistent even if the IL contains redundant arms.

## Memoization and reuse

Switch backends are stored in a `SwitchCacheEntry` per instruction. The cache is
kept on the active execution state (`VM::ExecState::switchCache`), ensuring that
dense tables and hash maps are reused every time the instruction executes within
the same frame. When the VM prepares a new frame it clears the cache before
running the function.

The interpreter falls back to a direct linear scan only when the cache is forced
into `Linear` mode (see below) or when `VIPER_VM_DEBUG_SWITCH_LINEAR` is set for
assert-only validation.

## Forcing a specific backend

Set the `VIPER_SWITCH_MODE` environment variable before constructing the VM to
override the heuristic. Accepted values (case-insensitive) are:

- `auto` (default): run the heuristic described above.
- `dense`: always build dense jump tables.
- `sorted`: always build sorted case vectors.
- `hashed`: always build hash maps.
- `linear`: disable caching and perform a linear scan every time.

The chosen mode is global for the process and applies to all VM instances
created afterwards.

# Tail-Call Optimisation (TCO)

The interpreter can perform tail-call optimisation (TCO) to reuse the current
stack frame when a function tail-calls another. This reduces stack growth and
matches the IL semantics for tail positions.

- Build-time flag: `VIPER_VM_TAILCALL` (default ON). Set with
  `-DVIPER_VM_TAILCALL=ON|OFF`.
- Trace/debug: when tracing is enabled, TCO emits a one-line event:

```
[IL] tailcall from_fn -> to_fn
```

There are no semantic differences; only performance and stack usage change.
