# Phase 4: ELF Object File Writer

## Purpose

Produce valid ELF relocatable object files (`.o`) for Linux, supporting both x86_64 and
AArch64 architectures. The writer takes CodeSection data (machine code bytes, relocations,
symbols) and serializes it into the ELF format that the system linker (`ld`) can consume.

## File

```
src/codegen/common/objfile/ElfWriter.hpp
src/codegen/common/objfile/ElfWriter.cpp
```

---

## 1. ELF Format Overview

An ELF relocatable object file (ET_REL) has this layout:

```
+---------------------------+  offset 0
| ELF Header (64 bytes)     |
+---------------------------+  offset 64
| .text section data        |  (machine code)
+---------------------------+
| .rodata section data      |  (string literals, float constants)
+---------------------------+
| .rela.text section data   |  (relocations for .text)
+---------------------------+
| .symtab section data      |  (symbol table)
+---------------------------+
| .strtab section data      |  (symbol name strings)
+---------------------------+
| .shstrtab section data    |  (section name strings)
+---------------------------+
| .note.GNU-stack (empty)   |  (non-executable stack marker)
+---------------------------+
| Section Header Table      |  (array of section headers)
+---------------------------+
```

All multi-byte fields are little-endian for both x86_64 and AArch64 Linux.

---

## 2. ELF Header (Elf64_Ehdr — 64 bytes)

```
Offset  Size  Field            Value
0       4     e_ident[EI_MAG]  0x7F 'E' 'L' 'F'
4       1     e_ident[EI_CLASS] 2 (ELFCLASS64)
5       1     e_ident[EI_DATA]  1 (ELFDATA2LSB — little-endian)
6       1     e_ident[EI_VERSION] 1 (EV_CURRENT)
7       1     e_ident[EI_OSABI]  0 (ELFOSABI_NONE / System V)
8       8     e_ident[EI_ABIVERSION..EI_PAD]  0 (padding)
16      2     e_type           1 (ET_REL — relocatable)
18      2     e_machine        62 (EM_X86_64) or 183 (EM_AARCH64)
20      4     e_version        1 (EV_CURRENT)
24      8     e_entry          0 (no entry point for .o)
32      8     e_phoff          0 (no program headers for .o)
40      8     e_shoff          <offset of section header table>
48      4     e_flags          0
52      2     e_ehsize         64
54      2     e_phentsize      0
56      2     e_phnum          0
58      2     e_shentsize      64 (sizeof Elf64_Shdr)
60      2     e_shnum          <number of sections>
62      2     e_shstrndx       <index of .shstrtab section>
```

---

## 3. Section Headers (Elf64_Shdr — 64 bytes each)

We need 8 sections:

| Index | Name | sh_type | sh_flags | Contents |
|-------|------|---------|----------|----------|
| 0 | (null) | SHT_NULL (0) | 0 | Required null entry |
| 1 | .text | SHT_PROGBITS (1) | SHF_ALLOC \| SHF_EXECINSTR (0x6) | Machine code |
| 2 | .rodata | SHT_PROGBITS (1) | SHF_ALLOC (0x2) | Read-only data |
| 3 | .rela.text | SHT_RELA (4) | SHF_INFO_LINK (0x40) | Relocations |
| 4 | .symtab | SHT_SYMTAB (2) | 0 | Symbol table |
| 5 | .strtab | SHT_STRTAB (3) | 0 | Symbol name strings |
| 6 | .shstrtab | SHT_STRTAB (3) | 0 | Section name strings |
| 7 | .note.GNU-stack | SHT_PROGBITS (1) | 0 | Empty (non-exec stack) |

### Section Header Fields (Elf64_Shdr)

```
Offset  Size  Field          Description
0       4     sh_name        Offset into .shstrtab
4       4     sh_type        Section type
8       8     sh_flags       Section flags
16      8     sh_addr        0 (relocatable)
24      8     sh_offset      File offset to section data
32      8     sh_size        Size of section data in bytes
40      4     sh_link        Depends on type (see below)
44      4     sh_info        Depends on type (see below)
48      8     sh_addralign   Alignment (power of 2)
56      8     sh_entsize     Entry size for fixed-size tables
```

