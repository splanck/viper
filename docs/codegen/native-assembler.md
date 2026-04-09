---
status: active
audience: contributors
last-verified: 2026-04-09
---

# Native Assembler — Binary Encoding & Object File Generation

The native assembler replaces the system assembler (`cc -c`) with a built-in pipeline that encodes MIR directly to
machine code bytes and writes platform-native object files (.o / .obj). This eliminates external tool dependencies for
the compilation phase.

> **Pipeline comparison:**
>
> - **System path:** MIR → AsmEmitter → `.s` file → `cc -c` → `.o` file
> - **Native path:** MIR → BinaryEncoder → CodeSection → ObjectFileWriter → `.o` file

---

## Table of Contents

1. [Overview](#overview)
2. [Binary Encoders](#binary-encoders)
3. [x86_64 Encoding](#x86_64-encoding)
4. [AArch64 Encoding](#aarch64-encoding)
5. [Object File Infrastructure](#object-file-infrastructure)
6. [Object File Writers](#object-file-writers)
7. [Pipeline Integration](#pipeline-integration)
8. [CLI Flags](#cli-flags)
9. [Source File Map](#source-file-map)

---

## Overview

### Motivation

The text assembly path requires a system C compiler to assemble `.s` files. This has several downsides:

1. **External dependency** — requires `cc` (Clang/GCC/MSVC) to be installed
2. **Performance overhead** — fork/exec + file I/O for the assembler subprocess
3. **Encoding indirection** — MIR is lowered to text, which the assembler parses back into bytes

The native assembler eliminates all three by directly encoding MIR instructions into machine code bytes, then
serializing those bytes into the correct object file format (ELF, Mach-O, or COFF).

### Architecture

The native assembler has three layers:

```text
┌──────────────────────────────────────────────────────────────┐
│  Binary Encoder  (X64BinaryEncoder / A64BinaryEncoder)       │
│  Translates MIR instructions → machine code bytes            │
│  Records relocations for external symbols and cross-section  │
│  references into CodeSection                                 │
├──────────────────────────────────────────────────────────────┤
│  CodeSection  +  SymbolTable  +  Relocation                 │
│  Growable byte buffer with relocation and symbol tracking    │
│  Separate instances for .text and .rodata                    │
├──────────────────────────────────────────────────────────────┤
│  Object File Writer  (ElfWriter / MachOWriter / CoffWriter)  │
│  Serializes CodeSections into platform-native .o format      │
│  Handles section headers, symbol tables, relocation entries  │
└──────────────────────────────────────────────────────────────┘
```

### Supported Targets

| Architecture | Format | Platform | Status |
|--------------|--------|----------|--------|
| x86_64 | ELF | Linux | Complete |
| x86_64 | Mach-O | macOS | Complete |
| x86_64 | COFF | Windows | Complete |
| AArch64 | ELF | Linux | Complete |
| AArch64 | Mach-O | macOS | Complete |
| AArch64 | COFF | Windows | Object emission only |

All 6 object-file combinations ({x86_64, AArch64} × {ELF, Mach-O, COFF}) are implemented.
End-to-end native executable linking remains incomplete on Windows ARM64 because the PE startup,
import, and unwind path is still x86_64-specific.

---

## Binary Encoders

Both encoders share the same interface pattern:

1. Accept an MIR function
2. Emit a prologue (stack frame setup)
3. Walk each basic block, encoding each MIR instruction into bytes
4. Emit an epilogue (stack frame teardown)
5. Patch internal branches (relative offsets now known)
6. Leave external calls and cross-section references as relocations

The output is a pair of `CodeSection` objects: one for `.text` (machine code) and one for `.rodata` (string literals,
float constants).

---

## x86_64 Encoding

### Instruction Format

x86_64 has variable-length encoding (1–15 bytes per instruction):

```text
[Legacy Prefixes 0-4B] [REX 0-1B] [Opcode 1-3B] [ModR/M 0-1B] [SIB 0-1B] [Disp 0/1/4B] [Imm 0/1/2/4/8B]
```

Key encoding components:

- **REX prefix** (`0100 WRXB`): W=64-bit operand, R/X/B extend register fields to 4 bits
- **ModR/M** (`mod[2] reg[3] r/m[3]`): addressing mode + register selection
- **SIB** (`scale[2] index[3] base[3]`): required for scaled-index addressing and RSP/R12 as base

### Register Hardware Encoding

The `PhysReg` enum order differs from hardware encoding. The encoder maps via lookup tables in `X64Encoding.hpp`:

| Register | Hardware 3-bit | REX.B needed |
|----------|---------------|--------------|
| RAX | 0 | No |
| RCX | 1 | No |
| RDX | 2 | No |
| RBX | 3 | No |
| RSP | 4 | No |
| RBP | 5 | No |
| RSI | 6 | No |
| RDI | 7 | No |
| R8–R15 | 0–7 | Yes |
| XMM0–7 | 0–7 | No |
| XMM8–15 | 0–7 | Yes |

### Encoding Categories

| Category | Example Opcodes | Encoding Pattern |
|----------|----------------|------------------|
| **Nullary** | RET, CQO, UD2 | Fixed 1–2 bytes |
| **Reg-Reg ALU** | MOVrr, ADDrr, SUBrr, CMPrr | REX.W + opcode + ModR/M(11,src,dst) |
| **Reg-Imm ALU** | ADDri, ANDri, CMPri | REX.W + opcode + ModR/M(11,/ext,reg) + imm32 |
| **Shifts** | SHLri, SHRri, SARri | REX.W + C1/D3 + ModR/M + imm8 |
| **Memory** | MOVrm, MOVmr, LEA | REX + opcode + ModR/M + [SIB] + [disp] |
| **Branches** | JMP, JCC, CALL | E9/0F8x/E8 + rel32 |
| **SSE scalar** | FADD, FSUB, FMUL | F2/66 prefix + 0F + opcode + ModR/M |
| **64-bit move** | MOVri | REX.W + B8+rd + imm64 |

### Special Cases

- **RSP/R12 as base**: Always requires a SIB byte (hardware rule)
- **RBP/R13 with mod=00**: Encodes as RIP-relative, not base+0 — encoder uses mod=01 with disp8=0 instead
- **RIP-relative addressing**: `OpRipLabel` generates ModR/M with mod=00, r/m=101, followed by a rel32 displacement
  that becomes a `PCRel32` relocation
- **PC-relative addend**: x86_64 branch and RIP-relative relocations always carry addend = −4 because the CPU
  computes displacement relative to the *end* of the instruction, but the relocation offset points to the *start*
  of the 4-byte displacement field

### Opcode Coverage

The encoder handles all 58 `MOpcode` values defined in the x86_64 MachineIR, including:

- All integer arithmetic (ADD, SUB, IMUL, DIV, CQO)
- All bitwise and shift operations
- Conditional moves (CMOVcc) and set-byte (SETcc)
- All memory addressing modes (FP-relative, base+index×scale, RIP-relative)
- SSE2 scalar double operations (FADD, FSUB, FMUL, FDIV, UCOMIS, CVTSI2SD, CVTTSD2SI)
- MOV between GPR and XMM registers

---

## AArch64 Encoding

### Instruction Format

Every AArch64 instruction is exactly 4 bytes (32 bits). Encoding is pure bit-field construction:

```c
uint32_t word = template | (Rd << 0) | (Rn << 5) | (Rm << 16) | ...;
```

This is dramatically simpler than x86_64 — no variable-length prefixes, no ModR/M/SIB.

### Register Encoding

Trivial 5-bit inline encoding: `X0=0, X1=1, ..., X30=30, SP/XZR=31, V0=0, ..., V31=31`.

### Encoding Classes

| Category | Bit Pattern | Examples |
|----------|-------------|---------|
| **3-reg arithmetic** | `sf opc 01011 sh 0 Rm imm6 Rn Rd` | add, sub, and, orr, eor |
| **Reg+imm12** | `sf opc 10001 sh imm12 Rn Rd` | add/sub with immediate |
| **Move wide** | `sf opc 100101 hw imm16 Rd` | movz, movk |
| **Multiply** | `sf 00 11011 000 Rm o0 Ra Rn Rd` | mul, madd, msub, sdiv, udiv |
| **Load/store** | `size opc 111 00 type imm Rn Rt` | ldr, str (unsigned offset) |
| **Load/store pair** | `opc 10100 type imm7 Rt2 Rn Rt` | ldp, stp |
| **Branch uncond** | `op 00101 imm26` | b, bl |
| **Branch cond** | `01010100 imm19 0 cond` | b.eq, b.lt |
| **FP scalar** | `00011110 type opc Rm Rn Rd` | fadd, fsub, fmul, fdiv |
| **System** | Fixed patterns | ret, blr |

### Prologue/Epilogue Synthesis

The AArch64 encoder synthesizes function prologues and epilogues at emit time:

- **Prologue**: `stp x29, x30, [sp, #-frameSize]!` + `mov x29, sp` + save callee-saved regs
- **Epilogue**: Restore callee-saved regs + `ldp x29, x30, [sp], #frameSize` + `ret`
- **Large frames**: SP adjustment chunked in multiples of 4080 (preserving 16-byte alignment between steps)

### Branch Resolution

- Internal branches (within the same function) are resolved by patching the instruction word after all blocks
  are emitted
- External branches (function calls) generate `A64Call26` relocations
- Cross-section references (rodata access) generate `A64AdrpPage21` + `A64AddPageOff12` relocation pairs

---

## Object File Infrastructure

### CodeSection

`CodeSection` (`codegen/common/objfile/CodeSection.hpp`) is the central data structure:

- **Byte buffer**: Growable `vector<uint8_t>` with `emit8/16/32/64()` and `emitBytes()` methods
- **Relocation tracking**: Records `Relocation` entries with offset, kind, symbol index, and addend
- **Symbol management**: Maintains a `SymbolTable` with defined and external symbols
- **Patching**: `patch32LE()` for backpatching resolved branch offsets

### SymbolTable

`SymbolTable` (`codegen/common/objfile/SymbolTable.hpp`):

- Index 0 is reserved for the null entry (ELF requirement)
- Symbols have name, binding (Local/Global/External), section (Text/Rodata/Undefined), offset, and size
- Name-to-index lookup via hash map for O(1) `findOrAdd()`

### Relocation

`Relocation` (`codegen/common/objfile/Relocation.hpp`) uses architecture-prefixed `RelocKind`:

| RelocKind | x86_64 | AArch64 |
|-----------|--------|---------|
| `PCRel32` | RIP-relative data ref | — |
| `Branch32` | CALL/JMP rel32 | — |
| `Abs64` | 64-bit absolute | — |
| `A64Call26` | — | BL 26-bit |
| `A64Jump26` | — | B 26-bit |
| `A64AdrpPage21` | — | ADRP page |
| `A64AddPageOff12` | — | ADD offset |
| `A64LdSt64Off12` | — | LDR/STR scaled |
| `A64CondBr19` | — | B.cond 19-bit |

Each writer maps these to format-specific codes (ELF `R_X86_64_*`, Mach-O `X86_64_RELOC_*`, COFF `IMAGE_REL_*`).

---

## Object File Writers

### ELF Writer

Produces valid ELF 64-bit relocatable object files for Linux.

**Layout:** ELF header (64B) → sections: null, `.text`, `.rodata`, `.note.GNU-stack`, `.rela.text`, `.symtab`,
`.strtab`, `.shstrtab` → section header table.

**Key details:**
- Machine: `EM_X86_64` (62) or `EM_AARCH64` (183)
- Relocations use `.rela` format (explicit addend)
- All local symbols precede global symbols (ELF spec requirement)
- `.note.GNU-stack` emitted for non-executable stack marker

### Mach-O Writer

Produces valid Mach-O 64-bit object files for macOS.

**Layout:** mach_header_64 → LC_SEGMENT_64 (containing sections `__TEXT,__text` and `__TEXT,__const`) →
LC_BUILD_VERSION → LC_SYMTAB → LC_DYSYMTAB → section data → relocations → symbol table → string table.

**Key details:**
- Magic: `0xFEEDFACF` (64-bit little-endian)
- Symbol names prefixed with `_` (Mach-O convention)
- Relocations stored per-section (not in separate `.rela` sections like ELF)
- Compact format: `r_address(32) | r_symbolnum(24) | r_pcrel(1) | r_length(2) | r_extern(1) | r_type(4)`
- LC_BUILD_VERSION mandatory (platform=macOS, minos=14.0)
- `MH_SUBSECTIONS_VIA_SYMBOLS` flag set

### COFF Writer

Produces valid COFF object files for Windows.

**Layout:** COFF header (20B) → section headers (40B each) → `.text` data → `.text` relocations →
`.rdata` data → symbol table → string table.

**Key details:**
- Machine: `IMAGE_FILE_MACHINE_AMD64` (0x8664) or `IMAGE_FILE_MACHINE_ARM64` (0xAA64)
- Relocations are 10 bytes: offset(4) + symbolTableIndex(4) + type(2)
- No explicit addend — addend is embedded in instruction bytes at relocation site
- Symbol names ≤8 chars stored inline; longer names reference string table (4-byte size prefix)

---

## Pipeline Integration

### BinaryEmitPass

The `BinaryEmitPass` is the final pass in the native assembler pipeline. It replaces the text `EmitPass` when the
native assembler is selected:

```text
x86_64: IL → Lowering → Legalization → RegAlloc → Peephole → BinaryEmitPass → .o file
                                                    (or EmitPass → .s file)

AArch64: IL → Lowering → RegAlloc → Scheduler → BlockLayout → Peephole → BinaryEmitPass → .o file
                                                                (and optionally EmitPass → .s text)
```

Both x86_64 and AArch64 have their own `BinaryEmitPass` implementations that:

1. Create a binary encoder instance
2. Iterate over MIR functions, encoding each
3. Write the resulting CodeSections through an ObjectFileWriter
4. Set up the `LinkContext` from the symbol table (no assembly text scanning needed)

### CodegenPipeline

`CodegenPipeline` (for both architectures) selects the path based on `AssemblerMode`:

- `AssemblerMode::System` — emit text assembly, invoke `cc -c` (the fallback path)
- `AssemblerMode::Native` — run `BinaryEmitPass`, write `.o` directly

---

## CLI Flags

| Flag | Effect |
|------|--------|
| `--native-asm` | Use native binary encoder (this is the default) |
| `--system-asm` | Override: use system assembler (`cc -c`) instead |
| `-S <path>` | Emit text assembly (always uses text emitter, no assembling) |

---

## Source File Map

### Binary Encoders

| File | LOC | Purpose |
|------|-----|---------|
| `src/codegen/x86_64/binenc/X64Encoding.hpp` | 261 | Register encoding tables, ModR/M/SIB helpers |
| `src/codegen/x86_64/binenc/X64BinaryEncoder.hpp` | 164 | x86_64 encoder interface |
| `src/codegen/x86_64/binenc/X64BinaryEncoder.cpp` | 873 | x86_64 encoder implementation (58 opcodes) |
| `src/codegen/aarch64/binenc/A64Encoding.hpp` | ~350 | AArch64 instruction templates, condition codes |
| `src/codegen/aarch64/binenc/A64BinaryEncoder.hpp` | ~120 | AArch64 encoder interface |
| `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp` | ~900 | AArch64 encoder implementation (~42 opcodes) |

### Object File Infrastructure

| File | LOC | Purpose |
|------|-----|---------|
| `src/codegen/common/objfile/CodeSection.hpp` | 170 | Growable byte buffer + relocation/symbol tracking |
| `src/codegen/common/objfile/Relocation.hpp` | 106 | Architecture-agnostic relocation types |
| `src/codegen/common/objfile/SymbolTable.hpp` | 96 | Symbol table with name lookup |
| `src/codegen/common/objfile/StringTable.hpp` | ~60 | Interned string table for object files |
| `src/codegen/common/objfile/ObjectFileWriter.hpp` | 97 | Abstract writer interface + factories |
| `src/codegen/common/objfile/ElfWriter.cpp` | 536 | ELF 64-bit .o serializer |
| `src/codegen/common/objfile/MachOWriter.cpp` | ~550 | Mach-O 64-bit .o serializer |
| `src/codegen/common/objfile/CoffWriter.cpp` | 473 | COFF .obj serializer |

### Pipeline Integration

| File | Purpose |
|------|---------|
| `src/codegen/x86_64/passes/BinaryEmitPass.hpp/.cpp` | x86_64 binary emission pass |
| `src/codegen/aarch64/passes/BinaryEmitPass.hpp/.cpp` | AArch64 binary emission pass |
| `src/codegen/x86_64/CodegenPipeline.cpp` | x86_64 pipeline (native asm at lines ~642–744) |
| `src/codegen/aarch64/CodegenPipeline.cpp` | AArch64 pipeline |
