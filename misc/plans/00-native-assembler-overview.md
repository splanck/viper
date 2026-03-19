# Native Viper Assembler — Master Plan

## Motivation

Viper currently emits text assembly (GAS/AT&T syntax) and shells out to the system C compiler
(`cc -c`) to assemble `.s` files into `.o` object files, then invokes `cc` again to link. This
creates a hard dependency on an external toolchain (clang/gcc) for every native compilation.

A native assembler that encodes MachineIR directly to binary object files:
- Eliminates the external assembler dependency (zero-dependency philosophy)
- Improves compilation speed by removing the text-serialize → parse → encode round-trip
- Enables cross-compilation without a target sysroot
- Gives Viper full control over the binary output

## Scope

**In scope:** Replace the `MIR → text .s → cc -c → .o` path with `MIR → binary encoder → .o writer → .o`

**Out of scope (for now):** Replacing the system linker. We continue using `cc`/`ld`/`link.exe` to
link `.o` files into executables. A native linker would be a separate 10,000-20,000 LOC project.

## Target Matrix

| Architecture | Linux (ELF) | macOS (Mach-O) | Windows (PE/COFF) |
|-------------|-------------|----------------|-------------------|
| x86_64      | Phase 4     | Phase 5        | Phase 6           |
| AArch64     | Phase 4     | Phase 5        | Phase 6           |

## Current Pipeline

```
IL Module → IL Optimizer → Lowering (IL→MIR) → Legalize → RegAlloc → Peephole
  → EmitPass (MIR → GAS text) → .s file
  → cc -c .s → .o file                    ← REPLACE THIS
  → cc .o + archives → executable          ← KEEP THIS
```

## New Pipeline

```
IL Module → IL Optimizer → Lowering (IL→MIR) → Legalize → RegAlloc → Peephole
  → BinaryEmitPass (MIR → machine code bytes)
  → ObjectFileWriter (bytes + symbols + relocs → .o file)
  → cc .o + archives → executable          ← KEEP THIS
```

The text AsmEmitter is preserved for `-S` flag and debugging. Both paths coexist.

## Implementation Phases

| Phase | Description | Plan File | Est. LOC |
|-------|-------------|-----------|----------|
| 1 | Common infrastructure (CodeSection, Relocation, SymbolTable, StringTable, ObjectFileWriter interface) | `01-common-infrastructure.md` | ~800 |
| 2 | x86_64 binary encoder (REX, ModR/M, SIB, all 49 encoding rows) | `02-x86_64-binary-encoder.md` | ~800 |
| 3 | AArch64 binary encoder (32-bit fixed-width instructions, ~42 opcodes) | `03-aarch64-binary-encoder.md` | ~450 |
| 4 | ELF object file writer (Linux, both architectures) | `04-elf-writer.md` | ~500 |
| 5 | Mach-O object file writer (macOS, both architectures) | `05-macho-writer.md` | ~550 |
| 6 | PE/COFF object file writer (Windows, both architectures) | `06-pe-coff-writer.md` | ~450 |
| 7 | Pipeline integration + linker support adaptation | `07-pipeline-integration.md` | ~535 |
| 8 | Testing strategy (byte-comparison, E2E, golden files, demo tests) | `08-testing-strategy.md` | ~2190 |
| 9 | Comprehensive documentation for `/docs/` | `09-documentation.md` | ~800 |

**Grand total: ~6,925 LOC** (production + tests + documentation)

## Key Architectural Decisions

1. **Table-driven encoding.** Both encoders are driven by opcode→encoding tables (extending the existing JSON specs), not hand-coded per-instruction switch statements.

2. **Architecture-agnostic relocation model.** A single `RelocKind` enum captures all relocation types; each object file writer maps these to format-specific relocation codes.

3. **Shared object file infrastructure.** CodeSection, SymbolTable, StringTable are shared across all three object file formats. Only the serialization differs.

4. **Preserved text path.** The existing AsmEmitter/EmitPass remains for `-S` output and as a reference implementation for testing.

## Current Instruction Counts

- x86_64: 49 encoding rows in `EncodingTable.inc`, 58 MOpcode values (9 are pseudo)
- AArch64: 42 dispatch cases in `OpcodeDispatch.inc`, 79 MOpcode values (~37 are pseudo or expanded)

These are tiny fractions of the full ISAs, making a focused assembler practical.
