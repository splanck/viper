//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/ElfWriter.cpp
// Purpose: Serialize CodeSection data into a valid ELF relocatable object file.
// Key invariants:
//   - All multi-byte fields are little-endian
//   - File layout: ehdr | .text | .rodata | .rela.text | .symtab | .strtab |
//     .shstrtab | (section header table)
//   - Section offsets are aligned per sh_addralign
//   - Local symbols precede globals (sh_info = first global index)
// Ownership/Lifetime:
//   - Stateless between write() calls
// Links: codegen/common/objfile/ElfWriter.hpp
//        plans/04-elf-writer.md
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/ElfWriter.hpp"
#include "codegen/common/objfile/StringTable.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace viper::codegen::objfile
{

// =============================================================================
// ELF Constants
// =============================================================================

// ELF ident
static constexpr uint8_t kElfMagic[4] = {0x7F, 'E', 'L', 'F'};
static constexpr uint8_t kElfClass64 = 2;
static constexpr uint8_t kElfData2LSB = 1;
static constexpr uint8_t kEvCurrent = 1;

// ELF header fields
static constexpr uint16_t kEtRel = 1;
static constexpr uint16_t kEmX86_64 = 62;
static constexpr uint16_t kEmAarch64 = 183;
static constexpr uint16_t kEhSize = 64;
static constexpr uint16_t kShEntSize = 64;

// Section types
static constexpr uint32_t kShtNull = 0;
static constexpr uint32_t kShtProgbits = 1;
static constexpr uint32_t kShtSymtab = 2;
static constexpr uint32_t kShtStrtab = 3;
static constexpr uint32_t kShtRela = 4;

// Section flags
static constexpr uint64_t kShfAlloc = 0x2;
static constexpr uint64_t kShfExecinstr = 0x4;
static constexpr uint64_t kShfInfoLink = 0x40;

// Symbol binding/type
static constexpr uint8_t kStbLocal = 0;
static constexpr uint8_t kStbGlobal = 1;
static constexpr uint8_t kSttNotype = 0;
static constexpr uint8_t kSttFunc = 2;
static constexpr uint8_t kSttSection = 3;
static constexpr uint8_t kStvDefault = 0;
static constexpr uint16_t kShnUndef = 0;

// Relocation types — x86_64
static constexpr uint32_t kRX86_64_64 = 1;
static constexpr uint32_t kRX86_64_Pc32 = 2;
static constexpr uint32_t kRX86_64_Plt32 = 4;

// Relocation types — AArch64
static constexpr uint32_t kRAarch64_AdrPrelPgHi21 = 275;
static constexpr uint32_t kRAarch64_AddAbsLo12Nc = 277;
static constexpr uint32_t kRAarch64_CondBr19 = 280;
static constexpr uint32_t kRAarch64_Jump26 = 282;
static constexpr uint32_t kRAarch64_Call26 = 283;
static constexpr uint32_t kRAarch64_LdSt64AbsLo12Nc = 286;

// Section indices (fixed layout)
static constexpr uint16_t kSecNull = 0;
static constexpr uint16_t kSecText = 1;
static constexpr uint16_t kSecRodata = 2;
static constexpr uint16_t kSecRelaText = 3;
static constexpr uint16_t kSecSymtab = 4;
static constexpr uint16_t kSecStrtab = 5;
static constexpr uint16_t kSecShstrtab = 6;
static constexpr uint16_t kSecNoteGnuStack = 7;
static constexpr uint16_t kNumSections = 8;

// =============================================================================
// Helpers
// =============================================================================

/// Append a little-endian uint16_t to a byte vector.
static void appendLE16(std::vector<uint8_t> &out, uint16_t val)
{
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
}

/// Append a little-endian uint32_t to a byte vector.
static void appendLE32(std::vector<uint8_t> &out, uint32_t val)
{
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
    out.push_back(static_cast<uint8_t>(val >> 16));
    out.push_back(static_cast<uint8_t>(val >> 24));
}

/// Append a little-endian uint64_t to a byte vector.
static void appendLE64(std::vector<uint8_t> &out, uint64_t val)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(val >> (i * 8)));
}

