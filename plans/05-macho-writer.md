# Phase 5: Mach-O Object File Writer

## Purpose

Produce valid Mach-O relocatable object files (`.o`) for macOS, supporting both x86_64 and
AArch64 (arm64) architectures. Mach-O is structurally different from ELF — it uses "load
commands" instead of section headers, and has different relocation encoding.

## File

```
src/codegen/common/objfile/MachOWriter.hpp
src/codegen/common/objfile/MachOWriter.cpp
```

---

## 1. Mach-O Format Overview

```
+----------------------------------+  offset 0
| Mach-O Header (32 bytes)         |
+----------------------------------+  offset 32
| Load Command: LC_SEGMENT_64      |  (contains section headers)
|   Section: __TEXT,__text          |
|   Section: __TEXT,__const         |
+----------------------------------+
| Load Command: LC_BUILD_VERSION   |  (minimum OS version)
+----------------------------------+
| Load Command: LC_SYMTAB          |  (symbol table location)
+----------------------------------+
| Load Command: LC_DYSYMTAB        |  (dynamic symbol table info)
+----------------------------------+
| Section Data: __text             |  (machine code)
+----------------------------------+
| Section Data: __const            |  (read-only data)
+----------------------------------+
| Relocation Entries               |  (for __text, then __const)
+----------------------------------+
| Symbol Table (nlist_64 entries)  |
+----------------------------------+
| String Table                     |
+----------------------------------+
```

---

## 2. Mach-O Header (mach_header_64 — 32 bytes)

```
Offset  Size  Field          Value
0       4     magic          0xFEEDFACF (MH_MAGIC_64)
4       4     cputype        0x01000007 (CPU_TYPE_X86_64) or 0x0100000C (CPU_TYPE_ARM64)
8       4     cpusubtype     0x00000003 (CPU_SUBTYPE_ALL) or 0x00000000 (ARM64_ALL)
12      4     filetype       0x00000001 (MH_OBJECT)
16      4     ncmds          4 (LC_SEGMENT_64 + LC_BUILD_VERSION + LC_SYMTAB + LC_DYSYMTAB)
20      4     sizeofcmds     <sum of all load command sizes>
24      4     flags          0x00002000 (MH_SUBSECTIONS_VIA_SYMBOLS)
28      4     reserved       0
```

---

## 3. Load Commands

### LC_SEGMENT_64 (cmd=0x19)

For object files, there's a single unnamed segment containing all sections.

**Segment header (72 bytes):**

```
Offset  Size  Field          Value
0       4     cmd            0x19 (LC_SEGMENT_64)
4       4     cmdsize        72 + (2 × 80) = 232 (segment header + 2 section headers)
8       16    segname        "" (16 zero bytes — unnamed for .o files)
24      8     vmaddr         0
32      8     vmsize         <total section data size>
40      8     fileoff        <offset to first section data>
48      8     filesize       <total section data size>
56      4     maxprot        0x7 (rwx)
60      4     initprot       0x7 (rwx)
64      4     nsects         2 (__text + __const)
68      4     flags          0
```

**Section header (80 bytes each) — within LC_SEGMENT_64:**

```
Offset  Size  Field          Description
0       16    sectname       "__text" (padded with zeros to 16 bytes)
16      16    segname        "__TEXT" (padded with zeros)
32      8     addr           0
40      8     size           Section data size
48      4     offset         File offset to section data
52      4     align          4 (= 2^4 = 16 for __text) or 3 (= 2^3 = 8 for __const)
56      4     reloff         File offset to relocation entries (0 if none)
60      4     nreloc         Number of relocation entries
64      4     flags          0x80000400 for __text (S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS)
                             0x00000000 for __const (S_REGULAR)
68      4     reserved1      0
72      4     reserved2      0 (or 16 for __text stub_size on some systems)
76      4     reserved3      0
```

**Section names:**
- Code: `__text` in segment `__TEXT`
- Read-only data: `__const` in segment `__TEXT` (note: `__TEXT,__const`, not `__DATA,__const`)

