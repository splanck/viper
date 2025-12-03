# AArch64 (arm64) Backend — Status and Plan

**Last Updated:** November 2025

This document captures the current state of the AArch64 backend, recent bug fixes, missing features needed for real programs, and the development roadmap. It is kept developer‑focused with concrete source references and test cases.

## Executive Summary

### Current Status (November 2025)
- ✅ **Basic compilation pipeline works**: IL → ARM64 assembly → native binary
- ✅ **Platform bugs fixed**: macOS section directives, runtime function prefixes, label sanitization
- ✅ **Simple programs execute**: Functions returning constants or simple arithmetic on parameters
- ❌ **Real programs crash**: Missing critical features for memory, strings, arrays, and OOP

### Test Case: Frogger Demo
The frogger demo (`demos/frogger/frogger.bas`) serves as our benchmark for backend completeness:
- **Compiles successfully**: 12,771 lines of IL → 56KB assembly → 121KB native binary
- **Links successfully**: All symbols resolved with runtime library
- **Crashes at runtime**: Missing instruction support causes immediate segfault

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

2) Cleanly factor the pattern‑lowerer — DONE
   - Extracted the pattern code into a small internal `Arm64PatternLowerer` helper inside `src/tools/ilc/cmd_codegen_arm64.cpp`, keeping the CLI tidy and making opcode→sequence mappings centralized.
   - Future: consider a table‑driven mapping from IL opcodes to emitter lambdas for rr/ri forms.

3) Expand parameter coverage to x2..x7 for 3+ argument functions (still single‑block patterns)
   - DONE for rr/ri patterns via normalization to x0/x1 using `x9` scratch.
   - DONE: generalized `ret %paramN` beyond the first two by moving `ArgRegN → x0`.

4) Simple compares feeding ret (i1 → i64)
   - Equality/relational compares producing i1 → set x0 to 0/1 using `cmp` + `cset` when the value is returned immediately.

## Medium‑Term Plan (next 2–6 weeks)

4) Minimal MIR layer for AArch64 (Phase A) — DONE (Step A)
   - Implemented a compact MIR for arm64 under `src/codegen/aarch64/MachineIR.hpp` with a tiny opcode set (mov rr/ri, add/sub/mul rrr/ri, shift‑imm, cmp rr/ri, cset) and containers (`MFunction`, `MBasicBlock`, `MInstr`).
   - Extended `AsmEmitter` with `emitFunction(MFunction)`, `emitBlock`, and `emitInstruction` to print MIR → asm using existing helpers.
   - Added unit test `src/tests/unit/codegen/test_emit_aarch64_mir_minimal.cpp` to validate prologue/add/epilogue emission via MIR.
   - Added a minimal IL→MIR lowering shim used by the CLI: `src/codegen/aarch64/LowerILToMIR.{hpp,cpp}` lowers the existing CLI patterns (ret const/param, rr/ri ops incl. bitwise, shift‑imm, compares) into MIR. The CLI now calls IL→MIR then MIR→asm.
   - Extended MIR opcode coverage to include bitwise rr (`AndRRR`/`OrrRRR`/`EorRRR`) and added a focused MIR unit test (`test_emit_aarch64_mir_bitwise.cpp`).
   - Added basic block label emission and branches in MIR (`Br`, `BCond`) with a unit test (`test_emit_aarch64_mir_branches.cpp`). IL→MIR now lowers trivial `br`/`cbr` (const folds and simple i1 via cmp x0, #0) as a starting point; fuller CF lowering remains next.

5) Frame and prologue/epilogue refinement
   - DONE (GPR saves): Introduced `FramePlan` (header-only) and extended `AsmEmitter` with `emitPrologue/Epilogue(FramePlan)` to save/restore requested callee‑saved GPRs (pairs + single). Added unit test `test_emit_aarch64_frameplan.cpp` covering odd/even saves. Next: incorporate FramePlan into MIR/CLI path when non‑volatile usage arises; add FPR saves later.
   - NEXT: Implement basic stack object placement for spills and outgoing call frames (kept out of scope for this step).

6) Register allocation (linear scan)
   - Adapt or re‑implement the x86 linear scan allocator for arm64 MIR.
   - Add spill code paths and tests.

7) Calls and ABI interop
  - Implement call lowering: argument assignment (x0..x7 / v0..v7), stack spills for extras, return value handling.
  - Add minimal runtime call smoke tests.

