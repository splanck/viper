# AArch64 (arm64) Backend — Status and Plan

This document captures the current state of the AArch64 backend work, how it is wired into the tree today, and the plan going forward (short- and long‑term). It is kept developer‑focused and references concrete source files and tests.

## Overview

We have scaffolded a native AArch64 (arm64) backend for macOS and implemented a minimal, test‑driven path that can emit assembly text from a very small subset of IL. The current strategy is incremental: land a correct, tiny emitter and CLI integration, then grow pattern coverage and ergonomics before introducing a full lowering pipeline and register allocation similar to x86‑64.

## Implemented So Far

### Target description (registers/ABI)
- Files:
  - `src/codegen/aarch64/TargetAArch64.hpp`
  - `src/codegen/aarch64/TargetAArch64.cpp`
- Contents:
  - `enum class PhysReg` enumerates GPRs `x0..x30`, `sp` and SIMD `v0..v31` (modeled as D‑regs for now).
  - ABI helpers:
    - `darwinTarget()` returns a singleton with:
      - Caller/callee‑saved sets (Darwin/AAPCS64), arg register orders (x0..x7, v0..v7), return regs (x0, v0), and stack alignment (16).
    - `isGPR`, `isFPR` classify physical registers.
    - `regName` renders canonical string names (e.g. "x0", "v15").

### Minimal AArch64 assembly emitter
- Files:
  - `src/codegen/aarch64/AsmEmitter.hpp`
  - `src/codegen/aarch64/AsmEmitter.cpp`
- Capabilities:
  - Function prologue/epilogue:
    - Prologue: `stp x29, x30, [sp, #-16]!; mov x29, sp`
    - Epilogue: `ldp x29, x30, [sp], #16; ret`
  - Function header directives: `.text; .align 2; .globl <name>; <name>:`
  - Integer ops:
    - Reg/reg: `mov`, `add`, `sub`, `mul`, `and`, `orr`, `eor`
    - Reg/imm: `mov #imm`, `add #imm`, `sub #imm`, shifts `lsl/lsr/asr #imm`
  - Notes: This emitter is intentionally small and stateless; it serves both tests and the early CLI lowering path.

### CLI driver integration (text emission only)
- Files:
  - `src/tools/ilc/cmd_codegen_arm64.hpp`
  - `src/tools/ilc/cmd_codegen_arm64.cpp`
  - `src/tools/ilc/main.cpp`
  - `src/CMakeLists.txt` (adds `ilc_cmd_arm64` static library and links it into `ilc`)
- Usage:
  - `ilc codegen arm64 <input.il> -S <out.s>`
  - The command:
    - Loads an IL module from disk.
    - Iterates functions and emits textual assembly using the AArch64 emitter.
    - Today: generates headers, prologue/epilogue, and optionally a small body based on simple patterns (described next).

### Minimal pattern‑based lowering (single‑block, i64 return)
- Location: `cmd_codegen_arm64.cpp`
- Patterns recognized (all must feed the function’s terminal `ret`):
  - Return constant (i64): `ret <const-int>` → `mov x0, #imm`.
  - Return parameter:
    - `ret %param0` → no body emission (arg already in `x0`).
    - `ret %param1` → `mov x0, x1`.
    - Generic: `ret %paramN` (N∈[0,7]) → `mov x0, <ArgRegN>` using ABI order.
  - Binary arithmetic on entry parameters (penultimate instruction, any arg index 0..7):
    - `add` / `iadd.ovf` → `add x0, x0, x1`
    - `sub` / `isub.ovf` → `sub x0, x0, x1`
    - `mul` / `imul.ovf` → `mul x0, x0, x1`
    - Bitwise: `and`/`or`/`xor` → `and/orr/eor x0, x0, x1`
    - Permutation: operands may be `%param0`/`%param1` in either order.
  - Binary arithmetic with immediates (entry param ± const; any arg index 0..7):
    - `%param0 + imm` → `add x0, x0, #imm`
    - `%param0 - imm` → `sub x0, x0, #imm`
    - `%param1 + imm` → `mov x0, x1; add x0, x0, #imm`
    - `%param1 - imm` → `mov x0, x1; sub x0, x0, #imm`
  - Shifts by immediate (entry param <<|>> const; any arg index 0..7):
    - Move `%paramK` to `x0` based on ABI order; then emit `lsl/lsr/asr x0, x0, #imm`.
  - Integer compares feeding ret (i1 widened to i64):
    - Detect `icmp_eq/ne`, `scmp_lt/le/gt/ge`, `ucmp_lt/le/gt/ge` on entry params.
    - Normalize operands into `(x0, x1)`, emit `cmp x0, x1`, then `cset x0, <cond>` mapping IL predicate to AArch64 condition codes (`eq/ne/lt/le/gt/ge/lo/ls/hi/hs`).
  - Limitations:
    - Only i64 functions.
    - Only single‑block functions for the param‑based patterns (ret const works across blocks).
    - No handling yet for `imm - %paramX` (non‑commutative reverse).

