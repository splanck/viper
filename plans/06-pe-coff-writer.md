# Phase 6: PE/COFF Object File Writer

## Purpose

Produce valid COFF object files (`.obj`) for Windows, supporting both x86_64 (AMD64) and
AArch64 (ARM64) architectures. COFF is the object file format used by the Microsoft linker
(`link.exe`) and by MinGW's `ld`.

## File

```
src/codegen/common/objfile/CoffWriter.hpp
src/codegen/common/objfile/CoffWriter.cpp
```

---

## 1. COFF Format Overview

```
+----------------------------------+  offset 0
| COFF Header (20 bytes)           |
+----------------------------------+  offset 20
| Section Headers (40 bytes each)  |
|   .text                          |
|   .rdata                         |
+----------------------------------+
| Section Data: .text              |  (machine code)
+----------------------------------+
| Section Data: .rdata             |  (read-only data)
+----------------------------------+
| Relocation Entries               |  (per-section, inline after each section's data)
+----------------------------------+
| Symbol Table (18 bytes each)     |
+----------------------------------+
| String Table (starts with 4-byte size) |
+----------------------------------+
```

**Key difference from ELF/Mach-O:** COFF has NO load commands and NO program headers in
object files. The COFF header points directly to the section table and symbol table.

---

## 2. COFF Header (IMAGE_FILE_HEADER — 20 bytes)

```
Offset  Size  Field                  Value
0       2     Machine                0x8664 (IMAGE_FILE_MACHINE_AMD64)
                                     or 0xAA64 (IMAGE_FILE_MACHINE_ARM64)
2       2     NumberOfSections       2 (.text + .rdata)
4       4     TimeDateStamp          0 (or Unix timestamp — we use 0 for reproducibility)
8       4     PointerToSymbolTable   File offset to symbol table
12      4     NumberOfSymbols        Total symbol count
16      2     SizeOfOptionalHeader   0 (object files have no optional header)
18      2     Characteristics        0 (no special flags for .obj)
```

---

## 3. Section Headers (IMAGE_SECTION_HEADER — 40 bytes each)

```
Offset  Size  Field                   Description
0       8     Name                    Section name (8 bytes, zero-padded)
                                      Or "/N" format for names > 8 chars (N = string table offset)
8       4     VirtualSize             0 (for object files)
12      4     VirtualAddress          0 (for object files)
16      4     SizeOfRawData           Size of section data
20      4     PointerToRawData        File offset to section data
24      4     PointerToRelocations    File offset to relocation entries
28      4     PointerToLinenumbers    0 (deprecated)
32      2     NumberOfRelocations     Count of relocation entries
34      2     NumberOfLinenumbers     0 (deprecated)
36      4     Characteristics         Section flags
```

### Section Definitions

| Section | Name | Characteristics |
|---------|------|----------------|
| .text | ".text\0\0\0" | `0x60000020` = IMAGE_SCN_CNT_CODE \| IMAGE_SCN_MEM_EXECUTE \| IMAGE_SCN_MEM_READ |
| .rdata | ".rdata\0\0" | `0x40000040` = IMAGE_SCN_CNT_INITIALIZED_DATA \| IMAGE_SCN_MEM_READ |