### LC_BUILD_VERSION (cmd=0x32)

Required on modern macOS (≥10.14). Without this, `ld64` may reject the object file.

```
Offset  Size  Field          Value
0       4     cmd            0x32 (LC_BUILD_VERSION)
4       4     cmdsize        24
8       4     platform       1 (PLATFORM_MACOS)
12      4     minos          0x000E0000 (14.0.0 — macOS 14, can adjust)
16      4     sdk            0x000F0000 (15.0.0 — or current SDK version)
20      4     ntools         0
```

### LC_SYMTAB (cmd=0x02)

```
Offset  Size  Field          Value
0       4     cmd            0x02 (LC_SYMTAB)
4       4     cmdsize        24
8       4     symoff         File offset to symbol table (nlist_64 array)
12      4     nsyms          Number of symbols
16      4     stroff         File offset to string table
20      4     strsize        String table size in bytes
```

### LC_DYSYMTAB (cmd=0x0B)

Dynamic symbol table metadata. For static object files, most fields are 0, but `ld64`
still expects this load command.

```
Offset  Size  Field          Value
0       4     cmd            0x0B (LC_DYSYMTAB)
4       4     cmdsize        80
8       4     ilocalsym      0 (index of first local symbol)
12      4     nlocalsym      <count of local symbols>
16      4     iextdefsym     <index of first external defined symbol>
20      4     nextdefsym     <count of external defined symbols>
24      4     iundefsym      <index of first undefined symbol>
28      4     nundefsym      <count of undefined symbols>
32-76   44    (remaining)    All zeros (no indirect symbols, etc.)
```

---

## 4. Symbol Table (nlist_64 — 16 bytes each)

```
Offset  Size  Field          Description
0       4     n_strx         Offset into string table
4       1     n_type         Type flags
5       1     n_sect         Section ordinal (1-based) or NO_SECT (0)
6       2     n_desc          0 (or REFERENCE_FLAG_UNDEFINED_NON_LAZY)
8       8     n_value        Value (offset within section for defined symbols)
```

### n_type Field

| Bits | Meaning | Values |
|------|---------|--------|
| 7-5 | N_STAB | 0 (not a debug symbol) |
| 4 | N_PEXT | 0 (not private extern) |
| 3 | N_EXT | 1 for global/external, 0 for local |
| 2-0 | N_TYPE | 0xE (N_SECT) for defined, 0x0 (N_UNDF) for undefined |

**Common combinations:**
- Local defined: `n_type = 0x0E` (N_SECT, not external)
- Global defined: `n_type = 0x0F` (N_SECT | N_EXT)
- Undefined (external): `n_type = 0x01` (N_UNDF | N_EXT)

### Symbol Ordering

Mach-O requires symbols in this order:
1. Local symbols (both defined and section)
2. External defined symbols (globally visible functions)
3. Undefined symbols (imported from other objects)

This matches `LC_DYSYMTAB`'s `ilocalsym`/`iextdefsym`/`iundefsym` ranges.

### Symbol Name Mangling

**Critical:** Mach-O uses underscore-prefixed symbol names. The writer must prepend `_` to
all function/data symbols when writing the string table:
- `main` → `_main`
- `rt_print_i64` → `_rt_print_i64`

Local labels (block labels starting with `L` or `.L`) do NOT get underscore prefixes.

---

## 5. Relocation Entries (8 bytes each)

Mach-O uses a compact 8-byte relocation format (NOT the ELF RELA format with addend).

```
Offset  Size  Field        Description
0       4     r_address    Byte offset within the section
4       4     r_info       Packed: symbolnum[24] | pcrel[1] | length[2] | extern[1] | type[4]
```

### r_info Bit Layout (big-endian bit numbering for the packed uint32_t)