### Tests
- Files:
  - `src/tests/unit/codegen/test_target_aarch64.cpp` — name/ABI sanity.
  - `src/tests/unit/codegen/test_emit_aarch64_minimal.cpp` — emitter sequencing (header, prologue, add, epilogue).
  - `src/tests/unit/codegen/test_codegen_arm64_cli.cpp` — `ret 0` → `mov x0, #0`.
  - `src/tests/unit/codegen/test_codegen_arm64_add_params.cpp` — add two params → `add x0, x0, x1`.
  - `src/tests/unit/codegen/test_codegen_arm64_ret_param.cpp` — ret `%param0` no‑op; ret `%param1` `mov x0, x1`.
  - `src/tests/unit/codegen/test_codegen_arm64_add_imm.cpp` — add/sub immediate forms on params.
  - `src/tests/unit/codegen/test_codegen_arm64_bitwise.cpp` — and/or/xor rr forms on params.
  - `src/tests/unit/codegen/test_codegen_arm64_params_wide.cpp` — rr/ri forms using params beyond x1 (e.g. x2/x3) and scratch‐based normalization.
- Test artifacts:
  - All arm64 CLI tests write transient `.il` and `.s` files under `build/test-out/arm64/` to avoid polluting the repository root. The directory is created on demand by the tests.
- CMake wiring present in `src/tests/CMakeLists.txt`.

### Build integration
- `src/codegen/aarch64/CMakeLists.txt` builds `il_codegen_aarch64` (target + emitter).
- `src/CMakeLists.txt` exposes `ilc_cmd_arm64` as a static lib; `ilc` links `ilc_cmd_arm64` and `il_codegen_aarch64`.

## Current Implementation Details

### Control flow and calling convention
- We model a leaf‑like prologue/epilogue per function (save/restore FP/LR) and return with `ret`.
- Arguments: currently only leverage x0/x1 (first two integer args) in patterns. Return in x0.
- No callee‑saved spills/restores beyond the FP/LR pair today; no varargs support.

### Lowering mechanics
- The arm64 CLI path uses direct IL inspection (no MIR) with deliberately conservative pattern matches:
  - Checks function kind (i64 return), block count, instruction ordering, and that the terminal `ret` consumes the immediately preceding arithmetic.
  - Checks entry block parameters (`bb.params`) and matches SSA temp uses/defs by id, avoiding string/label lookups. For parameter index → physical reg mapping, we use `darwinTarget().intArgOrder`.
  - For rr ops, we normalize arbitrary parameter registers to `(x0, x1)` using `x9` as a scratch register:
    - `mov x9, <rhs_reg>`; `mov x0, <lhs_reg>`; `mov x1, x9`; then emit the rr op.
  - For ri ops, we normalize by moving the source param register into `x0` and then emit the immediate op.
  - Emits body ops via `AsmEmitter` on success; otherwise falls back to prologue/epilogue only.
- This keeps the surface small and safe while we build confidence with unit tests.

### Parity with x86‑64 backend (docs/backend.md)
- Similar end goals: MIR layer, instruction selection, RA, frame lowering, emitter.
- For early patterns, x86‑64 also normalizes operands into conventional registers before emission (see Lowering.EmitCommon helpers); we mirror this with explicit moves into x0/x1.
- IL shift semantics note: IL defines x86‑like shift masking modulo 64. AArch64 immediate shifts do not mask; we rely on the IL verifier to ensure immediate ranges are valid for these patterns. Variable‑count shifts will require mapping to `and` the count and `lsl/lsr/asr` with register form later.

### Shared utilities (proposed)
- Create a `codegen/common/` mini‑library with utilities usable by multiple backends:
  - `ArgNormalizer`: map IL param indices to canonical working registers (e.g. `(dst0, dst1)`) with a scratch policy.
  - `ImmMaterializer`: target‑aware helpers to materialize large immediates (`movz/movk` sequences on AArch64; `movabs` on x86‑64), including peephole fold for small immediates.
  - `CondCode` and compare helpers: map IL predicates to target condition codes; provide `cmp` + `cset/csel` helpers (AArch64) and `setcc` (x86‑64) abstractions.
  - `FramePlan`: tiny struct to describe save/restore sets and stack alignment, share prologue/epilogue shape logic.
  - `TargetTraits`: capability flags per target (red zone, callee‑saved sets, arg orders), abstracted from the concrete Target files.
  - `CLI glue`: thin helpers to parse `-S/-o/--run-native` consistently across backends.
  - Goal: reduce duplication when AArch64 adds a proper MIR and pass pipeline.

Status: Added `src/codegen/common/ArgNormalize.hpp` and started consuming it from the arm64 CLI to normalize rr operands into `(x0, x1)` with a scratch. The header is header‑only and requires a target emitter API accepting `(ostream&, dst, src)`.