### Link/Info Fields

- `.rela.text`: `sh_link` = index of `.symtab` (4), `sh_info` = index of `.text` (1)
- `.symtab`: `sh_link` = index of `.strtab` (5), `sh_info` = index of first global symbol

---

## 4. Symbol Table (Elf64_Sym — 24 bytes each)

```
Offset  Size  Field       Description
0       4     st_name     Offset into .strtab
4       1     st_info     Binding (high 4 bits) + Type (low 4 bits)
5       1     st_other    0 (visibility: STV_DEFAULT)
6       2     st_shndx    Section index (SHN_UNDEF=0 for external)
8       8     st_value    Offset within section
16      8     st_size     0 (or size of function)
```

### Binding/Type Constants

- `STB_LOCAL` = 0, `STB_GLOBAL` = 1
- `STT_NOTYPE` = 0, `STT_FUNC` = 2, `STT_SECTION` = 3

### Symbol Ordering

ELF requires all local symbols (STB_LOCAL) before global symbols (STB_GLOBAL).
The `.symtab` header's `sh_info` field = index of first non-local symbol.

### Required Symbols

1. Index 0: Null symbol (all zeros — ELF requirement)
2. Section symbols (one per section with content): `.text` (STT_SECTION), `.rodata` (STT_SECTION)
3. Local function labels (if any)
4. Global defined symbols (exported functions)
5. Undefined symbols (external references like `rt_print_i64`)

### Mapping from Our SymbolTable

| Our SymbolBinding | ELF Binding | ELF Type | Section |
|-------------------|-------------|----------|---------|
| Local | STB_LOCAL | STT_FUNC | .text or .rodata |
| Global | STB_GLOBAL | STT_FUNC | .text or .rodata |
| External | STB_GLOBAL | STT_NOTYPE | SHN_UNDEF (0) |

---

## 5. Relocation Entries (Elf64_Rela — 24 bytes each)

```
Offset  Size  Field       Description
0       8     r_offset    Byte offset in section where fixup applies
8       8     r_info      Symbol index (high 32 bits) + Type (low 32 bits)
16      8     r_addend    Signed addend
```

### RelocKind → ELF Relocation Type Mapping

#### x86_64

| RelocKind | ELF Type | Value | Notes |
|-----------|----------|-------|-------|
| PCRel32 | R_X86_64_PC32 | 2 | RIP-relative data reference |
| Branch32 | R_X86_64_PLT32 | 4 | Call/branch to external symbol |
| Abs64 | R_X86_64_64 | 1 | Absolute 64-bit |

#### AArch64

| RelocKind | ELF Type | Value | Notes |
|-----------|----------|-------|-------|
| A64Call26 | R_AARCH64_CALL26 | 283 | BL instruction |
| A64Jump26 | R_AARCH64_JUMP26 | 282 | B instruction |
| A64AdrpPage21 | R_AARCH64_ADR_PREL_PG_HI21 | 275 | ADRP |
| A64AddPageOff12 | R_AARCH64_ADD_ABS_LO12_NC | 277 | ADD page offset |
| A64LdSt64Off12 | R_AARCH64_LDST64_ABS_LO12_NC | 286 | LDR/STR scaled |
| A64CondBr19 | R_AARCH64_CONDBR19 | 280 | B.cond, CBZ, CBNZ |

### r_info Construction

```cpp
uint64_t r_info = ((uint64_t)symbolIndex << 32) | relocType;
```

### Cross-Section vs External Relocations

Two distinct relocation scenarios exist:

1. **External function calls** — `CALL rt_print_i64` generates a `Branch32` relocation
   targeting an undefined symbol. The linker resolves this at link time.

2. **Rodata references from .text** — `LEA .LC_str_0(%rip), %rdi` (x86_64) or
   `ADRP+ADD L.str.0` (AArch64) references a label defined in `.rodata`. These are
   **cross-section** relocations: the symbol is defined in this object file but in a
   different section. The relocation targets a section symbol for `.rodata` with an addend
   equal to the label's offset within the section.

