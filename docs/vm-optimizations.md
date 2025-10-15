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

## Backend selection heuristic

When the VM first executes a `switch.i32`, it collects the scrutinee constants
and their successor indices. The dispatcher computes:

- `range = max_case - min_case + 1`
- `density = case_count / range`

The heuristic chooses among three backends:

| Backend | When selected | Notes |
|---------|---------------|-------|
| Dense jump table | `range ≤ 4096` **and** `density ≥ 0.60` | Materialises a contiguous table indexed by `scrutinee - base`. |
| Sorted cases | default | Keeps cases sorted and uses binary search. |
| Hashed cases | `case_count ≥ 64` **and** `density < 0.15` | Builds an `unordered_map` keyed by case value. |

Each entry records the default branch index to fall back on when no case
matches. Duplicate case values are ignored while building metadata so the cache
remains stable even if the IL contains redundant arms.

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

