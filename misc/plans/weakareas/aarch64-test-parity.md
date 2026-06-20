# AArch64 Codegen Test Organization & Cross-Backend Parity

**Status:** Reframed — AArch64 is **not** untested (~15+ unit tests + e2e + differential
exist); the gap is **organization/density** and an explicit cross-backend equivalence
story.
**Area:** `src/tests/codegen/`, `src/tests/unit/codegen/`
**Effort:** M
**Roadmap fit:** v0.2.x hardening

## Problem (corrected)

The review's original "AArch64 has zero codegen tests / critical gap" was **false**.
Verified reality:

- ~15+ ARM64 unit tests exist in `src/tests/unit/codegen/`
  (`test_codegen_arm64_*`, `test_aarch64_*`: switch, bitwise, logical-imm, icmp-imm,
  call/stack args, callee-saved, GEP load/store, rodata pool, vararg, trap blocks, CLI…).
- E2E: `test_arm64_comprehensive.cmake`, `test_arm64_frogger.cmake`,
  `test_arm64_native_link.cmake`, `test_arm64_demos*.cpp`.
- Differential VM↔native: `test_diff_vm_native_property.cpp` (gated to aarch64).
- Perf: `native_arm64_bench.cpp` (with a regression check).

The **actual** gaps:

1. **No dedicated `src/tests/codegen/aarch64/` directory** — x86_64 has
   `src/tests/codegen/x86_64/` with ~33 structured tests (ABI, encoding, regalloc
   stress, FP, determinism); ARM64's are scattered in `unit/codegen/` without that
   structure, so coverage is harder to audit and likely lower-density in some categories.
2. **No explicit cross-backend equivalence assertion** — each backend is validated
   against the VM, but there is no single artifact stating "x86_64 and aarch64 agree."

## Goal & scope

- **In:** A dedicated `src/tests/codegen/aarch64/` home mirroring x86_64's categories;
  fill the density gaps; add an explicit cross-backend equivalence harness built on the
  existing differential infrastructure.
- **Out:** Rewriting the AArch64 backend; new ISA features.

## Design — how "cross-arch parity" actually works

You generally cannot run an x86_64 native and an aarch64 native on the same host without
emulation, so "compile on both, diff the outputs directly" is impractical in-suite.
Instead, equivalence is enforced **transitively through the VM**, which is the
deterministic oracle:

```
x86_64 native ≡ VM   (existing differential)
aarch64 native ≡ VM   (existing differential)
⇒ x86_64 native ≡ aarch64 native  (transitively, for all covered programs)
```

So the highest-value work is **raising both backends' VM-differential coverage over a
shared IL corpus**, plus per-arch **encoding/asm golden snapshots** to catch silent
lowering drift. A true both-natives diff can run opportunistically under QEMU as a slow,
opt-in lane.

## Implementation steps

1. Create `src/tests/codegen/aarch64/` + CMake registration mirroring
   `src/tests/codegen/x86_64/CMakeLists.txt`; move the scattered `unit/codegen/*arm64*`
   tests in (keep ctest labels stable).
2. Diff x86_64's test categories against ARM64's; add the missing analogs (ABI/calling
   convention edge cases, encoding validation, regalloc stress/consistency, FP compare
   and conversion widths, determinism).
3. Define a **shared IL corpus** (one set of `.il`/source inputs) consumed by *both*
   backends' differential tests, so coverage is symmetric by construction.
4. Add per-arch **encoding golden snapshots** for the corpus (assemble → bytes/asm →
   compare to checked-in golden), maintained via `scripts/update_goldens.sh`.
5. (Optional) `scripts/` slow lane running both natives under QEMU for a direct diff;
   gate behind an env var and the `slow` label.

## Tests / labels

- New tests carry the `codegen` + `arm64` labels (see the whitelist in
  `src/tests/cmake/TestHelpers.cmake`).
- Shared-corpus differential runs `RUN_SERIAL` with a sane timeout (match the existing
  `test_diff_vm_native_property` properties).

## Cross-platform / host considerations

- Unit + golden tests **build everywhere, assert encodings without executing**, so they
  run on any host.
- Native-execution differential tests gate on host arch (as the ARM64 ones already do);
  the QEMU lane is opt-in.

## Verification

- `ctest --test-dir build -L codegen` green on both an arm64 and an x86_64 host.
- Golden snapshots stable across runs (determinism).
- Coverage report (see `code-coverage.md`) shows comparable `codegen/aarch64` vs
  `codegen/x86_64` line coverage.

## Documentation

- Update `docs/testing.md`: the new `src/tests/codegen/aarch64/` location, the
  cross-backend **equivalence-via-VM** rationale, the shared IL corpus location, and the
  opt-in QEMU lane.
- Update `docs/contributor-guide.md` if it documents codegen-testing conventions.
- Note the encoding-golden update path (`scripts/update_goldens.sh`) for future authors.

## Risks / open questions

- **QEMU lane flakiness/cost** — keep it opt-in and out of the default run.
- **Golden churn** — encoding goldens change with intentional lowering improvements;
  the update script must be the single, reviewed path.
