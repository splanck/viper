//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOExeWriter.cpp
// Purpose: Writes a Mach-O executable (MH_EXECUTE) with dynamic library support.
//          Generates __LINKEDIT segment with symbol table, bind opcodes, and
//          optional ad-hoc code signature for arm64.
// Key invariants:
//   - __PAGEZERO: vmaddr=0, vmsize=4GB (must be FIRST segment)
//   - __TEXT: starts at fileoff=0, encompasses header + code + rodata
//   - __DATA: writable segment containing data + GOT entries
//   - __LINKEDIT: contains symtab, strtab, bind opcodes
//   - LC_LOAD_DYLINKER: "/usr/lib/dyld"
//   - LC_LOAD_DYLIB: at least libSystem.B.dylib
//   - LC_DYLD_INFO_ONLY: non-lazy bind opcodes for GOT entries
//   - Section addresses match SectionMerger-assigned VAs exactly
//   - Page alignment: 16KB for arm64, 4KB for x86_64
// Security features:
//   - MH_PIE: ASLR — kernel randomizes load address on each execution
//   - __PAGEZERO: null-pointer dereference guard (4GB unmapped region)
//   - CS_LINKER_SIGNED: ad-hoc code signature (arm64) — required by AMFI
//   - W^X: __TEXT is R+X, __DATA is R+W — no segment is both writable
//     and executable
//   - Non-lazy binding: GOT entries resolved by dyld before main() runs
// Links: codegen/common/linker/MachOExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/MachOExeWriter.hpp"

#include "codegen/common/linker/AlignUtil.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace viper::codegen::linker
{

namespace
{

// Mach-O constants.
static constexpr uint32_t MH_MAGIC_64 = 0xFEEDFACF;
static constexpr uint32_t MH_EXECUTE = 2;
static constexpr uint32_t MH_NOUNDEFS = 0x1;
static constexpr uint32_t MH_DYLDLINK = 0x4;
static constexpr uint32_t MH_TWOLEVEL = 0x80;
static constexpr uint32_t MH_PIE = 0x200000;
static constexpr uint32_t MH_HAS_TLV_DESCRIPTORS = 0x800000;

// Mach-O section type constants (low 8 bits of flags).
static constexpr uint32_t S_THREAD_LOCAL_REGULAR = 0x11;
static constexpr uint32_t S_THREAD_LOCAL_ZEROFILL = 0x12;
static constexpr uint32_t S_THREAD_LOCAL_VARIABLES = 0x13;

static constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
static constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000C;
static constexpr uint32_t CPU_SUBTYPE_ARM64_ALL = 0;
static constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = 3;

static constexpr uint32_t LC_SEGMENT_64 = 0x19;
static constexpr uint32_t LC_MAIN = 0x80000028;
static constexpr uint32_t LC_LOAD_DYLIB = 0x0C;
static constexpr uint32_t LC_LOAD_DYLINKER = 0x0E;
static constexpr uint32_t LC_SYMTAB = 0x02;
static constexpr uint32_t LC_DYSYMTAB = 0x0B;
static constexpr uint32_t LC_DYLD_INFO_ONLY = 0x80000022;
static constexpr uint32_t LC_BUILD_VERSION = 0x32;
static constexpr uint32_t LC_CODE_SIGNATURE = 0x1D;

static constexpr uint32_t VM_PROT_READ = 1;
static constexpr uint32_t VM_PROT_WRITE = 2;
static constexpr uint32_t VM_PROT_EXECUTE = 4;

static constexpr uint32_t PLATFORM_MACOS = 1;

// Symbol table constants.
static constexpr uint8_t N_EXT = 0x01;
static constexpr uint8_t N_UNDF = 0x00;
static constexpr uint8_t N_SECT = 0x0E;

// Bind opcode constants (high 4 bits = opcode, low 4 bits = immediate).
static constexpr uint8_t BIND_OPCODE_DONE = 0x00;
static constexpr uint8_t BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10;
static constexpr uint8_t BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40;
static constexpr uint8_t BIND_OPCODE_SET_TYPE_IMM = 0x50;
static constexpr uint8_t BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70;
static constexpr uint8_t BIND_OPCODE_DO_BIND = 0x90;
static constexpr uint8_t BIND_TYPE_POINTER = 1;

// Rebase opcode constants (high 4 bits = opcode, low 4 bits = immediate).
static constexpr uint8_t REBASE_OPCODE_DONE = 0x00;
static constexpr uint8_t REBASE_OPCODE_SET_TYPE_IMM = 0x10;
static constexpr uint8_t REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x20;
static constexpr uint8_t REBASE_OPCODE_ADD_ADDR_ULEB = 0x30;
static constexpr uint8_t REBASE_OPCODE_DO_REBASE_IMM_TIMES = 0x50;
static constexpr uint8_t REBASE_TYPE_POINTER = 1;

void writeLE32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

void writeLE64(std::vector<uint8_t> &buf, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void writePad(std::vector<uint8_t> &buf, size_t count)
{
    buf.insert(buf.end(), count, 0);
}

void writeStr(std::vector<uint8_t> &buf, const char *s, size_t maxLen)
{
    size_t len = std::strlen(s);
    size_t n = std::min(len, maxLen);
    buf.insert(buf.end(), s, s + n);
    if (n < maxLen)
        writePad(buf, maxLen - n);
}

void writeULEB128(std::vector<uint8_t> &buf, uint64_t val)
{
    do
    {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val != 0)
            byte |= 0x80;
        buf.push_back(byte);
    } while (val != 0);
}

// Big-endian helpers (code signature uses network byte order).
void writeBE32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v));
}