For rodata labels that are **local** (e.g., `.LC_str_0`, `L.str.0`), the relocation
references the `.rodata` section symbol (STT_SECTION) rather than defining a named symbol
for each label. This matches system assembler behavior and reduces symbol table size.

### Addend Convention for x86_64 RIP-Relative References

For `R_X86_64_PC32` relocations (RIP-relative data references), the ELF addend must account
for the fact that the CPU adds the 32-bit displacement to the address of the byte AFTER the
displacement field (i.e., the start of the next instruction), not the address of the
displacement itself. The standard convention is:

```
r_addend = S + A - P
where:
  S = symbol value (address of target)
  A = addend from relocation entry
  P = address of the relocation (where the 4-byte displacement lives)
```

Our binary encoder emits `addend = -4` for all RIP-relative references. The linker computes:
`displacement = S + (-4) - P = S - P - 4`, which equals `S - (P + 4)`, exactly what the
CPU expects (it adds the displacement to the address after the 4-byte field).

For `R_X86_64_PLT32` (CALL rel32), the same -4 addend applies.

---

## 6. String Tables

### .shstrtab (Section Name Strings)

Fixed content (always the same):
```
\0 .text\0 .rodata\0 .rela.text\0 .symtab\0 .strtab\0 .shstrtab\0 .note.GNU-stack\0
```

Pre-computed offsets for `sh_name` fields.

### .strtab (Symbol Name Strings)

Built from our SymbolTable: add each symbol name to the StringTable, record the offset.

---

## 7. Alignment Rules

| Section | Alignment |
|---------|-----------|
| .text | 16 (x86_64), 4 (AArch64) |
| .rodata | 8 |
| .rela.text | 8 |
| .symtab | 8 |
| .strtab | 1 |
| .shstrtab | 1 |
| Section header table | 8 |

File offsets for sections must be aligned to their `sh_addralign`.

---

## 8. Writer Implementation

### Algorithm

```
1. Build .shstrtab (fixed content, pre-computed)
2. Build .strtab from symbol names
3. Build .symtab entries:
   a. Null symbol (index 0)
   b. Section symbols (.text, .rodata)
   c. Local defined symbols
   d. Global defined symbols
   e. Undefined (external) symbols
   f. Record sh_info = first global index
4. Build .rela.text entries from CodeSection relocations
5. Compute file layout:
   a. ELF header at offset 0 (64 bytes)
   b. .text at offset 64 (aligned)
   c. .rodata (aligned)
   d. .rela.text (aligned)
   e. .symtab (aligned)
   f. .strtab
   g. .shstrtab
   h. .note.GNU-stack (zero size)
   i. Section header table (aligned)
6. Write all sections in order
7. Write section header table
8. Patch ELF header with final e_shoff
```

### Cross-Architecture Support

The writer parameterizes:
- `e_machine`: EM_X86_64 (62) vs EM_AARCH64 (183)
- `.text` alignment: 16 vs 4
- Relocation type mapping table (x86_64 vs AArch64 RelocKind → ELF type)

Everything else is identical between architectures.

---

## 9. Verification

After writing an ELF .o file:
1. `readelf -h output.o` — verify header fields
2. `readelf -S output.o` — verify section headers
3. `readelf -s output.o` — verify symbol table
4. `readelf -r output.o` — verify relocations
5. `objdump -d output.o` — disassemble .text and compare against expected instructions
6. `ld -o test output.o libviper_rt_base.a ...` — link and run

---

## Estimated Line Counts

| Component | LOC |
|-----------|-----|
| ELF constants (types, flags, sizes) | ~80 |
| ELF header writing | ~40 |
| Section header writing | ~60 |
| Symbol table building + writing | ~80 |
| Relocation table building + writing | ~60 |
| String table integration | ~30 |
| File layout computation | ~50 |
| Top-level write() method | ~80 |
| **Total** | **~480** |

Plus tests (~200 LOC):
- Write minimal .o, verify with readelf
- Write .o with relocations, link with system linker, run
- Compare section contents against system-assembler-produced .o
