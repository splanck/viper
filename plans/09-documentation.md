# Phase 9: Documentation

## Purpose

Create comprehensive documentation for the native assembler and object file infrastructure
in `/docs/`. This documentation serves two audiences:
1. **Contributors** who will maintain or extend the assembler
2. **Users** who need to understand the compilation pipeline and its options

---

## 1. New Documentation Files

### 1A. `docs/codegen/native-assembler.md` — Main Reference (~300 lines)

**Audience:** Contributors

Comprehensive technical documentation for the native assembler subsystem:

```
# Native Assembler — Technical Reference

## Overview
- Motivation (zero-dependency philosophy)
- Architecture diagram: IL → MIR → Binary Encoder → Object File Writer → .o
- Comparison with text assembly path (preserved for -S flag)

## Pipeline Integration
- Binary path vs text path selection (--native-asm / --text-asm / -S)
- BinaryEmitPass position in the pass pipeline
- Pass selection logic (x86_64 class-based vs AArch64 functional)
- Platform detection and object format selection

## Binary Encoders
### x86_64 Encoder
- Instruction format: REX + Opcode + ModR/M + SIB + Disp + Imm
- Encoding table driven (49 encoding rows from EncodingTable.inc)
- Register hardware encoding map (PhysReg → 3-bit + REX extension)
- ModR/M/SIB special cases (RSP-needs-SIB, RBP-needs-disp8)
- RIP-relative addressing and the -4 addend convention
- SSE prefix ordering: mandatory prefix → REX → 0F → opcode
- Branch resolution (single-pass with forward-reference patching)

### AArch64 Encoder
- Fixed 32-bit instruction words (bit-field construction)
- Register encoding (direct 5-bit mapping)
- Prologue/epilogue synthesis from FrameLayout (NOT from MIR)
- Main function runtime init injection
- Large offset sequences (MOVZ/MOVK, chunked SP adjust)
- FMovRI: FP8 immediate validation + literal pool fallback
- Branch resolution (same two-phase approach as x86_64)

## Object File Writers
### Shared Infrastructure
- CodeSection: byte buffer + relocation + symbol tracking
- SymbolTable: local/global/external, unmangled names
- StringTable: interned NUL-terminated strings
- RelocKind enum: architecture-agnostic relocation types

### ELF Writer (Linux)
- Section layout (8 sections: null, .text, .rodata, .rela.text, .symtab, .strtab, .shstrtab, .note.GNU-stack)
- Symbol ordering (locals before globals, sh_info boundary)
- Relocation mapping (RelocKind → R_X86_64_* / R_AARCH64_*)

### Mach-O Writer (macOS)
- Load command structure (LC_SEGMENT_64, LC_BUILD_VERSION, LC_SYMTAB, LC_DYSYMTAB)
- Section naming (__TEXT,__text and __TEXT,__const)
- Underscore symbol prefix mangling
- MH_SUBSECTIONS_VIA_SYMBOLS flag
- Relocation addend embedded in instruction bytes (no r_addend field)

### PE/COFF Writer (Windows)
- 20-byte header, 40-byte section headers
- Symbol name encoding (inline ≤8 chars, string table offset >8 chars)
- Inline relocations (after each section's data)

## RoData Pool
- Architecture-specific label formats (.LC_str_N vs L.str.N)
- Binary emission (raw bytes, no text escaping)
- Cross-section relocation generation

## Limitations / Non-Support
- No CFI/DWARF debug info
- No BSS/data sections (all data in rodata or stack)
- No weak symbols, TLS, GOT, jump tables
- No branch range relaxation
- No cross-compilation (native platform only)
```

### 1B. `docs/codegen/object-formats.md` — Object File Format Reference (~250 lines)

**Audience:** Contributors

Detailed byte-level reference for the three object file formats:

```
# Object File Formats — Byte-Level Reference

## ELF (Linux)
- Header layout (64 bytes) with field descriptions
- Section header layout (64 bytes) with field descriptions
- Symbol table entry layout (24 bytes)
- Relocation entry layout (24 bytes, RELA with addend)
- x86_64 and AArch64 relocation type tables
- String table format (NUL-separated, offset 0 = empty)

## Mach-O (macOS)
- Header layout (32 bytes)
- Load command structures (LC_SEGMENT_64, LC_BUILD_VERSION, etc.)
- Section header layout (80 bytes, within LC_SEGMENT_64)
- nlist_64 symbol entry layout (16 bytes)
- Relocation entry layout (8 bytes, packed r_info)
- x86_64 and AArch64 relocation type tables
- Addend embedding convention

## PE/COFF (Windows)
- Header layout (20 bytes)
- Section header layout (40 bytes)
- Symbol entry layout (18 bytes)
- Relocation entry layout (10 bytes)
- String table format (4-byte size prefix)
- x86_64 and AArch64 relocation type tables

## Cross-Format Comparison Table
- Side-by-side comparison of header sizes, section structures, symbol formats
- Relocation handling differences (explicit addend vs in-instruction)
- Symbol mangling rules per platform
```