void writeBE64(std::vector<uint8_t> &buf, uint64_t v)
{
    writeBE32(buf, static_cast<uint32_t>(v >> 32));
    writeBE32(buf, static_cast<uint32_t>(v));
}

/// Compute SHA-256 hash of data. Returns 32 bytes.
void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
#if defined(__APPLE__)
    CC_SHA256(data, static_cast<CC_LONG>(len), out);
#else
    // Code signing only needed on macOS; zero-fill on other platforms.
    std::memset(out, 0, 32);
    (void)data;
    (void)len;
#endif
}

/// Build a linker-signed ad-hoc code signature (SuperBlob) for a Mach-O executable.
/// Returns the complete code signature blob to append to __LINKEDIT.
///
/// The signature includes:
///   - SuperBlob (CSMAGIC_EMBEDDED_SIGNATURE) containing:
///     - CodeDirectory (CSMAGIC_CODEDIRECTORY v0x20400) with CS_LINKER_SIGNED flag
///     - Empty Requirements blob (CSMAGIC_REQUIREMENTS)
///   - SHA-256 hash of each 4KB page up to codeLimit
///
/// The CS_LINKER_SIGNED flag is critical: macOS AMFI rejects ad-hoc binaries
/// without it. Only linkers are supposed to set this flag (codesign doesn't).
std::vector<uint8_t> buildCodeSignature(const std::vector<uint8_t> &file,
                                        size_t codeLimit,
                                        const std::string &identifier,
                                        uint64_t textSegFileOff,
                                        uint64_t textSegFileSize)
{
    // CodeDirectory constants (all big-endian in output).
    static constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0;
    static constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xFADE0C02;
    static constexpr uint32_t CSMAGIC_REQUIREMENTS = 0xFADE0C01;
    static constexpr uint32_t CS_ADHOC = 0x2;
    static constexpr uint32_t CS_LINKER_SIGNED = 0x20000;
    static constexpr uint32_t CS_EXECSEG_MAIN_BINARY = 0x1;
    static constexpr uint32_t CD_VERSION = 0x20400;

    const uint32_t nCodeSlots = static_cast<uint32_t>((codeLimit + 4095) / 4096);
    const size_t identLen = identifier.size() + 1; // including NUL
    const uint32_t cdHeaderSize = 88;              // version 0x20400 struct size
    const uint32_t hashOffset = static_cast<uint32_t>(cdHeaderSize + identLen);
    const uint32_t cdSize = hashOffset + nCodeSlots * 32;

    // --- Build CodeDirectory ---
    std::vector<uint8_t> cd;
    cd.reserve(cdSize);
    writeBE32(cd, CSMAGIC_CODEDIRECTORY);
    writeBE32(cd, cdSize);
    writeBE32(cd, CD_VERSION);
    writeBE32(cd, CS_ADHOC | CS_LINKER_SIGNED);
    writeBE32(cd, hashOffset);   // hashOffset
    writeBE32(cd, cdHeaderSize); // identOffset
    writeBE32(cd, 0);            // nSpecialSlots
    writeBE32(cd, nCodeSlots);
    writeBE32(cd, static_cast<uint32_t>(codeLimit));
    cd.push_back(32);                      // hashSize (SHA-256)
    cd.push_back(2);                       // hashType (SHA-256)
    cd.push_back(0);                       // platform
    cd.push_back(12);                      // pageSize (log2 4096)
    writeBE32(cd, 0);                      // spare2
    writeBE32(cd, 0);                      // scatterOffset (v >= 0x20100)
    writeBE32(cd, 0);                      // teamOffset (v >= 0x20200)
    writeBE32(cd, 0);                      // spare3 (v >= 0x20300)
    writeBE64(cd, codeLimit);              // codeLimit64
    writeBE64(cd, textSegFileOff);         // execSegBase (v >= 0x20400)
    writeBE64(cd, textSegFileSize);        // execSegLimit
    writeBE64(cd, CS_EXECSEG_MAIN_BINARY); // execSegFlags

    // Identifier string.
    cd.insert(cd.end(), identifier.begin(), identifier.end());
    cd.push_back(0);

    // Code slot hashes: SHA-256 of each 4KB page.
    for (uint32_t i = 0; i < nCodeSlots; ++i)
    {
        size_t pageStart = static_cast<size_t>(i) * 4096;
        size_t pageEnd = std::min(pageStart + 4096, codeLimit);
        uint8_t hash[32];
        sha256(file.data() + pageStart, pageEnd - pageStart, hash);
        cd.insert(cd.end(), hash, hash + 32);
    }

    // --- Build empty Requirements blob ---
    std::vector<uint8_t> req;
    writeBE32(req, CSMAGIC_REQUIREMENTS);
    writeBE32(req, 12);
    writeBE32(req, 0); // count = 0

    // --- Build SuperBlob ---
    const uint32_t sbHeaderSize = 12 + 2 * 8; // header(12) + 2 index entries(8 each)
    const uint32_t sbSize = sbHeaderSize + static_cast<uint32_t>(cd.size() + req.size());

    std::vector<uint8_t> sb;
    sb.reserve(sbSize);
    writeBE32(sb, CSMAGIC_EMBEDDED_SIGNATURE);
    writeBE32(sb, sbSize);
    writeBE32(sb, 2); // count = 2 blobs

    // Index entry 0: CodeDirectory (type = 0)
    writeBE32(sb, 0);
    writeBE32(sb, sbHeaderSize);

    // Index entry 1: Requirements (type = 2)
    writeBE32(sb, 2);
    writeBE32(sb, sbHeaderSize + static_cast<uint32_t>(cd.size()));

    sb.insert(sb.end(), cd.begin(), cd.end());
    sb.insert(sb.end(), req.begin(), req.end());
    return sb;
}

