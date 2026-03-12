# Phase 1: Common Infrastructure

## Purpose

Shared types and utilities used by all binary encoders (x86_64, AArch64) and all object file
writers (ELF, Mach-O, PE/COFF). This phase establishes the foundation that everything else
builds on.

## Directory

```
src/codegen/common/objfile/
    Relocation.hpp          # Relocation types and structures
    CodeSection.hpp         # Code buffer with relocation + symbol tracking
    SymbolTable.hpp         # Symbol management (local, global, external)
    StringTable.hpp         # Interned string table for names
    ObjectFileWriter.hpp    # Abstract writer interface + factory
```

---

## 1. Relocation.hpp

Defines architecture-agnostic relocation kinds that each object file writer maps to
format-specific relocation codes.

### RelocKind Enum

```cpp
enum class RelocKind : uint8_t {
    // === x86_64 relocations ===
    PCRel32,          // 32-bit PC-relative (data references via RIP)
                      //   ELF:    R_X86_64_PC32 (2)
                      //   Mach-O: X86_64_RELOC_SIGNED (1)
                      //   COFF:   IMAGE_REL_AMD64_REL32 (4)

    Branch32,         // 32-bit PC-relative call/branch (external symbols)
                      //   ELF:    R_X86_64_PLT32 (4)
                      //   Mach-O: X86_64_RELOC_BRANCH (2)
                      //   COFF:   IMAGE_REL_AMD64_REL32 (4)

    Abs64,            // 64-bit absolute address
                      //   ELF:    R_X86_64_64 (1)
                      //   Mach-O: X86_64_RELOC_UNSIGNED (0)
                      //   COFF:   IMAGE_REL_AMD64_ADDR64 (1)

    // === AArch64 relocations ===
    A64Call26,        // BL 26-bit PC-relative
                      //   ELF:    R_AARCH64_CALL26 (283)
                      //   Mach-O: ARM64_RELOC_BRANCH26 (2)
                      //   COFF:   IMAGE_REL_ARM64_BRANCH26 (3)

    A64Jump26,        // B 26-bit PC-relative
                      //   ELF:    R_AARCH64_JUMP26 (282)
                      //   Mach-O: ARM64_RELOC_BRANCH26 (2)
                      //   COFF:   IMAGE_REL_ARM64_BRANCH26 (3)

    A64AdrpPage21,    // ADRP 21-bit page-relative
                      //   ELF:    R_AARCH64_ADR_PREL_PG_HI21 (275)
                      //   Mach-O: ARM64_RELOC_PAGE21 (3)
                      //   COFF:   IMAGE_REL_ARM64_PAGEBASE_REL21 (4)

    A64AddPageOff12,  // ADD 12-bit page offset (no carry)
                      //   ELF:    R_AARCH64_ADD_ABS_LO12_NC (277)
                      //   Mach-O: ARM64_RELOC_PAGEOFF12 (4)
                      //   COFF:   IMAGE_REL_ARM64_PAGEOFFSET_12A (6)

    A64LdSt64Off12,   // LDR/STR 12-bit page offset scaled by 8
                      //   ELF:    R_AARCH64_LDST64_ABS_LO12_NC (286)
                      //   Mach-O: ARM64_RELOC_PAGEOFF12 (4)
                      //   COFF:   IMAGE_REL_ARM64_PAGEOFFSET_12L (7)

    A64CondBr19,      // B.cond/CBZ/CBNZ 19-bit PC-relative
                      //   ELF:    R_AARCH64_CONDBR19 (280)
                      //   Mach-O: ARM64_RELOC_BRANCH26 (2) [reused]
                      //   COFF:   IMAGE_REL_ARM64_BRANCH19 (8)
};
```

### Relocation Struct

```cpp
struct Relocation {
    size_t offset;            // Byte offset in section where fixup applies
    RelocKind kind;           // Architecture-agnostic relocation type
    uint32_t symbolIndex;     // Index into SymbolTable
    int64_t addend;           // Addend value (ELF RELA style; Mach-O embeds in instruction)
};
```

### Design Notes

- The addend is stored explicitly (ELF RELA style). For Mach-O (which uses REL-style
  relocations with the addend embedded in the instruction bytes), the Mach-O writer reads
  the addend from the Relocation struct and patches it into the instruction bytes before
  writing.
- RelocKind is architecture-prefixed (`A64*` for AArch64) to prevent confusion since the
  same object file format serves both architectures.
- **x86_64 addend convention:** For `PCRel32` and `Branch32` relocations, the encoder
  stores `addend = -4`. This accounts for the CPU adding the displacement to the address
  of the byte AFTER the 4-byte displacement field. ELF writes this addend into
  `r_addend`; Mach-O embeds it in the instruction bytes (writing `-4` at the displacement).
- **AArch64 addend convention:** For `A64Call26`, `A64AdrpPage21`, etc., the addend is
  typically 0 (branch to start of function, ADRP to page base).

