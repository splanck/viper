# CODEMAP: Codegen

- **src/codegen/x86_64/placeholder.cpp**

  Serves as the minimal translation unit anchoring the x86-64 code generation library so the component builds into a linkable object. It defines a stub `placeholder` function that returns zero, preserving the namespace until real emission stages arrive. The file includes nothing and therefore depends solely on the C++ core language, making it an isolated scaffold for future codegen work.

- **src/codegen/x86_64/Backend.hpp**, **src/codegen/x86_64/Backend.cpp**

  Declare and implement the Phase A x86‑64 backend façade that sequences lowering from IL to Machine IR, instruction selection, linear‑scan register allocation, frame lowering, peepholes, and final assembly emission. The façade provides `emitFunctionToAssembly` and `emitModuleToAssembly`, returning assembly text plus diagnostics while warning about unsupported options (e.g., Intel syntax). Dependencies include `LowerILToMIR.hpp`, `ISel.hpp`, `RegAllocLinear.hpp`, `FrameLowering.hpp`, `Peephole.hpp`, `AsmEmitter.hpp`, and `TargetX64.hpp`.

- **src/codegen/x86_64/ISel.hpp**, **src/codegen/x86_64/ISel.cpp**

  Instruction selection for Machine IR: canonicalises arithmetic, compare/branch sequences, and materialises i1 values using `setcc` + `movzx`, rewriting opcodes to match operand kinds (e.g., `CMPrr` vs `CMPri`). The pass operates in place over `MFunction`, preserving ordering and preparing IR for register allocation and emission. Dependencies are `MachineIR.hpp` and `TargetX64.hpp` plus standard containers.

- **src/codegen/x86_64/CallLowering.hpp**, **src/codegen/x86_64/CallLowering.cpp**

  Lowers abstract call plans into concrete argument moves and `CALL` instructions according to the SysV x86‑64 ABI. It assigns register arguments, lays out stack arguments in aligned slots, shuffles with scratch registers when needed, and updates frame summaries with outgoing stack usage. Dependencies include `TargetX64.hpp`, `MachineIR.hpp`, and the frame‑info utilities.

- **src/codegen/x86_64/RegAllocLinear.hpp**, **src/codegen/x86_64/RegAllocLinear.cpp**

  Phase A linear‑scan register allocator that assigns physical registers and inserts spills without sophisticated global liveness. The orchestration computes live intervals, runs the allocator over `MFunction`, and returns an `AllocationResult` describing vreg→phys mappings and spill slot counts for GPR/XMM classes. Dependencies cover `ra/LiveIntervals.hpp`, `ra/Allocator.hpp`, `MachineIR.hpp`, and `TargetX64.hpp`.

- **src/codegen/x86_64/AsmEmitter.hpp**, **src/codegen/x86_64/AsmEmitter.cpp**

  Declares and implements the x86‑64 assembly emitter that walks Machine IR blocks and instructions, prints AT&T syntax, and handles prologue/epilogue emission based on frame info. Supports registers, immediates, memory operands, and labels with deterministic formatting. Dependencies include `MachineIR.hpp`, `TargetX64.hpp`, and frame summaries.

- **src/codegen/x86_64/asmfmt/Format.hpp**, **src/codegen/x86_64/asmfmt/Format.cpp**

  Small formatting helpers for assembly text (indentation, comment alignment, immediate/sign rendering) used by the emitter to keep output stable and readable.

- **src/codegen/x86_64/CodegenPipeline.hpp**, **src/codegen/x86_64/CodegenPipeline.cpp**

  Wires the per‑function backend pipeline: IL→MIR bridging, selection, legalization, register allocation, frame lowering, peepholes, and emission. Exposes a single `runPipeline` entry returning assembly text and diagnostics.

- **src/codegen/x86_64/FrameLowering.hpp**, **src/codegen/x86_64/FrameLowering.cpp**

  Computes stack frame layout (callee‑saved saves/restores, spill slots, outgoing arg area) and materialises prologue/epilogue sequences. Updates frame summaries consumed by the emitter.

- **src/codegen/x86_64/LowerDiv.cpp**

  Lowers signed divide/remainder pseudos into guarded `idiv` sequences with explicit zero‑ and overflow‑checks, using temporary registers and preserving flags as required.

- **src/codegen/x86_64/LowerILToMIR.hpp**, **src/codegen/x86_64/LowerILToMIR.cpp**

  Declares and implements the Phase A IL→Machine IR bridge with temporary IL wrappers (`ILModule`, `ILFunction`). Produces Machine IR blocks/instructions ready for selection.

- **src/codegen/x86_64/Lowering.Arith.cpp**, **src/codegen/x86_64/Lowering.Bitwise.cpp**, **src/codegen/x86_64/Lowering.CF.cpp**, **src/codegen/x86_64/Lowering.EH.cpp**, **src/codegen/x86_64/Lowering.Mem.cpp**, **src/codegen/x86_64/Lowering.EmitCommon.cpp**

  Instruction‑selection subpasses for arithmetic, bitwise ops, control‑flow, exception scaffolding, and memory forms; convert MIR pseudos into concrete x86‑64 encodings.

