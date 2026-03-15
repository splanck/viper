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
#include "codegen/common/objfile/ObjFileWriterUtil.hpp"
#include "codegen/common/objfile/StringTable.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
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
// kNumSections is computed dynamically in write() based on whether debug data is present.

// Helpers: appendLE16/32/64, alignUp, padTo are provided by ObjFileWriterUtil.hpp.

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
static void writeEhdr(
    std::vector<uint8_t> &out, uint16_t machine, uint64_t shoff, uint16_t shnum, uint16_t shstrndx)
{
    // e_ident
    out.insert(out.end(), kElfMagic, kElfMagic + 4);
    out.push_back(kElfClass64);
    out.push_back(kElfData2LSB);
    out.push_back(kEvCurrent);
    out.push_back(0);              // OSABI: ELFOSABI_NONE
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
                      uint32_t name,
                      uint32_t type,
                      uint64_t flags,
                      uint64_t offset,
                      uint64_t size,
                      uint32_t link,
                      uint32_t info,
                      uint64_t addralign,
                      uint64_t entsize)
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
                     uint32_t name,
                     uint8_t info,
                     uint8_t other,
                     uint16_t shndx,
                     uint64_t value,
                     uint64_t size)
{
    appendLE32(out, name);
    out.push_back(info);
    out.push_back(other);
    appendLE16(out, shndx);
    appendLE64(out, value);
    appendLE64(out, size);
}

/// Write an Elf64_Rela (24 bytes).
static void writeRela(std::vector<uint8_t> &out, uint64_t offset, uint64_t info, int64_t addend)
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

    const bool hasDebugLine = !debugLineData_.empty();
    uint32_t shNameDebugLine = 0;
    if (hasDebugLine)
        shNameDebugLine = shstrtab.add(".debug_line");

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
    writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault, kSecText, 0, 0);

    uint32_t rodataSecSymIdx = 2;
    writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault, kSecRodata, 0, 0);

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
        writeSym(symtabBytes,
                 nameOff,
                 (kStbLocal << 4) | type,
                 kStvDefault,
                 ps.shndx,
                 static_cast<uint64_t>(ps.sym->offset),
                 0);
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
        uint64_t value = (ps.sym->binding == SymbolBinding::External)
                             ? 0
                             : static_cast<uint64_t>(ps.sym->offset);
        writeSym(symtabBytes, nameOff, (kStbGlobal << 4) | type, kStvDefault, shndx, value, 0);
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
    uint64_t offStrtab = offSymtab + symtabSize;   // alignment 1
    uint64_t offShstrtab = offStrtab + strtabSize; // alignment 1
    // .note.GNU-stack has zero size, its offset doesn't matter but we place it next.
    uint64_t offNoteGnuStack = offShstrtab + shstrtabSize;
    // .debug_line (optional, non-alloc) — placed after .note.GNU-stack.
    uint64_t debugLineSize = debugLineData_.size();
    uint64_t offDebugLine = offNoteGnuStack;
    uint64_t offShtab = alignUp(offDebugLine + debugLineSize, 8);

    // --- 5. Build the file ---
    const uint16_t numSections = hasDebugLine ? 9 : 8;

    std::vector<uint8_t> file;
    file.reserve(static_cast<size_t>(offShtab + numSections * kShEntSize));

    // ELF header (64 bytes)
    uint16_t machine = (arch_ == ObjArch::X86_64) ? kEmX86_64 : kEmAarch64;
    writeEhdr(file, machine, offShtab, numSections, kSecShstrtab);

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

    // .debug_line (optional)
    if (hasDebugLine)
        file.insert(file.end(), debugLineData_.begin(), debugLineData_.end());

    // Section header table
    padTo(file, static_cast<size_t>(offShtab));

    // [0] Null section
    writeShdr(file, shNameNull, kShtNull, 0, 0, 0, 0, 0, 0, 0);

    // [1] .text
    writeShdr(file,
              shNameText,
              kShtProgbits,
              kShfAlloc | kShfExecinstr,
              offText,
              textSize,
              0,
              0,
              textAlign,
              0);

    // [2] .rodata
    writeShdr(file, shNameRodata, kShtProgbits, kShfAlloc, offRodata, rodataSize, 0, 0, 8, 0);

    // [3] .rela.text
    writeShdr(file,
              shNameRelaText,
              kShtRela,
              kShfInfoLink,
              offRelaText,
              relaSize,
              kSecSymtab,
              kSecText,
              8,
              24);

    // [4] .symtab
    writeShdr(file,
              shNameSymtab,
              kShtSymtab,
              0,
              offSymtab,
              symtabSize,
              kSecStrtab,
              firstGlobalIdx,
              8,
              24);

    // [5] .strtab
    writeShdr(file, shNameStrtab, kShtStrtab, 0, offStrtab, strtabSize, 0, 0, 1, 0);

    // [6] .shstrtab
    writeShdr(file, shNameShstrtab, kShtStrtab, 0, offShstrtab, shstrtabSize, 0, 0, 1, 0);

    // [7] .note.GNU-stack
    writeShdr(file, shNameNoteGnuStack, kShtProgbits, 0, offNoteGnuStack, 0, 0, 0, 1, 0);

    // [8] .debug_line (optional, non-alloc)
    if (hasDebugLine)
        writeShdr(file, shNameDebugLine, kShtProgbits, 0, offDebugLine, debugLineSize, 0, 0, 1, 0);

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