### 1C. `docs/codegen/encoding-tables.md` — Instruction Encoding Reference (~200 lines)

**Audience:** Contributors

Quick-reference tables for all instruction encodings:

```
# Instruction Encoding Tables

## x86_64 Encoding Table (49 rows)
- Complete table: MOpcode → opcode bytes, form, flags
- Condition code → JCC/SETcc opcode nibble mapping
- SSE prefix table (F2/66 assignments)
- Register hardware encoding map

## AArch64 Encoding Table (~42 opcodes)
- Complete table: MOpcode → 32-bit instruction template, bit-field positions
- Condition code → 4-bit encoding
- Instruction format classes (R-type, I-type, B-type, etc.)
```

---

## 2. Updated Documentation Files

### 2A. Update `docs/backend.md`

Add a new section "Native Assembler" after the existing "Code Emission" section:

- Explain the binary emission path as an alternative to text assembly
- Link to `docs/codegen/native-assembler.md` for full details
- Document `--native-asm` / `--text-asm` CLI flags
- Update the pipeline diagram to show both paths

### 2B. Update `docs/codegen/x86_64.md`

- Add section on binary encoding (link to native-assembler.md)
- Document the encoding table source (`docs/spec/x86_64_encodings.json`)
- Note register hardware encoding differences from PhysReg enum order

### 2C. Update `docs/codegen/aarch64.md`

- Add section on binary encoding (link to native-assembler.md)
- Document prologue/epilogue synthesis (emission-time, not MIR)
- Note main function runtime init injection

### 2D. Update `docs/codemap/codegen.md`

Add the new files to the codemap tables:

```markdown
## Object File Infrastructure (`src/codegen/common/objfile/`)

| File | Purpose |
|------|---------|
| `Relocation.hpp` | Architecture-agnostic relocation types |
| `CodeSection.hpp/cpp` | Byte buffer with relocation and symbol tracking |
| `SymbolTable.hpp/cpp` | Symbol management (local, global, external) |
| `StringTable.hpp/cpp` | Interned string table for names |
| `ObjectFileWriter.hpp` | Abstract writer interface + factory |
| `ElfWriter.hpp/cpp` | ELF format serializer (Linux) |
| `MachOWriter.hpp/cpp` | Mach-O format serializer (macOS) |
| `CoffWriter.hpp/cpp` | PE/COFF format serializer (Windows) |

## x86_64 Binary Encoder (`src/codegen/x86_64/binenc/`)

| File | Purpose |
|------|---------|
| `X64Encoding.hpp` | Register hardware encoding, opcode tables |
| `X64BinaryEncoder.hpp/cpp` | MIR → machine code bytes |

## AArch64 Binary Encoder (`src/codegen/aarch64/binenc/`)

| File | Purpose |
|------|---------|
| `A64Encoding.hpp` | Instruction templates, condition codes |
| `A64BinaryEncoder.hpp/cpp` | MIR → machine code bytes |
```

### 2E. Update `docs/tools.md`

Document the new CLI flags:
- `--native-asm`: Use binary encoder (default)
- `--text-asm`: Force text assembly path
- Updated behavior matrix showing all flag combinations

### 2F. Update `docs/architecture.md`

Add the native assembler to the architecture overview diagram and component list.

---

## 3. Documentation Style Guide

Follow existing conventions found in the docs:

- **Frontmatter:** All docs start with `---\nstatus: active\naudience: ...\n---`
- **Table of contents:** Include for documents >3 sections
- **Source references:** Reference file paths relative to repo root
- **Code examples:** Use fenced code blocks with language tags
- **Cross-references:** Use relative markdown links between docs
- **Diagrams:** ASCII-art for pipeline/architecture diagrams (no external tools)

---

## 4. Estimated Line Counts

| Document | Lines |
|----------|-------|
| `docs/codegen/native-assembler.md` (new) | ~300 |
| `docs/codegen/object-formats.md` (new) | ~250 |
| `docs/codegen/encoding-tables.md` (new) | ~200 |
| `docs/backend.md` (update) | ~20 additions |
| `docs/codegen/x86_64.md` (update) | ~15 additions |
| `docs/codegen/aarch64.md` (update) | ~15 additions |
| `docs/codemap/codegen.md` (update) | ~30 additions |
| `docs/tools.md` (update) | ~10 additions |
| `docs/architecture.md` (update) | ~10 additions |
| **Total** | **~850 lines** |

---

## 5. Documentation Timing

Documentation should be written **during implementation**, not after:

- Phase 1: Write CodeSection/SymbolTable/StringTable API docs as they're implemented
- Phase 2-3: Write encoding table docs while implementing encoders
- Phase 4-6: Write object format docs while implementing writers
- Phase 7: Write pipeline integration docs and update backend.md/tools.md
- Phase 8: Final review pass — ensure all docs match the implementation

This ensures documentation stays accurate and captures design decisions while they're fresh.