**Alignment flags** (OR'd into Characteristics):
- `IMAGE_SCN_ALIGN_16BYTES` = `0x00500000` (for .text)
- `IMAGE_SCN_ALIGN_8BYTES` = `0x00400000` (for .rdata)

### Section Name Encoding

Section names ≤ 8 characters are stored inline (zero-padded). Names > 8 characters use the
format `"/N"` where N is the decimal offset into the string table. For Viper, `.text` (5 chars)
and `.rdata` (6 chars) both fit inline.

---

## 4. Symbol Table (IMAGE_SYMBOL — 18 bytes each)

```
Offset  Size  Field                Description
0       8     Name                 Symbol name:
                                   - If first 4 bytes are 0: remaining 4 bytes = offset into string table
                                   - Otherwise: 8-byte inline name (zero-padded)
8       4     Value                Offset within section (for defined symbols)
12      2     SectionNumber        1-based section index, or:
                                   IMAGE_SYM_UNDEFINED (0) for external
                                   IMAGE_SYM_ABSOLUTE (-1) for absolute
14      2     Type                 0x0000 (null) or 0x0020 (function)
16      1     StorageClass         IMAGE_SYM_CLASS_EXTERNAL (2) for global
                                   IMAGE_SYM_CLASS_STATIC (3) for local/section
                                   IMAGE_SYM_CLASS_LABEL (6) for labels
17      1     NumberOfAuxSymbols   0 (or 1 for section symbols with aux data)
```

### Symbol Ordering

COFF does NOT require local-before-global ordering (unlike ELF and Mach-O). However,
convention is:
1. Section symbols (.text, .rdata) with StorageClass=STATIC
2. Defined function symbols with StorageClass=EXTERNAL
3. Undefined symbols with StorageClass=EXTERNAL, SectionNumber=0

### Symbol Name Encoding

- Names ≤ 8 characters: stored inline in the 8-byte Name field
- Names > 8 characters: first 4 bytes = 0, next 4 bytes = offset into string table
- **No underscore prefix** on Windows (unlike Mach-O)

---

## 5. Relocation Entries (IMAGE_RELOCATION — 10 bytes each)

```
Offset  Size  Field              Description
0       4     VirtualAddress     Byte offset within section
4       4     SymbolTableIndex   Index into symbol table
8       2     Type               Relocation type
```

**Note:** COFF relocations are stored immediately after each section's data, not in a separate
section. The section header's `PointerToRelocations` and `NumberOfRelocations` fields point
to them.

### RelocKind → COFF Relocation Type Mapping

#### AMD64 (x86_64)

| RelocKind | COFF Type | Value | Notes |
|-----------|-----------|-------|-------|
| PCRel32 | IMAGE_REL_AMD64_REL32 | 4 | 32-bit PC-relative |
| Branch32 | IMAGE_REL_AMD64_REL32 | 4 | Same type for calls |
| Abs64 | IMAGE_REL_AMD64_ADDR64 | 1 | 64-bit absolute |

#### ARM64 (AArch64)

| RelocKind | COFF Type | Value | Notes |
|-----------|-----------|-------|-------|
| A64Call26 | IMAGE_REL_ARM64_BRANCH26 | 3 | BL/B |
| A64Jump26 | IMAGE_REL_ARM64_BRANCH26 | 3 | B |
| A64AdrpPage21 | IMAGE_REL_ARM64_PAGEBASE_REL21 | 4 | ADRP |
| A64AddPageOff12 | IMAGE_REL_ARM64_PAGEOFFSET_12A | 6 | ADD page offset |
| A64LdSt64Off12 | IMAGE_REL_ARM64_PAGEOFFSET_12L | 7 | LDR/STR page offset |
| A64CondBr19 | IMAGE_REL_ARM64_BRANCH19 | 8 | B.cond/CBZ/CBNZ (see note) |

**Note on A64CondBr19:** Unlike Mach-O, PE/COFF **does** define `IMAGE_REL_ARM64_BRANCH19`
for conditional branches. However, in the current Viper codebase, conditional branches always
target internal block labels and are resolved by the encoder via `patch32LE()`. This relocation
type is included for completeness but is unlikely to be used initially.

---

## 6. String Table

The COFF string table is used for symbol names > 8 characters and section names > 8 characters.

**Format:**
```
Offset  Size  Content
0       4     Total size of string table (including this 4-byte field)
4       N     Null-terminated strings, concatenated
```

**Difference from ELF:** The 4-byte size prefix is part of the string table itself. Symbol name
offsets reference positions starting from byte 0 of the string table (i.e., offset 4 is the
first string character).

### Integration with Our StringTable

Our `StringTable` class stores raw NUL-terminated strings without the size prefix. The COFF
writer prepends the 4-byte size when serializing.

---

## 7. SEH Unwind Data (Deferred)

For proper stack unwinding on Windows, COFF objects should include:
- `.pdata` section — function table entries (RUNTIME_FUNCTION structures)
- `.xdata` section — unwind info (UNWIND_INFO structures)

**Deferred to Phase 2.** Programs work correctly without SEH data; only debugger stack traces
and structured exception handling (SEH) are affected. Since Viper uses its own trap mechanism
rather than Windows SEH, this is low priority.

---

## 8. Writer Implementation

### Algorithm

```
1. Build string table:
   a. Start with 4-byte size (placeholder)
   b. Add symbol names that are > 8 characters
   c. Patch size field

2. Build symbol table:
   a. Section symbols (.text, .rdata) — StorageClass=STATIC
   b. Defined function symbols — StorageClass=EXTERNAL
   c. Undefined symbols — StorageClass=EXTERNAL, SectionNumber=0

3. Build relocation entries per section:
   a. Map RelocKind → COFF type
   b. Pack 10-byte entries

4. Compute file layout:
   a. COFF header (20 bytes)
   b. Section headers (2 × 40 = 80 bytes)
   c. .text data (after headers)
   d. .text relocations (immediately after .text data)
   e. .rdata data
   f. .rdata relocations (immediately after .rdata data)
   g. Symbol table
   h. String table

5. Write all structures

6. Patch header with PointerToSymbolTable
```

### Cross-Architecture Support

The writer parameterizes:
- `Machine` field: AMD64 vs ARM64
- Relocation type mapping table

Everything else (section structure, symbol format, string table) is identical.

---

## 9. PE/COFF vs ELF/Mach-O Comparison

| Aspect | ELF | Mach-O | PE/COFF |
|--------|-----|--------|---------|
| Header size | 64 bytes | 32 bytes | 20 bytes |
| Section descriptor | Section header (64B) | Within LC_SEGMENT_64 (80B) | Section header (40B) |
| Relocation size | 24 bytes (Rela) | 8 bytes | 10 bytes |
| Has addend field | Yes (explicit) | No (in-instruction) | No (in-instruction) |
| Symbol size | 24 bytes | 16 bytes | 18 bytes |
| Symbol name mangling | None | Underscore prefix | None |
| Section name limit | Unlimited (string table) | 16 chars | 8 chars (or /N format) |
| Relocation placement | Separate .rela section | Per-section, pointed by header | Inline after section data |
| Local-first ordering | Required | Required | Convention only |

---

## 10. Verification

After writing a COFF .obj file:
1. `dumpbin /headers output.obj` — verify header (Windows, MSVC tools)
2. `dumpbin /symbols output.obj` — verify symbol table
3. `dumpbin /relocations output.obj` — verify relocations
4. `dumpbin /disasm output.obj` — verify instructions
5. `llvm-objdump -d output.obj` — cross-platform verification
6. `link.exe output.obj viper_rt_base.lib ... /OUT:test.exe` — link and run
7. `clang output.obj -o test.exe ...` — alternative: link with clang on Windows

---

## Estimated Line Counts

| Component | LOC |
|-----------|-----|
| COFF constants (machine types, section flags, reloc types) | ~80 |
| Header writing | ~25 |
| Section header writing | ~50 |
| Symbol table building + writing | ~80 |
| Relocation table building + writing | ~50 |
| String table with size prefix | ~30 |
| File layout computation | ~40 |
| Top-level write() method | ~60 |
| **Total** | **~415** |

Plus tests (~200 LOC):
- Write minimal .obj, verify with llvm-objdump
- Write .obj with relocations, link with clang and run
- Symbol name encoding (inline vs string table offset)
- Compare against system-assembler output