// Bind opcode for flat namespace lookup (ordinal = -2).
// With flat namespace, dyld searches all loaded dylibs for the symbol.
// This avoids needing per-symbol ordinal tracking against specific dylibs.
static constexpr uint8_t BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30;

/// Emit one bind entry for a given symbol at a given segment offset.
void emitBindEntry(std::vector<uint8_t> &bindData,
                   const std::string &symbolName,
                   uint64_t segmentOffset,
                   uint32_t dataSegIndex)
{
    // Use flat namespace lookup: ordinal = -2 (BIND_SPECIAL_DYLIB_FLAT_LOOKUP).
    // Immediate field encodes -2 as 4-bit twos complement = 0x0E.
    bindData.push_back(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM | 0x0E);

    // Symbol name with Mach-O underscore prefix.
    bindData.push_back(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | 0);
    std::string machoName = "_" + symbolName;
    bindData.insert(bindData.end(), machoName.begin(), machoName.end());
    bindData.push_back(0); // NUL terminator

    bindData.push_back(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);

    bindData.push_back(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | (dataSegIndex & 0x0F));
    writeULEB128(bindData, segmentOffset);

    bindData.push_back(BIND_OPCODE_DO_BIND);
}

/// Build the bind opcode stream for non-lazy GOT binding + TLV descriptors.
void buildBindOpcodes(std::vector<uint8_t> &bindData,
                      const std::vector<GotEntry> &gotEntries,
                      const LinkLayout &layout,
                      uint64_t dataSegVmAddr,
                      uint32_t dataSegIndex)
{
    // Bind GOT entries for dynamic symbols.
    for (const auto &ge : gotEntries)
    {
        uint64_t offset = ge.gotAddr - dataSegVmAddr;
        emitBindEntry(bindData, ge.symbolName, offset, dataSegIndex);
    }

    // Bind TLV descriptor thunk fields → _tlv_bootstrap.
    // dyld uses these bind events to discover TLV descriptors and initialize
    // per-image TLS metadata. Without this, _tlv_bootstrap (a fail-stub) aborts.
    for (const auto &sec : layout.sections)
    {
        if (!sec.tls || sec.name != ".tdata")
            continue;

        // Each TLV descriptor is 24 bytes: {thunk(8), key(8), offset(8)}.
        // The thunk at byte 0 of each descriptor must be bound to _tlv_bootstrap.
        size_t numDescriptors = sec.data.size() / 24;
        for (size_t i = 0; i < numDescriptors; ++i)
        {
            uint64_t descVA = sec.virtualAddr + i * 24;
            uint64_t segOff = descVA - dataSegVmAddr;
            emitBindEntry(bindData, "_tlv_bootstrap", segOff, dataSegIndex);
        }
    }

    // Bind data-pointer references to dynamic symbols (e.g., ObjC classrefs,
    // superrefs, protocol refs). These are Abs64 relocations whose target is an
    // external symbol — the linker writes 0 and dyld fills the actual pointer.
    for (const auto &be : layout.bindEntries)
    {
        const auto &sec = layout.sections[be.sectionIndex];
        uint64_t addr = sec.virtualAddr + be.offset;
        if (addr >= dataSegVmAddr)
        {
            uint64_t segOff = addr - dataSegVmAddr;
            emitBindEntry(bindData, be.symbolName, segOff, dataSegIndex);
        }
    }

    bindData.push_back(BIND_OPCODE_DONE);
}

/// Build rebase opcodes for ASLR pointer fixups in writable sections.
/// dyld processes these at load time, adding the ASLR slide to each pointer.
void buildRebaseOpcodes(std::vector<uint8_t> &rebaseData,
                        const LinkLayout &layout,
                        uint64_t dataSegVmAddr,
                        uint32_t dataSegIndex)
{
    if (layout.rebaseEntries.empty())
        return;

    // Collect segment-relative offsets for all rebase locations in __DATA.
    std::vector<uint64_t> offsets;
    offsets.reserve(layout.rebaseEntries.size());
    for (const auto &entry : layout.rebaseEntries)
    {
        const auto &sec = layout.sections[entry.sectionIndex];
        uint64_t addr = sec.virtualAddr + entry.offset;
        if (addr >= dataSegVmAddr)
            offsets.push_back(addr - dataSegVmAddr);
    }
    std::sort(offsets.begin(), offsets.end());

    // Emit rebase opcodes.
    rebaseData.push_back(REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER);

    size_t i = 0;
    while (i < offsets.size())
    {
        // Count consecutive pointers (each 8 bytes apart).
        size_t runLen = 1;
        while (i + runLen < offsets.size() && offsets[i + runLen] == offsets[i] + runLen * 8)
            ++runLen;

        // Set segment and offset.
        rebaseData.push_back(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | (dataSegIndex & 0x0F));
        writeULEB128(rebaseData, offsets[i]);

        // Rebase the run of consecutive pointers (max 15 per opcode).
        size_t remaining = runLen;
        while (remaining > 0)
        {
            uint8_t count = static_cast<uint8_t>(std::min<size_t>(remaining, 15));
            rebaseData.push_back(REBASE_OPCODE_DO_REBASE_IMM_TIMES | count);
            remaining -= count;
        }

        i += runLen; // Advance past the run we just emitted.
    }

    rebaseData.push_back(REBASE_OPCODE_DONE);
}

