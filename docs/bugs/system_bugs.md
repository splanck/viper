# VIPER System Bugs and Issues

Last Updated: 2025-11-15

Scope: Tooling/driver/infra issues (viper/vm/runtime/registry), not language semantics.

---

## NEW: SYS-001 — ARG$/COMMAND$ cause segfault and bogus arity diagnostics

Status: Fixed (signature arity derived from registry; unit test added)
Category: Frontend (Semantics)/Tooling
Severity: High (crashes `viper` on common usage)

Reproduction:

- E2E test `basic_args_some` fails when running:
    - `viper front basic -run tests/e2e/basic_args_some.bas -- foo "bar baz"`
- Minimal cases:
    - `PRINT ARG$(0)` → `viper ... -emit-il` or `-run` segfaults (139)
    - `PRINT COMMAND$()` → reports nonsensical arity in diag, then crashes

Observed Diagnostics:

```
/tmp/cmd.bas:1:7: error[B2001]: COMMAND$: expected 4368290772-8736581632 args (got 0)
PRINT COMMAND$()
      ^
```

Impact:

- Any program invoking `ARG$` or `COMMAND$` can crash the driver.
- E2E test `basic_args_some` consistently fails.

Root Cause Analysis:

- Semantic analysis looks up builtin signatures from a hardcoded table
  `kBuiltinSignatures` in `src/frontends/basic/SemanticAnalyzer.Builtins.cpp`:
    - `const std::array<BuiltinSignature, ...> kBuiltinSignatures = { ... }`
    - The array must match the enum order in `BuiltinCallExpr::Builtin` (in
      `ExprNodes.hpp`).
- Builtins `ARGC`, `ARG$`, and `COMMAND$` were added to the enum and to the
  declarative registry (`builtin_registry.inc`), but the hardcoded signature
  array appears out-of-sync (missing/misaligned entries for these new symbols).
- When semantic analysis calls `builtinSignature(c.builtin)`, it indexes the
  wrong slot, reading garbage `requiredArgs/optionalArgs`:
    - Yields the absurd “expected 4368290772-8736581632 args” message.
    - Subsequent code paths dereference invalid/mismatched data → segfault.

Why this happens:

- There are two sources of truth for builtins:
    1) `builtin_registry.inc` (descriptors, lowering, scan rules) — includes
       `ARGC`, `ARG$`, `COMMAND$` with correct arities.
    2) `kBuiltinSignatures` (semantic signatures) — manually maintained order.
- The manual signature array was not updated to reflect the enum changes, so the
  enum-to-index mapping diverged.

Affected Files:

- `src/frontends/basic/SemanticAnalyzer.Builtins.cpp` (kBuiltinSignatures)
- `src/frontends/basic/ast/ExprNodes.hpp` (enum Builtin order)
- `src/frontends/basic/builtin_registry.inc` (descriptors are correct)
- `src/frontends/basic/BuiltinRegistry.cpp` (registry lookups OK; parser uses
  registry arity, semantic uses kBuiltinSignatures → mismatch).

Proposed Fix:

- Single source of truth: derive semantic arity/type from registry descriptors
  (and optional analysis hooks) instead of a separate hardcoded array.
    - Option A (minimal): Extend `BuiltinRegistry` to expose a semantic signature
      view and update `SemanticAnalyzer::builtinSignature` to query it.
    - Option B: Update `kBuiltinSignatures` to include exact entries for
      `ARGC` (0/0, Int), `ARG$` (1/0, Int arg → Str), `COMMAND$` (0/0, Str), and
      re-verify the entire order against `BuiltinCallExpr::Builtin`.
- Add unit tests for builtin signature lookups to prevent regressions, e.g.:
    - `COMMAND$` min/max args = 0/0 and result type String.
    - `ARG$` min/max args = 1/1 with arg type Int; result String.
    - `ARGC` min/max args = 0/0; result Int.

Verification Plan:

- Re-enable/execute E2E `basic_args_some` and add a unit test that compiles
  a small BASIC program invoking `ARG$` and `COMMAND$` via `compileBasic` and
  ensures semantic analysis succeeds and IL contains calls to
  `rt_args_get`/`rt_cmdline`.

Notes:

- Runtime side (rt_args_*) is correct; `test_rt_args` already passes.
- We also moved program-arguments seeding to post-VM init in `Runner`
  (previous early seeding in CLI was fragile). That change is orthogonal and
  does not cause this crash.

---

## NEW: CRIT-1 — Refcount Overflow Vulnerability

Status: Fixed (runtime patch applied)
Severity: Critical (memory safety)
Source: CODE_QUALITY_DEEP_DIVE.md (CRIT-1)

Repro/Analysis:

- In `src/runtime/rt_heap.c`, `rt_heap_retain` increments `hdr->refcnt` with no overflow guard.
- If `refcnt` reaches `SIZE_MAX`, a further retain wraps to 0; a subsequent release frees while still referenced.

Root Cause:

- Missing saturation/overflow check on the shared heap header refcount; code assumes “won’t happen”.

Affected Code:

- `src/runtime/rt_heap.c` — `rt_heap_retain`

Proposed Fix:

- Add overflow check and trap before increment; treat `SIZE_MAX-1` as the last valid count.
- Consider using a reserved immortal sentinel consistently to avoid wrap.