8) Assembler + link integration (optional gating on host/toolchain)
   - Add `-assemble`/`--run-native` modes for arm64 akin to x64 pipeline, gated behind host/toolchain checks.
   - Large immediates: MIR `movri` now materializes wide constants with `movz/movk` in the emitter. Addressing sequences (`adrp+add`) remain future work.

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

- Bitwise rr lowering (and/or/xor) with tests — DONE
  - Implemented via IL→MIR in `LowerILToMIR.cpp` and emitted by `AsmEmitter`.
  - Tests: `test_codegen_arm64_bitwise.cpp`, `test_emit_aarch64_mir_bitwise.cpp`.

- Shift‑by‑imm lowering (shl/lsr/asr) with tests — DONE
  - Implemented in IL→MIR (LslRI/LsrRI/AsrRI) and emitted by `AsmEmitter`.
  - Tests: `test_codegen_arm64_shift_imm.cpp`.

- Factor a small `Arm64PatternLowerer` helper — DONE
  - Functionality moved into `LowerILToMIR` keeping the CLI thin.

- Conditional branches on compares (rr and ri) — DONE
  - IL→MIR lowers `cbr (icmp/scmp/ucmp ...) , T, F` to `cmp` + `b.<cond>` sequences, with param normalization and immediate handling.
  - Tests: `test_emit_aarch64_mir_branches.cpp`, `test_codegen_arm64_cbr.cpp`, `test_codegen_arm64_icmp.cpp`, `test_codegen_arm64_icmp_imm.cpp`.

## Quick Start Testing

### Verify Backend Works
```bash
# Simple test that should return exit code 15
cat > /tmp/test.il << 'EOF'
il 0.1.2
func @main() -> i64 {
entry:
  ret 15
}
EOF

./build/src/tools/ilc/ilc codegen arm64 /tmp/test.il -S /tmp/test.s
as /tmp/test.s -o /tmp/test.o
clang++ /tmp/test.o -o /tmp/test_native
/tmp/test_native
echo "Exit code: $?"  # Should print 15
```

### Test Frogger Compilation
```bash
# Compiles but crashes at runtime due to missing features
./build/src/tools/ilc/ilc front basic -emit-il demos/frogger/frogger.bas > /tmp/frogger.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/frogger.il -S /tmp/frogger.s
as /tmp/frogger.s -o /tmp/frogger.o
clang++ /tmp/frogger.o build/src/runtime/libviper_runtime.a -o /tmp/frogger_native
# Binary created but segfaults when run
```

## Critical Bugs Fixed (November 2025)

During attempted native compilation of the frogger demo (`demos/frogger/frogger.bas`), three critical bugs were discovered in the ARM64 backend that prevent successful assembly on macOS:

### BUG 1: Incorrect Section Directive for macOS
- **Location**: `src/codegen/aarch64/cmd_codegen_arm64.cpp:136`
- **Issue**: The backend emits `.section .rodata` which is ELF/Linux syntax
- **Error**: `error: unexpected token in '.section' directive`
- **Root Cause**: Hardcoded Linux-style section directive without platform detection
- **Fix Required**: Use `__TEXT,__const` for macOS (Mach-O format) instead of `.section .rodata`
- **Code**:
  ```cpp
  os << ".section .rodata\n";  // Current (incorrect for macOS)
  // Should be: os << "__TEXT,__const\n";  // For macOS
  ```

### BUG 2: Invalid Label Generation with Negative Numbers
- **Location**: Label generation in IL→assembly lowering, likely in `cmd_codegen_arm64.cpp` or `LowerILToMIR.cpp`
- **Issue**: Labels like `L-1000000000_POSITION.INIT` are generated with negative numbers
- **Error**: `error: unexpected token in argument list` when assembler encounters labels starting with `L-`
- **Root Cause**: Uninitialized or placeholder ID values (-1000000000) being used in label generation
- **Examples from frogger.s**:
  ```asm
  L-1000000000_POSITION.INIT:      # Invalid
  L-1000000000_VEHICLE.MOVERIGHT:  # Invalid
  L-1000000000_FROG.INIT:          # Invalid
  ```
- **Fix Required**: Ensure all label IDs are properly initialized with valid positive values before label generation