/// Build symbol table and string table for __LINKEDIT.
void buildSymtab(std::vector<uint8_t> &symtabData,
                 std::vector<uint8_t> &strtabData,
                 const LinkLayout &layout,
                 const std::unordered_set<std::string> &dynSyms,
                 uint32_t &nExtDef,
                 uint32_t &nUndef)
{
    strtabData.push_back(0); // String table starts with NUL.

    auto addString = [&](const std::string &s) -> uint32_t
    {
        uint32_t off = static_cast<uint32_t>(strtabData.size());
        strtabData.insert(strtabData.end(), s.begin(), s.end());
        strtabData.push_back(0);
        return off;
    };

    auto writeNlist = [&](uint32_t strx, uint8_t type, uint8_t sect, uint16_t desc, uint64_t value)
    {
        writeLE32(symtabData, strx);
        symtabData.push_back(type);
        symtabData.push_back(sect);
        symtabData.push_back(static_cast<uint8_t>(desc));
        symtabData.push_back(static_cast<uint8_t>(desc >> 8));
        writeLE64(symtabData, value);
    };

    // External defined: _main.
    nExtDef = 0;
    {
        auto it = layout.globalSyms.find("main");
        if (it == layout.globalSyms.end())
            it = layout.globalSyms.find("_main");
        if (it != layout.globalSyms.end())
        {
            uint32_t strx = addString("_main");
            writeNlist(strx, N_EXT | N_SECT, 1, 0, it->second.resolvedAddr);
            nExtDef++;
        }
    }

    // Undefined: dynamic imports.
    nUndef = 0;
    std::vector<std::string> sortedDyn(dynSyms.begin(), dynSyms.end());
    std::sort(sortedDyn.begin(), sortedDyn.end());
    for (const auto &sym : sortedDyn)
    {
        if (sym.size() > 6 && sym.substr(0, 6) == "__got_")
            continue;
        uint32_t strx = addString("_" + sym);
        writeNlist(strx, N_EXT | N_UNDF, 0, 0x0100, 0);
        nUndef++;
    }

    while (strtabData.size() % 4 != 0)
        strtabData.push_back(0);
}

} // anonymous namespace

