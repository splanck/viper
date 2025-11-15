# VIPER System Bugs and Issues

Last Updated: 2025-11-15

Scope: Tooling/driver/infra issues (ilc/vm/runtime/registry), not language semantics.

---

## NEW: SYS-001 — ARG$/COMMAND$ cause segfault and bogus arity diagnostics
Status: Open
Category: Frontend (Semantics)/Tooling
Severity: High (crashes `ilc` on common usage)

Reproduction:
- E2E test `basic_args_some` fails when running:
  - `ilc front basic -run tests/e2e/basic_args_some.bas -- foo "bar baz"`
- Minimal cases:
  - `PRINT ARG$(0)` → `ilc ... -emit-il` or `-run` segfaults (139)
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

## RESOLVED: SYS-000 — Program arguments seeded before VM init (CLI)
Status: Resolved (2025-11-15)
Category: Tooling/Runner integration
Impact: None observed in tests, but order-of-init was brittle.

Summary:
- `ilc front basic -run ... -- args` seeded runtime arguments in the CLI before
  VM initialisation. This worked but coupled CLI to runtime init order.

Fix:
- Added `RunConfig::programArgs` and moved seeding into `vm::Runner` constructor
  after VM creation. CLI now passes args through `RunConfig`.

Files:
- `include/viper/vm/VM.hpp`, `src/vm/Runner.cpp`, `src/tools/ilc/cmd_front_basic.cpp`.

