---
status: active
audience: contributors
last-verified: 2026-04-09
---

# AArch64 (arm64) Backend — Status and Plan

This document captures the current state of the AArch64 backend, recent bug fixes, missing features needed for real
programs, and the development roadmap. It is kept developer-focused with concrete source references and test cases.

## Executive Summary

### Current Status (April 2026)

- **End-to-end validated on Apple Silicon**: All demo games compile and run natively
- **Core pipeline mature**: MIR layer, instruction selection, register allocation (with coalescer and protected-use eviction), frame lowering, peephole optimization (6 sub-passes), post-RA scheduler, linker integration
- **Immediate utils**: Extracted `A64ImmediateUtils.hpp` for consistent immediate encoding
- **Binary encoder**: Direct object code emission (bypassing assembler text)
- **Fastpaths**: Arithmetic and call fastpath optimizations for common patterns
- **Register allocator hardening**: Protected-use sets prevent source-operand eviction during def allocation; FPR load/store classification; operandRoles fix for immediate-ALU instructions; clean FPR spill slot reuse across calls; dead vreg early release
- **117 codegen test files**

## Source File Map

### Target description and ABI

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/TargetAArch64.hpp`/`.cpp` | `PhysReg` enum (X0–X30, SP, V0–V31), `TargetInfo` struct, `darwinTarget()` singleton, `isGPR()`, `isFPR()`, `regName()` |

`TargetInfo` contents:

- `darwinTarget()` returns a `const TargetInfo &` singleton with:
    - Caller/callee-saved sets (AAPCS64), arg register orders (X0–X7, V0–V7), return regs (X0, V0), stack alignment (16)
- `isGPR`, `isFPR` classify physical registers
- `regName` renders canonical string names (e.g., `"x0"`, `"v15"`)

### Machine IR

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/MachineIR.hpp`/`.cpp` | `MOpcode` enum, `MReg`, `MOperand`, `MInstr`, `MBasicBlock`, `MFunction` |
| `src/codegen/aarch64/MachineIR.hpp`/`.cpp` | `MFunction::savedGPRs` — callee-saved GPR list for prologue/epilogue |
| `src/codegen/aarch64/generated/OpcodeDispatch.inc` | Generated opcode dispatch table |

MIR opcode categories:

- Integer arithmetic: `AddRRR`, `AddRI`, `SubRRR`, `SubRI`, `MulRRR`, `SDivRRR`, `UDivRRR`, `MSubRRRR`, `MAddRRRR`
- Integer bitwise: `AndRRR`, `EorRRR`, `OrrRRR`, `TstRR`
- Integer shift: `AsrRI`, `AsrvRRR`, `LslRI`, `LslvRRR`, `LsrRI`, `LsrvRRR`
- Integer compare/select: `CmpRI`, `CmpRR`, `Csel`, `Cset`
- Moves: `MovRI`, `MovRR`
- Branches: `BCond`, `Bl`, `Blr`, `Br`, `Cbz`, `Cbnz`, `Ret`
- Address computation: `AddPageOff`, `AdrPage`
- Floating-point: `FAddRRR`, `FCmpRR`, `FCvtZS`, `FCvtZU`, `FDivRRR`, `FMovGR`, `FMovRI`, `FMovRR`,
  `FMulRRR`, `FRintN`, `FSubRRR`, `SCvtF`, `UCvtF`
- Memory (FP-relative): `AddFpImm`, `LdrFprFpImm`, `LdrRegFpImm`, `LdpFprFpImm`, `LdpRegFpImm`,
  `StpFprFpImm`, `StpRegFpImm`, `StrFprFpImm`, `StrRegFpImm`
- Memory (base+offset): `LdrFprBaseImm`, `LdrRegBaseImm`, `StrFprBaseImm`, `StrRegBaseImm`
- Stack pointer: `AddSpImm`, `StrFprSpImm`, `StrRegSpImm`, `SubSpImm`