### Emitter capabilities and constraints
- Emitter prints textual assembly; no assembling/linking in this step.
- Immediate forms (`mov/add/sub #imm`) assume small immediates. Large immediates will require `movz/movk` sequences later.
- SIMD/FPR registers are defined in the target description but are not yet used by the emitter logic.

## Short‑Term Plan (next 1–3 steps)

1) Verify and extend coverage with focused tests — DONE
   - Added dedicated shift‑imm tests (shl/lshr/ashr) for param0/param1 in `test_codegen_arm64_shift_imm.cpp`.
   - Added explicit `iadd.ovf`/`isub.ovf`/`imul.ovf` rr tests in `test_codegen_arm64_ovf.cpp`.

2) Cleanly factor the pattern‑lowerer
   - NEXT: Extract current pattern code into a small `Arm64PatternLowerer` to simplify the CLI file and enable testable units.
   - Consider a table‑driven mapping from IL opcodes to emitter lambdas for the rr/ri forms.

3) Expand parameter coverage to x2..x7 for 3+ argument functions (still single‑block patterns)
   - DONE for rr/ri patterns via normalization to x0/x1 using `x9` scratch.
   - DONE: generalized `ret %paramN` beyond the first two by moving `ArgRegN → x0`.

4) Simple compares feeding ret (i1 → i64)
   - Equality/relational compares producing i1 → set x0 to 0/1 using `cmp` + `cset` when the value is returned immediately.

## Medium‑Term Plan (next 2–6 weeks)

4) Minimal MIR layer for AArch64 (Phase A)
   - Define a compact Machine IR for arm64 (parallel to `codegen/x86_64/MachineIR.*`).
   - Create a thin lowering adapter from IL to MIR for the supported int ops, ret, simple branches.
   - Reuse emitter to print MIR → asm.

5) Frame and prologue/epilogue refinement
   - Model callee‑saved saves/restores as needed (x19..x28, v8..v15 per Darwin) once the MIR uses them.
   - Implement basic stack object placement for spills and outgoing call frames.

6) Register allocation (linear scan)
   - Adapt or re‑implement the x86 linear scan allocator for arm64 MIR.
   - Add spill code paths and tests.

7) Calls and ABI interop
   - Implement call lowering: argument assignment (x0..x7 / v0..v7), stack spills for extras, return value handling.
   - Add minimal runtime call smoke tests.

8) Assembler + link integration (optional gating on host/toolchain)
   - Add `-assemble`/`--run-native` modes for arm64 akin to x64 pipeline, gated behind host/toolchain checks.
   - Add `movz/movk` materialization for large immediates; prefer `adrp+add` sequences for addressing rodata labels.

## Long‑Term Plan

9) Full codegen pipeline parity with x86‑64
   - Pass structure mirroring `codegen/x86_64` (LowerILToMIR, ISel, Peephole, RegAlloc passes, FrameLowering, Emit pass).
   - Robust instruction selection and legality checks (integer and floating‑point).
   - Comprehensive emitter coverage (loads/stores addressing modes, branches, calls, traps, constants materialization).

10) Performance, robustness, and CI integration
   - Benchmarks comparing VM vs native on arm64 targets.
   - Golden asm tests for representative functions.
   - CI jobs building/assembling arm64 on appropriate runners.

## Next Concrete Items (actionable checklist)

- Add bitwise rr lowering for entry params (and/or/xor) with tests:
  - Files: `cmd_codegen_arm64.cpp`, new `test_codegen_arm64_bitwise.cpp`.

- Add shift‑by‑imm lowering for entry param (shl/lsr/asr) with tests:
  - Files: `cmd_codegen_arm64.cpp`, new `test_codegen_arm64_shift_imm.cpp`.

- Factor a small `Arm64PatternLowerer` helper inside `cmd_codegen_arm64.cpp` to declutter the CLI file and centralize opcode mapping.

- Generalize parameter coverage to x2..x7 for `ret %paramN` and rr ops feeding ret; add tests for 3‑arg functions.
  - Partial: rr/ri ops now normalize any arg index 0..7; next is `ret %paramN` beyond first two.

## Notes and Limitations (today)

- The CLI path is intentionally conservative and single‑function/single‑block oriented. It is a staging area to validate emitter, ABI assumptions, and basic instruction text before introducing a full lowering/MIR pipeline.
- Immediates are emitted naively. Large immediates will need proper materialization sequences once we assemble for real hardware.
- No floating‑point lowering yet; FPR sets and return registers are defined for future work.
 - Scratch usage: current patterns use `x9` as a caller‑saved scratch when normalizing rr operands. This is safe in leaf‑like codegen but must be tracked once prologue/epilogue and spills enter the picture.
 - Shift semantics: IL defines mask‑mod‑64 for shift counts in some backends; ensure IL shifts lowered on AArch64 adhere to IL rules (immediate variants are safe; register shifts will need masking or verifier guarantees).

## References

- AArch64 Procedure Call Standard (AAPCS64)
- Apple/Darwin ABI conventions for arm64 (callee‑saved v8..v15, etc.)
