---
status: active
audience: developers
last-updated: 2025-12-26
---

# Viper Backend — Native Code Generation

Comprehensive guide to the Viper native backends (x86-64 and AArch64), which compile Viper IL programs to executable
machine code. This document covers the backend's design philosophy, compilation pipeline, code generation strategies,
and source code organization.

> Status
>
> - AArch64: The native backend has been validated end‑to‑end on Apple Silicon by running a full "Frogger" demo.
> - x86_64: The backend is implemented with both System V (Linux/macOS) and Windows x64 ABI support. Validated on
    Windows with all codegen tests passing.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture & Design Philosophy](#architecture--design-philosophy)
3. [Compilation Pipeline](#compilation-pipeline)
4. [Machine IR (MIR)](#machine-ir-mir)
5. [IL Lowering](#il-lowering)
6. [Instruction Selection](#instruction-selection)
7. [Register Allocation](#register-allocation)
8. [Frame Lowering](#frame-lowering)
9. [Assembly Emission](#assembly-emission)
10. [Calling Convention](#calling-convention)
11. [AArch64 Backend](#aarch64-backend)
12. [Source Code Guide](#source-code-guide)
13. [Implementation Phases](#implementation-phases)

---

## Overview

### What is the Viper Backend?

The Viper backend is a **native code generator** that translates Viper IL (Intermediate Language) programs into
executable x86-64 machine code. It implements the final compilation stage in the Viper toolchain:

```
Source → Frontend → IL → Backend → Assembly → Executable
```

### Key Characteristics

| Feature           | Description                                              |
|-------------------|----------------------------------------------------------|
| **Target**        | x86-64 (AMD64) architecture                              |
| **ABI**           | System V AMD64 (Linux/macOS) and Windows x64             |
| **Output**        | AT&T syntax assembly (GAS-compatible)                    |
| **Strategy**      | SSA-based with linear scan register allocation           |
| **Pipeline**      | Multi-pass: Lowering → Selection → Allocation → Emission |
| **Current Phase** | Phase A (bring-up, educational focus)                    |

### Phase A Goals

The current implementation (Phase A) prioritizes:

1. **Correctness**: Deterministic, verifiable code generation
2. **Clarity**: Educational implementation demonstrating compiler techniques
3. **Completeness**: Full IL opcode coverage for basic programs
4. **Simplicity**: Clean architecture for future optimization phases

Future phases will add optimizations, additional ABIs, and performance tuning.

---

## Architecture & Design Philosophy

### Core Principles

The backend design emphasizes:

1. **Modularity**: Clean separation between lowering, allocation, and emission
2. **SSA Preservation**: Virtual registers map directly to IL SSA values
3. **Multi-Pass**: Each pass has a single, well-defined responsibility
4. **Testability**: Intermediate representations are inspectable and verifiable
5. **Determinism**: Identical inputs produce identical outputs

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Backend Pipeline                      │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  IL Module                                              │
│      ↓                                                  │
│  ┌──────────────┐                                       │
│  │  IL Lowering │ → Machine IR (Virtual Regs)          │
│  └──────────────┘                                       │
│      ↓                                                  │
│  ┌──────────────┐                                       │
│  │ Instruction  │ → Legalized Machine IR                │
│  │  Selection   │                                       │
│  └──────────────┘                                       │
│      ↓                                                  │
│  ┌──────────────┐                                       │
│  │   Register   │ → Physical Register Assignment        │
│  │  Allocation  │                                       │
│  └──────────────┘                                       │
│      ↓                                                  │
│  ┌──────────────┐                                       │
│  │    Frame     │ → Stack Frame Layout                  │
│  │   Lowering   │                                       │
│  └──────────────┘                                       │
│      ↓                                                  │
│  ┌──────────────┐                                       │
│  │   Assembly   │ → AT&T Syntax Output                  │
│  │   Emission   │                                       │
│  └──────────────┘                                       │
│      ↓                                                  │
│  Assembly Text                                          │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

---

## Compilation Pipeline

### Pipeline Stages

The backend implements a **sequential multi-pass pipeline**:

```cpp
// High-level pipeline flow
ILModule → LoweringPass → LegalizePass → RegAllocPass → EmitPass → Assembly
```

Each pass operates on a shared `Module` structure that threads state through the pipeline.

### PassManager

The `PassManager` orchestrates pass execution:

```cpp
class PassManager {
    void addPass(std::unique_ptr<Pass> pass);
    bool run(Module& module, Diagnostics& diags);
};
```

**Execution model:**

- Passes run sequentially in registration order
- Failure in any pass short-circuits the pipeline
- Diagnostics accumulate throughout execution
- Each pass reports success/failure via return value

### Module State

Mutable state threaded through passes:

```cpp
struct Module {
    il::core::Module il;                    // Original IL module
    std::optional<ILModule> lowered;        // Adapter module (MIR)
    bool legalised;                         // Post-selection flag
    bool registersAllocated;                // Post-allocation flag
    std::optional<CodegenResult> codegenResult; // Final assembly
};
```

### Diagnostic System

```cpp
class Diagnostics {
    void error(std::string message);        // Record fatal error
    void warning(std::string message);      // Record non-fatal warning
    bool hasErrors() const;
    void flush(std::ostream& err) const;
};
```

---

## Machine IR (MIR)

### Design Philosophy

Machine IR (MIR) is the **backend's internal representation**, positioned between high-level IL and final assembly:

- **IL**: High-level, platform-independent SSA
- **MIR**: Low-level, x86-64-specific, virtual registers
- **Assembly**: Final textual output

MIR provides:

- **SSA form**: Virtual registers assigned once
- **Target-specific opcodes**: x86-64 instruction semantics
- **Flexible operands**: Registers, immediates, memory, labels
- **Allocation-ready**: Virtual regs map to physical regs

### Virtual Registers

```cpp
struct VReg {
    uint16_t id;             // Unique within function
    RegClass cls;            // GPR or XMM
};
```

Virtual registers are:

- Numbered sequentially starting from 1
- Classified as GPR (general-purpose) or XMM (floating-point)
- Mapped to IL SSA values during lowering
- Allocated to physical registers later

### Operand Types

MIR supports five operand kinds:

```cpp
// Register operand
struct OpReg {
    bool isPhys;         // Virtual or physical?
    RegClass cls;        // GPR or XMM
    uint16_t idOrPhys;   // VReg ID or PhysReg enum
};

// Immediate operand
struct OpImm {
    int64_t val;
};

// Memory operand
struct OpMem {
    OpReg base;          // Base register
    OpReg index;         // Optional index register
    uint8_t scale;       // 1, 2, 4, or 8
    int32_t disp;        // Displacement
    bool hasIndex;
};

// Label operand
struct OpLabel {
    std::string name;
};

// RIP-relative label
struct OpRipLabel {
    std::string name;
};

using Operand = std::variant<OpReg, OpImm, OpMem, OpLabel, OpRipLabel>;
```

### Machine Instructions

```cpp
struct MInstr {
    MOpcode opcode;                  // Instruction opcode
    std::vector<Operand> operands;   // Operands in emission order
};
```

**Supported opcodes (Phase A):**

- **Moves**: `MOVrr`, `MOVri`, `LEA`, `CMOVNErr`
- **Arithmetic**: `ADDrr/ri`, `SUBrr`, `IMULrr`, `DIVS64rr`, `REMS64rr`
- **Bitwise**: `ANDrr/ri`, `ORrr/ri`, `XORrr/ri`, `SHLri/rc`, `SHRri/rc`, `SARri/rc`
- **Comparison**: `CMPrr/ri`, `TESTrr`, `SETcc`, `UCOMIS`
- **Control**: `JMP`, `JCC`, `CALL`, `RET`, `LABEL`
- **Floating-point**: `FADD`, `FSUB`, `FMUL`, `FDIV`, `CVTSI2SD`, `CVTTSD2SI`
- **Special**: `PX_COPY` (parallel copy pseudo), `UD2` (trap)

### Basic Blocks

```cpp
struct MBasicBlock {
    std::string label;              // Block label
    std::vector<MInstr> instructions;
};
```

Blocks are:

- Labeled for control flow
- Contain ordered instruction sequences
- Terminated implicitly (no explicit terminator in MIR)

### Functions

```cpp
struct MFunction {
    std::string name;               // Function symbol
    std::vector<MBasicBlock> blocks;
    FunctionMetadata metadata;      // Vararg flag, etc.
    size_t localLabelCounter;       // Unique label generation
};
```

---

## IL Lowering

### LowerILToMIR Class

The `LowerILToMIR` adapter converts IL to Machine IR:

```cpp
class LowerILToMIR {
    MFunction lower(const ILFunction& func);
    const std::vector<CallLoweringPlan>& callPlans() const;
};
```

**Key responsibilities:**

1. Map IL SSA values to MIR virtual registers
2. Translate IL opcodes to MIR instruction sequences
3. Materialize block parameters as `PX_COPY` pseudo-instructions
4. Record call lowering plans for later processing

### Lowering Rules

IL instructions are lowered via **pattern-matching rules**:

```cpp
struct LoweringRule {
    bool (*match)(const ILInstr&);              // Match predicate
    void (*emit)(const ILInstr&, MIRBuilder&);  // Code emitter
    const char* name;                           // Debug name
};
```

**Rule selection:**

```cpp
const LoweringRule* rule = viper_select_rule(ilInstr);
if (rule) {
    rule->emit(ilInstr, builder);
}
```

**Categories:**

- **Arithmetic** (`Lowering.Arith.cpp`): `add`, `sub`, `mul`, `div`, `rem`
- **Bitwise** (`Lowering.Bitwise.cpp`): `and`, `or`, `xor`, `shl`, `shr`
- **Control Flow** (`Lowering.CF.cpp`): `br`, `cbr`, `ret`, `switch`
- **Memory** (`Lowering.Mem.cpp`): `load`, `store`, `alloca`, `gep`
- **Exception Handling** (`Lowering.EH.cpp`): `eh.push`, `eh.pop`, `trap`

### MIRBuilder

Helper class for emitting MIR during lowering:

```cpp
class MIRBuilder {
    VReg ensureVReg(int id, ILValue::Kind kind);  // IL value → VReg
    VReg makeTempVReg(RegClass cls);              // Allocate temp
    Operand makeOperandForValue(const ILValue&);  // Create operand
    void append(MInstr instr);                    // Emit instruction
    void recordCallPlan(CallLoweringPlan plan);   // Record call
};
```

**Example lowering (IL `add`):**

```cpp
// IL: %3 = add %1, %2
void emitAdd(const ILInstr& instr, MIRBuilder& b) {
    VReg lhs = b.ensureVReg(instr.ops[0].id, ILValue::I64);
    VReg rhs = b.ensureVReg(instr.ops[1].id, ILValue::I64);
    VReg dest = b.ensureVReg(instr.resultId, ILValue::I64);

    // MOV dest, lhs
    b.append(MInstr::make(MOpcode::MOVrr, {
        makeVRegOperand(GPR, dest.id),
        makeVRegOperand(GPR, lhs.id)
    }));

    // ADD dest, rhs
    b.append(MInstr::make(MOpcode::ADDrr, {
        makeVRegOperand(GPR, dest.id),
        makeVRegOperand(GPR, rhs.id)
    }));
}
```

### Block Parameter Lowering

IL block parameters are lowered to **parallel copy** (`PX_COPY`) pseudo-instructions:

```cpp
// IL edge: br label %target(%arg1, %arg2)
// MIR:
PX_COPY %p1, %arg1
PX_COPY %p2, %arg2
JMP target
```

The parallel copy ensures SSA semantics are preserved across control flow edges.

---

## Instruction Selection

### ISel Class

The instruction selector legalizes MIR for x86-64:

```cpp
class ISel {
    void lowerArithmetic(MFunction& func) const;
    void lowerCompareAndBranch(MFunction& func) const;
    void lowerSelect(MFunction& func) const;
};
```

### Legalization Tasks

**1. Immediate Operand Constraints**

x86-64 restricts immediate sizes:

- Memory operands: 32-bit immediates only
- Register operands: 64-bit immediates allowed for `MOVri`

**2. Compare + Branch Fusion**

Fuse compare/test instructions with conditional branches:

```
CMPrr %a, %b
SETcc %tmp
TESTrr %tmp, %tmp
JCC label
```

→

```
CMPrr %a, %b
JCC label
```

**3. Boolean Materialization**

IL `i1` values are materialized using `SETcc` + `MOVZXrr32`:

```
CMPrr %a, %b
SETcc %result8      # Set byte based on condition
MOVZXrr32 %result, %result8  # Zero-extend to 64-bit
```

**4. Conditional Moves**

Select-like patterns are lowered to `CMOVcc`:

```
CMPrr %cond, 0
CMOVNErr %dest, %true_val  # Move if not equal (cond != 0)
```

**5. LEA Folding**

Single-use `LEA` instructions are folded into memory operands:

```
LEA %tmp, [%base + disp]
MOV %dest, [%tmp]
```

→

```
MOV %dest, [%base + disp]
```

---

## Register Allocation

### Linear Scan Algorithm

The backend uses **linear scan register allocation**:

```cpp
class LinearScanAllocator {
    AllocationResult run();
};
```

**Algorithm overview:**

1. **Compute live intervals** for each virtual register
2. **Walk instructions** in block order
3. **Expire old intervals** and release physical registers
4. **Allocate registers** or **spill to stack**
5. **Insert spill code** (loads/stores)
6. **Resolve parallel copies** by coalescing or emitting moves

### Register Classes

Two independent register classes:

```cpp
enum class RegClass {
    GPR,  // General-purpose: RAX, RBX, RCX, RDX, RSI, RDI, R8-R15
    XMM   // Floating-point: XMM0-XMM15
};
```

**Allocatable registers:**

- **GPR**: `RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`-`R11` (10 registers)
- **XMM**: `XMM0`-`XMM7` (8 registers)

**Reserved registers:**

- `RSP`: Stack pointer
- `RBP`: Frame pointer
- `RBX`, `R12`-`R15`: Callee-saved (allocated but require save/restore)

### Live Interval Analysis

```cpp
class LiveIntervals {
    struct Interval {
        size_t start;  // First definition
        size_t end;    // Last use
    };

    std::unordered_map<uint16_t, Interval> intervals_;
};
```

Intervals track the lifetime of each virtual register within a function.

### Spilling

When no free registers are available:

1. **Select victim**: Virtual register with furthest end point
2. **Allocate stack slot**: 8-byte aligned slot in spill area
3. **Insert spill store**: Before victim's definition
4. **Insert reload**: Before each use of victim

**Spill code example:**

```
# Before allocation
ADDrr %v1, %v2

# After spilling %v1
MOV [rbp - 8], %rax    # Spill
ADDrr %rax, %rdx       # Use RAX instead of %v1
MOV %rax, [rbp - 8]    # Reload (if needed later)
```

### Coalescing

The `Coalescer` attempts to eliminate `PX_COPY` instructions:

```cpp
class Coalescer {
    bool tryCoalesce(MInstr& copy,
                     std::unordered_map<uint16_t, VirtualAllocation>& states);
};
```

**Coalescing conditions:**

- Source and destination are both virtual registers
- Destination has not been allocated yet
- No interference in live ranges

**When successful:**

```
PX_COPY %v2, %v1  → (eliminated, %v2 uses same phys reg as %v1)
```

### Allocation Result

```cpp
struct AllocationResult {
    std::unordered_map<uint16_t, PhysReg> vregToPhys;  // Assignments
    std::vector<SpillSlot> spillSlots;                 // Spill slots
    std::vector<PhysReg> usedCalleeSaved;              // Callee-saved used
};
```

---

## Frame Lowering

### Stack Frame Layout

System V AMD64 stack frame (grows downward):

```
Higher addresses
┌────────────────────┐
│  Return address    │  [rbp + 8]
├────────────────────┤
│  Saved RBP         │  [rbp]      ← Frame pointer
├────────────────────┤
│  Callee-saved regs │  [rbp - 8 * N]
├────────────────────┤
│  GPR spill area    │  [rbp - ...]
├────────────────────┤
│  XMM spill area    │  [rbp - ...]
├────────────────────┤
│  Outgoing args     │  [rbp - ...]
└────────────────────┘  ← Stack pointer
Lower addresses
```

### FrameInfo

Summarizes frame requirements:

```cpp
struct FrameInfo {
    int spillAreaGPR;                // GPR spill bytes
    int spillAreaXMM;                // XMM spill bytes
    int outgoingArgArea;             // Call argument space
    int frameSize;                   // Total frame size
    std::vector<PhysReg> usedCalleeSaved;  // Regs to save
};
```

### Prologue Emission

```asm
# Function entry
push   %rbp
mov    %rsp, %rbp
sub    $frameSize, %rsp   # Allocate stack space

# Save callee-saved registers
push   %rbx
push   %r12
# ... (for each used callee-saved reg)
```

### Epilogue Emission

```asm
# Restore callee-saved registers
pop    %r12
pop    %rbx
# ...

# Function exit
leave                      # mov %rbp, %rsp; pop %rbp
ret
```

### Spill Slot Assignment

```cpp
void assignSpillSlots(MFunction& func, FrameInfo& frame);
```

Replaces abstract spill slots with concrete stack offsets:

```
# Before
MOV [SPILL_GPR(0)], %rax

# After
MOV [%rbp - 16], %rax
```

---

## Assembly Emission

### AsmEmitter Class

Translates MIR to AT&T syntax assembly:

```cpp
class AsmEmitter {
    void emitFunction(std::ostream& os, const MFunction& func) const;
    void emitRoData(std::ostream& os) const;
};
```

### Encoding Table

Instructions are matched against an **encoding specification table**:

```cpp
struct EncodingRow {
    MOpcode opcode;            // MIR opcode
    std::string_view mnemonic; // Assembly mnemonic
    EncodingForm form;         // Operand pattern
    OperandOrder order;        // Emission order
    OperandPattern pattern;    // Expected operands
    EncodingFlag flags;        // REX.W, ModRM, etc.
};
```

**Example encoding:**

```cpp
{MOpcode::ADDrr, "addq", EncodingForm::RegReg,
 OperandOrder::R_R, {Reg, Reg}, EncodingFlag::REXW}
```

### Operand Formatting

AT&T syntax rules:

- **Registers**: `%rax`, `%xmm0`
- **Immediates**: `$42`
- **Memory**: `displacement(%base, %index, scale)`
- **Labels**: `symbol` or `symbol(%rip)` for RIP-relative

**Examples:**

```asm
movq   %rax, %rbx           # Register to register
movq   $42, %rax            # Immediate to register
movq   (%rdi), %rax         # Memory to register
leaq   8(%rsp), %rax        # Address calculation
movsd  .LC0(%rip), %xmm0    # RIP-relative load
```

### RoData Pool

String and floating-point literals are pooled:

```cpp
class RoDataPool {
    int addStringLiteral(std::string bytes);
    int addF64Literal(double value);
    std::string stringLabel(int index) const;  // ".LC0"
    std::string f64Label(int index) const;      // ".LF0"
    void emit(std::ostream& os) const;
};
```

**Emitted rodata section:**

```asm
    .section .rodata
.LC0:
    .string "Hello, world!"
.LF0:
    .quad 0x400921fb54442d18  # 3.14159265358979323846
```

---

## Calling Convention

The backend supports both **System V AMD64** (Linux/macOS) and **Windows x64** calling conventions.
The appropriate ABI is selected automatically based on the host platform.

### System V AMD64 ABI

Used on Linux, macOS, and other Unix-like systems:

**Integer/Pointer Arguments:**

1. `RDI`
2. `RSI`
3. `RDX`
4. `RCX`
5. `R8`
6. `R9`
7. Stack (right-to-left)

**Floating-Point Arguments:**

1. `XMM0`
2. `XMM1`
3. `XMM2`
4. `XMM3`
5. `XMM4`
6. `XMM5`
7. `XMM6`
8. `XMM7`
9. Stack

**Return Values:**

- Integer/pointer: `RAX`
- Floating-point: `XMM0`

**Caller/Callee-Saved:**

- **Caller-saved**: `RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`-`R11`, `XMM0`-`XMM15`
- **Callee-saved**: `RBX`, `RBP`, `R12`-`R15`

### Windows x64 ABI

Used on Windows:

**Integer/Pointer Arguments:**

1. `RCX`
2. `RDX`
3. `R8`
4. `R9`
5. Stack (right-to-left)

**Floating-Point Arguments:**

1. `XMM0`
2. `XMM1`
3. `XMM2`
4. `XMM3`
5. Stack

**Return Values:**

- Integer/pointer: `RAX`
- Floating-point: `XMM0`

**Caller/Callee-Saved:**

- **Caller-saved**: `RAX`, `RCX`, `RDX`, `R8`-`R11`, `XMM0`-`XMM5`
- **Callee-saved**: `RBX`, `RBP`, `RDI`, `RSI`, `R12`-`R15`, `XMM6`-`XMM15`

**Shadow Space:**

Windows x64 requires 32 bytes of shadow space before each call for register argument spilling.

### Call Lowering

```cpp
struct CallLoweringPlan {
    std::string calleeLabel;   // Function to call
    std::vector<CallArg> args; // Arguments
    bool returnsF64;           // Return type
    bool isVarArg;            // Vararg flag
};

void lowerCall(MBasicBlock& block, size_t insertIdx,
               const CallLoweringPlan& plan, FrameInfo& frame);
```

**Call sequence:**

1. **Compute argument layout** (register vs. stack)
2. **Emit argument moves** to physical registers
3. **Update stack pointer** if stack arguments present
4. **Emit CALL** instruction
5. **Capture return value** from `RAX` or `XMM0`
6. **Restore stack pointer**

**Example:**

```asm
# Call foo(a, b, c) where a,b,c are in %v1,%v2,%v3
movq   %v1, %rdi       # First arg
movq   %v2, %rsi       # Second arg
movq   %v3, %rdx       # Third arg
call   foo
movq   %rax, %v_result # Capture return value
```

---

## AArch64 Backend

### Overview

The AArch64 backend targets 64-bit ARM processors (Apple Silicon, ARM servers). It shares design principles with the
x86-64 backend but is tailored for the ARM instruction set and AAPCS64 calling convention.

### Key Characteristics

| Feature      | Description                                    |
|--------------|------------------------------------------------|
| **Target**   | AArch64 (ARM64) architecture                   |
| **ABI**      | AAPCS64 (ARM Procedure Call Standard)          |
| **Output**   | ARM assembly (GAS-compatible)                  |
| **Strategy** | SSA-based with linear scan register allocation |
| **Status**   | Functional for core operations                 |

### Supported Features

- Integer arithmetic (add, sub, mul)
- Floating-point arithmetic (fadd, fsub, fmul, fdiv)
- Bitwise operations (and, or, xor, shifts)
- Comparisons and conditional branches
- Function calls (direct)
- Switch statements
- Local variables (FP-relative addressing)
- Array and object operations

### AArch64 Register Usage

**Integer Arguments:** X0-X7
**Float Arguments:** V0-V7 (D registers)
**Return Values:** X0 (integer), V0 (float)
**Callee-saved:** X19-X28
**Caller-saved:** X9-X15
**Frame Pointer:** X29 (FP)
**Link Register:** X30 (LR)
**Stack Pointer:** SP

### Source Files

```
src/codegen/aarch64/
├── AsmEmitter.hpp/cpp      # ARM assembly emission
├── FrameBuilder.hpp/cpp    # Stack frame construction
├── FramePlan.hpp           # Frame layout planning
├── LowerILToMIR.hpp/cpp    # IL → MIR lowering
├── MachineIR.hpp/cpp       # Machine IR structures
├── OpcodeMappings.hpp      # IL opcode to MIR mapping
├── RegAllocLinear.hpp/cpp  # Linear scan allocator
├── RodataPool.hpp/cpp      # Read-only data management
├── TargetAArch64.hpp/cpp   # Target description
└── generated/              # Generated dispatch tables
```

### Usage

```bash
# Generate ARM assembly from IL
ilc codegen arm64 program.il -S program.s

# Assemble and link (macOS, simple/monolithic runtime)
as program.s -o program.o
clang++ program.o build/src/runtime/libviper_runtime.a -o program

# Recommended: let ilc link the native executable (auto-selects required runtime components)
ilc codegen arm64 program.il -run-native
```

---

## Source Code Guide

### Directory Structure

```
src/codegen/
├── common/                        # Shared utilities
│   ├── ArgNormalize.hpp           # Argument normalization
│   └── LabelUtil.hpp              # Label generation helpers
│
├── aarch64/                       # ARM64 backend (see above)
│
└── x86_64/                        # x86-64 backend
    ├── Backend.hpp                # High-level facade
    ├── Backend.cpp                # Pipeline orchestration
    ├── CodegenPipeline.hpp/cpp    # End-to-end pipeline
    ├── MachineIR.hpp/cpp          # MIR data structures
    ├── TargetX64.hpp/cpp          # x86-64 target description
    ├── LowerILToMIR.hpp/cpp       # IL → MIR lowering
    ├── LoweringRules.hpp/cpp      # Lowering rule registry
    ├── LoweringRuleTable.hpp/cpp  # Rule table generator
    ├── Lowering.Arith.cpp         # Arithmetic ops lowering
    ├── Lowering.Bitwise.cpp       # Bitwise ops lowering
    ├── Lowering.CF.cpp            # Control flow lowering
    ├── Lowering.Mem.cpp           # Memory ops lowering
    ├── Lowering.EH.cpp            # Exception handling lowering
    ├── Lowering.EmitCommon.*      # Shared lowering helpers
    ├── ISel.hpp/cpp               # Instruction selection
    ├── Peephole.hpp/cpp           # Peephole optimization
    ├── LowerDiv.cpp               # Division/modulo lowering
    ├── Unsupported.hpp            # Unsupported opcode tracking
    ├── CallLowering.hpp/cpp       # Calling convention
    ├── FrameLowering.hpp/cpp      # Stack frame layout
    ├── RegAllocLinear.hpp/cpp     # Register allocation
    ├── ParallelCopyResolver.hpp   # Parallel copy resolution
    ├── OperandUtils.hpp           # Operand utilities
    ├── ra/                        # Register allocation internals
    │   ├── Allocator.hpp/cpp      # Linear scan allocator
    │   ├── LiveIntervals.hpp/cpp  # Live interval analysis
    │   ├── Spiller.hpp/cpp        # Spill code insertion
    │   └── Coalescer.hpp/cpp      # Copy coalescing
    ├── AsmEmitter.hpp/cpp         # Assembly emission
    ├── asmfmt/Format.hpp/cpp      # Formatting helpers
    └── passes/                    # Pipeline passes
        ├── PassManager.hpp/cpp    # Pass orchestration
        ├── LoweringPass.hpp/cpp   # IL lowering pass
        ├── LegalizePass.hpp/cpp   # Instruction selection pass
        ├── RegAllocPass.hpp/cpp   # Register allocation pass
        └── EmitPass.hpp/cpp       # Assembly emission pass
```

### Key Files by Functionality

**Core Infrastructure:**

- `Backend.hpp`, `Backend.cpp` — High-level API
- `CodegenPipeline.hpp` — End-to-end compilation
- `MachineIR.hpp` — MIR data structures
- `TargetX64.hpp` — x86-64 register/ABI definitions

**Lowering:**

- `LowerILToMIR.hpp` — IL to MIR adapter
- `LoweringRules.hpp` — Rule-based lowering
- `Lowering.*.cpp` — Lowering implementations by category

**Legalization:**

- `ISel.hpp` — Instruction selection and legalization
- `Peephole.hpp` — Simple peephole optimizations
- `LowerDiv.cpp` — Division/remainder lowering

**Register Allocation:**

- `ra/Allocator.hpp` — Linear scan algorithm
- `ra/LiveIntervals.hpp` — Liveness analysis
- `ra/Spiller.hpp` — Spill code insertion
- `ra/Coalescer.hpp` — Copy coalescing

**ABI & Frame:**

- `CallLowering.hpp` — System V call lowering
- `FrameLowering.hpp` — Stack frame construction

**Emission:**

- `AsmEmitter.hpp` — AT&T assembly output
- `asmfmt/Format.hpp` — Operand formatting

**Passes:**

- `passes/PassManager.hpp` — Pass orchestration
- `passes/*Pass.hpp` — Individual pipeline passes

---

## Implementation Phases

### Phase A: Bring-Up (Current)

**Goals:**

- ✅ Complete IL opcode coverage
- ✅ Functional register allocation
- ✅ Correct code generation
- ✅ System V ABI compliance
- ✅ Educational clarity

**Characteristics:**

- Simple linear scan allocation
- No optimizations beyond basic peephole
- AT&T syntax only
- Single-threaded compilation
- Emphasis on correctness over performance

**Supported Features:**

- Integer arithmetic (add, sub, mul, div, rem)
- Floating-point arithmetic (add, sub, mul, div)
- Bitwise operations (and, or, xor, shifts)
- Comparisons and branches
- Function calls (direct)
- Local variables (via stack)
- Basic exception handling

**Limitations:**

- No indirect calls
- No SIMD instructions
- No advanced optimizations (loop opts, inlining, etc.)
- No debug info generation
- No position-independent code (PIC)

### Future Phases

**Phase B: Optimization**

- Graph coloring register allocator
- SSA-based optimizations
- Instruction scheduling
- Loop optimizations

**Phase C: Advanced Features**

- SIMD instruction generation
- Position-independent code (PIC)
- Debug info (DWARF)
- Link-time optimization (LTO)

**Phase D: Production**

- Profile-guided optimization (PGO)
- Code size optimization
- Advanced peephole passes

---

## Best Practices

### For Backend Developers

1. **Lowering Rules**: Keep rules simple and focused on one opcode family
2. **MIR Design**: Add new operand types conservatively
3. **Register Allocation**: Maintain deterministic allocation order
4. **Testing**: Add golden assembly tests for each new feature
5. **Documentation**: Document ABI deviations and assumptions

### For IL Generators

1. **SSA Form**: Ensure proper SSA before backend entry
2. **Type Safety**: Match IL types to backend expectations
3. **ABI Awareness**: Understand calling convention requirements
4. **Testing**: Verify assembly output for correctness

### For Embedders

1. **Configuration**: Use `CodegenOptions` for future compatibility
2. **Error Handling**: Check `CodegenResult.errors` for diagnostics
3. **Assembly Integration**: Link generated assembly with system linker
4. **Toolchain**: Ensure GAS-compatible assembler is available

---

## Further Reading

**Viper Documentation:**

- **[IL Guide](il-guide.md)** — IL specification and semantics
- **[IL Reference](il-reference.md)** — Complete opcode catalog
- **[VM Architecture](vm.md)** — VM execution model

**Developer Documentation** (in `/devdocs`):**

- `codegen/x86_64.md` — Additional x86-64 backend notes
- `architecture.md` — Overall system architecture
- `abi/` — ABI specification details

**External References:**

- **System V AMD64 ABI
  **: [https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf](https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf)
- **Intel x86-64 Manual
  **: [https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- **SSA Book**: "SSA-based Compiler Design" by Rastello & Bouchez Tichadou

**Source Code:**

- `src/codegen/x86_64/` — Backend implementation
- `tests/codegen/` — Backend tests
