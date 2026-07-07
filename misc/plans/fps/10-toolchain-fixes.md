# 10 — Toolchain: Aggregate-Return Miscompile Fix, -O0/-O2 Differential Harness, Papercuts

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track E · 1 session · **runs first (P0)**.
> Eliminates constraint #22: the macOS native **aggregate-return miscompile** (xenoscape
> `VIPER_ZIA_BUGS.md:9-16` — struct-by-value returns miscompile in native-linked builds, forcing
> the reusable-heap-instance workaround) — a correctness bug that would otherwise dictate the
> style of every hot math helper in a 30K-line game. Adds the standing `-O0` vs `-O2` native
> differential harness (the BUG-006 class — cross-block f64 spill reload miscompile — recurred
> once already; ridgebound RUNTIME_API_BUGS.md:148-168).

## 0. TL;DR

Fix small-aggregate return classification in the AArch64 native path so Zia `struct` returns
are correct in native builds; prove it with fail-before/pass-after codegen tests + the
xenoscape repro; then wire a reusable differential harness that builds a probe at `-O0` and
`-O2` and diffs outputs — run at every ASHFALL phase gate. Two optional papercuts if time
remains (discard loop var, ambiguous-bind warning dedupe) — explicitly droppable.

## 1. Current state (verified anchors)

- Open bug: `examples/games/xenoscape/VIPER_ZIA_BUGS.md:9-16` — functions returning a
  lightweight record miscompile on native-linked macOS; demo works around it with a single
  reusable heap `MoveResult` mutated via `set(...)` (`xenoscape/physics.zia:148-151,206-310`).
- Precedent fix in the same area: BUG-006 (`ridgebound/RUNTIME_API_BUGS.md:148-168`) —
  forward-defined f64 block param reloaded with wrong register class in
  `src/codegen/aarch64/LowerILToMIR.cpp`; fixed by seeding temp register classes from the
  whole IL function; regression test
  `Arm64Bugfix.ForwardDefinedF64BlockParamReloadKeepsFprClass`.
- AArch64 backend is the monolithic simpler linear-scan (memory: codegen architecture);
  x86_64 path should be audited for the same class while in there.

## 2. Work items

### E34 — Aggregate-return fix
1. **Reproduce minimally**: Zia `struct Vec2 { x: Float; y: Float }` returned by value from a
   function called in a loop; compile native `-O0` and `-O2`, compare against VM. Capture the
   IL and MIR; identify where the return classification diverges from the AArch64 PCS
   (small aggregates ≤16 B return in x0/x1 or v0/v1 for HFA — a two-f64 struct is an HFA and
   must come back in d0/d1; the bug is likely GPR/FPR misclassification or a missed HFA case,
   consistent with the BUG-006 family).
2. Fix in `src/codegen/aarch64/` (LowerILToMIR.cpp return lowering + call-result binding);
   audit `src/codegen/x86_64/` SysV small-aggregate classification for the same pattern
   (two-f64 → XMM0/XMM1) — fix if affected.
3. Regression tests in the codegen unit suite (`Arm64Bugfix.*` naming precedent): HFA {f64×2},
   {f64×4}, mixed {i64,f64}, {i32×2}, nested struct, struct-in-loop (the miscompile shape).
   E2E: VM vs native output equality probe for a struct-heavy kernel.
4. Update `xenoscape/VIPER_ZIA_BUGS.md` status → fixed; leave the demo's workaround code
   as-is (it's also a zero-alloc optimization) but note it is no longer mandatory.

### E35 — Differential harness
- `scripts/native_opt_diff.sh <project> [args]`: builds the project native at `-O0` and `-O2`
  (two-step IL→codegen path), runs both with fixed args/env (`--smoke` style deterministic
  mode), diffs stdout + exit codes, prints a minimal report. Registered use: ASHFALL smoke
  probe at every phase gate (28-phasing §3); also usable for any demo.
- Keep it POSIX-sh compatible (BSD/macOS), no GNU-isms.

### E36 — Papercuts (optional, drop if session runs long)
- W001: accept `_` as a discard loop variable in Zia (`for _ in 0..N`) — lexer/sema allowlist,
  no binding emitted. Removes the manual-`while` convention for fixed-count loops.
- V3001 ambiguous-import warning: emit once per (symbol, module) pair instead of per use site.

## 3. Files

`src/codegen/aarch64/LowerILToMIR.cpp` (+ neighbors it implicates), possibly
`src/codegen/x86_64/` classification, codegen unit tests, new `scripts/native_opt_diff.sh`,
(papercuts) `src/frontends/zia/` lexer/sema + diagnostics, `examples/games/xenoscape/VIPER_ZIA_BUGS.md`.

## 4. Verification gate

Fail-before/pass-after codegen tests green → xenoscape minimal repro passes native `-O0`/`-O2`
== VM → full no-skip build + full ctest (codegen labels + `-L slow` explicitly — crackman/
native-link live there) → `native_opt_diff.sh` run on ridgebound + 3dbowling smokes as harness
validation. This doc lands **first** (P0) so every subsequent game line is written in natural
style (struct returns allowed in hot paths) and every phase gate has the diff harness.
