# CODEMAP: Codegen

Status:

- AArch64: Validated end‑to‑end on Apple Silicon (ran full Frogger demo).
- x86_64: Implemented but not yet tested on actual x86 hardware; experimental.

Native code generation backends for x86_64 and AArch64.

## Common Utilities (`src/codegen/common/`)

| File                   | Purpose                                                 |
|------------------------|---------------------------------------------------------|
| `ArgNormalize.hpp`     | Argument normalization utilities shared across backends |
| `LabelUtil.hpp`        | Deterministic label generation for blocks and jumps     |
| `MachineIRBuilder.hpp` | Helper to construct Machine IR programmatically         |
| `MachineIRFormat.hpp`  | Helpers for formatted MIR printing                      |

## AArch64 Backend (`src/codegen/aarch64/`)

Targeting AAPCS64 (Apple Silicon, Linux ARM64).

| Area       | Files                                                                     |
|------------|---------------------------------------------------------------------------|
| Target/ABI | `TargetAArch64.hpp/cpp`, `FramePlan.hpp`, `FrameBuilder.hpp/cpp`          |
| MIR        | `MachineIR.hpp/cpp`, `OpcodeMappings.hpp`                                 |
| Lowering   | `LowerILToMIR.hpp/cpp`, `InstrLowering.hpp/cpp`, `OpcodeDispatch.hpp/cpp` |
| Regalloc   | `RegAllocLinear.hpp/cpp`                                                  |
| Emission   | `AsmEmitter.hpp/cpp`, `RodataPool.hpp/cpp`                                |
| Fast paths | `FastPaths.hpp/cpp`                                                       |

## x86_64 Backend (`src/codegen/x86_64/`)

Targeting System V AMD64 ABI.

| Area            | Files                                                                                                                                                                                                                               |
|-----------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Driver/Pipeline | `Backend.hpp/cpp`, `CodegenPipeline.hpp/cpp`                                                                                                                                                                                        |
| Target/ABI      | `TargetX64.hpp/cpp`, `FrameLowering.hpp/cpp`, `CallLowering.hpp/cpp`                                                                                                                                                                |
| MIR             | `MachineIR.hpp/cpp`                                                                                                                                                                                                                 |
| Lowering        | `LowerILToMIR.hpp/cpp`, `LoweringRules.hpp/cpp`, `LoweringRuleTable.hpp/cpp`, `Lowering.Arith.cpp`, `Lowering.Bitwise.cpp`, `Lowering.CF.cpp`, `Lowering.EH.cpp`, `Lowering.Mem.cpp`, `Lowering.EmitCommon.hpp/cpp`, `LowerDiv.cpp` |
| ISel/Opts       | `ISel.hpp/cpp`, `Peephole.hpp/cpp`, `OperandUtils.hpp`, `ParallelCopyResolver.hpp`                                                                                                                                                  |
| Regalloc        | `RegAllocLinear.hpp/cpp`                                                                                                                                                                                                            |
| Emission        | `AsmEmitter.hpp/cpp`                                                                                                                                                                                                                |
| Misc            | `Unsupported.hpp`, `placeholder.cpp`                                                                                                                                                                                                |
