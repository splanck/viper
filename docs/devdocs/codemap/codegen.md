# CODEMAP: Codegen

Last updated: 2026-02-17

## Status

- AArch64: Validated end‑to‑end on Apple Silicon (ran full Frogger demo).
- x86_64: Validated on Windows with full codegen test suite passing. Supports System V AMD64 and Windows x64 ABIs.

Native code generation backends for x86_64 and AArch64.

## Common Utilities (`src/codegen/common/`)

| File                       | Purpose                                             |
|----------------------------|-----------------------------------------------------|
| `Diagnostics.hpp/cpp`      | Codegen-specific diagnostic reporting               |
| `LabelUtil.hpp`            | Deterministic label generation for blocks and jumps |
| `LinkerSupport.hpp/cpp`    | Platform linker invocation and object file support  |
| `ParallelCopyResolver.hpp` | Parallel copy resolution for SSA deconstruction     |
| `PassManager.hpp`          | Codegen pass manager interface                      |
| `RuntimeComponents.hpp`    | Runtime component descriptors for codegen           |
| `TargetInfoBase.hpp`       | Base class for target-specific information          |

## AArch64 Backend (`src/codegen/aarch64/`)

Targeting AAPCS64 (Apple Silicon, Linux ARM64).

### Core Infrastructure

| File                    | Purpose                                |
|-------------------------|----------------------------------------|
| `FrameBuilder.hpp/cpp`  | Stack frame construction               |
| `FramePlan.hpp`         | Stack frame layout planning            |
| `LoweringContext.hpp`   | Lowering pass context and state        |
| `MachineIR.hpp/cpp`     | Machine IR data structures             |
| `OpcodeMappings.hpp`    | IL opcode to MIR opcode mapping        |
| `TargetAArch64.hpp/cpp` | Target description and ABI information |

### Lowering

| File                        | Purpose                            |
|-----------------------------|------------------------------------|
| `InstrLowering.hpp/cpp`     | Per-instruction lowering rules     |
| `LowerILToMIR.hpp/cpp`      | Main IL to MIR lowering driver     |
| `OpcodeDispatch.hpp/cpp`    | Opcode-based dispatch for lowering |
| `TerminatorLowering.hpp/cpp`| Block terminator lowering          |

### Optimization & Register Allocation

| File                      | Purpose                              |
|---------------------------|--------------------------------------|
| `LivenessAnalysis.hpp/cpp`| Liveness analysis for register alloc |
| `Peephole.hpp/cpp`        | Peephole optimizations               |
| `RegAllocLinear.hpp/cpp`  | Linear scan register allocator       |

### Emission

| File                   | Purpose                                      |
|------------------------|----------------------------------------------|
| `AsmEmitter.hpp/cpp`   | ARM assembly emission                        |
| `RodataPool.hpp/cpp`   | Read-only data pool (constants, strings)     |

### Fast Paths (`fastpaths/`)

| File                       | Purpose                        |
|----------------------------|--------------------------------|
| `FastPaths.hpp/cpp`        | Fast path dispatch             |
| `FastPaths_Arithmetic.cpp` | Arithmetic operation fast paths|
| `FastPaths_Call.cpp`       | Call lowering fast paths       |
| `FastPaths_Cast.cpp`       | Type cast fast paths           |
| `FastPaths_Memory.cpp`     | Memory operation fast paths    |
| `FastPaths_Return.cpp`     | Return instruction fast paths  |
| `FastPathsInternal.hpp`    | Internal fast path utilities   |

## x86_64 Backend (`src/codegen/x86_64/`)

Targeting System V AMD64 ABI (Linux/macOS) and Windows x64 ABI.

### Driver & Pipeline

| File                     | Purpose                                    |
|--------------------------|--------------------------------------------|
| `Backend.hpp/cpp`        | High-level backend facade                  |
| `CodegenPipeline.hpp/cpp`| End-to-end compilation pipeline            |

### Target & ABI

| File                    | Purpose                                   |
|-------------------------|-------------------------------------------|
| `CallLowering.hpp/cpp`  | Calling convention implementation         |
| `FrameLowering.hpp/cpp` | Stack frame layout and prologue/epilogue  |
| `TargetX64.hpp/cpp`     | Target description, SysV and Win64 ABIs   |

### Machine IR

| File               | Purpose                                          |
|--------------------|--------------------------------------------------|
| `MachineIR.hpp/cpp`| Machine IR data structures (MInstr, MBasicBlock) |

### IL Lowering

| File                         | Purpose                              |
|------------------------------|--------------------------------------|
| `LowerDiv.cpp`               | Division/modulo lowering             |
| `LowerILToMIR.hpp/cpp`       | Main IL to MIR lowering adapter      |
| `LoweringRuleTable.hpp/cpp`  | Lowering rule table and dispatch     |
| `LoweringRules.hpp/cpp`      | Rule-based lowering infrastructure   |
| `Lowering.Arith.cpp`         | Arithmetic operation lowering        |
| `Lowering.Bitwise.cpp`       | Bitwise operation lowering           |
| `Lowering.CF.cpp`            | Control flow lowering                |
| `Lowering.EH.cpp`            | Exception handling lowering          |
| `Lowering.EmitCommon.hpp/cpp`| Shared lowering emission helpers     |
| `Lowering.Mem.cpp`           | Memory operation lowering            |

### Instruction Selection & Optimization

| File                       | Purpose                              |
|----------------------------|--------------------------------------|
| `ISel.hpp/cpp`             | Instruction selection and legalization|
| `OperandUtils.hpp`         | Operand manipulation utilities       |
| `ParallelCopyResolver.hpp` | Parallel copy resolution for SSA     |
| `Peephole.hpp/cpp`         | Peephole optimizations               |

### Pipeline Passes (`passes/`)

| File                   | Purpose                                 |
|------------------------|-----------------------------------------|
| `EmitPass.hpp/cpp`     | Assembly emission pass                  |
| `LegalizePass.hpp/cpp` | Instruction selection/legalization pass |
| `LoweringPass.hpp/cpp` | IL to MIR lowering pass                 |
| `PassManager.hpp/cpp`  | Pass orchestration and sequencing       |
| `RegAllocPass.hpp/cpp` | Register allocation pass                |

### Register Allocation (`ra/`)

| File                    | Purpose                        |
|-------------------------|--------------------------------|
| `Allocator.hpp/cpp`     | Linear scan register allocator |
| `Coalescer.hpp/cpp`     | Copy coalescing                |
| `LiveIntervals.hpp/cpp` | Live interval computation      |
| `Spiller.hpp/cpp`       | Spill code insertion           |

### Legacy Register Allocation

| File                    | Purpose                                      |
|-------------------------|----------------------------------------------|
| `RegAllocLinear.hpp/cpp`| Original linear scan allocator (deprecated)  |

### Assembly Formatting (`asmfmt/`)

| File              | Purpose                                           |
|-------------------|---------------------------------------------------|
| `Format.hpp/cpp`  | AT&T syntax assembly formatting helpers           |

### Emission

| File                 | Purpose                                        |
|----------------------|------------------------------------------------|
| `AsmEmitter.hpp/cpp` | x86-64 assembly emission with encoding table   |

### Overflow Lowering

| File             | Purpose                                            |
|------------------|----------------------------------------------------|
| `LowerOvf.cpp`   | Overflow-checked operation lowering                |

### Miscellaneous

| File             | Purpose                                            |
|------------------|----------------------------------------------------|
| `Unsupported.hpp`| Tracking for unsupported IL opcodes                |