/// Align a value up to the given alignment.
static size_t alignUp(size_t val, size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

/// Pad a byte vector to reach the given total size.
static void padTo(std::vector<uint8_t> &out, size_t target)
{
    if (out.size() < target)
        out.resize(target, 0);
}

/// Map RelocKind to ELF relocation type.
static uint32_t elfRelocType(RelocKind kind, ObjArch arch)
{
    switch (kind)
    {
    // x86_64
    case RelocKind::PCRel32:
        return kRX86_64_Pc32;
    case RelocKind::Branch32:
        return kRX86_64_Plt32;
    case RelocKind::Abs64:
        return kRX86_64_64;
    // AArch64
    case RelocKind::A64Call26:
        return kRAarch64_Call26;
    case RelocKind::A64Jump26:
        return kRAarch64_Jump26;
    case RelocKind::A64AdrpPage21:
        return kRAarch64_AdrPrelPgHi21;
    case RelocKind::A64AddPageOff12:
        return kRAarch64_AddAbsLo12Nc;
    case RelocKind::A64LdSt64Off12:
        return kRAarch64_LdSt64AbsLo12Nc;
    case RelocKind::A64CondBr19:
        return kRAarch64_CondBr19;
    }
    (void)arch;
    return 0;
}

// =============================================================================
// ELF Structs written to the output buffer
// =============================================================================

/// Write an Elf64_Ehdr (64 bytes).
static void writeEhdr(std::vector<uint8_t> &out, uint16_t machine,
                       uint64_t shoff, uint16_t shnum, uint16_t shstrndx)
{
    // e_ident
    out.insert(out.end(), kElfMagic, kElfMagic + 4);
    out.push_back(kElfClass64);
    out.push_back(kElfData2LSB);
    out.push_back(kEvCurrent);
    out.push_back(0); // OSABI: ELFOSABI_NONE
    out.resize(out.size() + 8, 0); // padding to byte 16

    appendLE16(out, kEtRel);     // e_type
    appendLE16(out, machine);    // e_machine
    appendLE32(out, 1);          // e_version
    appendLE64(out, 0);          // e_entry
    appendLE64(out, 0);          // e_phoff
    appendLE64(out, shoff);      // e_shoff
    appendLE32(out, 0);          // e_flags
    appendLE16(out, kEhSize);    // e_ehsize
    appendLE16(out, 0);          // e_phentsize
    appendLE16(out, 0);          // e_phnum
    appendLE16(out, kShEntSize); // e_shentsize
    appendLE16(out, shnum);      // e_shnum
    appendLE16(out, shstrndx);   // e_shstrndx
}

/// Write an Elf64_Shdr (64 bytes).
static void writeShdr(std::vector<uint8_t> &out,
                      uint32_t name, uint32_t type, uint64_t flags,
                      uint64_t offset, uint64_t size,
                      uint32_t link, uint32_t info,
                      uint64_t addralign, uint64_t entsize)
{
    appendLE32(out, name);
    appendLE32(out, type);
    appendLE64(out, flags);
    appendLE64(out, 0); // sh_addr
    appendLE64(out, offset);
    appendLE64(out, size);
    appendLE32(out, link);
    appendLE32(out, info);
    appendLE64(out, addralign);
    appendLE64(out, entsize);
}

/// Write an Elf64_Sym (24 bytes).
static void writeSym(std::vector<uint8_t> &out,
                     uint32_t name, uint8_t info, uint8_t other,
                     uint16_t shndx, uint64_t value, uint64_t size)
{
    appendLE32(out, name);
    out.push_back(info);
    out.push_back(other);
    appendLE16(out, shndx);
    appendLE64(out, value);
    appendLE64(out, size);
}

/// Write an Elf64_Rela (24 bytes).
static void writeRela(std::vector<uint8_t> &out,
                      uint64_t offset, uint64_t info, int64_t addend)
{
    appendLE64(out, offset);
    appendLE64(out, info);
    appendLE64(out, static_cast<uint64_t>(addend));
}

// =============================================================================
// ElfWriter::write
// =============================================================================

bool ElfWriter::write(const std::string &path,
                      const CodeSection &text,
                      const CodeSection &rodata,
                      std::ostream &err)
{
    // --- 1. Build .shstrtab (section name string table) ---
    StringTable shstrtab;
    uint32_t shNameNull = 0; // empty string at offset 0
    uint32_t shNameText = shstrtab.add(".text");
    uint32_t shNameRodata = shstrtab.add(".rodata");
    uint32_t shNameRelaText = shstrtab.add(".rela.text");
    uint32_t shNameSymtab = shstrtab.add(".symtab");
    uint32_t shNameStrtab = shstrtab.add(".strtab");
    uint32_t shNameShstrtab = shstrtab.add(".shstrtab");
    uint32_t shNameNoteGnuStack = shstrtab.add(".note.GNU-stack");

    // --- 2. Build symbol table and .strtab ---
    // ELF requires: null sym, section syms (local), then globals, then externals.
    // We need to remap symbol indices from CodeSection's table to ELF indices.

    StringTable strtab;
    std::vector<uint8_t> symtabBytes;

    // Collect symbols from both text and rodata sections.
    // We need a unified symbol table.
    struct ElfSym
    {
        uint32_t strOffset;
        uint8_t info;
        uint16_t shndx;
        uint64_t value;
        uint64_t size;
        bool isLocal;
    };
    std::vector<ElfSym> locals, globals;

    // Symbol index 0: null symbol (always first).
    writeSym(symtabBytes, 0, 0, 0, 0, 0, 0);

    // Section symbols for .text and .rodata (local).
    // These are used as targets for cross-section relocations.
    uint32_t textSecSymIdx = 1; // ELF index of .text section symbol
    writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault,
             kSecText, 0, 0);

    uint32_t rodataSecSymIdx = 2;
    writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault,
             kSecRodata, 0, 0);

    uint32_t elfLocalCount = 3; // null + 2 section symbols

    // Map from CodeSection symbol index → ELF symbol index.
    // We process text symbols then rodata symbols.
    std::unordered_map<uint32_t, uint32_t> textSymMap;
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;

    // First pass: collect locals (defined symbols other than section symbols).
    // Second pass: collect globals and externals.
    // For simplicity, treat all defined symbols as globals (they're exported functions).

    // Process text section symbols.
    struct PendingSym
    {
        uint32_t origIdx;
        const Symbol *sym;
        uint16_t shndx;
        bool fromText; // true = text section, false = rodata
    };
    std::vector<PendingSym> pendingLocals, pendingGlobals;

    for (uint32_t i = 1; i < text.symbols().count(); ++i)
    {
        const Symbol &s = text.symbols().at(i);
        PendingSym ps{i, &s, 0, true};

        if (s.binding == SymbolBinding::External)
        {
            ps.shndx = kShnUndef;
            pendingGlobals.push_back(ps);
        }
        else if (s.binding == SymbolBinding::Local)
        {
            ps.shndx = kSecText;
            pendingLocals.push_back(ps);
        }
        else
        {
            // Global defined symbol.
            ps.shndx = kSecText;
            pendingGlobals.push_back(ps);
        }
    }

    // Process rodata section symbols.
    for (uint32_t i = 1; i < rodata.symbols().count(); ++i)
    {
        const Symbol &s = rodata.symbols().at(i);
        PendingSym ps{i, &s, 0, false};

        if (s.binding == SymbolBinding::External)
        {
            ps.shndx = kShnUndef;
            pendingGlobals.push_back(ps);
        }
        else if (s.binding == SymbolBinding::Local)
        {
            ps.shndx = kSecRodata;
            pendingLocals.push_back(ps);
        }
        else
        {
            ps.shndx = kSecRodata;
            pendingGlobals.push_back(ps);
        }
    }

    // Write local symbols first.
    for (const auto &ps : pendingLocals)
    {
        uint32_t nameOff = strtab.add(ps.sym->name);
        uint8_t type = (ps.sym->section == SymbolSection::Undefined) ? kSttNotype : kSttFunc;
        writeSym(symtabBytes, nameOff, (kStbLocal << 4) | type, kStvDefault,
                 ps.shndx, static_cast<uint64_t>(ps.sym->offset), 0);
        uint32_t elfIdx = elfLocalCount++;
        if (ps.fromText)
            textSymMap[ps.origIdx] = elfIdx;
        else
            rodataSymMap[ps.origIdx] = elfIdx;
    }

    // sh_info = first non-local symbol index.
    uint32_t firstGlobalIdx = elfLocalCount;

    // Write global/external symbols.
    uint32_t elfGlobalIdx = elfLocalCount;
    for (const auto &ps : pendingGlobals)
    {
        uint32_t nameOff = strtab.add(ps.sym->name);
        uint8_t type = (ps.sym->binding == SymbolBinding::External) ? kSttNotype : kSttFunc;
        uint16_t shndx = (ps.sym->binding == SymbolBinding::External) ? kShnUndef : ps.shndx;
        uint64_t value = (ps.sym->binding == SymbolBinding::External) ? 0 : static_cast<uint64_t>(ps.sym->offset);
        writeSym(symtabBytes, nameOff, (kStbGlobal << 4) | type, kStvDefault,
                 shndx, value, 0);
        if (ps.fromText)
            textSymMap[ps.origIdx] = elfGlobalIdx;
        else
            rodataSymMap[ps.origIdx] = elfGlobalIdx;
        ++elfGlobalIdx;
    }

    // --- 3. Build .rela.text ---
    std::vector<uint8_t> relaBytes;
    for (const auto &rel : text.relocations())
    {
        // Map symbol index to ELF index.
        uint32_t elfSymIdx = 0;
        auto it = textSymMap.find(rel.symbolIndex);
        if (it != textSymMap.end())
        {
            elfSymIdx = it->second;
        }
        else
        {
            // Symbol might be in rodata (cross-section reference).
            // For cross-section relocs, use the .rodata section symbol.
            auto rit = rodataSymMap.find(rel.symbolIndex);
            if (rit != rodataSymMap.end())
                elfSymIdx = rit->second;
            else
                elfSymIdx = textSecSymIdx; // fallback
        }

        uint32_t relocType = elfRelocType(rel.kind, arch_);
        uint64_t rInfo = (static_cast<uint64_t>(elfSymIdx) << 32) | relocType;
        writeRela(relaBytes, static_cast<uint64_t>(rel.offset), rInfo, rel.addend);
    }

    // --- 4. Compute file layout ---
    uint64_t textAlign = (arch_ == ObjArch::X86_64) ? 16 : 4;

    // Section data sizes
    uint64_t textSize = text.bytes().size();
    uint64_t rodataSize = rodata.bytes().size();
    uint64_t relaSize = relaBytes.size();
    uint64_t symtabSize = symtabBytes.size();
    uint64_t strtabSize = strtab.size();
    uint64_t shstrtabSize = shstrtab.size();

    // File offsets (sequential, aligned)
    uint64_t offText = alignUp(kEhSize, textAlign);
    uint64_t offRodata = alignUp(offText + textSize, 8);
    uint64_t offRelaText = alignUp(offRodata + rodataSize, 8);
    uint64_t offSymtab = alignUp(offRelaText + relaSize, 8);
    uint64_t offStrtab = offSymtab + symtabSize; // alignment 1
    uint64_t offShstrtab = offStrtab + strtabSize; // alignment 1
    // .note.GNU-stack has zero size, its offset doesn't matter but we place it next.
    uint64_t offNoteGnuStack = offShstrtab + shstrtabSize;
    uint64_t offShtab = alignUp(offNoteGnuStack, 8);

    // --- 5. Build the file ---
    std::vector<uint8_t> file;
    file.reserve(static_cast<size_t>(offShtab + kNumSections * kShEntSize));

    // ELF header (64 bytes)
    uint16_t machine = (arch_ == ObjArch::X86_64) ? kEmX86_64 : kEmAarch64;
    writeEhdr(file, machine, offShtab, kNumSections, kSecShstrtab);

    // .text
    padTo(file, static_cast<size_t>(offText));
    file.insert(file.end(), text.bytes().begin(), text.bytes().end());

    // .rodata
    padTo(file, static_cast<size_t>(offRodata));
    file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());

    // .rela.text
    padTo(file, static_cast<size_t>(offRelaText));
    file.insert(file.end(), relaBytes.begin(), relaBytes.end());

    // .symtab
    padTo(file, static_cast<size_t>(offSymtab));
    file.insert(file.end(), symtabBytes.begin(), symtabBytes.end());

    // .strtab
    padTo(file, static_cast<size_t>(offStrtab));
    {
        const auto &d = strtab.data();
        file.insert(file.end(), d.begin(), d.end());
    }

    // .shstrtab
    padTo(file, static_cast<size_t>(offShstrtab));
    {
        const auto &d = shstrtab.data();
        file.insert(file.end(), d.begin(), d.end());
    }

    // .note.GNU-stack (zero size, no data to append)

    // Section header table
    padTo(file, static_cast<size_t>(offShtab));

    // [0] Null section
    writeShdr(file, shNameNull, kShtNull, 0, 0, 0, 0, 0, 0, 0);

    // [1] .text
    writeShdr(file, shNameText, kShtProgbits, kShfAlloc | kShfExecinstr,
              offText, textSize, 0, 0, textAlign, 0);

    // [2] .rodata
    writeShdr(file, shNameRodata, kShtProgbits, kShfAlloc,
              offRodata, rodataSize, 0, 0, 8, 0);

    // [3] .rela.text
    writeShdr(file, shNameRelaText, kShtRela, kShfInfoLink,
              offRelaText, relaSize,
              kSecSymtab, kSecText, 8, 24);

    // [4] .symtab
    writeShdr(file, shNameSymtab, kShtSymtab, 0,
              offSymtab, symtabSize,
              kSecStrtab, firstGlobalIdx, 8, 24);

    // [5] .strtab
    writeShdr(file, shNameStrtab, kShtStrtab, 0,
              offStrtab, strtabSize, 0, 0, 1, 0);

    // [6] .shstrtab
    writeShdr(file, shNameShstrtab, kShtStrtab, 0,
              offShstrtab, shstrtabSize, 0, 0, 1, 0);

    // [7] .note.GNU-stack
    writeShdr(file, shNameNoteGnuStack, kShtProgbits, 0,
              offNoteGnuStack, 0, 0, 0, 1, 0);

    // --- 6. Write to disk ---
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        err << "ElfWriter: cannot open " << path << " for writing\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char *>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!ofs)
    {
        err << "ElfWriter: write failed for " << path << "\n";
        return false;
    }
    return true;
}

} // namespace viper::codegen::objfile