---

## 2. CodeSection.hpp

A growable byte buffer that accumulates machine code (or read-only data) while tracking
relocations and symbol definitions.

### Interface

```cpp
class CodeSection {
public:
    // === Byte emission ===
    size_t currentOffset() const;          // Current write position
    void emit8(uint8_t val);               // Append 1 byte
    void emit16LE(uint16_t val);           // Append 2 bytes, little-endian
    void emit32LE(uint32_t val);           // Append 4 bytes, little-endian
    void emit64LE(uint64_t val);           // Append 8 bytes, little-endian
    void emitBytes(const void* data, size_t len);  // Append arbitrary bytes
    void emitZeros(size_t count);          // Append N zero bytes (for alignment padding)

    // === Alignment ===
    void alignTo(size_t alignment);        // Pad with zeros to reach alignment boundary

    // === Relocation tracking ===
    void addRelocation(RelocKind kind, uint32_t symbolIndex, int64_t addend = 0);
    // Records relocation at currentOffset()

    // === Symbol management ===
    uint32_t defineSymbol(const std::string& name, SymbolBinding binding);
    // Defines symbol at currentOffset(), returns index

    uint32_t declareExternal(const std::string& name);
    // Declares an external (undefined) symbol, returns index

    uint32_t findOrDeclareSymbol(const std::string& name);
    // Returns existing index or declares external

    // === Patch (for resolved internal branches) ===
    void patch32LE(size_t offset, uint32_t val);  // Overwrite 4 bytes at offset
    void patch8(size_t offset, uint8_t val);       // Overwrite 1 byte at offset

    // === Accessors ===
    const std::vector<uint8_t>& bytes() const;
    const std::vector<Relocation>& relocations() const;
    const SymbolTable& symbols() const;

private:
    std::vector<uint8_t> bytes_;
    std::vector<Relocation> relocations_;
    SymbolTable symbols_;
};
```

### Design Notes

- **Little-endian only.** Both x86_64 and AArch64 (in the configurations we target) use
  little-endian byte order. No big-endian support needed.
- **Internal branch resolution.** For branches within the same function (block-to-block jumps),
  the encoder resolves offsets directly using `patch32LE()` after all blocks are emitted.
  Only external symbols and cross-section references generate Relocation entries.
- **Separate sections.** The caller creates one CodeSection for `.text` and one for `.rodata`.
  The object file writer receives both and serializes them into the appropriate format.

---

## 3. SymbolTable.hpp

Manages symbol entries: name, binding, section, offset.

### SymbolBinding Enum

```cpp
enum class SymbolBinding : uint8_t {
    Local,      // File-local symbol (static function, local label)
    Global,     // Globally visible defined symbol (exported function)
    External    // Undefined symbol (imported from another object/library)
};
```

### SymbolSection Enum

```cpp
enum class SymbolSection : uint8_t {
    Text,       // .text section
    Rodata,     // .rodata section
    Data,       // .data section (if needed)
    Undefined   // External symbol, not defined in this object
};
```

### Symbol Struct

```cpp
struct Symbol {
    std::string name;
    SymbolBinding binding;
    SymbolSection section;
    size_t offset;           // Byte offset within section (0 for External)
    size_t size;             // Size of symbol (0 if unknown)
};
```

### SymbolTable Class

```cpp
class SymbolTable {
public:
    uint32_t add(Symbol sym);                          // Add symbol, return index
    uint32_t findOrAdd(const std::string& name);       // Find by name or add as External
    const Symbol& at(uint32_t index) const;
    uint32_t count() const;

    // Iteration for object file writers
    using const_iterator = std::vector<Symbol>::const_iterator;
    const_iterator begin() const;
    const_iterator end() const;

private:
    std::vector<Symbol> symbols_;
    std::unordered_map<std::string, uint32_t> nameIndex_;  // name → index
};
```

### Design Notes

- Index 0 is reserved for "no symbol" / null entry (ELF requires this).
- The `nameIndex_` map enables O(1) lookup for `findOrDeclareSymbol()` in CodeSection.
- Symbol names stored without platform-specific mangling (underscore prefix for Mach-O is
  applied by the Mach-O writer during serialization).
- Symbol names are stored as the **mapped C-level names** (e.g., `rt_print_i64`, not
  `Viper.Console.PrintI64`). The encoder applies `RuntimeNameMap` and `MangleLink` before
  adding symbols to the table.

---

## 4. StringTable.hpp

An interned string table that accumulates null-terminated strings and returns offsets.
Used for section names (`.shstrtab` in ELF) and symbol names (`.strtab` in ELF, string
table in PE/COFF).

### Interface