### BUG 3: Missing Rodata Pool Implementation
- **Location**: `src/codegen/aarch64/AsmEmitter.cpp` (compared to `src/codegen/x86_64/AsmEmitter.cpp`)
- **Issue**: No rodata pool for deduplicating string literals and constants
- **Impact**: Duplicate symbol definitions for string literals (e.g., multiple `L0:` labels)
- **Root Cause**: The AArch64 backend lacks the rodata pool mechanism present in the x86_64 backend
- **x86_64 Implementation** (for reference):
  - Has `RodataPool` class in `AsmEmitter.cpp:25-54`
  - Deduplicates literals via hash tracking
  - Emits unique labels for each distinct literal
- **Fix Required**: Implement similar rodata pool mechanism for AArch64 to prevent duplicate symbols

### Impact Analysis
These bugs completely prevent native ARM64 compilation of any non-trivial BASIC program on macOS. The frogger demo, which successfully compiles to 12,771 lines of IL and runs correctly in the VM, generates a 56KB assembly file that cannot be assembled due to these issues. The bugs affect:
- Any program with string literals (BUG 1 & 3)
- Any OOP program with classes (BUG 2)
- Cross-platform compatibility (BUG 1)

### Test Case
To reproduce these bugs:
```bash
cd /Users/stephen/git/viper
./build/src/tools/ilc/ilc front basic -emit-il demos/frogger/frogger.bas > /tmp/frogger.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/frogger.il -S /tmp/frogger.s
as /tmp/frogger.s  # Fails with multiple errors
```

### Resolution (November 2025)
All three bugs have been fixed:
- **BUG 1**: Already had platform detection implemented
- **BUG 2**: Label sanitization was already dropping negative signs
- **BUG 3**: RodataPoolAArch64 was already implemented
- **Additional fix**: Added runtime function underscore prefixing for macOS

## Missing Features for Frogger (November 2025 Analysis)

### IL Instruction Coverage Gap
Analysis of frogger.il reveals heavy usage of unsupported instructions:

| Instruction | Count | Status | Required For |
|------------|-------|--------|--------------|
| call | 1294 | ❌ Partial | Method dispatch, runtime calls |
| load | 866 | ❌ Missing | All variable access |
| store | 393 | ❌ Missing | All variable assignment |
| alloca | 266 | ❌ Missing | Local variables |
| gep | 139 | ❌ Missing | Array/struct access |
| cast | 36 | ❌ Missing | Type conversions |
| phi | N/A | ❌ Missing | Control flow joins |
| select | 1 | ❌ Missing | Conditional values |

### Runtime Function Support
Top runtime functions called by frogger (unsupported):

| Function | Calls | Purpose |
|----------|-------|---------|
| @Viper.Console.PrintStr | 251 | String output |
| @rt_modvar_addr_* | 252 | Global variables |
| @rt_str_* | 184 | String operations |
| @rt_arr_obj_* | 138 | Array operations |
| @rt_obj_new_* | 29 | Object allocation |
| OOP methods | ~100 | Class method calls |

### Memory Operations
The backend currently cannot:
- Allocate stack space (`alloca`)
- Load from memory (`load`)
- Store to memory (`store`)
- Calculate addresses (`gep`)
- Access globals (no global variable support)

### Control Flow Limitations
- No phi nodes for SSA form
- No switch statements (frogger uses several)
- Limited branch patterns (only simple cbr)
- No exception handling

### Type System Gaps
- No string type support
- No array operations
- No object/class support
- No floating-point operations
- Limited integer sizes (only i64)

### Calling Convention Issues
- Cannot marshal >2 arguments properly
- No stack argument passing
- No struct return values
- No varargs support

## Development Priority for Real Program Support

Based on frogger's requirements, the critical path to a working game is:

### Phase 1: Core Memory (Required First)
1. **Stack frame layout** - Allocate space for locals
2. **alloca/load/store** - Basic memory access
3. **Global variables** - At least read-only strings

### Phase 2: Full Calling Convention
1. **Register allocation** - Proper argument marshalling
2. **Stack arguments** - Support >8 parameters
3. **Callee-save registers** - Preserve across calls

### Phase 3: Essential Runtime
1. **String operations** - PrintStr, concatenation
2. **Basic arrays** - Integer and object arrays
3. **Memory allocation** - rt_malloc/free bridge

### Phase 4: Control Flow
1. **Phi nodes** - SSA form for branches
2. **Switch statements** - Jump tables
3. **Loop optimizations** - Basic patterns