bool writeMachOExe(const std::string &path,
                   const LinkLayout &layout,
                   LinkArch arch,
                   const std::vector<DylibImport> &dylibs,
                   const std::unordered_set<std::string> &dynSyms,
                   std::ostream &err)
{
    const size_t pageSize = layout.pageSize;
    const bool isArm64 = (arch == LinkArch::AArch64);
    const uint32_t cpuType = isArm64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    const uint32_t cpuSubtype = isArm64 ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;

    // =======================================================================
    // Phase 1: Classify sections and compute data sizes.
    // =======================================================================
    std::vector<size_t> textSections, dataSections;
    for (size_t i = 0; i < layout.sections.size(); ++i)
    {
        const auto &sec = layout.sections[i];
        if (sec.data.empty())
            continue;
        if (sec.writable)
            dataSections.push_back(i);
        else
            textSections.push_back(i);
    }

    // Compute text/data sizes accounting for VA gaps between sections.
    // Sections within a segment have page-aligned VAs; file layout must mirror this.
    size_t textDataSize = 0;
    if (!textSections.empty())
    {
        uint64_t firstVA = layout.sections[textSections.front()].virtualAddr;
        for (size_t i : textSections)
        {
            const auto &sec = layout.sections[i];
            uint64_t endVA = sec.virtualAddr + sec.data.size();
            size_t spanFromFirst = static_cast<size_t>(endVA - firstVA);
            if (spanFromFirst > textDataSize)
                textDataSize = spanFromFirst;
        }
    }
    size_t dataDataSize = 0;
    if (!dataSections.empty())
    {
        uint64_t firstVA = layout.sections[dataSections.front()].virtualAddr;
        for (size_t i : dataSections)
        {
            const auto &sec = layout.sections[i];
            uint64_t endVA = sec.virtualAddr + sec.data.size();
            size_t spanFromFirst = static_cast<size_t>(endVA - firstVA);
            if (spanFromFirst > dataDataSize)
                dataDataSize = spanFromFirst;
        }
    }

    // =======================================================================
    // Phase 2: Build __LINKEDIT content.
    // =======================================================================
    // Need LC_DYLD_INFO_ONLY if we have GOT entries, TLV descriptors, or rebase entries.
    bool hasTLS = false;
    for (const auto &sec : layout.sections)
        if (sec.tls && sec.name == ".tdata" && !sec.data.empty())
            hasTLS = true;
    const bool hasDynamic =
        !layout.gotEntries.empty() || hasTLS || !layout.rebaseEntries.empty() ||
        !layout.bindEntries.empty();

    std::vector<uint8_t> symtabData, strtabData;
    uint32_t nExtDef = 0, nUndef = 0;
    buildSymtab(symtabData, strtabData, layout, dynSyms, nExtDef, nUndef);

    // Bind and rebase opcodes (built with correct VAs after layout computation below).
    std::vector<uint8_t> bindData;
    std::vector<uint8_t> rebaseData;

    size_t linkeditDataSize = symtabData.size() + strtabData.size();

    // =======================================================================
    // Phase 3: Compute file layout.
    //
    // Key: __TEXT segment starts at fileoff=0 and includes the header.
    //      The section data starts after the header, page-aligned.
    //      Section VAs come from SectionMerger (already correct).
    // =======================================================================
    const uint64_t pagezeroSize = 0x100000000ULL;
    const uint64_t textSegVmAddr = pagezeroSize; // __TEXT segment base

    // Count and size load commands.
    uint32_t ncmds = 0;
    uint32_t sizeofcmds = 0;

    ncmds++;
    sizeofcmds += 72; // __PAGEZERO

    const uint32_t textSecCount = static_cast<uint32_t>(textSections.size());
    ncmds++;
    sizeofcmds += 72 + textSecCount * 80; // __TEXT

    const uint32_t dataSecCount = static_cast<uint32_t>(dataSections.size());
    if (!dataSections.empty())
    {
        ncmds++;
        sizeofcmds += 72 + dataSecCount * 80; // __DATA
    }

    ncmds++;
    sizeofcmds += 72; // __LINKEDIT

    const char *dylinkerPath = "/usr/lib/dyld";
    const size_t dylinkerCmdSize = alignUp(12 + std::strlen(dylinkerPath) + 1, 8);
    ncmds++;
    sizeofcmds += static_cast<uint32_t>(dylinkerCmdSize); // LC_LOAD_DYLINKER

    ncmds++;
    sizeofcmds += 24; // LC_MAIN

    std::vector<size_t> dylibCmdSizes;
    for (const auto &dl : dylibs)
    {
        size_t cmdSize = alignUp(24 + dl.path.size() + 1, 8);
        dylibCmdSizes.push_back(cmdSize);
        ncmds++;
        sizeofcmds += static_cast<uint32_t>(cmdSize);
    }

    ncmds++;
    sizeofcmds += 24; // LC_SYMTAB
    ncmds++;
    sizeofcmds += 80; // LC_DYSYMTAB

    if (hasDynamic)
    {
        ncmds++;
        sizeofcmds += 48; // LC_DYLD_INFO_ONLY
    }

    ncmds++;
    sizeofcmds += 24; // LC_BUILD_VERSION

    // LC_CODE_SIGNATURE (arm64 macOS requires ad-hoc code signing).
    const bool needsCodeSign = isArm64;
    if (needsCodeSign)
    {
        ncmds++;
        sizeofcmds += 16; // LC_CODE_SIGNATURE
    }

    const size_t headerSize = 32;
    const size_t headerAndCmds = headerSize + sizeofcmds;

    // Section data starts at the first page boundary after header+cmds.
    // In standard Mach-O, __TEXT segment fileoff=0 and includes the header.
    const size_t textDataFileOff = alignUp(headerAndCmds, pageSize);
    const size_t textDataFileSize = alignUp(textDataSize, pageSize);

    // __TEXT segment: fileoff=0, filesize includes header+cmds+section data.
    const size_t textSegFileOff = 0;
    const size_t textSegFileSize = textDataFileOff + textDataFileSize;

    // Use the first text section's VA from SectionMerger to derive __TEXT segment vmsize.
    // SectionMerger assigns text section VA = baseAddr + pageSize = 0x100004000.
    // __TEXT segment vmaddr = 0x100000000.
    // __TEXT segment vmsize must cover from vmaddr to end of text data.
    uint64_t textLastVA = textSegVmAddr;
    for (size_t idx : textSections)
    {
        uint64_t end = layout.sections[idx].virtualAddr + layout.sections[idx].data.size();
        if (end > textLastVA)
            textLastVA = end;
    }
    const uint64_t textSegVmSize = alignUp(textLastVA - textSegVmAddr, pageSize);

    // __DATA segment.
    const size_t dataFileOff = textSegFileSize;
    const size_t dataFileSize = dataSections.empty() ? 0 : alignUp(dataDataSize, pageSize);
    uint64_t dataSegVmAddr = 0;
    uint64_t dataSegVmSize = 0;
    if (!dataSections.empty())
    {
        dataSegVmAddr = layout.sections[dataSections[0]].virtualAddr;
        uint64_t dataLastVA = dataSegVmAddr;
        for (size_t idx : dataSections)
        {
            uint64_t end = layout.sections[idx].virtualAddr + layout.sections[idx].data.size();
            if (end > dataLastVA)
                dataLastVA = end;
        }
        dataSegVmSize = alignUp(dataLastVA - dataSegVmAddr, pageSize);
    }

    // Build rebase and bind opcodes now that we have correct VAs.
    // __DATA is segment index 2 (0=__PAGEZERO, 1=__TEXT, 2=__DATA).
    {
        // Rebase opcodes: ASLR fixups for internal pointers in writable sections.
        buildRebaseOpcodes(rebaseData, layout, dataSegVmAddr, 2);
        while (rebaseData.size() % 8 != 0)
            rebaseData.push_back(0);

        // Bind opcodes: dynamic symbol resolution + TLV descriptor thunks.
        buildBindOpcodes(bindData, layout.gotEntries, layout, dataSegVmAddr, 2);
        while (bindData.size() % 8 != 0)
            bindData.push_back(0);
    }
    linkeditDataSize =
        symtabData.size() + strtabData.size() + rebaseData.size() + bindData.size();

    // __LINKEDIT segment.
    const size_t linkeditFileOff = dataFileOff + dataFileSize;

    // Code signature lives at end of __LINKEDIT (arm64 only).
    // Pre-compute offset and size so __LINKEDIT filesize includes it.
    const std::string codeSignIdent = std::filesystem::path(path).stem().string();
    size_t codeSignOff = 0;
    size_t codeSignSize = 0;
    if (needsCodeSign)
    {
        codeSignOff = alignUp(linkeditFileOff + linkeditDataSize, 16);
        const uint32_t nSlots = static_cast<uint32_t>((codeSignOff + 4095) / 4096);
        const size_t identLen = codeSignIdent.size() + 1;
        const uint32_t cdSize = 88 + static_cast<uint32_t>(identLen) + nSlots * 32;
        codeSignSize = 28 + cdSize + 12; // SuperBlob(28) + CodeDirectory + Requirements(12)
    }

    const size_t linkeditTotalSize =
        needsCodeSign ? (codeSignOff - linkeditFileOff + codeSignSize) : linkeditDataSize;
    const size_t linkeditFileSize = alignUp(linkeditTotalSize, pageSize);
    const uint64_t linkeditVmAddr =
        textSegVmAddr + textSegVmSize + (dataSections.empty() ? 0 : dataSegVmSize);
    const uint64_t linkeditVmSize = alignUp(linkeditTotalSize, pageSize);

    // Offsets within __LINKEDIT: symtab → strtab → rebase → bind.
    const uint32_t symtabOff = static_cast<uint32_t>(linkeditFileOff);
    const uint32_t strtabOff = symtabOff + static_cast<uint32_t>(symtabData.size());
    const uint32_t rebaseOff = strtabOff + static_cast<uint32_t>(strtabData.size());
    const uint32_t bindOff = rebaseOff + static_cast<uint32_t>(rebaseData.size());

    // Entry point: LC_MAIN entryoff = file offset of main().
    // main's file offset = textDataFileOff + (main's VA - first text section VA).
    uint64_t mainEntryOff = 0;
    {
        auto it = layout.globalSyms.find("main");
        if (it == layout.globalSyms.end())
            it = layout.globalSyms.find("_main");
        if (it != layout.globalSyms.end() && !textSections.empty())
        {
            // Find which text section main is in and compute file offset.
            uint64_t mainVA = it->second.resolvedAddr;
            uint64_t firstTextSecVA = layout.sections[textSections[0]].virtualAddr;
            mainEntryOff = textDataFileOff + (mainVA - firstTextSecVA);
        }
    }

    // =======================================================================
    // Phase 4: Write Mach-O file.
    // =======================================================================
    std::vector<uint8_t> file;
    file.reserve(linkeditFileOff + linkeditFileSize);

    // --- Mach-O header (32 bytes) ---
    writeLE32(file, MH_MAGIC_64);
    writeLE32(file, cpuType);
    writeLE32(file, cpuSubtype);
    writeLE32(file, MH_EXECUTE);
    writeLE32(file, ncmds);
    writeLE32(file, sizeofcmds);
    // Use flat namespace (no MH_TWOLEVEL) so bind opcodes don't need per-symbol
    // dylib ordinals. dyld searches all loaded dylibs for each symbol.
    uint32_t mhFlags = MH_PIE | MH_DYLDLINK;
    if (!hasDynamic)
        mhFlags |= MH_NOUNDEFS;
    if (hasTLS)
        mhFlags |= MH_HAS_TLV_DESCRIPTORS;
    writeLE32(file, mhFlags);
    writeLE32(file, 0); // reserved

    // --- __PAGEZERO ---
    writeLE32(file, LC_SEGMENT_64);
    writeLE32(file, 72);
    writeStr(file, "__PAGEZERO", 16);
    writeLE64(file, 0);
    writeLE64(file, pagezeroSize);
    writeLE64(file, 0);
    writeLE64(file, 0);
    writeLE32(file, 0);
    writeLE32(file, 0);
    writeLE32(file, 0);
    writeLE32(file, 0);

    // --- __TEXT ---
    writeLE32(file, LC_SEGMENT_64);
    writeLE32(file, 72 + textSecCount * 80);
    writeStr(file, "__TEXT", 16);
    writeLE64(file, textSegVmAddr);
    writeLE64(file, textSegVmSize);
    writeLE64(file, textSegFileOff);
    writeLE64(file, textSegFileSize);
    writeLE32(file, VM_PROT_READ | VM_PROT_EXECUTE);
    writeLE32(file, VM_PROT_READ | VM_PROT_EXECUTE);
    writeLE32(file, textSecCount);
    writeLE32(file, 0);

    // Section headers — file offsets mirror VA offsets from segment base.
    // Since __TEXT fileoff=0 and vmaddr=textSegVmAddr, each section's
    // file offset is simply: sec.virtualAddr - textSegVmAddr.
    for (size_t idx : textSections)
    {
        const auto &sec = layout.sections[idx];

        // Determine Mach-O section name.
        // ObjC metadata sections (e.g., __DATA,__objc_methname) keep their name.
        // Regular sections use __text or __const.
        std::string machoSecName;
        if (isObjCSection(sec.name))
        {
            // Name format from MachOReader: "__SEGMENT,__section"
            auto comma = sec.name.find(',');
            machoSecName = (comma != std::string::npos) ? sec.name.substr(comma + 1) : sec.name;
        }
        else
        {
            machoSecName = sec.executable ? "__text" : "__const";
        }

        uint32_t secFileOff = static_cast<uint32_t>(sec.virtualAddr - textSegVmAddr);
        writeStr(file, machoSecName.c_str(), 16);
        writeStr(file, "__TEXT", 16);
        writeLE64(file, sec.virtualAddr); // addr = SectionMerger VA
        writeLE64(file, sec.data.size());
        writeLE32(file, secFileOff);
        writeLE32(file, 0); // align (log2)
        writeLE32(file, 0);
        writeLE32(file, 0); // reloff, nreloc
        uint32_t flags = sec.executable ? 0x80000400u : 0u;
        writeLE32(file, flags);
        writeLE32(file, 0);
        writeLE32(file, 0);
        writeLE32(file, 0); // reserved
    }

    // --- __DATA ---
    if (!dataSections.empty())
    {
        writeLE32(file, LC_SEGMENT_64);
        writeLE32(file, 72 + dataSecCount * 80);
        writeStr(file, "__DATA", 16);
        writeLE64(file, dataSegVmAddr);
        writeLE64(file, dataSegVmSize);
        writeLE64(file, dataFileOff);
        writeLE64(file, dataFileSize);
        writeLE32(file, VM_PROT_READ | VM_PROT_WRITE);
        writeLE32(file, VM_PROT_READ | VM_PROT_WRITE);
        writeLE32(file, dataSecCount);
        writeLE32(file, 0);

        for (size_t idx : dataSections)
        {
            const auto &sec = layout.sections[idx];
            uint32_t secFileOff =
                static_cast<uint32_t>(dataFileOff + (sec.virtualAddr - dataSegVmAddr));

            // Choose section name and type flags.
            std::string machoSecName = "__data";
            uint32_t secFlags = 0;
            bool isZerofill = false;
            if (isObjCSection(sec.name))
            {
                // ObjC metadata: preserve original section name (e.g., __objc_classlist).
                auto comma = sec.name.find(',');
                machoSecName = (comma != std::string::npos) ? sec.name.substr(comma + 1) : sec.name;
            }
            else if (sec.tls)
            {
                if (sec.name == ".tdata")
                {
                    machoSecName = "__thread_vars";
                    secFlags = S_THREAD_LOCAL_VARIABLES;
                }
                else
                {
                    // Use __thread_data (S_THREAD_LOCAL_REGULAR) for TLS template
                    // data, even for zero-initialized data. This avoids the zerofill
                    // offset=0 requirement and ensures dyld finds the TLS template.
                    machoSecName = "__thread_data";
                    secFlags = S_THREAD_LOCAL_REGULAR;
                }
            }
            else if (sec.name == ".bss")
            {
                // Emit BSS as regular data (S_REGULAR) with file backing.
                // Using S_ZEROFILL is more correct but complicates file layout
                // (zerofill sections must be last, offset=0, separate filesize calc).
                // Since BSS is already zero-filled in the merged section, this works.
                machoSecName = "__bss";
                secFlags = 0; // S_REGULAR — not S_ZEROFILL, to simplify layout
            }

            writeStr(file, machoSecName.c_str(), 16);
            writeStr(file, "__DATA", 16);
            writeLE64(file, sec.virtualAddr); // addr = SectionMerger VA
            writeLE64(file, sec.data.size());
            writeLE32(file, isZerofill ? 0 : secFileOff);
            writeLE32(file, 0); // align
            writeLE32(file, 0);
            writeLE32(file, 0); // reloff, nreloc
            writeLE32(file, secFlags);
            writeLE32(file, 0);
            writeLE32(file, 0);
            writeLE32(file, 0); // reserved
        }
    }

    // --- __LINKEDIT ---
    writeLE32(file, LC_SEGMENT_64);
    writeLE32(file, 72);
    writeStr(file, "__LINKEDIT", 16);
    writeLE64(file, linkeditVmAddr);
    writeLE64(file, linkeditVmSize);
    writeLE64(file, linkeditFileOff);
    writeLE64(file, linkeditFileSize);
    writeLE32(file, VM_PROT_READ);
    writeLE32(file, VM_PROT_READ);
    writeLE32(file, 0);
    writeLE32(file, 0);

    // --- LC_LOAD_DYLINKER ---
    writeLE32(file, LC_LOAD_DYLINKER);
    writeLE32(file, static_cast<uint32_t>(dylinkerCmdSize));
    writeLE32(file, 12);
    {
        size_t nameLen = std::strlen(dylinkerPath) + 1;
        file.insert(file.end(), dylinkerPath, dylinkerPath + nameLen);
        size_t pad = dylinkerCmdSize - 12 - nameLen;
        if (pad > 0)
            writePad(file, pad);
    }

    // --- LC_MAIN ---
    writeLE32(file, LC_MAIN);
    writeLE32(file, 24);
    writeLE64(file, mainEntryOff);
    writeLE64(file, 0);

    // --- LC_LOAD_DYLIB ---
    for (size_t di = 0; di < dylibs.size(); ++di)
    {
        const auto &dl = dylibs[di];
        const uint32_t cmdSize = static_cast<uint32_t>(dylibCmdSizes[di]);
        writeLE32(file, LC_LOAD_DYLIB);
        writeLE32(file, cmdSize);
        writeLE32(file, 24);       // name offset
        writeLE32(file, 2);        // timestamp
        writeLE32(file, 0x010000); // current_version
        writeLE32(file, 0x010000); // compat_version
        size_t nameLen = dl.path.size() + 1;
        file.insert(file.end(), dl.path.begin(), dl.path.end());
        file.push_back(0);
        size_t pad = cmdSize - 24 - nameLen;
        if (pad > 0)
            writePad(file, pad);
    }

    // --- LC_SYMTAB ---
    writeLE32(file, LC_SYMTAB);
    writeLE32(file, 24);
    writeLE32(file, symtabOff);
    writeLE32(file, nExtDef + nUndef);
    writeLE32(file, strtabOff);
    writeLE32(file, static_cast<uint32_t>(strtabData.size()));

    // --- LC_DYSYMTAB ---
    writeLE32(file, LC_DYSYMTAB);
    writeLE32(file, 80);
    writeLE32(file, 0);
    writeLE32(file, 0); // ilocalsym, nlocalsym
    writeLE32(file, 0);
    writeLE32(file, nExtDef); // iextdefsym, nextdefsym
    writeLE32(file, nExtDef);
    writeLE32(file, nUndef); // iundefsym, nundefsym
    for (int i = 0; i < 12; ++i)
        writeLE32(file, 0); // remaining fields (toc, modtab, extref, indirect, extrel, locrel)

    // --- LC_DYLD_INFO_ONLY ---
    if (hasDynamic)
    {
        writeLE32(file, LC_DYLD_INFO_ONLY);
        writeLE32(file, 48);
        writeLE32(file, rebaseData.empty() ? 0 : rebaseOff);
        writeLE32(file, static_cast<uint32_t>(rebaseData.size())); // rebase
        writeLE32(file, bindOff);
        writeLE32(file, static_cast<uint32_t>(bindData.size())); // bind
        writeLE32(file, 0);
        writeLE32(file, 0); // weak_bind
        writeLE32(file, 0);
        writeLE32(file, 0); // lazy_bind
        writeLE32(file, 0);
        writeLE32(file, 0); // export
    }

    // --- LC_BUILD_VERSION ---
    writeLE32(file, LC_BUILD_VERSION);
    writeLE32(file, 24);
    writeLE32(file, PLATFORM_MACOS);
    writeLE32(file, 0x000E0000); // minos: 14.0.0
    writeLE32(file, 0x000F0000); // sdk: 15.0.0
    writeLE32(file, 0);

    // --- LC_CODE_SIGNATURE ---
    if (needsCodeSign)
    {
        writeLE32(file, LC_CODE_SIGNATURE);
        writeLE32(file, 16);
        writeLE32(file, static_cast<uint32_t>(codeSignOff));
        writeLE32(file, static_cast<uint32_t>(codeSignSize));
    }

    // =======================================================================
    // Phase 5: Write segment data.
    // =======================================================================

    // Write text sections at their VA-relative file positions.
    // Since __TEXT fileoff=0, each section's file position = sec.VA - textSegVmAddr.
    for (size_t idx : textSections)
    {
        const auto &sec = layout.sections[idx];
        size_t targetOff = static_cast<size_t>(sec.virtualAddr - textSegVmAddr);
        if (file.size() < targetOff)
            writePad(file, targetOff - file.size());
        file.insert(file.end(), sec.data.begin(), sec.data.end());
    }

    // Write data sections at their VA-relative file positions within __DATA.
    if (!dataSections.empty())
    {
        for (size_t idx : dataSections)
        {
            const auto &sec = layout.sections[idx];
            size_t targetOff = dataFileOff + static_cast<size_t>(sec.virtualAddr - dataSegVmAddr);
            if (file.size() < targetOff)
                writePad(file, targetOff - file.size());
            file.insert(file.end(), sec.data.begin(), sec.data.end());
        }
    }

    // Pad to __LINKEDIT start.
    if (file.size() < linkeditFileOff)
        writePad(file, linkeditFileOff - file.size());

    // __LINKEDIT content: symtab → strtab → rebase → bind.
    file.insert(file.end(), symtabData.begin(), symtabData.end());
    file.insert(file.end(), strtabData.begin(), strtabData.end());
    file.insert(file.end(), rebaseData.begin(), rebaseData.end());
    file.insert(file.end(), bindData.begin(), bindData.end());

    // Append native code signature (arm64) or just pad to final page.
    if (needsCodeSign)
    {
        // Pad to code signature offset (16-byte aligned within __LINKEDIT).
        if (file.size() < codeSignOff)
            writePad(file, codeSignOff - file.size());

        // Build linker-signed ad-hoc code signature with CS_LINKER_SIGNED flag.
        auto sig =
            buildCodeSignature(file, codeSignOff, codeSignIdent, textSegFileOff, textSegFileSize);
        file.insert(file.end(), sig.begin(), sig.end());
    }

    // Pad to final page boundary.
    size_t finalPad = alignUp(file.size(), pageSize) - file.size();
    if (finalPad > 0)
        writePad(file, finalPad);

    // =======================================================================
    // Phase 6: Write to disk + make executable.
    // =======================================================================
    std::ofstream f_out(path, std::ios::binary);
    if (!f_out)
    {
        err << "error: cannot open '" << path << "' for writing\n";
        return false;
    }
    f_out.write(reinterpret_cast<const char *>(file.data()),
                static_cast<std::streamsize>(file.size()));
    if (!f_out)
    {
        err << "error: write failed to '" << path << "'\n";
        return false;
    }
    f_out.close();

#if !defined(_WIN32)
    std::error_code ec;
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write | std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        ec);
#endif

    return true;
}

} // namespace viper::codegen::linker