```
Bits 31-8:  r_symbolnum  (24 bits) — symbol table index (if r_extern=1) or section ordinal
Bit  7:     r_pcrel      (1 bit)   — 1 if PC-relative
Bits 6-5:   r_length     (2 bits)  — 0=byte, 1=word, 2=dword, 3=qword
Bit  4:     r_extern     (1 bit)   — 1 if symbolnum is symbol index, 0 if section ordinal
Bits 3-0:   r_type       (4 bits)  — relocation type
```

### RelocKind → Mach-O Relocation Type Mapping

#### x86_64

| RelocKind | r_type | r_pcrel | r_length | r_extern | Notes |
|-----------|--------|---------|----------|----------|-------|
| PCRel32 | 1 (X86_64_RELOC_SIGNED) | 1 | 2 (32-bit) | 1 | RIP-relative data ref |
| Branch32 | 2 (X86_64_RELOC_BRANCH) | 1 | 2 (32-bit) | 1 | CALL/JMP |
| Abs64 | 0 (X86_64_RELOC_UNSIGNED) | 0 | 3 (64-bit) | 1 | Absolute address |

#### AArch64 (arm64)

| RelocKind | r_type | r_pcrel | r_length | r_extern | Notes |
|-----------|--------|---------|----------|----------|-------|
| A64Call26 | 2 (ARM64_RELOC_BRANCH26) | 1 | 2 (32-bit) | 1 | BL |
| A64Jump26 | 2 (ARM64_RELOC_BRANCH26) | 1 | 2 (32-bit) | 1 | B |
| A64AdrpPage21 | 3 (ARM64_RELOC_PAGE21) | 1 | 2 (32-bit) | 1 | ADRP |
| A64AddPageOff12 | 4 (ARM64_RELOC_PAGEOFF12) | 0 | 2 (32-bit) | 1 | ADD page offset |
| A64LdSt64Off12 | 4 (ARM64_RELOC_PAGEOFF12) | 0 | 2 (32-bit) | 1 | LDR/STR page offset |
| A64CondBr19 | N/A — see note below | — | — | — | — |

**Note on A64CondBr19:** Mach-O does NOT define an `ARM64_RELOC_BRANCH19` type. In the
current Viper codebase, conditional branches (`b.cond`, `cbz`, `cbnz`) **always** target
internal block labels within the same function — they never require external relocations.
If a future need arises, the pattern would be: emit an unconditional `b` or `bl` (which
CAN be relocated) with a nearby trampoline. For now, the binary encoder resolves all
conditional branch offsets internally via `patch32LE()` and no Mach-O relocation is needed.

### Mach-O Relocation Addend

Mach-O does NOT have an explicit addend field (unlike ELF RELA). The addend is encoded in
the instruction bytes themselves:
- For branch instructions: the addend is the initial value of the branch offset field
- For ADRP/ADD: the addend is embedded in the immediate field
- The writer must ensure the instruction bytes contain the correct addend before writing

**Critical: x86_64 RIP-relative addend.** For `X86_64_RELOC_SIGNED`, the system assembler
embeds a -4 addend in the instruction's displacement field (the 4-byte placeholder at the
relocation offset). The Mach-O writer must ensure the instruction bytes contain this value:

```cpp
// At the relocation offset in the .text bytes, write the initial displacement value.
// For RIP-relative: write -4 (little-endian: FC FF FF FF)
// The linker adds the symbol's final address to this value.
writeLEI32(textBytes + relocOffset, -4);
```

For function calls (`X86_64_RELOC_BRANCH`), the same -4 convention applies.

For AArch64, most addends are 0 (branch to start of function, ADRP to page base). The
instruction's immediate field is left as 0, and the linker fills in the final offset.

---

## 6. Cross-Section Rodata References

Rodata labels (`L.str.0` on AArch64, `.LC_str_0` on x86_64) are **local labels** defined in
`__TEXT,__const`. When `.text` references these labels:

- **x86_64:** `RIP-relative` addressing generates an `X86_64_RELOC_SIGNED` relocation
  targeting the `__const` section (section ordinal, `r_extern=0`) with the label offset
  as the in-instruction addend.

