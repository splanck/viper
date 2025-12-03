# CODEMAP: Codegen

Native code generation backends for x86-64 and ARM64.

## Common Utilities (`src/codegen/common/`)

| File | Purpose |
|------|---------|
| `ArgNormalize.hpp` | Argument normalization utilities shared across backends |
| `LabelUtil.hpp` | Deterministic label generation for blocks and jumps |

## ARM64 Backend (`src/codegen/aarch64/`)

Functional backend targeting AAPCS64 (Apple Silicon, Linux ARM64).

### Core Infrastructure

| File | Purpose |
|------|---------|
| `TargetAArch64.hpp/cpp` | Target description: registers, calling convention, ABI |
| `MachineIR.hpp/cpp` | Machine IR data structures (MFunction, MBasicBlock, MInstr, MOperand) |
| `OpcodeMappings.hpp` | IL opcode to ARM64 MIR opcode mappings |

### Compilation Pipeline

| File | Purpose |
|------|---------|
| `LowerILToMIR.hpp/cpp` | IL to Machine IR lowering with CFG construction |
| `RegAllocLinear.hpp/cpp` | Linear-scan register allocator with spill support |
| `FrameBuilder.hpp/cpp` | Stack frame construction and spill slot management |
| `FramePlan.hpp` | Frame layout planning structures |

### Code Emission

| File | Purpose |
|------|---------|
| `AsmEmitter.hpp/cpp` | ARM64 assembly emission (GAS syntax) |
| `RodataPool.hpp/cpp` | Read-only data pool for constants and strings |

### Generated Files (`generated/`)

| File | Purpose |
|------|---------|
| `OpcodeDispatch.inc` | Generated opcode dispatch tables |

## x86-64 Backend (`src/codegen/x86_64/`)

Phase A complete backend targeting System V AMD64 ABI.

### Core Infrastructure

| File | Purpose |
|------|---------|
| `Backend.hpp/cpp` | High-level backend façade orchestrating the pipeline |
| `CodegenPipeline.hpp/cpp` | End-to-end compilation pipeline |
| `TargetX64.hpp/cpp` | Target description: registers, calling convention, ABI |
| `MachineIR.hpp/cpp` | Machine IR data structures |
| `Unsupported.hpp` | Diagnostics for unimplemented features |

### IL Lowering

| File | Purpose |
|------|---------|
| `LowerILToMIR.hpp/cpp` | IL to Machine IR bridge |
| `LoweringRules.hpp/cpp` | Declarative lowering rule definitions |
| `LoweringRuleTable.hpp/cpp` | Compiled rule table for pattern matching |
| `Lowering.Arith.cpp` | Arithmetic operation lowering |
| `Lowering.Bitwise.cpp` | Bitwise operation lowering |
| `Lowering.CF.cpp` | Control flow lowering |
| `Lowering.EH.cpp` | Exception handling lowering |
| `Lowering.Mem.cpp` | Memory operation lowering |
| `Lowering.EmitCommon.hpp/cpp` | Shared lowering helpers |
| `LowerDiv.cpp` | Division/remainder with guards |

### Instruction Selection

| File | Purpose |
|------|---------|
| `ISel.hpp/cpp` | Instruction selection and legalization |
| `Peephole.hpp/cpp` | Peephole optimizations |
| `OperandUtils.hpp` | Operand manipulation utilities |
| `ParallelCopyResolver.hpp` | Parallel copy resolution for phi elimination |

### Register Allocation (`ra/`)

| File | Purpose |
|------|---------|
| `Allocator.hpp/cpp` | Core linear-scan allocator |
| `LiveIntervals.hpp/cpp` | Live interval computation |
| `Spiller.hpp/cpp` | Spill code insertion |
| `Coalescer.hpp/cpp` | Copy coalescing |
| `RegAllocLinear.hpp/cpp` | Allocator orchestration |

### Frame and ABI

| File | Purpose |
|------|---------|
| `FrameLowering.hpp/cpp` | Stack frame layout and prologue/epilogue |
| `CallLowering.hpp/cpp` | System V calling convention implementation |

### Code Emission

| File | Purpose |
|------|---------|
| `AsmEmitter.hpp/cpp` | AT&T syntax assembly emission |
| `asmfmt/Format.hpp/cpp` | Assembly formatting helpers |

### Pipeline Passes (`passes/`)

| File | Purpose |
|------|---------|
| `PassManager.hpp/cpp` | Backend pass orchestration |
| `LoweringPass.hpp/cpp` | IL lowering pass |
| `LegalizePass.hpp/cpp` | Instruction legalization pass |
| `RegAllocPass.hpp/cpp` | Register allocation pass |
| `EmitPass.hpp/cpp` | Assembly emission pass |