- **src/codegen/x86_64/Lowering.EmitCommon.hpp**, **Lowering.EmitCommon.cpp**

  Shared building blocks used by lowering subpasses to construct common instruction sequences (moves, compares, branches) and manage temporary registers.

- **src/codegen/x86_64/LoweringRules.hpp**, **LoweringRules.cpp**; **src/codegen/x86_64/LoweringRuleTable.hpp**, **LoweringRuleTable.cpp**

  Declarative mapping from IL/MIR operation kinds to concrete x86‑64 lowering patterns; the table drives selection to keep logic compact and testable.

- **src/codegen/x86_64/MachineIR.hpp**, **MachineIR.cpp**

  Defines the lightweight Machine IR model (functions, blocks, instructions, operand variants, register classes) consumed by all backend passes.

- **src/codegen/x86_64/ParallelCopyResolver.hpp**

  Resolves multiple parallel register copies into a legal sequence avoiding clobbers (e.g., via swap temporaries), used before/after allocation.

- **src/codegen/x86_64/passes/EmitPass.hpp**, **EmitPass.cpp**

  Runs the final assembly emission over a function and aggregates text into the backend result.

- **src/codegen/x86_64/passes/LegalizePass.hpp**, **LegalizePass.cpp**

  Canonicalises illegal instruction forms (e.g., immediate sizes, addressing modes) into encodings supported by the emitter and target.

- **src/codegen/x86_64/passes/LoweringPass.hpp**, **LoweringPass.cpp**

  Coordinates instruction selection subpasses in a defined order for each function.

- **src/codegen/x86_64/passes/PassManager.hpp**, **PassManager.cpp**

  Minimal backend‑local pass manager for Machine IR with simple registration and per‑function execution.

- **src/codegen/x86_64/passes/RegAllocPass.hpp**, **RegAllocPass.cpp**

  Adapts the linear‑scan allocator to the pass pipeline and threads frame spill slot counts to later passes.

- **src/codegen/x86_64/Peephole.hpp**, **Peephole.cpp**

  Local peephole optimisations over Machine IR (canonicalise `cmp` forms, insert `movzx` after `setcc`, fold LEA into mem operands) to clean up after selection.

- **src/codegen/x86_64/ra/Allocator.hpp**, **src/codegen/x86_64/ra/Allocator.cpp**

  Core linear‑scan allocator implementation operating over live intervals and target register classes; assigns phys regs, inserts spills/reloads.

- **src/codegen/x86_64/ra/Coalescer.hpp**, **src/codegen/x86_64/ra/Coalescer.cpp**

  Coalesces non‑interfering moves to reduce copies before/after allocation.

- **src/codegen/x86_64/ra/LiveIntervals.hpp**, **src/codegen/x86_64/ra/LiveIntervals.cpp**

  Computes live intervals for virtual registers by scanning Machine IR and block orderings; feeds the allocator.

- **src/codegen/x86_64/ra/Spiller.hpp**, **src/codegen/x86_64/ra/Spiller.cpp**

  Inserts spill/reload sequences for values evicted during allocation and tracks stack slot usage per class.

- **src/codegen/x86_64/TargetX64.hpp**, **src/codegen/x86_64/TargetX64.cpp**

  Target description: register classes, callee‑saved sets, calling convention assignments, and convenience constructors for operands.

- **src/codegen/x86_64/Unsupported.hpp**

  Helper for surfacing unimplemented/unsupported features in Phase A with consistent diagnostics collected into the backend result.

- **src/codegen/x86_64/Lowering.Bitwise.cpp**, **Lowering.CF.cpp**, **Lowering.EH.cpp**, **Lowering.Mem.cpp**, **Lowering.EmitCommon.cpp**

  Implement the corresponding lowering subpasses declared in headers above, materialising concrete encodings for bitwise ops, control‑flow, exception scaffolding, and memory forms, plus shared emit helpers used across subpasses.

- **src/codegen/x86_64/LoweringRules.cpp**, **src/codegen/x86_64/LoweringRuleTable.cpp**

  Define the declarative rule sets and compiled rule tables referenced by selection passes to drive MIR→x86‑64 mappings.

- **src/codegen/x86_64/MachineIR.cpp**

  Implements small helpers and ctors declared in Machine IR headers; keeps the MIR model’s out‑of‑line definitions centralised.

- **src/codegen/x86_64/passes/EmitPass.cpp**, **src/codegen/x86_64/passes/LegalizePass.cpp**, **src/codegen/x86_64/passes/LoweringPass.cpp**, **src/codegen/x86_64/passes/PassManager.cpp**, **src/codegen/x86_64/passes/RegAllocPass.cpp**

  Implement the backend pass pipeline components and minimal pass manager orchestration referred to in the corresponding headers.

- **src/codegen/x86_64/Peephole.cpp**

  Implements peephole optimisations described above (cmp canonicalisation, movzx after setcc, lea folding).