- **AArch64:** `ADRP+ADD` pair generates `ARM64_RELOC_PAGE21` + `ARM64_RELOC_PAGEOFF12`
  relocations. For local references, these can target the section ordinal (`r_extern=0`)
  with appropriate addend, or a local symbol (`r_extern=1`).

Since both sections are within the same `__TEXT` segment in a Mach-O object file, the
linker resolves these as intra-segment fixups.

---

## 7. Mach-O Quirks and Requirements

1. **Underscore prefix:** All global symbols get `_` prefix. The writer handles this.

2. **MH_SUBSECTIONS_VIA_SYMBOLS flag:** Tells `ld64` that each symbol starts a new subsection,
   enabling dead-stripping of unused functions.

3. **LC_BUILD_VERSION:** Mandatory on macOS ≥ 10.14. Without it, the linker may emit warnings
   or refuse to link.

4. **Section alignment in header:** Encoded as log2 (e.g., `align=4` means 2^4 = 16-byte
   aligned).

5. **No `.note.GNU-stack` equivalent:** Mach-O doesn't need it; non-executable stack is the
   default.

6. **Relocation entry ordering:** Mach-O expects relocations in descending address order
   within each section (highest r_address first). Some linkers tolerate any order, but
   matching convention is safer.

7. **String table:** First byte is `\0` (null string at offset 0), then a space ` ` at offset 1
   (some tools expect this), then symbol names.

---

## 8. Writer Implementation

### Algorithm

```
1. Build string table:
   a. Add "" (null) at offset 0
   b. Add " " at offset 1 (Mach-O convention)
   c. For each symbol: mangle name (prepend _ if needed), add to string table

2. Build symbol table (nlist_64 entries):
   a. Local defined symbols first
   b. External defined symbols
   c. Undefined symbols
   d. Track ilocalsym, nlocalsym, iextdefsym, nextdefsym, iundefsym, nundefsym

3. Build relocation entries for __text:
   a. Map RelocKind → Mach-O type/pcrel/length/extern
   b. Pack r_info field
   c. Sort by r_address descending

4. Build relocation entries for __const (if any cross-section refs)

5. Compute file layout:
   a. Mach-O header (32 bytes)
   b. LC_SEGMENT_64 (72 + nsects×80 bytes)
   c. LC_BUILD_VERSION (24 bytes)
   d. LC_SYMTAB (24 bytes)
   e. LC_DYSYMTAB (80 bytes)
   f. __text data (aligned)
   g. __const data (aligned)
   h. Relocation entries
   i. Symbol table (nlist_64 array)
   j. String table

6. Write all structures in order

7. Patch file offsets in headers (symoff, stroff, reloff, etc.)
```

### Cross-Architecture Support

The writer parameterizes:
- `cputype` / `cpusubtype` in the header
- Relocation type mapping table (x86_64 vs arm64)
- `.text` alignment (16 for x86_64, 4 for arm64)

---

## 9. Verification

After writing a Mach-O .o file:
1. `otool -h output.o` — verify header
2. `otool -l output.o` — verify load commands and sections
3. `nm output.o` — verify symbols
4. `otool -rv output.o` — verify relocations
5. `objdump -d output.o` — disassemble and verify instructions
6. `cc output.o -o test -L... -l...` — link with system linker and run

---

## Estimated Line Counts

| Component | LOC |
|-----------|-----|
| Mach-O constants (magic, types, flags) | ~100 |
| Header writing | ~30 |
| Load command writing | ~80 |
| Section header writing | ~60 |
| Symbol table building + writing | ~80 |
| Relocation table building + writing | ~70 |
| String table with mangling | ~40 |
| File layout computation | ~50 |
| Top-level write() method | ~60 |
| **Total** | **~570** |

Plus tests (~200 LOC):
- Write minimal .o with single function, verify with otool
- Write .o with external calls, link and run
- Verify underscore mangling
- Compare against system-assembler output