// =============================================================================
// ElfWriter::write (multi-section)
//
// Emits per-function .text.funcname sections to enable function-level dead
// stripping at link time.  For 0 or 1 text sections, delegates to the
// single-section write() to avoid unnecessary complexity.
// =============================================================================

bool ElfWriter::write(const std::string &path,
                      const std::vector<CodeSection> &textSections,
                      const CodeSection &rodata,
                      std::ostream &err)
{
    // For 0 or 1 text sections, delegate to single-section write.
    if (textSections.size() <= 1)
    {
        if (textSections.size() == 1)
            return write(path, textSections[0], rodata, err);
        CodeSection empty;
        return write(path, empty, rodata, err);
    }

    const bool hasDebugLine = !debugLineData_.empty();
    const size_t N = textSections.size();

    // --- 1. Extract function names from each text section ---
    std::vector<std::string> funcNames(N);
    for (size_t i = 0; i < N; ++i)
    {
        funcNames[i] = "func_" + std::to_string(i);
        for (uint32_t j = 1; j < textSections[i].symbols().count(); ++j)
        {
            const auto &s = textSections[i].symbols().at(j);
            if (s.binding == SymbolBinding::Global && s.section == SymbolSection::Text)
            {
                funcNames[i] = s.name;
                break;
            }
        }
    }

    // --- 2. Section index layout ---
    // [0] null
    // [1..N] .text.func1 ... .text.funcN
    // [N+1] .rodata
    // [N+2..2N+1] .rela.text.func1 ... .rela.text.funcN
    // [2N+2] .symtab
    // [2N+3] .strtab
    // [2N+4] .shstrtab
    // [2N+5] .note.GNU-stack
    auto secText = [](size_t i) -> uint16_t { return static_cast<uint16_t>(i + 1); };
    const uint16_t secRodata = static_cast<uint16_t>(N + 1);
    auto secRelaText = [&](size_t i) -> uint16_t { return static_cast<uint16_t>(N + 2 + i); };
    const uint16_t secSymtab = static_cast<uint16_t>(2 * N + 2);
    const uint16_t secStrtab = static_cast<uint16_t>(2 * N + 3);
    const uint16_t secShstrtab = static_cast<uint16_t>(2 * N + 4);
    const uint16_t secNoteGnuStack = static_cast<uint16_t>(2 * N + 5);
    const uint16_t numSections = static_cast<uint16_t>(2 * N + 6 + (hasDebugLine ? 1 : 0));

    // --- 3. Build .shstrtab ---
    StringTable shstrtab;

    std::vector<uint32_t> shNameText(N);
    for (size_t i = 0; i < N; ++i)
        shNameText[i] = shstrtab.add(".text." + funcNames[i]);

    uint32_t shNameRodata = shstrtab.add(".rodata");

    std::vector<uint32_t> shNameRelaText(N);
    for (size_t i = 0; i < N; ++i)
        shNameRelaText[i] = shstrtab.add(".rela.text." + funcNames[i]);

    uint32_t shNameSymtab = shstrtab.add(".symtab");
    uint32_t shNameStrtab = shstrtab.add(".strtab");
    uint32_t shNameShstrtab = shstrtab.add(".shstrtab");
    uint32_t shNameNoteGnuStack = shstrtab.add(".note.GNU-stack");

    uint32_t shNameDebugLine = 0;
    if (hasDebugLine)
        shNameDebugLine = shstrtab.add(".debug_line");

    // --- 4. Build symbol table ---
    StringTable strtab;
    std::vector<uint8_t> symtabBytes;

    // [0] null symbol
    writeSym(symtabBytes, 0, 0, 0, 0, 0, 0);

    // Section symbols for each text section
    for (size_t i = 0; i < N; ++i)
        writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault, secText(i), 0, 0);

    // Section symbol for .rodata
    writeSym(symtabBytes, 0, (kStbLocal << 4) | kSttSection, kStvDefault, secRodata, 0, 0);

    // null + N text section syms + 1 rodata section sym
    uint32_t elfLocalCount = static_cast<uint32_t>(N + 2);

    // Per-text-section maps: CodeSection sym idx → unified ELF sym idx
    std::vector<std::unordered_map<uint32_t, uint32_t>> textSymMaps(N);
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;

    struct PendingSym
    {
        uint32_t origIdx;
        const Symbol *sym;
        uint16_t shndx;
        size_t textIdx; // index into textSections; SIZE_MAX for rodata
    };

    std::vector<PendingSym> pendingLocals, pendingDefinedGlobals, pendingExternals;

    // Collect symbols from all text sections.
    for (size_t ti = 0; ti < N; ++ti)
    {
        const auto &sec = textSections[ti];
        for (uint32_t i = 1; i < sec.symbols().count(); ++i)
        {
            const auto &s = sec.symbols().at(i);
            PendingSym ps{i, &s, 0, ti};

            if (s.binding == SymbolBinding::External)
            {
                ps.shndx = kShnUndef;
                pendingExternals.push_back(ps);
            }
            else if (s.binding == SymbolBinding::Local)
            {
                ps.shndx = secText(ti);
                pendingLocals.push_back(ps);
            }
            else
            {
                ps.shndx = secText(ti);
                pendingDefinedGlobals.push_back(ps);
            }
        }
    }

    // Collect rodata symbols.
    for (uint32_t i = 1; i < rodata.symbols().count(); ++i)
    {
        const auto &s = rodata.symbols().at(i);
        PendingSym ps{i, &s, 0, SIZE_MAX};

        if (s.binding == SymbolBinding::External)
        {
            ps.shndx = kShnUndef;
            pendingExternals.push_back(ps);
        }
        else if (s.binding == SymbolBinding::Local)
        {
            ps.shndx = secRodata;
            pendingLocals.push_back(ps);
        }
        else
        {
            ps.shndx = secRodata;
            pendingDefinedGlobals.push_back(ps);
        }
    }

    // Write local symbols first (ELF requires locals before globals).
    for (const auto &ps : pendingLocals)
    {
        uint32_t nameOff = strtab.add(ps.sym->name);
        uint8_t type = (ps.sym->section == SymbolSection::Undefined) ? kSttNotype : kSttFunc;
        writeSym(symtabBytes,
                 nameOff,
                 (kStbLocal << 4) | type,
                 kStvDefault,
                 ps.shndx,
                 static_cast<uint64_t>(ps.sym->offset),
                 0);
        uint32_t elfIdx = elfLocalCount++;
        if (ps.textIdx != SIZE_MAX)
            textSymMaps[ps.textIdx][ps.origIdx] = elfIdx;
        else
            rodataSymMap[ps.origIdx] = elfIdx;
    }

    uint32_t firstGlobalIdx = elfLocalCount;

    // Write defined global symbols first (so defined takes priority in dedup map).
    std::unordered_map<std::string, uint32_t> globalNameMap;
    uint32_t elfGlobalIdx = elfLocalCount;

    for (const auto &ps : pendingDefinedGlobals)
    {
        auto it = globalNameMap.find(ps.sym->name);
        if (it != globalNameMap.end())
        {
            if (ps.textIdx != SIZE_MAX)
                textSymMaps[ps.textIdx][ps.origIdx] = it->second;
            else
                rodataSymMap[ps.origIdx] = it->second;
            continue;
        }
        uint32_t nameOff = strtab.add(ps.sym->name);
        writeSym(symtabBytes,
                 nameOff,
                 (kStbGlobal << 4) | kSttFunc,
                 kStvDefault,
                 ps.shndx,
                 static_cast<uint64_t>(ps.sym->offset),
                 0);
        globalNameMap[ps.sym->name] = elfGlobalIdx;
        if (ps.textIdx != SIZE_MAX)
            textSymMaps[ps.textIdx][ps.origIdx] = elfGlobalIdx;
        else
            rodataSymMap[ps.origIdx] = elfGlobalIdx;
        ++elfGlobalIdx;
    }

    // Then write external (undefined) symbols (reuse if name already present).
    for (const auto &ps : pendingExternals)
    {
        auto it = globalNameMap.find(ps.sym->name);
        if (it != globalNameMap.end())
        {
            if (ps.textIdx != SIZE_MAX)
                textSymMaps[ps.textIdx][ps.origIdx] = it->second;
            else
                rodataSymMap[ps.origIdx] = it->second;
            continue;
        }
        uint32_t nameOff = strtab.add(ps.sym->name);
        writeSym(
            symtabBytes, nameOff, (kStbGlobal << 4) | kSttNotype, kStvDefault, kShnUndef, 0, 0);
        globalNameMap[ps.sym->name] = elfGlobalIdx;
        if (ps.textIdx != SIZE_MAX)
            textSymMaps[ps.textIdx][ps.origIdx] = elfGlobalIdx;
        else
            rodataSymMap[ps.origIdx] = elfGlobalIdx;
        ++elfGlobalIdx;
    }

    // --- 5. Build .rela.text.* entries ---
    std::vector<std::vector<uint8_t>> allRelaBytes(N);
    for (size_t ti = 0; ti < N; ++ti)
    {
        for (const auto &rel : textSections[ti].relocations())
        {
            uint32_t elfSymIdx = 0;
            auto it = textSymMaps[ti].find(rel.symbolIndex);
            if (it != textSymMaps[ti].end())
            {
                elfSymIdx = it->second;
            }
            else
            {
                // Fallback: check rodata map (cross-section reference).
                auto rit = rodataSymMap.find(rel.symbolIndex);
                if (rit != rodataSymMap.end())
                    elfSymIdx = rit->second;
                else
                    elfSymIdx = static_cast<uint32_t>(ti + 1); // section sym
            }
            uint32_t relocType = elfRelocType(rel.kind, arch_);
            uint64_t rInfo = (static_cast<uint64_t>(elfSymIdx) << 32) | relocType;
            writeRela(allRelaBytes[ti], static_cast<uint64_t>(rel.offset), rInfo, rel.addend);
        }
    }

    // --- 6. Compute file layout ---
    uint64_t textAlign = (arch_ == ObjArch::X86_64) ? 16 : 4;

    // Text section offsets.
    std::vector<uint64_t> textOffsets(N);
    std::vector<uint64_t> textSizes(N);
    uint64_t cursor = kEhSize;
    for (size_t i = 0; i < N; ++i)
    {
        textSizes[i] = textSections[i].bytes().size();
        textOffsets[i] = alignUp(cursor, textAlign);
        cursor = textOffsets[i] + textSizes[i];
    }

    uint64_t rodataSize = rodata.bytes().size();
    uint64_t offRodata = alignUp(cursor, 8);
    cursor = offRodata + rodataSize;

    // .rela.text.* offsets (always allocated, even if empty).
    std::vector<uint64_t> relaOffsets(N);
    std::vector<uint64_t> relaSizes(N);
    for (size_t i = 0; i < N; ++i)
    {
        relaSizes[i] = allRelaBytes[i].size();
        relaOffsets[i] = alignUp(cursor, 8);
        cursor = relaOffsets[i] + relaSizes[i];
    }

    uint64_t symtabSize = symtabBytes.size();
    uint64_t offSymtab = alignUp(cursor, 8);
    cursor = offSymtab + symtabSize;

    uint64_t strtabSize = strtab.size();
    uint64_t offStrtab = cursor;
    cursor = offStrtab + strtabSize;

    uint64_t shstrtabSize = shstrtab.size();
    uint64_t offShstrtab = cursor;
    cursor = offShstrtab + shstrtabSize;

    uint64_t offNoteGnuStack = cursor;
    uint64_t debugLineSize = debugLineData_.size();
    uint64_t offDebugLine = offNoteGnuStack;
    uint64_t offShtab = alignUp(offDebugLine + debugLineSize, 8);

    // --- 7. Build the file ---
    std::vector<uint8_t> file;
    file.reserve(static_cast<size_t>(offShtab + numSections * kShEntSize));

    uint16_t machine = (arch_ == ObjArch::X86_64) ? kEmX86_64 : kEmAarch64;
    writeEhdr(file, machine, offShtab, numSections, secShstrtab);

    // .text.* section data
    for (size_t i = 0; i < N; ++i)
    {
        padTo(file, static_cast<size_t>(textOffsets[i]));
        file.insert(file.end(), textSections[i].bytes().begin(), textSections[i].bytes().end());
    }

    // .rodata
    padTo(file, static_cast<size_t>(offRodata));
    file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());

    // .rela.text.* data
    for (size_t i = 0; i < N; ++i)
    {
        if (!allRelaBytes[i].empty())
        {
            padTo(file, static_cast<size_t>(relaOffsets[i]));
            file.insert(file.end(), allRelaBytes[i].begin(), allRelaBytes[i].end());
        }
    }

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

    // .note.GNU-stack (zero size — no data to append)

    // .debug_line (optional)
    if (hasDebugLine)
        file.insert(file.end(), debugLineData_.begin(), debugLineData_.end());

    // --- 8. Section header table ---
    padTo(file, static_cast<size_t>(offShtab));

    // [0] Null section
    writeShdr(file, 0, kShtNull, 0, 0, 0, 0, 0, 0, 0);

    // [1..N] .text.funcname sections
    for (size_t i = 0; i < N; ++i)
    {
        writeShdr(file,
                  shNameText[i],
                  kShtProgbits,
                  kShfAlloc | kShfExecinstr,
                  textOffsets[i],
                  textSizes[i],
                  0,
                  0,
                  textAlign,
                  0);
    }

    // [N+1] .rodata
    writeShdr(file, shNameRodata, kShtProgbits, kShfAlloc, offRodata, rodataSize, 0, 0, 8, 0);

    // [N+2..2N+1] .rela.text.funcname sections
    for (size_t i = 0; i < N; ++i)
    {
        writeShdr(file,
                  shNameRelaText[i],
                  kShtRela,
                  kShfInfoLink,
                  relaOffsets[i],
                  relaSizes[i],
                  secSymtab,
                  secText(i),
                  8,
                  24);
    }

    // .symtab
    writeShdr(
        file, shNameSymtab, kShtSymtab, 0, offSymtab, symtabSize, secStrtab, firstGlobalIdx, 8, 24);

    // .strtab
    writeShdr(file, shNameStrtab, kShtStrtab, 0, offStrtab, strtabSize, 0, 0, 1, 0);

    // .shstrtab
    writeShdr(file, shNameShstrtab, kShtStrtab, 0, offShstrtab, shstrtabSize, 0, 0, 1, 0);

    // .note.GNU-stack
    writeShdr(file, shNameNoteGnuStack, kShtProgbits, 0, offNoteGnuStack, 0, 0, 0, 1, 0);

    // .debug_line (optional, non-alloc)
    if (hasDebugLine)
        writeShdr(file, shNameDebugLine, kShtProgbits, 0, offDebugLine, debugLineSize, 0, 0, 1, 0);

    // --- 9. Write to disk ---
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