### Phase 5: OOP Support
1. **Method dispatch** - Virtual calls
2. **Object layout** - Fields and vtables
3. **Constructor/destructor** - Lifecycle management

## New Work Completed

- Minimal call lowering (rr args, ret-forward) — DONE
  - MIR: Added `Bl` opcode for `bl <callee>`.
  - Emitter: Prints `bl <name>` from MIR.
  - Lowering: IL→MIR recognizes single-block pattern `%t = call @callee(%paramK...)` feeding `ret %t` and emits `Bl callee`. Assumes args already in x0..x7 as per ABI (no marshalling yet).
  - Tests: `test_codegen_arm64_call.cpp` validates CLI emits `bl h` for a simple extern call.

- Argument marshalling for simple calls (params + immediates) — DONE
  - IL→MIR now marshals integer arguments into `x0..x7` for call sites when operands are entry parameters or constant i64 values.
  - Handles register permutations and cycles using `x9` scratch; immediates are materialized with `mov`/`movz+movk` by the emitter.
  - Tests: `test_codegen_arm64_call_args.cpp` covers `%a, 5` (RI), swapped params `%b, %a` (RR with cycle), and a three-arg case `%b, 7, %a`.

- FramePlan integration in MIR emission — DONE
  - `MFunction` now carries an optional list of callee‑saved GPRs (`savedGPRs`).
  - `AsmEmitter::emitFunction` saves/restores those via `emitPrologue/Epilogue(FramePlan)`.
  - Test: `test_emit_aarch64_mir_frameplan.cpp` verifies `stp/str` saves and matching restores are emitted around blocks.

- One non-entry temp as call arg — DONE
  - Lowerer can compute a limited expression into a scratch (X9/X10) and marshal it to the arg reg: rr arithmetic/bitwise, ri add/sub/shift-imm, and compares (rr/ri) over entry params only.
  - Uses `cmp + cset` for compares; reuses existing param-indexing and condition mapping.
  - Tests: `test_codegen_arm64_call_temp_args.cpp` covers rr (`add`), ri (`add` with imm), shift-imm, compare, and two-temp cases (`x9` and `x10`).

- Outgoing stack-argument scaffolding — PARTIAL
  - MIR: Added opcodes for stack pointer adjust and stores to outgoing arg area: `SubSpImm`, `AddSpImm`, `StrRegSpImm`.
  - Emitter: Emits `sub sp, sp, #N`, `add sp, sp, #N`, and `str xN, [sp, #off]` for these MIR ops.
  - Lowering: Extended IL→MIR call marshalling to allocate an outgoing stack area (16-byte aligned), materialize extra integer args (>8) into scratch regs, and store them at `[sp, #offset]` before `bl`, then deallocate the area after the call.
  - Notes: CLI enablement for stack args is conservative; existing tests remain green. A focused test will be enabled once additional edge cases (e.g., reading this-function’s stack params) are covered.

## What’s Next

- Calls: Lift remaining temp restrictions (multiple temps beyond two; non-entry temps built from non-param operands); enable and harden >8 integer args path now that MIR/emitter scaffolding exists; add FPR args (v0..v7).
- Control Flow: Broaden IL→MIR CF lowering for non-trivial diamonds (beyond simple cbr), as needed by future patterns.
- Frame: Drive `savedGPRs` from lowering when callee-saved usage is detected (MIR/emitter support is ready); add FPR saves when FP usage lands.

- Generalize parameter coverage to x2..x7 for `ret %paramN` and rr ops feeding ret; add tests for 3‑arg functions. — DONE
  - rr/ri ops normalize any arg index 0..7 using scratch; `ret %paramN` moves ArgRegN → x0 as needed.

## Current Limitations

- **Architecture**: Single-function, single-block focus for pattern matching
- **Memory**: No stack frames, loads/stores, or global access
- **Types**: Only i64 integers; no strings, arrays, objects, or floats
- **Control Flow**: Limited to simple branches; no phi nodes or switch statements
- **Calling Convention**: Maximum 2-3 arguments; no stack passing
- **Runtime**: Only rt_trap supported; missing all string/array/OOP runtime functions

## References

- AArch64 Procedure Call Standard (AAPCS64)
- Apple/Darwin ABI conventions for arm64 (callee‑saved v8..v15, etc.)
