//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/MachOWriter.cpp
// Purpose: Serialize CodeSection data into a valid Mach-O relocatable object
//          file for macOS (x86_64 and AArch64/arm64).
// Key invariants:
//   - File layout: header | load commands | __text | __const | relocs |
//     symtab | strtab
//   - Symbols ordered: locals, external defined, undefined (matches LC_DYSYMTAB)
//   - Darwin underscore prefix applied to all non-local-label symbols
//   - Relocations packed into 8-byte entries, sorted by address descending
//   - x86_64 PC-relative addends (-4) patched into instruction bytes
// Ownership/Lifetime:
//   - Stateless between write() calls
// Links: codegen/common/objfile/MachOWriter.hpp
//        plans/05-macho-writer.md
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/MachOWriter.hpp"
#include "codegen/common/objfile/StringTable.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace viper::codegen::objfile
{

// =============================================================================
// Mach-O Constants
// =============================================================================

// Header
static constexpr uint32_t kMhMagic64 = 0xFEEDFACF;
static constexpr uint32_t kCpuTypeX86_64 = 0x01000007;
static constexpr uint32_t kCpuTypeArm64 = 0x0100000C;
static constexpr uint32_t kCpuSubtypeX86_64All = 3;
static constexpr uint32_t kCpuSubtypeArm64All = 0;
static constexpr uint32_t kMhObject = 1;
static constexpr uint32_t kMhSubsectionsViaSymbols = 0x00002000;

// Load command types
static constexpr uint32_t kLcSegment64 = 0x19;
static constexpr uint32_t kLcSymtab = 0x02;
static constexpr uint32_t kLcDysymtab = 0x0B;
static constexpr uint32_t kLcBuildVersion = 0x32;

// Section flags
static constexpr uint32_t kSAttrPureInstructions = 0x80000000;
static constexpr uint32_t kSAttrSomeInstructions = 0x00000400;

// Relocation types — x86_64
static constexpr uint32_t kX86_64RelocUnsigned = 0;
static constexpr uint32_t kX86_64RelocSigned = 1;
static constexpr uint32_t kX86_64RelocBranch = 2;

// Relocation types — AArch64
static constexpr uint32_t kArm64RelocBranch26 = 2;
static constexpr uint32_t kArm64RelocPage21 = 3;
static constexpr uint32_t kArm64RelocPageoff12 = 4;

// nlist_64 type field
static constexpr uint8_t kNUndf = 0x00;
static constexpr uint8_t kNSect = 0x0E;
static constexpr uint8_t kNExt = 0x01;

// Section ordinals (1-based in Mach-O)
static constexpr uint8_t kSectText = 1;
static constexpr uint8_t kSectConst = 2;
static constexpr uint8_t kNoSect = 0;

// Load command sizes (fixed)
static constexpr uint32_t kSegCmdSize = 72 + 2 * 80; // segment header + 2 section headers
static constexpr uint32_t kBuildVerCmdSize = 24;
static constexpr uint32_t kSymtabCmdSize = 24;
static constexpr uint32_t kDysymtabCmdSize = 80;
static constexpr uint32_t kHeaderSize = 32;
static constexpr uint32_t kNlistSize = 16;
static constexpr uint32_t kRelocSize = 8;

// =============================================================================
// Helpers
// =============================================================================

static void appendLE16(std::vector<uint8_t> &out, uint16_t val)
{
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
}

static void appendLE32(std::vector<uint8_t> &out, uint32_t val)
{
    out.push_back(static_cast<uint8_t>(val));
    out.push_back(static_cast<uint8_t>(val >> 8));
    out.push_back(static_cast<uint8_t>(val >> 16));
    out.push_back(static_cast<uint8_t>(val >> 24));
}

static void appendLE64(std::vector<uint8_t> &out, uint64_t val)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(val >> (i * 8)));
}