```cpp
class StringTable {
public:
    StringTable();                          // Starts with a single NUL byte (offset 0 = empty string)
    uint32_t add(std::string_view str);     // Add string, return offset. Deduplicates.
    uint32_t find(std::string_view str) const; // Find existing, return offset or UINT32_MAX
    const std::vector<char>& data() const;  // Raw table bytes (NUL-separated strings)
    uint32_t size() const;                  // Total byte size

private:
    std::vector<char> data_;
    std::unordered_map<std::string, uint32_t> offsets_;  // string → offset
};
```

### Design Notes

- ELF convention: offset 0 is always the empty string (single NUL byte).
- PE/COFF convention: first 4 bytes of string table are a uint32_t size field. The COFF writer
  prepends this when serializing; StringTable itself doesn't include it.
- Deduplication via `offsets_` map prevents redundant strings.

---

## 5. ObjectFileWriter.hpp

Abstract interface for producing `.o` files from CodeSections.

### Interface

```cpp
/// Target architecture for the object file.
enum class ObjArch : uint8_t {
    X86_64,
    AArch64
};

/// Target object file format.
enum class ObjFormat : uint8_t {
    ELF,        // Linux
    MachO,      // macOS
    COFF        // Windows
};

/// Abstract base for object file writers.
class ObjectFileWriter {
public:
    virtual ~ObjectFileWriter() = default;

    /// Write a complete .o file to disk.
    /// @param path   Output file path.
    /// @param text   Machine code section (.text).
    /// @param rodata Read-only data section (.rodata). May be empty.
    /// @param err    Error output stream.
    /// @return true on success, false on failure.
    virtual bool write(const std::string& path,
                       const CodeSection& text,
                       const CodeSection& rodata,
                       std::ostream& err) = 0;
};

/// Factory: create the appropriate writer for the target.
std::unique_ptr<ObjectFileWriter> createObjectFileWriter(ObjFormat format, ObjArch arch);
```

### Design Notes

- The factory detects the host platform if `ObjFormat` is not explicitly specified.
- Each writer (ELF, Mach-O, COFF) is a separate class implementing this interface.
- The writer is responsible for:
  1. Serializing headers
  2. Mapping `RelocKind` → format-specific relocation type codes
  3. Applying symbol name mangling (e.g., underscore prefix for Mach-O)
  4. Writing section contents, symbol tables, string tables, and relocation entries
  5. Computing file offsets for all structures

---

## Estimated Line Counts

| File | LOC |
|------|-----|
| Relocation.hpp | ~80 |
| CodeSection.hpp + .cpp | ~200 |
| SymbolTable.hpp + .cpp | ~120 |
| StringTable.hpp + .cpp | ~80 |
| ObjectFileWriter.hpp | ~60 |
| **Total** | **~540** |

Plus unit tests (~200 LOC):
- StringTable: add, dedup, find, data layout
- SymbolTable: add, findOrAdd, index stability
- CodeSection: emit bytes, alignment, patch, relocation tracking

---

## CMake Integration

New files are added to the existing `viper_codegen_common` library in
`src/codegen/common/CMakeLists.txt`:

```cmake
add_library(viper_codegen_common STATIC
        Diagnostics.cpp
        LinkerSupport.cpp
        objfile/CodeSection.cpp
        objfile/SymbolTable.cpp
        objfile/StringTable.cpp
        objfile/ElfWriter.cpp       # Phase 4
        objfile/MachOWriter.cpp     # Phase 5
        objfile/CoffWriter.cpp      # Phase 6
)
```

No new sub-library needed — the objfile/ code naturally extends `viper_codegen_common`.

---

## Relationship to Existing Utilities

- **`StringInterner`** (`src/support/string_interner.hpp`): Exists for IL symbol interning.
  Our `StringTable` is different — it's a format-specific serializable string table for ELF
  `.strtab`/`.shstrtab` and COFF string tables. No conflict.

- **`RuntimeNameMap`** (`src/il/runtime/RuntimeNameMap.hpp`): Maps IL canonical names
  (e.g., `Viper.Console.PrintI64`) to C symbols (e.g., `rt_print_i64`). The binary encoder
  must apply this mapping before storing symbol names in the SymbolTable.

- **`MangleLink`** (`src/common/Mangle.hpp`): Used by x86_64 for qualified name mangling
  (`A.B.F` → `@a_b_f`). The binary encoder uses this for the same purpose.

- **`LabelUtil`** (`src/codegen/common/LabelUtil.hpp`): Provides `sanitizeLabel()` for
  converting IL names to assembly-safe labels. The binary encoder uses this to generate
  symbol names that match the text emitter's conventions.

---

## Dependencies

This phase has zero external dependencies — it's a new self-contained component under
`src/codegen/common/objfile/` using only standard C++ (vector, string, unordered_map).
It integrates into the existing `viper_codegen_common` CMake target. It will be consumed by:
- Phase 2 (x86_64 encoder)
- Phase 3 (AArch64 encoder)
- Phases 4-6 (object file writers)