Verification Plan:

- Unit test: artificially bump refcount near `SIZE_MAX` via test hook and verify trap.
- Fuzz retain/release sequences under address sanitizer.

---

## NEW: CRIT-2 — Double-free risk in string literal release (not reproduced)

Status: Needs Triage
Severity: Critical (claimed) → Not reproduced
Source: CODE_QUALITY_DEEP_DIVE.md (CRIT-2)

Repro/Analysis:

- Report claims underflow on `literal_refs` in `rt_string_unref` can double-free.
- Current code guards correctly:
  `if (s->literal_refs > 0 && --s->literal_refs == 0) free(s);`
- No underflow path observed; repeated releases with zero do nothing.

Root Cause:

- Likely stale code snapshot used for the report. Current implementation is safe against underflow.

Affected Code:

- `src/runtime/rt_string_ops.c` — `rt_string_unref`

Recommendation:

- Optionally add an explicit else-branch trap for visibility on spurious extra releases.

---

## NEW: CRIT-3 — realloc error handling loses original pointer

Status: Fixed (runtime patch applied)
Severity: Critical (leak + crash)
Source: CODE_QUALITY_DEEP_DIVE.md (CRIT-3)

Repro/Analysis:

- In `src/runtime/rt_type_registry.c::ensure_cap`, `realloc` return is assigned directly to `*buf` without NULL check.
- On failure, original pointer is leaked and `*buf` becomes NULL; subsequent writes crash.

Root Cause:

- Unchecked `realloc` assignment; classic pointer-loss on allocation failure.

Affected Code:

- `src/runtime/rt_type_registry.c` — `ensure_cap`

Proposed Fix:

- Assign realloc to a temp, check NULL, trap/return error; only then store to `*buf`.
- Also cap growth to avoid overflow (`cap * 2` guard).

Verification Plan:

- Fault injection: make `realloc` fail (test shim) and assert no pointer loss and proper trap.

---

## NEW: CRIT-4 — Null dereference after guard in DELETE lowering (not reproduced)

Status: Not Reproducible
Severity: Critical (claimed) → Appears safe as implemented
Source: CODE_QUALITY_DEEP_DIVE.md (CRIT-4)

Repro/Analysis:

- Inspected `src/frontends/basic/Lower_OOP_Stmt.cpp::lowerDelete` around the cited lines.
- Code guards `func` and `origin` and computes an index, then appends new blocks. It then re-derives the base pointer
  `&func->blocks[...]` after potential vector growth, which is safe.
- No null deref path identified; control flow respects guards.

Root Cause:

- Likely false positive from static heuristic; current pattern is valid.

Recommendation:

- Add a small unit test that exercises DELETE lowering to confirm no crashes under ASan/UBSan.

---

## NEW: HIGH-2 — Use-after-move in array expression parsing

Status: Fixed (parser patch + unit test)
Severity: High (memory safety)
Source: CODE_QUALITY_DEEP_DIVE.md (HIGH-2)

Repro/Analysis:

- In `Parser_Expr.cpp`, code moves `indexList[0]` into `arr->index`, then moves `indexList` into `arr->indices`.
- The moved-from `indexList[0]` element is then part of the moved vector; subsequent uses are UB.

Root Cause:

- Dual ownership design between legacy single-dim `index` and multi-dim `indices` with incorrect move sequence.

Affected Code:

- `src/frontends/basic/Parser_Expr.cpp` near array index list handling (around lines ~340-346 per report).

Proposed Fix:

- Copy the single element into `arr->index` or avoid populating both fields concurrently.

Verification Plan:

- Unit test: parse `a(1)` and assert AST has either `index` or `indices` consistently without null/moved-from nodes.

---

## NEW: HIGH-3 — Heap corruption risk after realloc in array resize

Status: Fixed (runtime patch applied)
Severity: High (memory safety)
Source: CODE_QUALITY_DEEP_DIVE.md (HIGH-3)

Repro/Analysis:

- In `src/runtime/rt_array.c`, code reads fields from a header pointer immediately after `realloc`, using the new
  pointer but assuming old state without validation.
- If `realloc` moved the block, directly reading/dependent logic can observe inconsistent state.

Root Cause:

- Insufficient capture of old values prior to `realloc` and missing post-alloc validation.

Affected Code:

- `src/runtime/rt_array.c` (resize path cited around 222-227 in report).

Proposed Fix:

- Snapshot old fields before `realloc`; after successful `realloc`, use the returned pointer and recompute derived
  metadata defensively.

Verification Plan:

- Unit test that triggers resize with reallocation (force small caps) under ASan; check array invariants and no invalid
  reads/writes.

## RESOLVED: SYS-000 — Program arguments seeded before VM init (CLI)

Status: Resolved (2025-11-15)
Category: Tooling/Runner integration
Impact: None observed in tests, but order-of-init was brittle.

Summary:

- `viper front basic -run ... -- args` seeded runtime arguments in the CLI before
  VM initialisation. This worked but coupled CLI to runtime init order.

Fix:

- Added `RunConfig::programArgs` and moved seeding into `vm::Runner` constructor
  after VM creation. CLI now passes args through `RunConfig`.

Files:

- `include/viper/vm/VM.hpp`, `src/vm/Runner.cpp`, `src/tools/ilc/cmd_front_basic.cpp`.
