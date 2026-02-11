# CODEMAP: Codegen

Last updated: 2026-01-15

## Status

- AArch64: Validated end‑to‑end on Apple Silicon (ran full Frogger demo).
- x86_64: Validated on Windows with full codegen test suite passing. Supports System V AMD64 and Windows x64 ABIs.

Native code generation backends for x86_64 and AArch64.

## Common Utilities (`src/codegen/common/`)

| File                        | Purpose                                                 |
|-----------------------------|---------------------------------------------------------|
| `Diagnostics.hpp/cpp`       | Codegen-specific diagnostic reporting                   |
| `LabelUtil.hpp`             | Deterministic label generation for blocks and jumps     |
| `LinkerSupport.hpp/cpp`     | Platform linker invocation and object file support      |
| `ParallelCopyResolver.hpp`  | Parallel copy resolution for SSA deconstruction         |
| `PassManager.hpp`           | Codegen pass manager interface                          |
| `RuntimeComponents.hpp`     | Runtime component descriptors for codegen               |
| `TargetInfoBase.hpp`        | Base class for target-specific information              |

## AArch64 Backend (`src/codegen/aarch64/`)

Targeting AAPCS64 (Apple Silicon, Linux ARM64).

### Core Infrastructure

| File                       | Purpose                                  |
|----------------------------|------------------------------------------|
| `TargetAArch64.hpp/cpp`    | Target description and ABI information   |
| `FramePlan.hpp`            | Stack frame layout planning              |
| `FrameBuilder.hpp/cpp`     | Stack frame construction                 |
| `MachineIR.hpp/cpp`        | Machine IR data structures               |
| `OpcodeMappings.hpp`       | IL opcode to MIR opcode mapping          |
| `LoweringContext.hpp`      | Lowering pass context and state          |

### Lowering

| File                        | Purpose                                 |
|-----------------------------|-----------------------------------------|
| `LowerILToMIR.hpp/cpp`      | Main IL to MIR lowering driver          |
| `InstrLowering.hpp/cpp`     | Per-instruction lowering rules          |
| `OpcodeDispatch.hpp/cpp`    | Opcode-based dispatch for lowering      |
| `TerminatorLowering.hpp/cpp`| Block terminator lowering               |

### Optimization & Register Allocation

| File                        | Purpose                                 |
|-----------------------------|-----------------------------------------|
| `Peephole.hpp/cpp`          | Peephole optimizations                  |
| `LivenessAnalysis.hpp/cpp`  | Liveness analysis for register alloc    |
| `RegAllocLinear.hpp/cpp`    | Linear scan register allocator          |

### Emission

| File                   | Purpose                                      |
|------------------------|----------------------------------------------|
| `AsmEmitter.hpp/cpp`   | ARM assembly emission                        |
| `RodataPool.hpp/cpp`   | Read-only data pool (constants, strings)     |

### Fast Paths (`fastpaths/`)

| File                        | Purpose                                 |
|-----------------------------|-----------------------------------------|
| `FastPaths.hpp/cpp`         | Fast path dispatch                      |
| `FastPathsInternal.hpp`     | Internal fast path utilities            |
| `FastPaths_Arithmetic.cpp`  | Arithmetic operation fast paths         |
| `FastPaths_Call.cpp`        | Call lowering fast paths                |
| `FastPaths_Cast.cpp`        | Type cast fast paths                    |
| `FastPaths_Memory.cpp`      | Memory operation fast paths             |
| `FastPaths_Return.cpp`      | Return instruction fast paths           |

## x86_64 Backend (`src/codegen/x86_64/`)

Targeting System V AMD64 ABI (Linux/macOS) and Windows x64 ABI.

### Driver & Pipeline

| File                     | Purpose                                    |
|--------------------------|--------------------------------------------|
| `Backend.hpp/cpp`        | High-level backend facade                  |
| `CodegenPipeline.hpp/cpp`| End-to-end compilation pipeline            |

### Target & ABI

| File                    | Purpose                                     |
|-------------------------|---------------------------------------------|
| `TargetX64.hpp/cpp`     | Target description, SysV and Win64 ABIs     |
| `FrameLowering.hpp/cpp` | Stack frame layout and prologue/epilogue    |
| `CallLowering.hpp/cpp`  | Calling convention implementation           |

### Machine IR

| File               | Purpose                                          |
|--------------------|--------------------------------------------------|
| `MachineIR.hpp/cpp`| Machine IR data structures (MInstr, MBasicBlock) |

### IL Lowering

| File                        | Purpose                                 |
|-----------------------------|-----------------------------------------|
| `LowerILToMIR.hpp/cpp`      | Main IL to MIR lowering adapter         |
| `LoweringRules.hpp/cpp`     | Rule-based lowering infrastructure      |
| `LoweringRuleTable.hpp/cpp` | Lowering rule table and dispatch        |
| `Lowering.Arith.cpp`        | Arithmetic operation lowering           |
| `Lowering.Bitwise.cpp`      | Bitwise operation lowering              |
| `Lowering.CF.cpp`           | Control flow lowering                   |
| `Lowering.EH.cpp`           | Exception handling lowering             |
| `Lowering.Mem.cpp`          | Memory operation lowering               |
| `Lowering.EmitCommon.hpp/cpp`| Shared lowering emission helpers       |
| `LowerDiv.cpp`              | Division/modulo lowering                |

### Instruction Selection & Optimization

| File                       | Purpose                                  |
|----------------------------|------------------------------------------|
| `ISel.hpp/cpp`             | Instruction selection and legalization   |
| `Peephole.hpp/cpp`         | Peephole optimizations                   |
| `OperandUtils.hpp`         | Operand manipulation utilities           |
| `ParallelCopyResolver.hpp` | Parallel copy resolution for SSA         |

### Pipeline Passes (`passes/`)

| File                    | Purpose                                      |
|-------------------------|----------------------------------------------|
| `PassManager.hpp/cpp`   | Pass orchestration and sequencing            |
| `LoweringPass.hpp/cpp`  | IL to MIR lowering pass                      |
| `LegalizePass.hpp/cpp`  | Instruction selection/legalization pass      |
| `RegAllocPass.hpp/cpp`  | Register allocation pass                     |
| `EmitPass.hpp/cpp`      | Assembly emission pass                       |

### Register Allocation (`ra/`)

| File                     | Purpose                                     |
|--------------------------|---------------------------------------------|
| `Allocator.hpp/cpp`      | Linear scan register allocator              |
| `LiveIntervals.hpp/cpp`  | Live interval computation                   |
| `Spiller.hpp/cpp`        | Spill code insertion                        |
| `Coalescer.hpp/cpp`      | Copy coalescing                             |

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
