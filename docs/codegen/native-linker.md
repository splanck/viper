---
status: active
audience: contributors
last-verified: 2026-04-09
---

# Native Linker — Object File Linking & Executable Generation

The native linker provides a built-in object/archive link pipeline that reads object files, resolves symbols, merges
sections, applies relocations, and writes platform-native executables. Combined with the
[native assembler](native-assembler.md), this removes the system linker dependency from the executable link step.

> **Pipeline comparison:**
>
> - **Deprecated alias:** `.o` + runtime `.a` archives → NativeLinker (`--system-link` now maps here)
> - **Native path:** `.o` + runtime `.a` archives → NativeLinker → executable

---

## Table of Contents

1. [Overview](#overview)
2. [Linker Pipeline](#linker-pipeline)
3. [Archive Reader](#archive-reader)
4. [Object File Readers](#object-file-readers)
5. [Symbol Resolution](#symbol-resolution)
6. [Section Merging](#section-merging)
7. [Relocation Application](#relocation-application)
8. [Executable Writers](#executable-writers)
9. [Platform-Specific Details](#platform-specific-details)
10. [CLI Flags](#cli-flags)
11. [Source File Map](#source-file-map)
12. [Design Decisions](#design-decisions)

---

## Overview

### What the Linker Does

The native linker takes the user's compiled `.o` file plus the Viper runtime archives (`.a` files) and produces a
runnable executable. It performs the classic linker pipeline:

1. Parse the user's `.o` file
2. Parse runtime `.a` archives
3. Iteratively extract archive members needed to resolve undefined symbols
4. Merge sections from all objects into output segments
5. Assign virtual addresses with platform-appropriate page alignment
6. Patch machine code with resolved symbol addresses (relocation application)
7. Write the final executable in the target format (ELF / Mach-O / PE)

### Supported Output Formats

| Format | Platform | Architecture | Page Size | Dynamic Imports |
|--------|----------|-------------|-----------|-----------------|
| ELF | Linux | x86_64 | 4KB | Supported |
| ELF | Linux | AArch64 | 4KB | Static/archive-only links supported; shared-library imports not yet supported |
| Mach-O | macOS | x86_64 | 4KB | Not yet supported by the native linker |
| Mach-O | macOS | AArch64 (Apple Silicon) | **16KB** | Supported |
| PE | Windows | x86_64, AArch64 | 4KB | Supported |

Archive-only / fully self-contained links can use the native linker on every output-writer target above.
Programs that depend on libc, the Windows CRT, or OS frameworks require one of the native dynamic-import
targets above; unsupported shared-library targets now fail explicitly instead of delegating to the host linker.

> **Critical:** macOS arm64 requires 16KB page alignment. The dynamic linker (`dyld`) rejects executables with
> incorrect page alignment. This is the single most important platform-specific detail in the linker.

---

## Linker Pipeline

The `NativeLinker` orchestrator (`NativeLinker.cpp`) ties the pipeline together:

```text
┌─────────────┐    ┌──────────────┐    ┌──────────────────┐
│ ObjFileReader│    │ArchiveReader │    │  SymbolResolver  │
│ (ELF/MachO/ │───▶│ (GNU/BSD/    │───▶│  (iterative      │
│  COFF)      │    │  COFF .lib)  │    │   demand-pull)   │
└─────────────┘    └──────────────┘    └────────┬─────────┘
                                                │
                                       ┌────────▼─────────┐
                                       │  SectionMerger   │
                                       │  (classify, merge│
                                       │   assign VAs)    │
                                       └────────┬─────────┘
                                                │
                                       ┌────────▼─────────┐
                                       │  RelocApplier    │
                                       │  (patch machine  │
                                       │   code bytes)    │
                                       └────────┬─────────┘
                                                │
                              ┌─────────────────┼──────────────────┐
                              ▼                 ▼                  ▼
                       ┌─────────────┐   ┌──────────────┐   ┌──────────┐
                       │ElfExeWriter │   │MachOExeWriter│   │PeExeWriter│
                       └─────────────┘   └──────────────┘   └──────────┘
```

---

## Archive Reader

**File:** `codegen/common/linker/ArchiveReader.hpp/.cpp`

Parses Unix `ar` archives (`.a` files) containing object files. The runtime libraries are compiled by CMake into `.a`
archives, one per component (base, oop, collections, text, io_fs, exec, threads, graphics, audio, network).

### Archive Format Variants

The reader handles all three major variants:

| Variant | Platform | Symbol Table | Long Names |
|---------|----------|-------------|------------|
| **GNU** | Linux | `/` member (big-endian count + offsets + NUL names) | `//` string table, `/offset` references |
| **BSD** | macOS | `__.SYMDEF SORTED` (ranlib array + string pool) | `#1/N` (N name bytes inline after header) |
| **COFF** | Windows | `/` member (same as GNU first linker member) | `//` string table (same as GNU) |

All variants share the `!<arch>\n` magic (8 bytes) and 60-byte member headers.

### Selective Extraction

Archives contain a symbol index mapping symbol names to member offsets. The linker uses this to extract only the
members that define needed symbols, rather than linking every member. This is critical for keeping executable sizes
reasonable — the full runtime is ~3,200 symbols across 11 component archives.

---

## Object File Readers

**Files:** `codegen/common/linker/ObjFileReader.hpp`, `ElfReader.cpp`, `MachOReader.cpp`, `CoffReader.cpp`

### Auto-Detection

The reader auto-detects format from magic bytes:

| Bytes | Format |
|-------|--------|
| `7F 45 4C 46` | ELF |
| `CF FA ED FE` | Mach-O 64-bit (little-endian) |
| `64 86` or `64 AA` | COFF (AMD64 or ARM64 machine field) |

### Unified Representation

All three readers produce the same `ObjFile` structure:

```
ObjFile
├── sections[]     → ObjSection (name, data bytes, relocations, alignment, flags)
├── symbols[]      → ObjSymbol  (name, binding, section index, offset)
├── format         → ELF / MachO / COFF
└── machine        → x86_64 / AArch64
```

### Format-Specific Handling

- **ELF**: Explicit addend from `.rela` sections. Symbol binding from `STB_LOCAL/GLOBAL/WEAK`.
- **Mach-O**: No explicit addend — extracted from instruction bytes at the relocation site. Leading `_` stripped
  from symbol names (Mach-O convention).
- **COFF**: No explicit addend — extracted from instruction bytes. Symbol names ≤8 chars inline; longer names
  via string table.

---

## Symbol Resolution

**Files:** `codegen/common/linker/SymbolResolver.hpp/.cpp`

### Algorithm

Symbol resolution uses an iterative fixed-point algorithm:

1. **Seed**: Add the user's `.o` file. All its globals → defined, all its extern refs → undefined.
2. **Scan archives**: For each undefined symbol, check each archive's symbol index. If found, extract that member,
   parse it, add its definitions and new undefined refs.
3. **Repeat** until no new definitions are found (handles cross-archive dependencies).
4. **Classify remaining**: Unresolved symbols are marked as dynamic (expected from shared libraries).

### Precedence Rules

| Existing | New | Result |
|----------|-----|--------|
| Undefined | Global | Global wins |
| Undefined | Weak | Weak wins |
| Weak | Global | Global wins (strong overrides weak) |
| Global | Global | **Error** (multiply defined) |
| Global | Weak | Existing Global kept |

### Runtime Archive Order

Archives are searched in the order provided by `RuntimeComponents.hpp`, which resolves dependencies:

```
network → threads → audio → graphics → exec → io_fs → text → collections → arrays → oop → base
```

Base is always included. Other components are pulled in based on which `rt_*` symbols the program references.

---

## Section Merging

**Files:** `codegen/common/linker/SectionMerger.hpp/.cpp`, `LinkTypes.hpp`

### Section Classification

Input sections are classified by name and attributes:

| Class | Sections | Permissions |
|-------|----------|-------------|
| **Text** | `.text`, `__TEXT,__text` | R-X |
| **Rodata** | `.rodata`, `.rdata`, `__TEXT,__const` | R-- |
| **Data** | `.data`, `__DATA,__data` | RW- |
| **BSS** | `.bss` | RW- (no file backing) |
| **TLS Data** | `.tdata`, `__DATA,__thread_data` | RW- (thread-local) |
| **TLS BSS** | `.tbss`, `__DATA,__thread_bss` | RW- (thread-local, zero-filled) |

### Layout

Output sections are laid out in order: `.text` → `.rodata` → `.data` → `.tdata` → `.bss` → `.tbss`.

Each section starts on a page boundary. Within a section, input chunks are concatenated with their alignment
requirements preserved.

### Base Addresses

| Platform | Base Address |
|----------|-------------|
| macOS | `0x100000000` (4GB, above __PAGEZERO) |
| Linux | `0x400000` (traditional non-PIE) |
| Windows | `0x140000000` (PE default ImageBase) |

---

## Relocation Application

**Files:** `codegen/common/linker/RelocApplier.hpp/.cpp`

### The Collision Problem

ELF, Mach-O, and COFF relocation type numbers collide numerically. For example, type `1` means:
- ELF: `R_X86_64_64` (64-bit absolute)
- Mach-O: `X86_64_RELOC_SIGNED` (32-bit PC-relative)
- COFF: `IMAGE_REL_AMD64_ADDR64` (64-bit absolute)

### Solution: RelocAction Dispatch

The applier first classifies each relocation by `(format, arch, type)` into a semantic `RelocAction`, then applies
the action uniformly:

```
(ObjFileFormat, LinkArch, raw type) → RelocAction → patch bytes
```

This avoids a single `switch` with colliding cases.

### Relocation Formulas

| Action | Formula | Width |
|--------|---------|-------|
| **PCRel32** | `S + A - P` | 32-bit |
| **Abs64** | `S + A` | 64-bit |
| **Abs32** | `S + A` | 32-bit |
| **Branch26** | `(S + A - P) >> 2` masked to 26 bits | instruction field |
| **Page21** | `Page(S+A) - Page(P)` encoded as ADRP immhi:immlo | instruction field |
| **PageOff12** | `(S + A) & 0xFFF` into ADD imm12 | instruction field |
| **LdSt64Off** | `((S + A) & 0xFFF) >> 3` into LDR/STR imm12 | instruction field |
| **CondBr19** | `(S + A - P) >> 2` masked to 19 bits | instruction field |

Where: `S` = symbol address, `A` = addend, `P` = patch site address, `Page(X)` = `X & ~0xFFF`.

### Range Checking

- AArch64 B/BL: ±128MB (`imm26` × 4)
- AArch64 B.cond/CBZ/CBNZ: ±1MB (`imm19` × 4)
- x86_64: rel32 can reach ±2GB (sufficient for most programs)

---

## Executable Writers

### ELF Executable Writer

**File:** `codegen/common/linker/ElfExeWriter.hpp/.cpp`

Produces a minimal static ELF executable (`ET_EXEC`).

**Layout:**
```
ELF Header (64B)
Program Headers:
  PT_LOAD (.text, RE)
  PT_LOAD (.rodata, R)
  PT_LOAD (.data, RW)
  PT_GNU_STACK (non-executable stack)
.text segment data
.rodata segment data
.data segment data
.shstrtab
Section Header Table
```

- Entry point set via `e_entry` field
- Each PT_LOAD segment is page-aligned
- `PT_GNU_STACK` flags = `PF_R | PF_W` (no `PF_X`, non-executable stack)
- File permissions set to 755 after writing

### Mach-O Executable Writer

**File:** `codegen/common/linker/MachOExeWriter.hpp/.cpp`

Produces a Mach-O executable (`MH_EXECUTE`).

**Layout:**
```
mach_header_64 (32B, MH_EXECUTE | MH_PIE)
Load Commands:
  LC_SEGMENT_64 "__PAGEZERO" (vmaddr=0, vmsize=4GB, MUST BE FIRST)
  LC_SEGMENT_64 "__TEXT"     (code + rodata, R-X)
  LC_SEGMENT_64 "__DATA"     (data, RW-)
  LC_MAIN                    (entryoff = offset of main() within __TEXT)
  LC_BUILD_VERSION           (platform=macOS, minos=14.0, sdk=15.0)
__TEXT segment data (page-aligned)
__DATA segment data (page-aligned)
```

**Key details:**
- `__PAGEZERO` must be the first load command (vmaddr=0, vmsize=0x100000000)
- `LC_MAIN` specifies offset of `_main` within `__TEXT` file data — dyld handles argc/argv setup
- No CRT objects needed on macOS (unlike Linux)
- Page alignment: 16KB on arm64, 4KB on x86_64
- `MH_PIE` flag set for ASLR

### PE Executable Writer

**File:** `codegen/common/linker/PeExeWriter.hpp/.cpp`

Produces a PE32+ executable.

**Layout:**
```
DOS Header (64B, "MZ")
PE Signature (4B, "PE\0\0")
COFF Header (20B)
Optional Header (240B, PE32+):
  ImageBase = 0x140000000
  SectionAlignment = 0x1000
  FileAlignment = 0x200
  Subsystem = WINDOWS_CUI (console)
Section Headers (.text, .rdata, .data)
Section Data (file-aligned to 0x200)
```

**Key details:**
- DLL characteristics: `DYNAMIC_BASE | NX_COMPAT | HIGH_ENTROPY_VA | TERMINAL_SERVER_AWARE`
- Section data aligned to 0x200 (file) and 0x1000 (memory)
- 16 data directory entries (all zeroed in minimal output)
- Stack reserve: 1MB, heap reserve: 1MB

---

## Platform-Specific Details

### macOS: Code Signing

macOS Ventura+ requires ad-hoc code signing for arm64 executables. The native linker includes
`MachOCodeSign.hpp/.cpp` which produces ad-hoc signed binaries (`LC_CODE_SIGNATURE` load command with
a `CodeDirectory` hash). The system `codesign` tool is used as a fallback when the built-in signer is
insufficient.

### Linux: CRT Startup

Viper programs emit a `main` function. On Linux x86_64, the native linker emits its own loader metadata
(`PT_INTERP`, `PT_DYNAMIC`, `DT_NEEDED`, `.dynsym`, `.rela.dyn`) and still enters at `main`; there is no
external `crt1.o` or host linker dependency in the executable link step.

### Windows: CRT

Windows executables need `mainCRTStartup` or link against the CRT DLLs. The native linker generates
minimal PE executables with the entry point at `main` and emits the required DLL import tables itself.

### Thread-Local Storage

The Viper runtime uses 13 thread-local variables across 9 `.c` files. The linker preserves TLS sections (`.tdata`,
`.tbss`, `__DATA,__thread_data`) when present in input objects. TLS relocations are handled by the relocation
applier using the appropriate format-specific types.

---

## CLI Flags

| Flag | Effect |
|------|--------|
| `--native-link` | Use native linker (this is the default on AArch64) |
| `--system-link` | Deprecated alias for native linking |
| `--native-asm` | Use native assembler (this is the default) |
| `--system-asm` | Override: use system assembler (`cc -c`) instead |

Default behavior: the native assembler and native linker are used by default. `--system-asm` still requests
the host assembler for `.s -> .o`, but `--system-link` no longer routes the final executable through `cc`.

---

## Source File Map

### Core Types

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/LinkTypes.hpp` | 153 | OutputSection, InputChunk, LinkLayout, GlobalSymEntry, enums |

### Archive Reader (Phase 10)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/ArchiveReader.hpp` | 64 | Archive, ArchiveMember structures |
| `codegen/common/linker/ArchiveReader.cpp` | 328 | GNU/BSD/COFF archive parsing |

### Object File Readers (Phase 11)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/ObjFileReader.hpp` | 119 | Unified ObjFile/ObjSection/ObjSymbol/ObjReloc |
| `codegen/common/linker/ObjFileReader.cpp` | 87 | Format detection and dispatch |
| `codegen/common/linker/ElfReader.cpp` | 296 | ELF 64-bit .o reader |
| `codegen/common/linker/MachOReader.cpp` | 352 | Mach-O 64-bit .o reader |
| `codegen/common/linker/CoffReader.cpp` | 290 | COFF .obj reader |

### Symbol Resolution (Phase 12)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/SymbolResolver.hpp` | 52 | `resolveSymbols()` interface |
| `codegen/common/linker/SymbolResolver.cpp` | 245 | Iterative archive demand-pull |

### Section Merging (Phase 13)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/SectionMerger.hpp` | 44 | `mergeSections()` interface |
| `codegen/common/linker/SectionMerger.cpp` | 226 | Section classification, merging, VA layout |

### Relocation Application (Phase 14)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/RelocApplier.hpp` | 48 | `applyRelocations()` interface |
| `codegen/common/linker/RelocApplier.cpp` | 405 | Format-dispatched relocation patching |

### Executable Writers (Phases 16–18)

| File | LOC | Purpose |
|------|-----|---------|
| `codegen/common/linker/ElfExeWriter.hpp` | 39 | `writeElfExe()` interface |
| `codegen/common/linker/ElfExeWriter.cpp` | 330 | Static ELF executable output |
| `codegen/common/linker/MachOExeWriter.hpp` | 51 | `writeMachOExe()` interface |
| `codegen/common/linker/MachOExeWriter.cpp` | 351 | Mach-O MH_EXECUTE output |
| `codegen/common/linker/PeExeWriter.hpp` | 42 | `writePeExe()` interface |
| `codegen/common/linker/PeExeWriter.cpp` | 267 | PE32+ executable output |

### Additional Linker Passes

| File | Purpose |
|------|---------|
| `codegen/common/linker/BranchTrampoline.hpp/.cpp` | AArch64 branch trampoline insertion for ±128MB range |
| `codegen/common/linker/DeadStripPass.hpp/.cpp` | Unreferenced section removal via reachability |
| `codegen/common/linker/DynStubGen.hpp/.cpp` | Dynamic symbol stub generation |
| `codegen/common/linker/ICF.hpp/.cpp` | Identical Code Folding |
| `codegen/common/linker/MachOBindRebase.hpp/.cpp` | Mach-O bind/rebase opcode emission |
| `codegen/common/linker/MachOCodeSign.hpp/.cpp` | Mach-O ad-hoc code signing |
| `codegen/common/linker/NameMangling.hpp` | Platform-aware symbol name mangling and Mach-O fallback |
| `codegen/common/linker/StringDedup.hpp/.cpp` | String deduplication for merged sections |
| `codegen/common/linker/AlignUtil.hpp` | Alignment calculation utilities |
| `codegen/common/linker/ExeWriterUtil.hpp` | Shared executable writer utilities |
| `codegen/common/linker/RelocClassify.hpp` | Relocation classification utilities |
| `codegen/common/linker/RelocConstants.hpp` | Relocation type constants |

### Top-Level Orchestrator

| File | Purpose |
|------|---------|
| `codegen/common/linker/NativeLinker.hpp` | `nativeLink()` interface + options struct |
| `codegen/common/linker/NativeLinker.cpp` | Full pipeline orchestration (~1,365 LOC) |

### Total: 41 files, ~9,600 LOC

---

## Design Decisions

### Why Not Use LLD?

Viper's core principle is zero external dependencies. LLVM's LLD is an excellent linker, but it would introduce
a massive dependency on the LLVM project. The native linker is purpose-built for Viper's specific needs and is
significantly simpler than a general-purpose linker.

### Why Format-Dispatched Relocations?

A common approach is to normalize all relocations to a unified enum (like `RelocKind` in the assembler). However,
the linker must handle relocations from *other* compilers' object files (the runtime `.a` archives contain objects
built by the system C compiler), whose type numbers we cannot control. The `(format, arch, type) → RelocAction`
dispatch cleanly separates format-specific constants from the patching logic.

### Why Static Linking First?

Dynamic linking (PLT/GOT, dyld stubs, IAT) is significantly more complex than static linking. The implementation
started from static linking and then grew native dynamic-import support per platform: Windows import tables,
Mach-O dyld bind metadata on macOS arm64, and ELF loader metadata on Linux x86_64.

### Why Iterative Archive Resolution?

Archives may have cross-dependencies (e.g., `collections` needs symbols from `arrays`, which needs symbols from
`oop`). A single-pass scan would miss these transitive dependencies. The iterative fixed-point algorithm
keeps re-scanning until no new definitions are found, guaranteeing completeness regardless of archive ordering.