static size_t alignUp(size_t val, size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

static void padTo(std::vector<uint8_t> &out, size_t target)
{
    if (out.size() < target)
        out.resize(target, 0);
}

/// Mangle a symbol name for Darwin: prepend '_' unless it's a local label.
static std::string mangleName(const std::string &name)
{
    if (name.empty()) return name;
    if (name[0] == 'L' || name[0] == '.') return name; // local label
    return "_" + name;
}

/// Pack a Mach-O relocation info field (little-endian bit-field layout).
/// Layout: symbolnum[23:0] | pcrel[24] | length[26:25] | extern[27] | type[31:28]
static uint32_t packRelocInfo(uint32_t symbolnum, uint8_t pcrel,
                               uint8_t length, uint8_t ext, uint8_t type)
{
    return (symbolnum & 0x00FFFFFF)
           | (static_cast<uint32_t>(pcrel & 1) << 24)
           | (static_cast<uint32_t>(length & 3) << 25)
           | (static_cast<uint32_t>(ext & 1) << 27)
           | (static_cast<uint32_t>(type & 0xF) << 28);
}

// =============================================================================
// Mach-O Struct Writers
// =============================================================================

/// Write mach_header_64 (32 bytes).
static void writeMachOHeader(std::vector<uint8_t> &out,
                              uint32_t cputype, uint32_t cpusubtype,
                              uint32_t ncmds, uint32_t sizeofcmds,
                              uint32_t flags)
{
    appendLE32(out, kMhMagic64);
    appendLE32(out, cputype);
    appendLE32(out, cpusubtype);
    appendLE32(out, kMhObject);
    appendLE32(out, ncmds);
    appendLE32(out, sizeofcmds);
    appendLE32(out, flags);
    appendLE32(out, 0); // reserved
}

/// Write LC_SEGMENT_64 command header (72 bytes, excluding section headers).
static void writeSegmentCmd(std::vector<uint8_t> &out, uint32_t cmdsize,
                             uint64_t vmsize, uint64_t fileoff,
                             uint64_t filesize, uint32_t nsects)
{
    appendLE32(out, kLcSegment64);
    appendLE32(out, cmdsize);
    // segname: 16 zero bytes (unnamed for .o files)
    for (int i = 0; i < 16; ++i) out.push_back(0);
    appendLE64(out, 0);        // vmaddr
    appendLE64(out, vmsize);
    appendLE64(out, fileoff);
    appendLE64(out, filesize);
    appendLE32(out, 7);        // maxprot = rwx
    appendLE32(out, 7);        // initprot = rwx
    appendLE32(out, nsects);
    appendLE32(out, 0);        // flags
}

/// Write a section_64 header (80 bytes).
static void writeSectionHdr(std::vector<uint8_t> &out,
                             const char *sectname, const char *segname,
                             uint64_t addr, uint64_t size,
                             uint32_t offset, uint32_t align,
                             uint32_t reloff, uint32_t nreloc,
                             uint32_t flags)
{
    // sectname: 16 bytes, zero-padded
    char buf[16] = {};
    std::strncpy(buf, sectname, 16);
    out.insert(out.end(), buf, buf + 16);
    // segname: 16 bytes, zero-padded
    std::memset(buf, 0, 16);
    std::strncpy(buf, segname, 16);
    out.insert(out.end(), buf, buf + 16);
    appendLE64(out, addr);
    appendLE64(out, size);
    appendLE32(out, offset);
    appendLE32(out, align);   // log2 of alignment
    appendLE32(out, reloff);
    appendLE32(out, nreloc);
    appendLE32(out, flags);
    appendLE32(out, 0);       // reserved1
    appendLE32(out, 0);       // reserved2
    appendLE32(out, 0);       // reserved3
}

/// Write LC_BUILD_VERSION (24 bytes).
static void writeBuildVersionCmd(std::vector<uint8_t> &out)
{
    appendLE32(out, kLcBuildVersion);
    appendLE32(out, kBuildVerCmdSize);
    appendLE32(out, 1);          // platform = PLATFORM_MACOS
    appendLE32(out, 0x000E0000); // minos = 14.0.0
    appendLE32(out, 0x000F0000); // sdk = 15.0.0
    appendLE32(out, 0);          // ntools
}

/// Write LC_SYMTAB (24 bytes).
static void writeSymtabCmd(std::vector<uint8_t> &out,
                            uint32_t symoff, uint32_t nsyms,
                            uint32_t stroff, uint32_t strsize)
{
    appendLE32(out, kLcSymtab);
    appendLE32(out, kSymtabCmdSize);
    appendLE32(out, symoff);
    appendLE32(out, nsyms);
    appendLE32(out, stroff);
    appendLE32(out, strsize);
}

/// Write LC_DYSYMTAB (80 bytes).
static void writeDysymtabCmd(std::vector<uint8_t> &out,
                              uint32_t ilocal, uint32_t nlocal,
                              uint32_t iextdef, uint32_t nextdef,
                              uint32_t iundef, uint32_t nundef)
{
    appendLE32(out, kLcDysymtab);
    appendLE32(out, kDysymtabCmdSize);
    appendLE32(out, ilocal);
    appendLE32(out, nlocal);
    appendLE32(out, iextdef);
    appendLE32(out, nextdef);
    appendLE32(out, iundef);
    appendLE32(out, nundef);
    // Remaining 12 fields (tocoff through nlocrel) are all zero.
    for (int i = 0; i < 12; ++i)
        appendLE32(out, 0);
}

/// Write one nlist_64 entry (16 bytes).
static void writeNlist(std::vector<uint8_t> &out,
                        uint32_t strx, uint8_t type, uint8_t sect,
                        uint16_t desc, uint64_t value)
{
    appendLE32(out, strx);
    out.push_back(type);
    out.push_back(sect);
    appendLE16(out, desc);
    appendLE64(out, value);
}

/// Write one relocation_info entry (8 bytes).
static void writeMachoReloc(std::vector<uint8_t> &out,
                              uint32_t address, uint32_t packed)
{
    appendLE32(out, address);
    appendLE32(out, packed);
}

// =============================================================================
// Relocation Mapping
// =============================================================================

struct MachoRelocAttrs
{
    uint8_t type;
    uint8_t pcrel;
    uint8_t length;
    bool skip; // true if this reloc kind has no Mach-O equivalent
};

static MachoRelocAttrs machoRelocAttrs(RelocKind kind)
{
    switch (kind)
    {
    // x86_64
    case RelocKind::PCRel32:
        return {static_cast<uint8_t>(kX86_64RelocSigned), 1, 2, false};
    case RelocKind::Branch32:
        return {static_cast<uint8_t>(kX86_64RelocBranch), 1, 2, false};
    case RelocKind::Abs64:
        return {static_cast<uint8_t>(kX86_64RelocUnsigned), 0, 3, false};
    // AArch64
    case RelocKind::A64Call26:
    case RelocKind::A64Jump26:
        return {static_cast<uint8_t>(kArm64RelocBranch26), 1, 2, false};
    case RelocKind::A64AdrpPage21:
        return {static_cast<uint8_t>(kArm64RelocPage21), 1, 2, false};
    case RelocKind::A64AddPageOff12:
    case RelocKind::A64LdSt64Off12:
        return {static_cast<uint8_t>(kArm64RelocPageoff12), 0, 2, false};
    case RelocKind::A64CondBr19:
        // Mach-O has no ARM64_RELOC_BRANCH19; conditional branches are
        // always resolved internally by the encoder.
        return {0, 0, 0, true};
    }
    return {0, 0, 0, true};
}

// =============================================================================
// MachOWriter::write
// =============================================================================

bool MachOWriter::write(const std::string &path,
                         const CodeSection &text,
                         const CodeSection &rodata,
                         std::ostream &err)
{
    // --- Architecture-specific parameters ---
    uint32_t cputype, cpusubtype;
    uint32_t textAlignLog2;
    if (arch_ == ObjArch::X86_64)
    {
        cputype = kCpuTypeX86_64;
        cpusubtype = kCpuSubtypeX86_64All;
        textAlignLog2 = 4; // 2^4 = 16
    }
    else
    {
        cputype = kCpuTypeArm64;
        cpusubtype = kCpuSubtypeArm64All;
        textAlignLog2 = 2; // 2^2 = 4
    }
    size_t textAlign = static_cast<size_t>(1) << textAlignLog2;
    constexpr uint32_t constAlignLog2 = 3; // 2^3 = 8
    constexpr size_t constAlign = 8;

    // --- 1. Build string table with Darwin mangled names ---
    StringTable strtab;
    strtab.add(" "); // Mach-O convention: space at offset 1

    // --- 2. Collect and categorize symbols ---
    struct PendingSym
    {
        uint32_t encoderIdx;
        bool fromText;
        uint32_t strx;
        uint8_t type;
        uint8_t sect;
        uint64_t value;
        std::string mangledName;
    };
    std::vector<PendingSym> pendingLocals, pendingExtDef, pendingUndef;

    // Track names to avoid duplicate symbols in the Mach-O table.
    std::unordered_map<std::string, uint32_t> nameToMachoIdx;

    // Map from (CodeSection symbol index) → Mach-O symbol table index.
    std::unordered_map<uint32_t, uint32_t> textSymMap;
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;

    auto processSymbols = [&](const CodeSection &sec, bool isText)
    {
        for (uint32_t i = 1; i < sec.symbols().count(); ++i)
        {
            const Symbol &s = sec.symbols().at(i);
            std::string mangled = mangleName(s.name);
            uint32_t strOff = strtab.add(mangled);

            PendingSym ps{};
            ps.encoderIdx = i;
            ps.fromText = isText;
            ps.strx = strOff;
            ps.value = 0;
            ps.mangledName = mangled;

            if (s.binding == SymbolBinding::External)
            {
                ps.type = kNUndf | kNExt;
                ps.sect = kNoSect;
                pendingUndef.push_back(ps);
            }
            else if (s.binding == SymbolBinding::Local)
            {
                ps.type = kNSect;
                ps.sect = isText ? kSectText : kSectConst;
                ps.value = s.offset;
                pendingLocals.push_back(ps);
            }
            else
            {
                // Global defined.
                ps.type = kNSect | kNExt;
                ps.sect = isText ? kSectText : kSectConst;
                ps.value = s.offset;
                pendingExtDef.push_back(ps);
            }
        }
    };

    processSymbols(text, true);
    processSymbols(rodata, false);

    // --- 3. Assign Mach-O symbol indices ---
    // Order: locals → external defined → undefined.
    uint32_t machoIdx = 0;

    auto assignIndices = [&](std::vector<PendingSym> &syms)
    {
        for (auto &ps : syms)
        {
            auto it = nameToMachoIdx.find(ps.mangledName);
            if (it != nameToMachoIdx.end())
            {
                // Duplicate name — reuse existing Mach-O index.
                if (ps.fromText)
                    textSymMap[ps.encoderIdx] = it->second;
                else
                    rodataSymMap[ps.encoderIdx] = it->second;
                continue;
            }
            nameToMachoIdx[ps.mangledName] = machoIdx;
            if (ps.fromText)
                textSymMap[ps.encoderIdx] = machoIdx;
            else
                rodataSymMap[ps.encoderIdx] = machoIdx;
            ++machoIdx;
        }
    };

    assignIndices(pendingLocals);
    uint32_t ilocal = 0;
    uint32_t nlocal = machoIdx;

    assignIndices(pendingExtDef);
    uint32_t iextdef = nlocal;
    uint32_t nextdef = machoIdx - nlocal;

    assignIndices(pendingUndef);
    uint32_t iundef = iextdef + nextdef;
    uint32_t nundef = machoIdx - iundef;

    uint32_t nsyms = machoIdx;

    // Build the final symbol list (deduplicated, in order).
    struct FinalSym
    {
        uint32_t strx;
        uint8_t type;
        uint8_t sect;
        uint64_t value;
    };
    std::vector<FinalSym> allSyms(nsyms);
    auto emitSyms = [&](const std::vector<PendingSym> &syms, bool skipIfDefined)
    {
        for (const auto &ps : syms)
        {
            uint32_t idx = (ps.fromText)
                ? textSymMap[ps.encoderIdx]
                : rodataSymMap[ps.encoderIdx];
            // When an undefined symbol in text duplicates a defined symbol in
            // rodata, the defined version must win (they share the same Mach-O
            // index). Skip the undefined overwrite when the slot already holds
            // a defined symbol.
            if (skipIfDefined && allSyms[idx].type != 0)
                continue;
            allSyms[idx] = FinalSym{ps.strx, ps.type, ps.sect, ps.value};
        }
    };
    emitSyms(pendingLocals, false);
    emitSyms(pendingExtDef, false);
    emitSyms(pendingUndef, true);

    // --- 4. Build relocation entries for __text ---
    struct MachoReloc
    {
        uint32_t address;
        uint32_t packed;
    };
    std::vector<MachoReloc> textRelocs;

    for (const auto &rel : text.relocations())
    {
        auto attrs = machoRelocAttrs(rel.kind);
        if (attrs.skip) continue;

        // Map encoder symbol index to Mach-O index.
        uint32_t symIdx = 0;
        auto it = textSymMap.find(rel.symbolIndex);
        if (it != textSymMap.end())
            symIdx = it->second;
        else
        {
            auto rit = rodataSymMap.find(rel.symbolIndex);
            if (rit != rodataSymMap.end())
                symIdx = rit->second;
        }

        uint32_t packed = packRelocInfo(symIdx, attrs.pcrel, attrs.length,
                                         1 /*extern*/, attrs.type);
        textRelocs.push_back({static_cast<uint32_t>(rel.offset), packed});
    }

    // Sort relocations by address descending (Mach-O convention).
    std::sort(textRelocs.begin(), textRelocs.end(),
              [](const MachoReloc &a, const MachoReloc &b)
              { return a.address > b.address; });

    // --- 5. Compute file layout ---
    uint32_t sizeOfCmds = kSegCmdSize + kBuildVerCmdSize
                          + kSymtabCmdSize + kDysymtabCmdSize;
    size_t afterHeaders = kHeaderSize + sizeOfCmds;

    size_t textSize = text.bytes().size();
    size_t rodataSize = rodata.bytes().size();

    size_t textFileOff = alignUp(afterHeaders, textAlign);
    size_t constFileOff = alignUp(textFileOff + textSize, constAlign);
    size_t constAddr = constFileOff - textFileOff; // virtual address of __const

    size_t segFileOff = textFileOff;
    // Segment size must encompass the __const section's aligned address even
    // when rodata is empty, because the section header is always emitted.
    size_t segFileSize = constFileOff + rodataSize - textFileOff;
    size_t segVmSize = constAddr + rodataSize;

    size_t textRelocOff = constFileOff + rodataSize;
    uint32_t nTextRelocs = static_cast<uint32_t>(textRelocs.size());

    size_t symOff = textRelocOff + nTextRelocs * kRelocSize;
    size_t strOff = symOff + nsyms * kNlistSize;
    size_t totalSize = strOff + strtab.size();

    // --- 6. Build the file ---
    std::vector<uint8_t> file;
    file.reserve(totalSize);

    // Mach-O header (32 bytes)
    writeMachOHeader(file, cputype, cpusubtype, 4, sizeOfCmds,
                      kMhSubsectionsViaSymbols);

    // LC_SEGMENT_64 (232 bytes)
    writeSegmentCmd(file, kSegCmdSize, segVmSize, segFileOff, segFileSize, 2);

    // __text section header
    writeSectionHdr(file, "__text", "__TEXT",
                    0, textSize,
                    static_cast<uint32_t>(textFileOff), textAlignLog2,
                    (nTextRelocs > 0) ? static_cast<uint32_t>(textRelocOff) : 0,
                    nTextRelocs,
                    kSAttrPureInstructions | kSAttrSomeInstructions);

    // __const section header
    writeSectionHdr(file, "__const", "__TEXT",
                    constAddr, rodataSize,
                    static_cast<uint32_t>(constFileOff), constAlignLog2,
                    0, 0, // no __const relocations
                    0);

    // LC_BUILD_VERSION (24 bytes)
    writeBuildVersionCmd(file);

    // LC_SYMTAB (24 bytes)
    writeSymtabCmd(file,
                   static_cast<uint32_t>(symOff), nsyms,
                   static_cast<uint32_t>(strOff), strtab.size());

    // LC_DYSYMTAB (80 bytes)
    writeDysymtabCmd(file, ilocal, nlocal, iextdef, nextdef, iundef, nundef);

    // --- Section data ---
    // __text
    padTo(file, textFileOff);
    file.insert(file.end(), text.bytes().begin(), text.bytes().end());

    // __const
    padTo(file, constFileOff);
    file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());

    // --- Patch addends into instruction bytes (Mach-O REL convention) ---
    if (arch_ == ObjArch::X86_64)
    {
        for (const auto &rel : text.relocations())
        {
            if (rel.kind == RelocKind::PCRel32 || rel.kind == RelocKind::Branch32)
            {
                size_t patchOff = textFileOff + rel.offset;
                if (patchOff + 4 > file.size())
                {
                    err << "error: addend patch at offset " << patchOff
                        << " out of bounds (file size=" << file.size() << ")\n";
                    return false;
                }
                auto addend = static_cast<int32_t>(rel.addend);
                file[patchOff + 0] = static_cast<uint8_t>(addend);
                file[patchOff + 1] = static_cast<uint8_t>(addend >> 8);
                file[patchOff + 2] = static_cast<uint8_t>(addend >> 16);
                file[patchOff + 3] = static_cast<uint8_t>(addend >> 24);
            }
            else if (rel.kind == RelocKind::Abs64 && rel.addend != 0)
            {
                size_t patchOff = textFileOff + rel.offset;
                if (patchOff + 8 > file.size())
                {
                    err << "error: addend patch at offset " << patchOff
                        << " out of bounds (file size=" << file.size() << ")\n";
                    return false;
                }
                auto addend = static_cast<uint64_t>(rel.addend);
                for (int i = 0; i < 8; ++i)
                    file[patchOff + i] = static_cast<uint8_t>(addend >> (i * 8));
            }
        }
    }

    // --- Relocation entries ---
    for (const auto &r : textRelocs)
        writeMachoReloc(file, r.address, r.packed);

    // --- Symbol table ---
    for (const auto &sym : allSyms)
    {
        uint64_t value = sym.value;
        // Rodata symbols store offsets within the __const section. Mach-O nlist
        // values are segment-relative, so add the __const section's base address.
        if (sym.sect == kSectConst)
            value += constAddr;
        writeNlist(file, sym.strx, sym.type, sym.sect, 0, value);
    }

    // --- String table ---
    {
        const auto &d = strtab.data();
        file.insert(file.end(), d.begin(), d.end());
    }

    // --- 7. Write to disk ---
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        err << "MachOWriter: cannot open " << path << " for writing\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char *>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!ofs)
    {
        err << "MachOWriter: write failed for " << path << "\n";
        return false;
    }
    return true;
}

} // namespace viper::codegen::objfile