### Assembly emitter

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/AsmEmitter.hpp`/`.cpp` | AT&T-compatible AArch64 text emitter |
| `src/codegen/aarch64/RodataPool.hpp`/`.cpp` | String literal pool (deduplication, unique labels) |

`AsmEmitter` capabilities:

- `emitFunction(os, fn)` — full MIR function to assembly (header, prologue, blocks, epilogue)
- `emitBlock(os, bb)` — single basic block with label
- `emitInstruction(os, mi)` — individual MIR instruction
- `emitFunctionHeader(os, name)` — `.globl` + `name:` directives
- `emitPrologue(os)` / `emitEpilogue(os)` — default `stp x29, x30, [sp, #-16]!; mov x29, sp` / `ldp x29, x30, [sp], #16; ret`
- `emitPrologue(os, plan)` / `emitEpilogue(os, plan)` — callee-saved GPR save/restore via `FramePlan`
- Integer ops: `emitMovRR`, `emitMovRI`, `emitAddRRR`, `emitSubRRR`, `emitMulRRR`, `emitAddRI`, `emitSubRI`,
  `emitAndRRR`, `emitOrrRRR`, `emitEorRRR`, `emitLslRI`, `emitLsrRI`, `emitAsrRI`
- Chunk-safe SP adjustment: `emitSubSp` / `emitAddSp` use 4080-byte chunks (largest 16-byte-aligned 12-bit value)
- Internal `mangleSymbol()` static function in `AsmEmitter.cpp` provides platform-correct symbol mangling (`_` prefix on Darwin)

### IL to MIR lowering

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/LowerILToMIR.hpp`/`.cpp` | `LowerILToMIR::lowerFunction()` — top-level IL → MIR entry point |
| `src/codegen/aarch64/InstrLowering.hpp`/`.cpp` | Per-opcode lowering handlers (`materializeValueToVReg`, arithmetic, FP, OOP) |
| `src/codegen/aarch64/TerminatorLowering.hpp`/`.cpp` | Branch/return lowering; CBr entry-block restriction (correctness guard) |
| `src/codegen/aarch64/LoweringContext.hpp` | Per-function mutable state threaded through all lowering handlers |
| `src/codegen/aarch64/OpcodeMappings.hpp` | IL opcode → MIR opcode tables |
| `src/codegen/aarch64/OpcodeDispatch.hpp`/`.cpp` | Dispatch infrastructure for per-opcode lowering |

### Fast paths

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/FastPaths.hpp`/`.cpp` | Fast-path dispatcher for common single-block patterns |
| `src/codegen/aarch64/fastpaths/FastPaths_Arithmetic.cpp` | Arithmetic fast paths (register-register and register-immediate; shifts excluded from commutative swap) |
| `src/codegen/aarch64/fastpaths/FastPaths_Call.cpp` | Call fast paths (argument marshalling, stack args); returns `std::nullopt` on failure to fall through to generic path |
| `src/codegen/aarch64/fastpaths/FastPaths_Cast.cpp` | Cast fast paths (narrowing overflow checks) |
| `src/codegen/aarch64/fastpaths/FastPaths_Memory.cpp` | Memory operation fast paths |
| `src/codegen/aarch64/fastpaths/FastPaths_Return.cpp` | Return fast paths |
| `src/codegen/aarch64/fastpaths/FastPathsInternal.hpp` | Shared internal helpers for all fast-path files |

### Register allocation and liveness

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/ra/Allocator.hpp`/`.cpp` | Linear-scan register allocator with protected-use eviction |
| `src/codegen/aarch64/ra/Liveness.hpp`/`.cpp` | Live variable analysis |
| `src/codegen/aarch64/ra/RegPools.hpp`/`.cpp` | Physical register pools (GPR/FPR) |
| `src/codegen/aarch64/ra/OperandRoles.hpp`/`.cpp` | Per-operand use/def role classification |
| `src/codegen/aarch64/ra/OpcodeClassify.hpp` | Opcode classification (call, terminator, mem load/store) |
| `src/codegen/aarch64/ra/InstrBuilders.hpp` | MIR instruction builder helpers for spill/reload |
| `src/codegen/aarch64/ra/RegClassify.hpp` | Register class classification |
| `src/codegen/aarch64/ra/VState.hpp` | Virtual register state tracking |
| `src/codegen/aarch64/Coalescer.hpp`/`.cpp` | Pre-RA register coalescer (~270 LOC) |
| `src/codegen/aarch64/LivenessAnalysis.hpp`/`.cpp` | CFG-level liveness analysis |

### Frame layout

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/FramePlan.hpp` | `FramePlan` struct: callee-saved GPR list, frame size |
| `src/codegen/aarch64/FrameBuilder.hpp`/`.cpp` | `FrameBuilder`: stack slot allocation, aligned slot assignment |

### Peephole optimization

| File | Purpose |
|------|---------|
| `src/codegen/aarch64/Peephole.hpp`/`.cpp` | Top-level peephole dispatcher |
| `src/codegen/aarch64/peephole/BranchOpt.hpp`/`.cpp` | CBZ/CBNZ fusion, branch inversion |
| `src/codegen/aarch64/peephole/CopyPropDCE.hpp`/`.cpp` | Copy propagation + dead code elimination |
| `src/codegen/aarch64/peephole/IdentityElim.hpp`/`.cpp` | Identity operation removal |
| `src/codegen/aarch64/peephole/LoopOpt.hpp`/`.cpp` | Loop-specific optimizations |
| `src/codegen/aarch64/peephole/MemoryOpt.hpp`/`.cpp` | LDP/STP merging |
| `src/codegen/aarch64/peephole/StrengthReduce.hpp`/`.cpp` | MADD fusion, immediate folding |

### CLI driver integration

| File | Purpose |
|------|---------|
| `src/tools/viper/cmd_codegen_arm64.hpp`/`.cpp` | `cmd_codegen_arm64(argc, argv)` — CLI entry for `viper codegen arm64` |

Usage: `viper codegen arm64 <input.il> -S <out.s>`

### Common library (`src/codegen/common/`)

| File | Purpose |
|------|---------|
| `src/codegen/common/Diagnostics.hpp`/`.cpp` | Diagnostic sink |
| `src/codegen/common/LabelUtil.hpp` | Assembler-safe label sanitization (hyphens → underscores) |
| `src/codegen/common/LinkerSupport.hpp`/`.cpp` | Shared linker invocation, runtime archive selection |
| `src/codegen/common/ParallelCopyResolver.hpp` | Target-independent parallel-copy resolution |
| `src/codegen/common/PassManager.hpp` | Generic `Pass<M>` / `PassManager<M>` templates |
| `src/codegen/common/RuntimeComponents.hpp` | Runtime component classification for selective linking |
| `src/codegen/common/TargetInfoBase.hpp` | Shared base for `TargetInfo` structs |

Note: `ArgNormalize.hpp`, `MachineIRBuilder.hpp`, and `MachineIRFormat.hpp` were removed as dead code during the
codegen review.

### Build integration

- `src/codegen/aarch64/CMakeLists.txt` builds `il_codegen_aarch64` (target, emitter, lowering, RA, peephole).
- `src/CMakeLists.txt` exposes `viper_cmd_arm64` as a static lib; `viper` links `viper_cmd_arm64` and
  `il_codegen_aarch64`.

## Backend Pipeline

The AArch64 backend uses `CodegenPipeline` to orchestrate passes. The pipeline stages are:

1. **IL loading** — load module from disk
2. **Rodata pool construction** (`RodataPool`) — scan globals, deduplicate string literals
3. **IL to MIR lowering** (`LowerILToMIR::lowerFunction`) — instruction selection via `InstrLowering` + `TerminatorLowering` + fast paths
4. **Register coalescing** (`Coalescer`) — pre-RA copy elimination
5. **Register allocation** (`ra/Allocator`) — linear scan, spill/reload insertion
6. **Frame finalization** (`FrameBuilder`) — stack slot layout, frame size computation
7. **Peephole optimization** (`Peephole` + 6 sub-passes) — post-RA pattern rewrites
8. **Post-RA scheduling** — instruction reordering for pipeline utilization
9. **Assembly emission** (`AsmEmitter::emitFunction`) — MIR → text assembly
10. **Binary encoding** (`A64BinaryEncoder`) — direct object code emission (optional)
11. **Rodata emission** — string/FP constant pool to `.section __TEXT,__const` (macOS) or `.section .rodata` (Linux)
12. **Assembly + linking** (`LinkerSupport`) — invoke assembler/linker, link with runtime archives

## Calling Convention (AAPCS64 / Darwin)

- **Integer arguments**: X0–X7 (independent sequence from FP args)
- **Floating-point arguments**: V0–V7 (D-register aliases; independent from integer arg sequence)
- **Integer return**: X0
- **Float return**: V0 (D-register)
- **Stack alignment**: 16 bytes at all call sites
- **Callee-saved GPRs**: X19–X28, X29 (FP), X30 (LR)
- **Callee-saved FPRs**: Lower 64 bits of V8–V15 (D8–D15); upper bits are caller-saved
- **Caller-saved GPRs**: X0–X15
- **Caller-saved FPRs**: V0–V7, V16–V31

## Debug Dump Flags

### IL-Level Dumps

Before code reaches the AArch64 backend, it passes through the IL pipeline. The following shared flags inspect those earlier stages (see `docs/debugging.md` §8 for details):

| Flag | Stage |
|------|-------|
| `--dump-tokens` | Lexer token stream |
| `--dump-ast` | AST after parsing |
| `--dump-il` | IL after lowering (pre-optimization) |
| `--dump-il-passes` | IL before/after each optimization pass |
| `--dump-il-opt` | IL after full optimization pipeline |

### MIR Dump Flags

The AArch64 CLI supports flags to dump Machine IR for debugging:

| Flag | Description |
|------|-------------|
| `--dump-mir-before-ra` | Print MIR to stderr before register allocation |
| `--dump-mir-after-ra` | Print MIR to stderr after register allocation |
| `--dump-mir-full` | Print MIR both before and after register allocation |

The complete inspection pipeline from source to native is:

```text
--dump-tokens → --dump-ast → --dump-il → --dump-il-passes → --dump-il-opt → --dump-mir-* → assembly
```

**Example:**

```bash
./build/src/tools/viper/viper codegen arm64 /tmp/test.il -S /tmp/test.s --dump-mir-after-ra
```

**Example output:**

```text
=== MIR after RA: test_func ===
MFunction: test_func
  Block: entry
    AddRRR @x0:gpr <- @x0:gpr, @x1:gpr
    MulRRR @x0:gpr <- @x0:gpr, @x2:gpr
    Ret
```

Virtual registers (`%v0:gpr`) appear before RA; physical registers (`@x0:gpr`) appear after RA.

## Tests

96 AArch64-specific test files live in `src/tests/unit/codegen/`. Selected coverage:

| Test file | Coverage |
|-----------|---------|
| `test_target_aarch64.cpp` | Register name / ABI sanity |
| `test_emit_aarch64_minimal.cpp` | Emitter sequencing (header, prologue, add, epilogue) |
| `test_emit_aarch64_mir_minimal.cpp` | MIR → asm: prologue, add, epilogue |
| `test_emit_aarch64_mir_bitwise.cpp` | MIR AND/ORR/EOR emission |
| `test_emit_aarch64_mir_branches.cpp` | MIR conditional branch emission |
| `test_emit_aarch64_frameplan.cpp` | FramePlan odd/even callee-save pairs |
| `test_emit_aarch64_mir_frameplan.cpp` | MIR `savedGPRs` prologue/epilogue |
| `test_codegen_arm64_cli.cpp` | `ret 0` → `mov x0, #0` |
| `test_codegen_arm64_add_params.cpp` | Add two params → `add x0, x0, x1` |
| `test_codegen_arm64_add_imm.cpp` | Add/sub immediate forms |
| `test_codegen_arm64_bitwise.cpp` | AND/OR/XOR register-register |
| `test_codegen_arm64_bitwise.cpp` | AND/OR/XOR register-register |
| `test_codegen_arm64_call.cpp` | Simple extern call → `bl h` |
| `test_codegen_arm64_call_args.cpp` | Argument marshalling with permutations and cycles |
| `test_codegen_arm64_call_temp_args.cpp` | Non-param temporaries as call args |
| `test_codegen_arm64_callee_saved.cpp` | Callee-saved register preservation across calls |
| `test_codegen_arm64_callee_stack_params.cpp` | Reading stack-passed parameters |
| `test_codegen_arm64_casts.cpp` | Integer cast operations |
| `test_codegen_arm64_cbr.cpp` | Conditional branch patterns |
| `test_codegen_arm64_cbr_cbnz.cpp` | CBZ/CBNZ fusion peephole |
| `test_codegen_arm64_cf_if_else_phi.cpp` | If/else with phi nodes |
| `test_codegen_arm64_cf_loop_phi.cpp` | Loop with phi nodes |
| `test_codegen_arm64_division.cpp` | Signed/unsigned div/rem with zero-check traps |
| `test_codegen_arm64_exception_handling.cpp` | Exception handling paths |
| `test_codegen_arm64_fp.cpp` | Floating-point arithmetic |
| `test_codegen_arm64_fp_basic.cpp` | Basic FP operations (add/sub/mul/div) |
| `test_codegen_arm64_fp_cmp_all.cpp` | All FP comparison predicates including NaN |
| `test_codegen_arm64_gep_load_store.cpp` | GEP, load, store lowering |
| `test_codegen_arm64_icmp.cpp` | Integer compare predicates |
| `test_codegen_arm64_icmp_imm.cpp` | Integer compare with immediates |
| `test_codegen_arm64_indirect_call.cpp` | Indirect call via register (`blr`) |
| `test_codegen_arm64_large_imm.cpp` | Large immediate materialization (`movz/movk`) |
| `test_codegen_arm64_ovf.cpp` | Overflow-checked arithmetic |
| `test_codegen_arm64_params_wide.cpp` | Params beyond x1 (x2/x3 normalization) |
| `test_codegen_arm64_peephole.cpp` | Peephole optimization correctness |
| `test_codegen_arm64_peephole_comprehensive.cpp` | All five peephole patterns |
| `test_codegen_arm64_ra_many_temps.cpp` | Register allocation under high pressure |
| `test_codegen_arm64_ret_param.cpp` | `ret %param0` no-op; `ret %param1` `mov x0, x1` |
| `test_codegen_arm64_rodata_literals.cpp` | String literal pool deduplication |
| `test_codegen_arm64_run_native.cpp` | Full assemble + link + execute on Apple Silicon |
| `test_codegen_arm64_select.cpp` | Select (conditional value) lowering |
| `test_codegen_arm64_shift_imm.cpp` | `lsl`/`lsr`/`asr` by immediate |
| `test_codegen_arm64_shift_reg.cpp` | Shift by register (`lslv`/`lsrv`/`asrv`) |
| `test_codegen_arm64_spill_fpr.cpp` | FPR spill/reload under register pressure |
| `test_codegen_arm64_stack_locals.cpp` | Stack local allocation and access |
| `test_codegen_arm64_switch.cpp` | Switch statement lowering |
| `test_codegen_arm64_unsigned_cmp.cpp` | Unsigned comparison predicates |

All test artifacts are written under `build/test-out/arm64/` (created on demand). CMake wiring is in
`src/tests/CMakeLists.txt`.

## Quick Start Testing

### Verify Backend Works

```bash
cat > /tmp/test.il << 'EOF'
il 0.2.0
func @main() -> i64 {
entry:
  ret 15
}
EOF

./build/src/tools/viper/viper codegen arm64 /tmp/test.il -S /tmp/test.s
as /tmp/test.s -o /tmp/test.o
clang++ /tmp/test.o -o /tmp/test_native
/tmp/test_native
echo "Exit code: $?"  # Should print 15
```

## Critical Bugs Fixed (Historical)

### BUG 1: Incorrect Section Directive for macOS (FIXED)

- **Was**: Always emitted `.section .rodata` (ELF/Linux syntax)
- **Fix**: `AsmEmitter` now emits `.section __TEXT,__const` on macOS (detected via `#ifdef __APPLE__`)

### BUG 2: Invalid Label Generation with Negative Numbers (FIXED)

- **Was**: Labels like `L-1000000000_POSITION.INIT` generated from uninitialized IDs
- **Fix**: Label sanitization in `LabelUtil.hpp` replaces hyphens with underscores; all IDs are initialized before use

### BUG 3: Missing Rodata Pool (FIXED)

- **Was**: No deduplication of string literals, causing duplicate symbol definitions
- **Fix**: `RodataPool` class in `src/codegen/aarch64/RodataPool.hpp`/`.cpp` deduplicates via hash tracking

### BUG 4: V8–V15 in callerSavedFPR (FIXED)

- **Was**: V8–V15 listed in both caller-saved and callee-saved FPR sets; register allocator saved and also treated as clobbered across calls
- **Fix**: Removed V8–V15 from `callerSavedFPR` in `TargetAArch64.cpp`

### BUG 5: Shift commutativity in fast paths (FIXED)

- **Was**: `FastPaths_Arithmetic.cpp` and `FastPaths_Call.cpp::computeTempTo` treated shifts as commutative, swapping operands for `shl`/`lshr`/`ashr`
- **Fix**: Shifts excluded from the commutative operand swap path in both files

### BUG 6: X19 fallback without occupancy check (FIXED)

- **Was**: `RegAllocLinear::takeGPR()` silently returned X19 when all allocatable registers were occupied, without checking if X19 was already in use
- **Fix**: Replaced silent fallback with `assert` to catch register conflicts

### BUG 7: emplace_back invalidates block references (FIXED)

- **Was**: `InstrLowering.cpp` called `emplace_back` on the blocks vector while holding references to earlier blocks (use-after-reallocation UB in 5 functions: `lowerSRemChk0`, `lowerSDivChk0`, `lowerUDivChk0`, `lowerURemChk0`, `lowerBoundsCheck`)
- **Fix**: Moved trap block creation to after all uses of the `out` reference

### BUG 8: Shared GPR/FPR parameter index (FIXED)

- **Was**: `materializeValueToVReg` used the overall parameter position to index both GPR and FPR arg arrays; for `f(i64, f64, i64)` the second i64 loaded from X2 instead of X1
- **Fix**: Count same-class parameters preceding the target to compute the correct register index

### BUG 9: Hardcoded `_rt_*` symbol mangling (FIXED)

- **Was**: Runtime symbols emitted with hardcoded `_rt_` prefix; incorrect on macOS where C symbols need `__rt_`
- **Fix**: `AsmEmitter.cpp` uses an internal `mangleSymbol()` static function for platform-correct symbol name generation

### BUG 10: SP chunk size 4095 not 16-byte aligned (FIXED)

- **Was**: `emitSubSp`/`emitAddSp` used 4095-byte chunks; AArch64 hardware requires SP 16-byte aligned at all times
- **Fix**: Changed chunk size to 4080 (largest multiple of 16 fitting in 12-bit immediate)

### BUG 11: `emitFMovRI` ostream format flags not restored (FIXED)

- **Was**: `std::fixed` applied to ostream but never restored, corrupting all subsequent FP emission
- **Fix**: Save and restore `std::ostream` format flags around FP immediate materialization

## Peephole Optimizations Implemented

| Pattern | Description |
|---------|-------------|
| CBZ/CBNZ fusion | `cmp xN, #0; b.eq label` → `cbz xN, label` (and `b.ne` → `cbnz`). Also handles `tst xN, xN` pattern. |
| MADD fusion | `mul tmp, a, b; add dst, tmp, c` → `madd dst, a, b, c` when mul destination is dead after add. |
| LDP/STP merging | Adjacent `ldr`/`str` with consecutive FP offsets (diff 8) → `ldp`/`stp` pairs. Supports GPR and FPR. |
| Branch inversion | `b.cond .Lnext; b .Lother` (fall-through) → `b.!cond .Lother`. Supports all AArch64 condition codes. |
| Immediate folding | `AddRRR`/`SubRRR` → `AddRI`/`SubRI` when one operand is a known constant in 12-bit range (0–4095). |

New MIR opcodes added to support these patterns: `Cbnz`, `MAddRRRR`, `Csel`, `LdpRegFpImm`, `StpRegFpImm`,
`LdpFprFpImm`, `StpFprFpImm`. (`Cbz` was already present in the original opcode set.)

## Remaining Work

### Unimplemented Peephole Patterns

- **Address mode folding**: Base+offset computations could fold into addressing modes
- **AND-immediate optimization**: AND with bitmask constants could use logical immediate encoding
- **Conditional select (CSEL) pattern matching**: `Csel` opcode exists; pattern matching not yet wired
- **Compare elimination**: Comparisons whose flags are set by a preceding arithmetic instruction
- **Shift-add fusion**: Shift followed by add → shifted-register form of ADD
- **Zero-extension elimination**: Redundant explicit zero-extensions after 32-bit operations

### Architectural Gaps

- **AArch64 PassManager**: `CodegenPipeline` now uses `PassManager`-based composition with Scheduler and BlockLayout passes at O1+; per-pass verification hooks remain limited
- **Darwin symbol fixup**: Uses string search-and-replace on full assembly output (fragile); needs redesign
- **Debug information**: DWARF v5 emitted by native linker; source-level debugging available
- **Instruction scheduling**: Post-RA scheduler implemented; further scheduling opportunities remain
- **MIR verification**: No inter-pass MIR invariant checker
- **Stack size configuration**: No `--stack-size` flag equivalent to x86-64 backend

## References

- AArch64 Procedure Call Standard (AAPCS64)
- Apple/Darwin ABI conventions for arm64 (callee-saved V8–V15 lower 64 bits, etc.)
