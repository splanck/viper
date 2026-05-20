//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/CoffWriter.cpp
// Purpose: Serialize CodeSection data into a valid COFF relocatable object file
//          for the Windows platform (x86_64 and AArch64).
// Key invariants:
//   - All multi-byte fields are little-endian
//   - File layout: COFF header (20B) | section headers (40B each) |
//     .text data | .text relocs | generated unwind sections | symbol table |
//     string table
//   - Symbol names <= 8 chars are stored inline; longer names use string table
//   - String table starts with a 4-byte size field (includes itself)
//   - COFF relocations are 10 bytes: offset(4) + symIdx(4) + type(2)
//   - No explicit addend field; addends are embedded in instruction bytes
// Ownership/Lifetime:
//   - Stateless between write() calls
// Links: codegen/common/objfile/CoffWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/CoffWriter.hpp"
#include "codegen/common/objfile/ObjFileWriterUtil.hpp"
#include "codegen/common/objfile/RelocationValidation.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::objfile {

// =============================================================================
// COFF Constants
// =============================================================================

static constexpr uint16_t kMachineAMD64 = 0x8664;
static constexpr uint16_t kMachineARM64 = 0xAA64;

static constexpr uint16_t kCoffHeaderSize = 20;
static constexpr uint16_t kSectionHeaderSize = 40;

static constexpr uint32_t kImageScnCntCode = 0x00000020;
static constexpr uint32_t kImageScnCntInitData = 0x00000040;
static constexpr uint32_t kImageScnAlignText = 0x00600000;
static constexpr uint32_t kImageScnAlign4 = 0x00300000;
static constexpr uint32_t kImageScnAlign8 = 0x00400000;
static constexpr uint32_t kImageScnLnkNrelocOvfl = 0x01000000;
static constexpr uint32_t kImageScnMemExecute = 0x20000000;
static constexpr uint32_t kImageScnMemDiscardable = 0x02000000;
static constexpr uint32_t kImageScnMemRead = 0x40000000;
static constexpr uint32_t kImageScnAlign1 = 0x00100000;

static constexpr uint8_t kImageSymClassExternal = 2;
static constexpr uint8_t kImageSymClassStatic = 3;

static constexpr int16_t kImageSymUndefined = 0;

static constexpr uint32_t kCoffRelocSize = 10;
static constexpr uint32_t kCoffMaxStandardRelocs = 0xFFFFu;

static constexpr uint16_t kImageRelAMD64_Addr64 = 1;
static constexpr uint16_t kImageRelAMD64_Addr32Nb = 3;
static constexpr uint16_t kImageRelAMD64_Rel32 = 4;

static constexpr uint16_t kImageRelARM64_Addr64 = 14;
static constexpr uint16_t kImageRelARM64_Addr32Nb = 2;
static constexpr uint16_t kImageRelARM64_Branch26 = 3;
static constexpr uint16_t kImageRelARM64_PagebaseRel21 = 4;
static constexpr uint16_t kImageRelARM64_Pageoffset12A = 6;
static constexpr uint16_t kImageRelARM64_Pageoffset12L = 7;
static constexpr uint16_t kImageRelARM64_Branch19 = 15;

static uint16_t coffRelocType(RelocKind kind, ObjArch arch) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
            return kImageRelAMD64_Rel32;
        case RelocKind::Abs64:
            return arch == ObjArch::AArch64 ? kImageRelARM64_Addr64 : kImageRelAMD64_Addr64;
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
            return kImageRelARM64_Branch26;
        case RelocKind::A64AdrpPage21:
            return kImageRelARM64_PagebaseRel21;
        case RelocKind::A64AddPageOff12:
            return kImageRelARM64_Pageoffset12A;
        case RelocKind::A64LdSt32Off12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64LdSt128Off12:
            return kImageRelARM64_Pageoffset12L;
        case RelocKind::A64CondBr19:
            return kImageRelARM64_Branch19;
    }
    return 0;
}

static void writeLE32At(std::vector<uint8_t> &bytes, size_t off, uint32_t value) {
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
    bytes[off + 2] = static_cast<uint8_t>(value >> 16);
    bytes[off + 3] = static_cast<uint8_t>(value >> 24);
}

static void writeLE64At(std::vector<uint8_t> &bytes, size_t off, uint64_t value) {
    for (size_t i = 0; i < 8; ++i)
        bytes[off + i] = static_cast<uint8_t>(value >> (i * 8));
}

static uint32_t readLE32At(const std::vector<uint8_t> &bytes, size_t off) {
    return static_cast<uint32_t>(bytes[off]) | (static_cast<uint32_t>(bytes[off + 1]) << 8) |
           (static_cast<uint32_t>(bytes[off + 2]) << 16) |
           (static_cast<uint32_t>(bytes[off + 3]) << 24);
}

static bool checkedI32(int64_t value, const char *what, std::ostream &err, int32_t &out) {
    if (value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        err << "CoffWriter: " << what << " addend " << value
            << " is outside signed 32-bit range\n";
        return false;
    }
    out = static_cast<int32_t>(value);
    return true;
}

static bool checkedA64BranchAddend(int64_t value,
                                   unsigned bits,
                                   const char *what,
                                   std::ostream &err,
                                   int64_t &scaled) {
    if ((value & 0x3) != 0) {
        err << "CoffWriter: " << what << " addend " << value
            << " is not instruction aligned\n";
        return false;
    }
    scaled = value >> 2;
    const int64_t minVal = -(int64_t{1} << (bits - 1));
    const int64_t maxVal = (int64_t{1} << (bits - 1)) - 1;
    if (scaled < minVal || scaled > maxVal) {
        err << "CoffWriter: " << what << " addend " << value << " is out of range\n";
        return false;
    }
    return true;
}

static bool patchCoffRelocationAddend(const CodeSection &section,
                                      const Relocation &rel,
                                      int64_t effectiveAddend,
                                      std::vector<uint8_t> &bytes,
                                      std::ostream &err) {
    if (rel.offset < section.logicalOffsetBias()) {
        err << "CoffWriter: relocation offset is before logical section bias\n";
        return false;
    }
    const size_t physicalOffset = rel.offset - section.logicalOffsetBias();

    switch (rel.kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32: {
            int32_t encoded = 0;
            if (effectiveAddend > std::numeric_limits<int64_t>::max() - 4 ||
                effectiveAddend < std::numeric_limits<int64_t>::min() + 4 ||
                !checkedI32(effectiveAddend + 4, "x86_64 rel32", err, encoded))
                return false;
            writeLE32At(bytes, physicalOffset, static_cast<uint32_t>(encoded));
            return true;
        }

        case RelocKind::Abs64: {
            if (effectiveAddend == 0)
                return true;
            if (!section.containsOffsetRange(rel.offset, 8)) {
                err << "CoffWriter: 64-bit addend relocation at offset " << rel.offset
                    << " is out of bounds\n";
                return false;
            }
            writeLE64At(bytes, physicalOffset, static_cast<uint64_t>(effectiveAddend));
            return true;
        }

        case RelocKind::A64Call26:
        case RelocKind::A64Jump26: {
            int64_t scaled = 0;
            if (!checkedA64BranchAddend(effectiveAddend, 26, "AArch64 branch26", err, scaled))
                return false;
            uint32_t insn = readLE32At(bytes, physicalOffset);
            insn = (insn & 0xFC000000u) | (static_cast<uint32_t>(scaled) & 0x03FFFFFFu);
            writeLE32At(bytes, physicalOffset, insn);
            return true;
        }

        case RelocKind::A64AdrpPage21: {
            const int64_t pageAddend = effectiveAddend >> 12;
            if (pageAddend < -(int64_t{1} << 20) || pageAddend > ((int64_t{1} << 20) - 1)) {
                err << "CoffWriter: AArch64 ADRP addend " << effectiveAddend
                    << " is out of range\n";
                return false;
            }
            uint32_t insn = readLE32At(bytes, physicalOffset);
            const uint32_t imm = static_cast<uint32_t>(pageAddend);
            const uint32_t immlo = imm & 0x3u;
            const uint32_t immhi = (imm >> 2) & 0x7FFFFu;
            insn = (insn & 0x9F00001Fu) | (immlo << 29) | (immhi << 5);
            writeLE32At(bytes, physicalOffset, insn);
            return true;
        }

        case RelocKind::A64AddPageOff12: {
            const uint32_t pageOff = static_cast<uint32_t>(effectiveAddend) & 0xFFFu;
            uint32_t insn = readLE32At(bytes, physicalOffset);
            insn = (insn & 0xFFC003FFu) |
                   (pageOff << 10);
            writeLE32At(bytes, physicalOffset, insn);
            return true;
        }

        case RelocKind::A64LdSt32Off12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64LdSt128Off12: {
            const uint32_t shift = rel.kind == RelocKind::A64LdSt32Off12
                                       ? 2u
                                       : (rel.kind == RelocKind::A64LdSt64Off12 ? 3u : 4u);
            const uint32_t pageOff = static_cast<uint32_t>(effectiveAddend) & 0xFFFu;
            const uint32_t scale = 1u << shift;
            if ((pageOff & (scale - 1u)) != 0) {
                err << "CoffWriter: AArch64 load/store addend " << effectiveAddend
                    << " has a misaligned page offset\n";
                return false;
            }
            uint32_t insn = readLE32At(bytes, physicalOffset);
            insn = (insn & 0xFFC003FFu) |
                   ((pageOff >> shift) << 10);
            writeLE32At(bytes, physicalOffset, insn);
            return true;
        }

        case RelocKind::A64CondBr19: {
            int64_t scaled = 0;
            if (!checkedA64BranchAddend(effectiveAddend, 19, "AArch64 branch19", err, scaled))
                return false;
            uint32_t insn = readLE32At(bytes, physicalOffset);
            insn = (insn & 0xFF00001Fu) | ((static_cast<uint32_t>(scaled) & 0x7FFFFu) << 5);
            writeLE32At(bytes, physicalOffset, insn);
            return true;
        }
    }

    return true;
}

static void writeSectionHeader(std::vector<uint8_t> &out,
                               const char *name,
                               uint32_t virtualSize,
                               uint32_t virtualAddr,
                               uint32_t rawDataSize,
                               uint32_t rawDataPtr,
                               uint32_t relocPtr,
                               uint32_t numRelocs,
                               uint32_t characteristics) {
    for (int i = 0; i < 8; ++i) {
        if (name[i] != '\0') {
            out.push_back(static_cast<uint8_t>(name[i]));
        } else {
            for (int j = i; j < 8; ++j)
                out.push_back(0);
            break;
        }
    }

    appendLE32(out, virtualSize);
    appendLE32(out, virtualAddr);
    appendLE32(out, rawDataSize);
    appendLE32(out, rawDataPtr);
    appendLE32(out, relocPtr);
    appendLE32(out, 0);
    appendLE16(out, static_cast<uint16_t>(numRelocs));
    appendLE16(out, 0);
    appendLE32(out, characteristics);
}

static void writeSymbol(std::vector<uint8_t> &out,
                        const std::string &name,
                        uint32_t strTabOffset,
                        uint32_t value,
                        int16_t sectionNumber,
                        uint16_t type,
                        uint8_t storageClass) {
    if (name.size() <= 8) {
        for (size_t i = 0; i < 8; ++i) {
            if (i < name.size())
                out.push_back(static_cast<uint8_t>(name[i]));
            else
                out.push_back(0);
        }
    } else {
        appendLE32(out, 0);
        appendLE32(out, strTabOffset);
    }

    appendLE32(out, value);
    appendLE16(out, static_cast<uint16_t>(sectionNumber));
    appendLE16(out, type);
    out.push_back(storageClass);
    out.push_back(0);
}

static void writeReloc(std::vector<uint8_t> &out,
                       uint32_t virtualAddr,
                       uint32_t symbolTableIndex,
                       uint16_t type) {
    appendLE32(out, virtualAddr);
    appendLE32(out, symbolTableIndex);
    appendLE16(out, type);
}

static bool checkedU32(size_t value, const char *what, std::ostream &err, uint32_t &out) {
    if (value > UINT32_MAX) {
        err << "CoffWriter: " << what << " exceeds 32-bit COFF limit\n";
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

static bool addU32Checked(uint32_t a, uint32_t b, const char *what, std::ostream &err, uint32_t &out) {
    if (a > UINT32_MAX - b) {
        err << "CoffWriter: " << what << " exceeds 32-bit COFF limit\n";
        return false;
    }
    out = a + b;
    return true;
}

static bool alignU32Checked(uint32_t value,
                            uint32_t align,
                            const char *what,
                            std::ostream &err,
                            uint32_t &out) {
    size_t aligned = 0;
    if (!checkedAlignUpSize(
            static_cast<size_t>(value), align, "CoffWriter", what, err, aligned))
        return false;
    return checkedU32(aligned, what, err, out);
}

static bool checkedPhysicalSymbolValue(const CodeSection &section,
                                       const Symbol &sym,
                                       const char *sectionName,
                                       std::ostream &err,
                                       uint32_t &out) {
    if (sym.offset < section.logicalOffsetBias()) {
        err << "CoffWriter: symbol '" << sym.name << "' in " << sectionName
            << " is before the section logical offset bias\n";
        return false;
    }
    const size_t physicalOffset = sym.offset - section.logicalOffsetBias();
    if (physicalOffset > section.bytes().size()) {
        err << "CoffWriter: symbol '" << sym.name << "' in " << sectionName
            << " is outside section contents\n";
        return false;
    }
    return checkedU32(physicalOffset, "symbol value", err, out);
}

static bool checkedCoffReserveSize(uint32_t symtabOff,
                                   size_t symtabSize,
                                   size_t strtabSize,
                                   std::ostream &err,
                                   size_t &out) {
    size_t total = 0;
    if (!checkedAddSize(static_cast<size_t>(symtabOff),
                        symtabSize,
                        "CoffWriter",
                        "file reserve size",
                        err,
                        total))
        return false;
    return checkedAddSize(total, strtabSize, "CoffWriter", "file reserve size", err, out);
}

static bool validateSectionHeaderName(const std::string &name,
                                      const char *what,
                                      std::ostream &err) {
    if (name.size() > 8) {
        err << "CoffWriter: encoded section name reference for " << what
            << " exceeds COFF's 8-byte section-name field\n";
        return false;
    }
    return true;
}

static bool rememberDefinedGlobal(std::unordered_set<std::string> &names,
                                  const Symbol &sym,
                                  const char *sectionName,
                                  std::ostream &err) {
    if (sym.binding != SymbolBinding::Global)
        return true;
    if (!names.insert(sym.name).second) {
        err << "CoffWriter: duplicate global symbol '" << sym.name << "' in " << sectionName
            << "\n";
        return false;
    }
    return true;
}

static void addRelocationOverflowRecord(std::vector<uint8_t> &relocBytes, uint32_t relocCount) {
    if (relocCount <= kCoffMaxStandardRelocs)
        return;
    std::vector<uint8_t> withOverflow;
    withOverflow.reserve(relocBytes.size() + kCoffRelocSize);
    writeReloc(withOverflow, relocCount + 1u, 0, 0);
    withOverflow.insert(withOverflow.end(), relocBytes.begin(), relocBytes.end());
    relocBytes.swap(withOverflow);
}

static uint32_t coffHeaderRelocCount(uint32_t relocCount) {
    return relocCount > kCoffMaxStandardRelocs ? kCoffMaxStandardRelocs : relocCount;
}

static uint32_t coffRelocRecordCount(uint32_t relocCount) {
    return relocCount + (relocCount > kCoffMaxStandardRelocs ? 1u : 0u);
}

static bool coffRelocTableSize(uint32_t relocCount,
                               const char *what,
                               std::ostream &err,
                               uint32_t &out) {
    const uint64_t bytes =
        static_cast<uint64_t>(coffRelocRecordCount(relocCount)) * kCoffRelocSize;
    if (bytes > UINT32_MAX) {
        err << "CoffWriter: " << what << " relocation table exceeds 32-bit COFF limit\n";
        return false;
    }
    out = static_cast<uint32_t>(bytes);
    return true;
}

struct PendingCoffSymbol {
    std::string name;
    uint32_t value{0};
    uint16_t type{0};
    uint8_t storageClass{kImageSymClassStatic};
};

struct PendingCoffReloc {
    uint32_t offset{0};
    std::string symbolName;
    uint16_t type{0};
    bool hasSymbolRef{false};
    uint64_t symbolSectionIdentity{0};
    uint32_t symbolIndex{0};
};

static bool validatePdataRelocOffset(uint32_t offset,
                                      size_t pdataSize,
                                      std::ostream &err) {
    if (static_cast<size_t>(offset) > pdataSize || 4 > pdataSize - static_cast<size_t>(offset)) {
        err << "CoffWriter: .pdata relocation at offset " << offset
            << " extends beyond .pdata contents\n";
        return false;
    }
    return true;
}

static size_t win64UnwindSlotCount(const Win64UnwindCode &code) {
    switch (code.kind) {
        case Win64UnwindCode::Kind::PushNonVol:
            return 1;
        case Win64UnwindCode::Kind::AllocStack:
            if (code.stackOffset <= 128)
                return 1;
            if (code.stackOffset <= 524280)
                return 2;
            return 3;
        case Win64UnwindCode::Kind::SaveNonVol:
            if ((code.stackOffset / 8) <= 0xFFFF)
                return 2;
            return 3;
        case Win64UnwindCode::Kind::SaveXmm128:
            if ((code.stackOffset / 16) <= 0xFFFF)
                return 2;
            return 3;
    }
    return 0;
}

static void emitWin64UnwindNodes(std::vector<uint8_t> &out, const Win64UnwindCode &code) {
    constexpr uint8_t kUwopPushNonVol = 0;
    constexpr uint8_t kUwopAllocLarge = 1;
    constexpr uint8_t kUwopAllocSmall = 2;
    constexpr uint8_t kUwopSaveNonVol = 4;
    constexpr uint8_t kUwopSaveNonVolFar = 5;
    constexpr uint8_t kUwopSaveXmm128 = 8;
    constexpr uint8_t kUwopSaveXmm128Far = 9;

    const auto emitNode = [&](uint8_t codeOffset, uint8_t unwindOp, uint8_t opInfo) {
        out.push_back(codeOffset);
        out.push_back(static_cast<uint8_t>(unwindOp | (opInfo << 4)));
    };

    switch (code.kind) {
        case Win64UnwindCode::Kind::PushNonVol:
            emitNode(code.codeOffset, kUwopPushNonVol, code.reg);
            return;
        case Win64UnwindCode::Kind::AllocStack:
            if (code.stackOffset <= 128) {
                emitNode(code.codeOffset,
                         kUwopAllocSmall,
                         static_cast<uint8_t>((code.stackOffset - 8) / 8));
                return;
            }
            if (code.stackOffset <= 524280) {
                emitNode(code.codeOffset, kUwopAllocLarge, 0);
                appendLE16(out, static_cast<uint16_t>(code.stackOffset / 8));
                return;
            }
            emitNode(code.codeOffset, kUwopAllocLarge, 1);
            appendLE16(out, static_cast<uint16_t>(code.stackOffset & 0xFFFF));
            appendLE16(out, static_cast<uint16_t>(code.stackOffset >> 16));
            return;
        case Win64UnwindCode::Kind::SaveNonVol:
            if ((code.stackOffset / 8) <= 0xFFFF) {
                emitNode(code.codeOffset, kUwopSaveNonVol, code.reg);
                appendLE16(out, static_cast<uint16_t>(code.stackOffset / 8));
                return;
            }
            emitNode(code.codeOffset, kUwopSaveNonVolFar, code.reg);
            appendLE16(out, static_cast<uint16_t>(code.stackOffset & 0xFFFF));
            appendLE16(out, static_cast<uint16_t>(code.stackOffset >> 16));
            return;
        case Win64UnwindCode::Kind::SaveXmm128:
            if ((code.stackOffset / 16) <= 0xFFFF) {
                emitNode(code.codeOffset, kUwopSaveXmm128, code.reg);
                appendLE16(out, static_cast<uint16_t>(code.stackOffset / 16));
                return;
            }
            emitNode(code.codeOffset, kUwopSaveXmm128Far, code.reg);
            appendLE16(out, static_cast<uint16_t>(code.stackOffset & 0xFFFF));
            appendLE16(out, static_cast<uint16_t>(code.stackOffset >> 16));
            return;
    }
}

static bool validateWin64UnwindCode(const Win64UnwindEntry &entry,
                                    const Win64UnwindCode &code,
                                    std::ostream &err) {
    if (code.codeOffset == 0 || code.codeOffset > entry.prologueSize) {
        err << "CoffWriter: Win64 unwind code offset " << static_cast<unsigned>(code.codeOffset)
            << " is outside the function prologue\n";
        return false;
    }

    switch (code.kind) {
        case Win64UnwindCode::Kind::PushNonVol:
            if (code.reg > 15) {
                err << "CoffWriter: Win64 push unwind register is out of range\n";
                return false;
            }
            return true;
        case Win64UnwindCode::Kind::AllocStack:
            if (code.stackOffset < 8 || (code.stackOffset % 8) != 0) {
                err << "CoffWriter: Win64 stack allocation unwind offset must be >= 8 and "
                       "8-byte aligned\n";
                return false;
            }
            return true;
        case Win64UnwindCode::Kind::SaveNonVol:
            if (code.reg > 15 || (code.stackOffset % 8) != 0) {
                err << "CoffWriter: Win64 nonvolatile save unwind offset must be 8-byte aligned\n";
                return false;
            }
            return true;
        case Win64UnwindCode::Kind::SaveXmm128:
            if (code.reg > 15 || (code.stackOffset % 16) != 0) {
                err << "CoffWriter: Win64 XMM save unwind offset must be 16-byte aligned\n";
                return false;
            }
            return true;
    }
    return true;
}

static bool buildWin64UnwindSections(const CodeSection &text,
                                     uint32_t xdataNameBase,
                                     std::vector<uint8_t> &xdataBytes,
                                     std::vector<PendingCoffSymbol> &xdataSymbols,
                                     std::vector<uint8_t> &pdataBytes,
                                     std::vector<PendingCoffReloc> &pdataRelocs,
                                     std::ostream &err) {
    const auto &entries = text.win64UnwindEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        if (entry.symbolIndex >= text.symbols().count()) {
            err << "CoffWriter: Win64 unwind entry references unknown symbol index "
                << entry.symbolIndex << "\n";
            return false;
        }
        const auto &funcSym = text.symbols().at(entry.symbolIndex);
        if (funcSym.binding == SymbolBinding::External ||
            funcSym.section == SymbolSection::Undefined) {
            err << "CoffWriter: Win64 unwind entry references undefined symbol '"
                << funcSym.name << "'\n";
            return false;
        }
        if (entry.prologueSize == 0 && !entry.codes.empty()) {
            err << "CoffWriter: Win64 unwind entry has codes but zero prologue size\n";
            return false;
        }
        uint32_t xdataOffset = 0;
        if (!checkedU32(xdataBytes.size(), ".xdata offset", err, xdataOffset))
            return false;
        uint32_t xdataOrdinal = 0;
        uint32_t entryOrdinal = 0;
        if (!checkedU32(i, ".xdata symbol ordinal", err, entryOrdinal) ||
            !addU32Checked(xdataNameBase, entryOrdinal, ".xdata symbol ordinal", err, xdataOrdinal))
            return false;
        const std::string xdataName = "$xdata$" + std::to_string(xdataOrdinal);
        xdataSymbols.push_back({xdataName, xdataOffset, 0, kImageSymClassStatic});

        std::vector<Win64UnwindCode> codes = entry.codes;
        std::stable_sort(codes.begin(), codes.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.codeOffset > rhs.codeOffset;
        });

        size_t codeSlots = 0;
        for (const auto &code : codes) {
            if (!validateWin64UnwindCode(entry, code, err))
                return false;
            codeSlots += win64UnwindSlotCount(code);
        }
        if (codeSlots > std::numeric_limits<uint8_t>::max()) {
            err << "CoffWriter: Win64 unwind code slot count exceeds 255\n";
            return false;
        }

        xdataBytes.push_back(1);
        xdataBytes.push_back(entry.prologueSize);
        xdataBytes.push_back(static_cast<uint8_t>(codeSlots));
        xdataBytes.push_back(0);
        for (const auto &code : codes)
            emitWin64UnwindNodes(xdataBytes, code);
        padTo(xdataBytes, alignUp(xdataBytes.size(), 4));

        uint32_t pdataOffset = 0;
        if (!checkedU32(pdataBytes.size(), ".pdata offset", err, pdataOffset))
            return false;
        appendLE32(pdataBytes, 0);
        appendLE32(pdataBytes, entry.functionLength);
        appendLE32(pdataBytes, 0);

        uint32_t pdataEndOffset = 0;
        uint32_t pdataUnwindOffset = 0;
        if (!addU32Checked(pdataOffset, 4, ".pdata unwind end relocation offset", err, pdataEndOffset) ||
            !addU32Checked(pdataOffset, 8, ".pdata xdata relocation offset", err, pdataUnwindOffset))
            return false;
        pdataRelocs.push_back({pdataOffset,
                               funcSym.name,
                               kImageRelAMD64_Addr32Nb,
                               true,
                               text.sectionIdentity(),
                               entry.symbolIndex});
        pdataRelocs.push_back({pdataEndOffset,
                               funcSym.name,
                               kImageRelAMD64_Addr32Nb,
                               true,
                               text.sectionIdentity(),
                               entry.symbolIndex});
        pdataRelocs.push_back({pdataUnwindOffset, xdataName, kImageRelAMD64_Addr32Nb});
    }
    return true;
}

static bool buildWinArm64UnwindSections(const CodeSection &text,
                                        uint32_t xdataNameBase,
                                        std::vector<uint8_t> &xdataBytes,
                                        std::vector<PendingCoffSymbol> &xdataSymbols,
                                        std::vector<uint8_t> &pdataBytes,
                                        std::vector<PendingCoffReloc> &pdataRelocs,
                                        std::ostream &err) {
    const auto &entries = text.winArm64UnwindEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        if (entry.symbolIndex >= text.symbols().count()) {
            err << "CoffWriter: Windows ARM64 unwind entry references unknown symbol index "
                << entry.symbolIndex << "\n";
            return false;
        }
        const auto &funcSym = text.symbols().at(entry.symbolIndex);
        if (funcSym.binding == SymbolBinding::External ||
            funcSym.section == SymbolSection::Undefined) {
            err << "CoffWriter: Windows ARM64 unwind entry references undefined symbol '"
                << funcSym.name << "'\n";
            return false;
        }
        if ((entry.functionLength & 0x3u) != 0) {
            err << "CoffWriter: Windows ARM64 function length must be instruction aligned\n";
            return false;
        }
        const uint32_t functionWords = entry.functionLength / 4;
        if (functionWords > 0x3FFFFu) {
            err << "CoffWriter: Windows ARM64 function length exceeds xdata header range\n";
            return false;
        }
        const size_t codeWordsSize = alignUp(entry.unwindCodes.size(), 4) / 4;
        if (codeWordsSize > 31) {
            err << "CoffWriter: Windows ARM64 unwind code word count exceeds 31\n";
            return false;
        }
        if (entry.packedEpilogInHeader && entry.epilogCodeIndex > 31) {
            err << "CoffWriter: Windows ARM64 epilog code index exceeds header range\n";
            return false;
        }

        uint32_t xdataOffset = 0;
        if (!checkedU32(xdataBytes.size(), ".xdata offset", err, xdataOffset))
            return false;
        uint32_t xdataOrdinal = 0;
        uint32_t entryOrdinal = 0;
        if (!checkedU32(i, ".xdata symbol ordinal", err, entryOrdinal) ||
            !addU32Checked(xdataNameBase, entryOrdinal, ".xdata symbol ordinal", err, xdataOrdinal))
            return false;
        const std::string xdataName = "$xdata$" + std::to_string(xdataOrdinal);
        xdataSymbols.push_back({xdataName, xdataOffset, 0, kImageSymClassStatic});

        const uint32_t epilogField =
            entry.packedEpilogInHeader ? entry.epilogCodeIndex : 0u;
        const uint32_t header = functionWords |
                                (0u << 18) | // version
                                (0u << 20) | // no exception data
                                ((entry.packedEpilogInHeader ? 1u : 0u) << 21) |
                                ((epilogField & 0x1Fu) << 22) |
                                (static_cast<uint32_t>(codeWordsSize) << 27);
        appendLE32(xdataBytes, header);
        xdataBytes.insert(xdataBytes.end(), entry.unwindCodes.begin(), entry.unwindCodes.end());
        padTo(xdataBytes, alignUp(xdataBytes.size(), 4));

        uint32_t pdataOffset = 0;
        if (!checkedU32(pdataBytes.size(), ".pdata offset", err, pdataOffset))
            return false;
        appendLE32(pdataBytes, 0);
        appendLE32(pdataBytes, 0);

        uint32_t pdataUnwindOffset = 0;
        if (!addU32Checked(pdataOffset, 4, ".pdata xdata relocation offset", err, pdataUnwindOffset))
            return false;
        pdataRelocs.push_back({pdataOffset,
                               funcSym.name,
                               kImageRelARM64_Addr32Nb,
                               true,
                               text.sectionIdentity(),
                               entry.symbolIndex});
        pdataRelocs.push_back({pdataUnwindOffset, xdataName, kImageRelARM64_Addr32Nb});
    }
    return true;
}

bool CoffWriter::write(const std::string &path,
                       const CodeSection &text,
                       const CodeSection &rodata,
                       std::ostream &err) {
    try {
    std::vector<uint8_t> xdataBytes;
    std::vector<PendingCoffSymbol> xdataSymbols;
    std::vector<uint8_t> pdataBytes;
    std::vector<PendingCoffReloc> pdataRelocs;
    if (arch_ == ObjArch::X86_64 && !text.win64UnwindEntries().empty()) {
        if (!buildWin64UnwindSections(
                text, 0, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs, err))
            return false;
    } else if (arch_ == ObjArch::AArch64 && !text.winArm64UnwindEntries().empty()) {
        if (!buildWinArm64UnwindSections(
                text, 0, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs, err))
            return false;
    }

    const bool hasRodata = !rodata.empty();
    const bool hasXdata = !xdataBytes.empty();
    const bool hasPdata = !pdataBytes.empty();
    const bool hasDebugLine = !debugLineData_.empty();

    uint32_t nextSecIndex = 1;
    const uint16_t secIdxText = static_cast<uint16_t>(nextSecIndex++);
    const uint16_t secIdxRdata = hasRodata ? static_cast<uint16_t>(nextSecIndex++) : 0;
    const uint16_t secIdxXdata = hasXdata ? static_cast<uint16_t>(nextSecIndex++) : 0;
    if (hasPdata)
        ++nextSecIndex;
    const uint32_t sectionCount = (nextSecIndex - 1) + (hasDebugLine ? 1u : 0u);
    if (sectionCount > static_cast<uint32_t>(std::numeric_limits<int16_t>::max())) {
        err << "CoffWriter: section count exceeds standard COFF symbol section-number range; "
               "BigObj output is not available\n";
        return false;
    }
    const uint16_t numSections = static_cast<uint16_t>(sectionCount);

    std::vector<uint8_t> symtabBytes;
    std::vector<uint8_t> strtabBytes(4, 0);
    std::unordered_map<uint32_t, uint32_t> textSymMap;
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;
    std::unordered_map<std::string, uint32_t> definedNameMap;
    std::unordered_map<std::string, uint32_t> definedGlobalNameMap;
    std::unordered_map<std::string, uint32_t> externalNameMap;
    std::unordered_set<std::string> definedGlobalNames;
    uint32_t coffSymCount = 0;

    auto relocationNeedsAnchor = [](const CodeSection &sec, SymbolSection targetSection) {
        for (const auto &rel : sec.relocations())
            if (rel.targetOffsetValid && rel.targetSection == targetSection)
                return true;
        return false;
    };
    const bool needTextAnchor = relocationNeedsAnchor(text, SymbolSection::Text) ||
                                relocationNeedsAnchor(rodata, SymbolSection::Text);
    const bool needRodataAnchor = hasRodata &&
                                  (relocationNeedsAnchor(text, SymbolSection::Rodata) ||
                                   relocationNeedsAnchor(rodata, SymbolSection::Rodata));
    uint32_t textAnchorIdx = UINT32_MAX;
    uint32_t rodataAnchorIdx = UINT32_MAX;

    if (needTextAnchor) {
        writeSymbol(symtabBytes,
                    ".text",
                    0,
                    0,
                    static_cast<int16_t>(secIdxText),
                    0,
                    kImageSymClassStatic);
        textAnchorIdx = coffSymCount++;
    }
    if (needRodataAnchor) {
        writeSymbol(symtabBytes,
                    ".rdata",
                    0,
                    0,
                    static_cast<int16_t>(secIdxRdata),
                    0,
                    kImageSymClassStatic);
        rodataAnchorIdx = coffSymCount++;
    }

    struct PendingExternal {
        bool fromText = false;
        uint32_t origIdx = 0;
        std::string name;
        uint16_t type = 0;
    };
    std::vector<PendingExternal> pendingExternals;

    auto addToStrTab = [&](const std::string &s) -> uint32_t {
        if (strtabBytes.size() > std::numeric_limits<uint32_t>::max())
            throw std::length_error("CoffWriter: string table offset exceeds 32-bit COFF limit");
        if (s.size() > std::numeric_limits<size_t>::max() - strtabBytes.size() - 1 ||
            strtabBytes.size() + s.size() + 1 > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("CoffWriter: string table size exceeds 32-bit COFF limit");
        }
        uint32_t offset = static_cast<uint32_t>(strtabBytes.size());
        strtabBytes.insert(strtabBytes.end(), s.begin(), s.end());
        strtabBytes.push_back(0);
        return offset;
    };

    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            int16_t secNum = 0;
            uint32_t value = 0;
            uint8_t storageClass = kImageSymClassExternal;

            if (s.binding == SymbolBinding::External) {
                pendingExternals.push_back({false, i, s.name, 0});
                continue;
            } else if (s.binding == SymbolBinding::Local) {
                secNum = static_cast<int16_t>(secIdxRdata);
                if (!checkedPhysicalSymbolValue(rodata, s, ".rdata", err, value))
                    return false;
                storageClass = kImageSymClassStatic;
            } else {
                if (!rememberDefinedGlobal(definedGlobalNames, s, ".rdata", err))
                    return false;
                secNum = static_cast<int16_t>(secIdxRdata);
                if (!checkedPhysicalSymbolValue(rodata, s, ".rdata", err, value))
                    return false;
            }

            uint32_t strOff = 0;
            if (s.name.size() > 8)
                strOff = addToStrTab(s.name);

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            rodataSymMap[i] = coffIdx;
            if (s.binding != SymbolBinding::External) {
                definedNameMap[s.name] = coffIdx;
                if (s.binding == SymbolBinding::Global)
                    definedGlobalNameMap[s.name] = coffIdx;
            }
        }
    }

    std::unordered_map<std::string, uint32_t> definedRodataByName;
    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External)
                continue;
            auto it = rodataSymMap.find(i);
            if (it != rodataSymMap.end()) {
                auto [nameIt, inserted] = definedRodataByName.emplace(s.name, it->second);
                if (!inserted)
                    nameIt->second = UINT32_MAX;
            }
        }
    }

    if (hasXdata) {
        for (const auto &sym : xdataSymbols) {
            uint32_t strOff = 0;
            if (sym.name.size() > 8)
                strOff = addToStrTab(sym.name);
            writeSymbol(symtabBytes,
                        sym.name,
                        strOff,
                        sym.value,
                        static_cast<int16_t>(secIdxXdata),
                        sym.type,
                        sym.storageClass);
            definedNameMap[sym.name] = coffSymCount++;
        }
    }

    std::unordered_set<uint32_t> crossSectionTextAliases;
    std::unordered_set<uint32_t> undefinedTextRefs;
    for (const auto &rel : text.relocations()) {
        if (rel.symbolIndex >= text.symbols().count())
            continue;
        if (rel.targetSection != SymbolSection::Undefined)
            crossSectionTextAliases.insert(rel.symbolIndex);
        else
            undefinedTextRefs.insert(rel.symbolIndex);
    }

    for (uint32_t i = 1; i < text.symbols().count(); ++i) {
        const Symbol &s = text.symbols().at(i);
        if (s.binding == SymbolBinding::External) {
            if (crossSectionTextAliases.count(i) != 0 && undefinedTextRefs.count(i) == 0) {
                continue;
            }
            pendingExternals.push_back({true, i, s.name, 0x20});
            continue;
        }

        int16_t secNum = 0;
        uint32_t value = 0;
        uint8_t storageClass = kImageSymClassExternal;

        if (s.binding == SymbolBinding::External) {
            secNum = kImageSymUndefined;
        } else if (s.binding == SymbolBinding::Local) {
            secNum = static_cast<int16_t>(secIdxText);
            if (!checkedPhysicalSymbolValue(text, s, ".text", err, value))
                return false;
            storageClass = kImageSymClassStatic;
        } else {
            if (!rememberDefinedGlobal(definedGlobalNames, s, ".text", err))
                return false;
            secNum = static_cast<int16_t>(secIdxText);
            if (!checkedPhysicalSymbolValue(text, s, ".text", err, value))
                return false;
        }

        uint32_t strOff = 0;
        if (s.name.size() > 8)
            strOff = addToStrTab(s.name);

        writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0x20, storageClass);
        const uint32_t coffIdx = coffSymCount++;
        textSymMap[i] = coffIdx;
        if (s.binding != SymbolBinding::External) {
            definedNameMap[s.name] = coffIdx;
            if (s.binding == SymbolBinding::Global)
                definedGlobalNameMap[s.name] = coffIdx;
        }
    }

    for (const auto &ext : pendingExternals) {
        const auto definedIt = definedGlobalNameMap.find(ext.name);
        if (definedIt != definedGlobalNameMap.end()) {
            if (ext.fromText)
                textSymMap[ext.origIdx] = definedIt->second;
            else
                rodataSymMap[ext.origIdx] = definedIt->second;
            continue;
        }

        auto extIt = externalNameMap.find(ext.name);
        if (extIt == externalNameMap.end()) {
            uint32_t strOff = 0;
            if (ext.name.size() > 8)
                strOff = addToStrTab(ext.name);
            writeSymbol(symtabBytes,
                        ext.name,
                        strOff,
                        0,
                        kImageSymUndefined,
                        ext.type,
                        kImageSymClassExternal);
            extIt = externalNameMap.emplace(ext.name, coffSymCount++).first;
        }

        if (ext.fromText)
            textSymMap[ext.origIdx] = extIt->second;
        else
            rodataSymMap[ext.origIdx] = extIt->second;
    }

    std::unordered_map<std::string, uint32_t> definedTextByName;
    for (uint32_t i = 1; i < text.symbols().count(); ++i) {
        const Symbol &s = text.symbols().at(i);
        if (s.binding == SymbolBinding::External)
            continue;
        auto it = textSymMap.find(i);
        if (it == textSymMap.end())
            continue;
        auto [nameIt, inserted] = definedTextByName.emplace(s.name, it->second);
        if (!inserted)
            nameIt->second = UINT32_MAX;
    }

    uint32_t debugLineStrOff = 0;
    if (hasDebugLine)
        debugLineStrOff = addToStrTab(".debug_line");

    uint32_t strtabSize = 0;
    if (!checkedU32(strtabBytes.size(), "string table size", err, strtabSize))
        return false;
    strtabBytes[0] = static_cast<uint8_t>(strtabSize);
    strtabBytes[1] = static_cast<uint8_t>(strtabSize >> 8);
    strtabBytes[2] = static_cast<uint8_t>(strtabSize >> 16);
    strtabBytes[3] = static_cast<uint8_t>(strtabSize >> 24);

    std::vector<uint8_t> patchedTextBytes = text.bytes();
    std::vector<uint8_t> patchedRodataBytes = rodata.bytes();
    std::vector<uint8_t> textRelocBytes;
    auto resolveRelocSym = [&](const Relocation &rel,
                               const CodeSection &source,
                               const std::unordered_map<uint32_t, uint32_t> &sourceMap,
                               const char *sectionName,
                               uint32_t &coffSymIdx,
                               int64_t &effectiveAddend) -> bool {
        coffSymIdx = 0;
        effectiveAddend = rel.addend;
        if (rel.targetSection != SymbolSection::Undefined) {
            if (rel.targetOffsetValid) {
                const CodeSection &target =
                    (rel.targetSection == SymbolSection::Text) ? text : rodata;
                const char *targetName =
                    (rel.targetSection == SymbolSection::Text) ? ".text" : ".rdata";
                const uint32_t anchorIdx =
                    (rel.targetSection == SymbolSection::Text) ? textAnchorIdx : rodataAnchorIdx;
                if (anchorIdx == UINT32_MAX) {
                    err << "CoffWriter: missing section anchor for " << targetName << "\n";
                    return false;
                }
                if (rel.targetOffset > target.bytes().size()) {
                    err << "CoffWriter: relocation in " << sectionName << " at offset "
                        << rel.offset << " references " << targetName << " offset "
                        << rel.targetOffset << " beyond section contents\n";
                    return false;
                }
                if (!checkedSectionOffsetAddend(rel.addend,
                                                rel.targetOffset,
                                                "CoffWriter",
                                                sectionName,
                                                rel.offset,
                                                err,
                                                effectiveAddend))
                    return false;
                coffSymIdx = anchorIdx;
                return true;
            }
            if (rel.symbolIndex >= source.symbols().count()) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references unknown symbol index " << rel.symbolIndex << "\n";
                return false;
            }
            const Symbol &sym = source.symbols().at(rel.symbolIndex);
            const auto &targetByName = (rel.targetSection == SymbolSection::Rodata)
                                           ? definedRodataByName
                                           : definedTextByName;
            auto nameIt = targetByName.find(sym.name);
            if (nameIt == targetByName.end()) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references missing cross-section target '" << sym.name << "'\n";
                return false;
            }
            if (nameIt->second == UINT32_MAX) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references ambiguous cross-section target '" << sym.name << "'\n";
                return false;
            }
            coffSymIdx = nameIt->second;
            return true;
        }

        auto it = sourceMap.find(rel.symbolIndex);
        if (it == sourceMap.end()) {
            err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                << " references unknown symbol index " << rel.symbolIndex << "\n";
            return false;
        }
        coffSymIdx = it->second;
        return true;
    };

    auto appendRelocBytes = [&](const CodeSection &source,
                                const std::unordered_map<uint32_t, uint32_t> &sourceMap,
                                const char *sectionName,
                                std::vector<uint8_t> &patchedBytes,
                                std::vector<uint8_t> &relocBytes) -> bool {
        for (const auto &rel : source.relocations()) {
            if (!validateRelocationShape("CoffWriter", arch_, source, rel, sectionName, err))
                return false;
            uint32_t coffSymIdx = 0;
            int64_t effectiveAddend = rel.addend;
            if (!resolveRelocSym(rel, source, sourceMap, sectionName, coffSymIdx, effectiveAddend))
                return false;
            if (!patchCoffRelocationAddend(source, rel, effectiveAddend, patchedBytes, err))
                return false;
            const size_t physicalRelOffset = rel.offset - source.logicalOffsetBias();
            uint32_t relocOff = 0;
            if (!checkedU32(physicalRelOffset, "relocation offset", err, relocOff))
                return false;
            writeReloc(relocBytes, relocOff, coffSymIdx, coffRelocType(rel.kind, arch_));
        }
        return true;
    };

    if (!appendRelocBytes(text, textSymMap, ".text", patchedTextBytes, textRelocBytes))
        return false;
    uint32_t numTextRelocs = 0;
    if (!checkedU32(text.relocations().size(), "text relocation count", err, numTextRelocs))
        return false;
    addRelocationOverflowRecord(textRelocBytes, numTextRelocs);

    std::vector<uint8_t> rdataRelocBytes;
    if (!appendRelocBytes(rodata, rodataSymMap, ".rdata", patchedRodataBytes, rdataRelocBytes))
        return false;
    uint32_t numRdataRelocs = 0;
    if (!checkedU32(rodata.relocations().size(), ".rdata relocation count", err, numRdataRelocs))
        return false;
    addRelocationOverflowRecord(rdataRelocBytes, numRdataRelocs);

    std::vector<uint8_t> pdataRelocBytes;
    for (const auto &rel : pdataRelocs) {
        if (!validatePdataRelocOffset(rel.offset, pdataBytes.size(), err))
            return false;
        uint32_t targetIdx = 0;
        if (rel.hasSymbolRef) {
            if (!text.matchesSectionIdentity(rel.symbolSectionIdentity)) {
                err << "CoffWriter: .pdata relocation for '" << rel.symbolName
                    << "' references a different text section identity\n";
                return false;
            }
            auto symIt = textSymMap.find(rel.symbolIndex);
            if (symIt == textSymMap.end()) {
                err << "CoffWriter: missing symbol '" << rel.symbolName
                    << "' for .pdata relocation\n";
                return false;
            }
            targetIdx = symIt->second;
        } else {
            auto it = definedNameMap.find(rel.symbolName);
            if (it == definedNameMap.end()) {
                err << "CoffWriter: missing symbol '" << rel.symbolName
                    << "' for .pdata relocation\n";
                return false;
            }
            targetIdx = it->second;
        }
        writeReloc(pdataRelocBytes, rel.offset, targetIdx, rel.type);
    }
    uint32_t numPdataRelocs = 0;
    if (!checkedU32(pdataRelocs.size(), ".pdata relocation count", err, numPdataRelocs))
        return false;
    addRelocationOverflowRecord(pdataRelocBytes, numPdataRelocs);

    uint32_t textSize = 0;
    uint32_t rdataSize = 0;
    uint32_t xdataSize = 0;
    uint32_t pdataSize = 0;
    uint32_t debugLineDataSize = 0;
    if (!checkedU32(patchedTextBytes.size(), ".text size", err, textSize) ||
        !checkedU32(hasRodata ? patchedRodataBytes.size() : 0, ".rdata size", err, rdataSize) ||
        !checkedU32(hasXdata ? xdataBytes.size() : 0, ".xdata size", err, xdataSize) ||
        !checkedU32(hasPdata ? pdataBytes.size() : 0, ".pdata size", err, pdataSize) ||
        !checkedU32(
            hasDebugLine ? debugLineData_.size() : 0, ".debug_line size", err, debugLineDataSize))
        return false;

    const uint32_t headerAreaSize = kCoffHeaderSize + numSections * kSectionHeaderSize;
    uint32_t textDataOff = 0;
    if (!alignU32Checked(headerAreaSize, 4, "header area", err, textDataOff))
        return false;
    uint32_t textRelocOff = 0;
    if (!addU32Checked(textDataOff, textSize, ".text relocation offset", err, textRelocOff))
        return false;
    uint32_t textRelocTotalSize = 0;
    if (!coffRelocTableSize(numTextRelocs, ".text", err, textRelocTotalSize))
        return false;
    uint32_t textRelocEnd = 0;
    if (!addU32Checked(textRelocOff, textRelocTotalSize, ".text relocation table", err, textRelocEnd))
        return false;
    uint32_t cursor = 0;
    if (!alignU32Checked(textRelocEnd, 4, ".text relocation table", err, cursor))
        return false;

    uint32_t rdataDataOff = 0;
    uint32_t rdataRelocOff = 0;
    if (hasRodata) {
        rdataDataOff = cursor;
        uint32_t end = 0;
        if (!addU32Checked(rdataDataOff, rdataSize, ".rdata relocation offset", err, rdataRelocOff))
            return false;
        uint32_t rdataRelocSize = 0;
        if (!coffRelocTableSize(numRdataRelocs, ".rdata", err, rdataRelocSize))
            return false;
        if (!addU32Checked(rdataRelocOff, rdataRelocSize, ".rdata relocation table", err, end))
            return false;
        if (!alignU32Checked(end, 4, ".rdata relocation table", err, cursor))
            return false;
    }
    uint32_t xdataDataOff = 0;
    if (hasXdata) {
        xdataDataOff = cursor;
        uint32_t end = 0;
        if (!addU32Checked(xdataDataOff, xdataSize, ".xdata data", err, end))
            return false;
        if (!alignU32Checked(end, 4, ".xdata data", err, cursor))
            return false;
    }
    uint32_t pdataDataOff = 0;
    uint32_t pdataRelocOff = 0;
    if (hasPdata) {
        pdataDataOff = cursor;
        if (!addU32Checked(pdataDataOff, pdataSize, ".pdata relocation offset", err, pdataRelocOff))
            return false;
        uint32_t pdataRelocEnd = 0;
        uint32_t pdataRelocSize = 0;
        if (!coffRelocTableSize(numPdataRelocs, ".pdata", err, pdataRelocSize))
            return false;
        if (!addU32Checked(
                pdataRelocOff, pdataRelocSize, ".pdata relocation table", err, pdataRelocEnd))
            return false;
        if (!alignU32Checked(pdataRelocEnd, 4, ".pdata relocation table", err, cursor))
            return false;
    }
    uint32_t debugLineDataOff = 0;
    if (hasDebugLine) {
        debugLineDataOff = cursor;
        uint32_t end = 0;
        if (!addU32Checked(debugLineDataOff, debugLineDataSize, ".debug_line data", err, end))
            return false;
        if (!alignU32Checked(end, 4, ".debug_line data", err, cursor))
            return false;
    }
    const uint32_t symtabOff = cursor;

    std::vector<uint8_t> file;
    size_t reserveSize = 0;
    if (!checkedCoffReserveSize(symtabOff, symtabBytes.size(), strtabBytes.size(), err, reserveSize))
        return false;
    file.reserve(reserveSize);

    const uint16_t machine = (arch_ == ObjArch::X86_64) ? kMachineAMD64 : kMachineARM64;
    appendLE16(file, machine);
    appendLE16(file, numSections);
    appendLE32(file, 0);
    appendLE32(file, symtabOff);
    appendLE32(file, coffSymCount);
    appendLE16(file, 0);
    appendLE16(file, 0);

    uint32_t textChars = kImageScnCntCode | kImageScnMemExecute | kImageScnMemRead;
    textChars |= (arch_ == ObjArch::X86_64) ? kImageScnAlignText : kImageScnAlign4;
    if (numTextRelocs > kCoffMaxStandardRelocs)
        textChars |= kImageScnLnkNrelocOvfl;
    writeSectionHeader(file,
                       ".text",
                       0,
                       0,
                       textSize,
                       textDataOff,
                       (numTextRelocs > 0) ? textRelocOff : 0,
                       coffHeaderRelocCount(numTextRelocs),
                       textChars);

    if (hasRodata) {
        uint32_t rdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign8;
        if (numRdataRelocs > kCoffMaxStandardRelocs)
            rdataChars |= kImageScnLnkNrelocOvfl;
        writeSectionHeader(file,
                           ".rdata",
                           0,
                           0,
                           rdataSize,
                           rdataDataOff,
                           (numRdataRelocs > 0) ? rdataRelocOff : 0,
                           coffHeaderRelocCount(numRdataRelocs),
                           rdataChars);
    }
    if (hasXdata) {
        const uint32_t xdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file, ".xdata", 0, 0, xdataSize, xdataDataOff, 0, 0, xdataChars);
    }
    if (hasPdata) {
        const uint32_t pdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        uint32_t pdataHeaderChars = pdataChars;
        if (numPdataRelocs > kCoffMaxStandardRelocs)
            pdataHeaderChars |= kImageScnLnkNrelocOvfl;
        writeSectionHeader(file,
                           ".pdata",
                           0,
                           0,
                           pdataSize,
                           pdataDataOff,
                           (numPdataRelocs > 0) ? pdataRelocOff : 0,
                           coffHeaderRelocCount(numPdataRelocs),
                           pdataHeaderChars);
    }
    if (hasDebugLine) {
        const std::string debugSecName = "/" + std::to_string(debugLineStrOff);
        if (!validateSectionHeaderName(debugSecName, ".debug_line", err))
            return false;
        const uint32_t debugChars =
            kImageScnCntInitData | kImageScnMemDiscardable | kImageScnMemRead | kImageScnAlign1;
        writeSectionHeader(file,
                           debugSecName.c_str(),
                           0,
                           0,
                           debugLineDataSize,
                           debugLineDataOff,
                           0,
                           0,
                           debugChars);
    }

    padTo(file, textDataOff);
    file.insert(file.end(), patchedTextBytes.begin(), patchedTextBytes.end());

    if (!textRelocBytes.empty()) {
        padTo(file, textRelocOff);
        file.insert(file.end(), textRelocBytes.begin(), textRelocBytes.end());
    }
    if (hasRodata) {
        padTo(file, rdataDataOff);
        file.insert(file.end(), patchedRodataBytes.begin(), patchedRodataBytes.end());
        if (!rdataRelocBytes.empty()) {
            padTo(file, rdataRelocOff);
            file.insert(file.end(), rdataRelocBytes.begin(), rdataRelocBytes.end());
        }
    }
    if (hasXdata) {
        padTo(file, xdataDataOff);
        file.insert(file.end(), xdataBytes.begin(), xdataBytes.end());
    }
    if (hasPdata) {
        padTo(file, pdataDataOff);
        file.insert(file.end(), pdataBytes.begin(), pdataBytes.end());
        if (!pdataRelocBytes.empty()) {
            padTo(file, pdataRelocOff);
            file.insert(file.end(), pdataRelocBytes.begin(), pdataRelocBytes.end());
        }
    }
    if (hasDebugLine) {
        padTo(file, debugLineDataOff);
        file.insert(file.end(), debugLineData_.begin(), debugLineData_.end());
    }

    padTo(file, symtabOff);
    file.insert(file.end(), symtabBytes.begin(), symtabBytes.end());
    file.insert(file.end(), strtabBytes.begin(), strtabBytes.end());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        err << "CoffWriter: cannot open " << path << " for writing\n";
        return false;
    }
    if (!checkedWriteAll(ofs, file, "CoffWriter", path, err))
        return false;
    return true;
    } catch (const std::exception &ex) {
        err << "CoffWriter: " << ex.what() << "\n";
        return false;
    }
}

static std::string inferTextSectionName(const CodeSection &text, size_t index) {
    std::string funcName = "func_" + std::to_string(index);
    for (uint32_t i = 1; i < text.symbols().count(); ++i) {
        const Symbol &sym = text.symbols().at(i);
        if (sym.binding == SymbolBinding::Global && sym.section == SymbolSection::Text) {
            funcName = sym.name;
            break;
        }
    }
    return ".text." + funcName;
}

bool CoffWriter::write(const std::string &path,
                       const std::vector<CodeSection> &textSections,
                       const CodeSection &rodata,
                       std::ostream &err) {
    try {
    if (textSections.size() <= 1) {
        if (textSections.empty()) {
            CodeSection empty;
            return write(path, empty, rodata, err);
        }
        return write(path, textSections[0], rodata, err);
    }

    std::vector<uint8_t> xdataBytes;
    std::vector<PendingCoffSymbol> xdataSymbols;
    std::vector<uint8_t> pdataBytes;
    std::vector<PendingCoffReloc> pdataRelocs;
    if (arch_ == ObjArch::X86_64) {
        uint32_t xdataNameBase = 0;
        for (const auto &text : textSections) {
            if (!text.win64UnwindEntries().empty()) {
                if (!buildWin64UnwindSections(
                        text, xdataNameBase, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs, err))
                    return false;
                uint32_t entryCount = 0;
                if (!checkedU32(text.win64UnwindEntries().size(),
                                ".xdata symbol count",
                                err,
                                entryCount) ||
                    !addU32Checked(
                        xdataNameBase, entryCount, ".xdata symbol count", err, xdataNameBase))
                    return false;
            }
        }
    } else if (arch_ == ObjArch::AArch64) {
        uint32_t xdataNameBase = 0;
        for (const auto &text : textSections) {
            if (!text.winArm64UnwindEntries().empty()) {
                if (!buildWinArm64UnwindSections(
                        text, xdataNameBase, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs, err))
                    return false;
                uint32_t entryCount = 0;
                if (!checkedU32(text.winArm64UnwindEntries().size(),
                                ".xdata symbol count",
                                err,
                                entryCount) ||
                    !addU32Checked(
                        xdataNameBase, entryCount, ".xdata symbol count", err, xdataNameBase))
                    return false;
            }
        }
    }

    const bool hasRodata = !rodata.empty();
    const bool hasXdata = !xdataBytes.empty();
    const bool hasPdata = !pdataBytes.empty();
    const bool hasDebugLine = !debugLineData_.empty();

    const size_t textCount = textSections.size();
    std::vector<uint16_t> secIdxText(textCount, 0);
    uint32_t nextSecIndex = 1;
    if (textCount > static_cast<size_t>(std::numeric_limits<int16_t>::max())) {
        err << "CoffWriter: text section count exceeds standard COFF symbol section-number range; "
               "BigObj output is not available\n";
        return false;
    }
    for (size_t i = 0; i < textCount; ++i)
        secIdxText[i] = static_cast<uint16_t>(nextSecIndex++);
    const uint16_t secIdxRdata = hasRodata ? static_cast<uint16_t>(nextSecIndex++) : 0;
    const uint16_t secIdxXdata = hasXdata ? static_cast<uint16_t>(nextSecIndex++) : 0;
    if (hasPdata)
        ++nextSecIndex;
    const uint32_t sectionCount = (nextSecIndex - 1) + (hasDebugLine ? 1u : 0u);
    if (sectionCount > static_cast<uint32_t>(std::numeric_limits<int16_t>::max())) {
        err << "CoffWriter: section count exceeds standard COFF symbol section-number range; "
               "BigObj output is not available\n";
        return false;
    }
    const uint16_t numSections = static_cast<uint16_t>(sectionCount);

    std::vector<uint8_t> symtabBytes;
    std::vector<uint8_t> strtabBytes(4, 0);
    std::vector<std::unordered_map<uint32_t, uint32_t>> textSymMaps(textCount);
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;
    std::unordered_map<std::string, uint32_t> definedNameMap;
    std::unordered_map<std::string, uint32_t> definedGlobalNameMap;
    std::unordered_map<std::string, uint32_t> externalNameMap;
    std::unordered_set<std::string> definedGlobalNames;
    uint32_t coffSymCount = 0;

    auto addToStrTab = [&](const std::string &s) -> uint32_t {
        if (strtabBytes.size() > std::numeric_limits<uint32_t>::max())
            throw std::length_error("CoffWriter: string table offset exceeds 32-bit COFF limit");
        if (s.size() > std::numeric_limits<size_t>::max() - strtabBytes.size() - 1 ||
            strtabBytes.size() + s.size() + 1 > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("CoffWriter: string table size exceeds 32-bit COFF limit");
        }
        const uint32_t offset = static_cast<uint32_t>(strtabBytes.size());
        strtabBytes.insert(strtabBytes.end(), s.begin(), s.end());
        strtabBytes.push_back(0);
        return offset;
    };

    auto encodeSectionHeaderName = [&](const std::string &name) -> std::string {
        if (name.size() <= 8)
            return name;
        const std::string encoded = "/" + std::to_string(addToStrTab(name));
        return encoded;
    };

    struct PendingExternal {
        bool fromText = false;
        size_t textIdx = SIZE_MAX;
        uint32_t origIdx = 0;
        std::string name;
        uint16_t type = 0;
    };

    std::vector<PendingExternal> pendingExternals;

    std::vector<std::string> textSectionNames(textCount);
    std::vector<std::string> textHeaderNames(textCount);
    for (size_t i = 0; i < textCount; ++i) {
        textSectionNames[i] = inferTextSectionName(textSections[i], i);
        textHeaderNames[i] = encodeSectionHeaderName(textSectionNames[i]);
        if (!validateSectionHeaderName(textHeaderNames[i], textSectionNames[i].c_str(), err))
            return false;
    }
    std::string debugHeaderName;
    if (hasDebugLine) {
        debugHeaderName = encodeSectionHeaderName(".debug_line");
        if (!validateSectionHeaderName(debugHeaderName, ".debug_line", err))
            return false;
    }

    auto relocationNeedsAnchor = [](const CodeSection &sec, SymbolSection targetSection) {
        for (const auto &rel : sec.relocations())
            if (rel.targetOffsetValid && rel.targetSection == targetSection)
                return true;
        return false;
    };
    std::vector<uint32_t> textAnchorIdx(textCount, UINT32_MAX);
    bool needAnyTextAnchor = relocationNeedsAnchor(rodata, SymbolSection::Text);
    for (size_t ti = 0; ti < textCount; ++ti)
        needAnyTextAnchor = needAnyTextAnchor ||
                            relocationNeedsAnchor(textSections[ti], SymbolSection::Text);
    if (needAnyTextAnchor) {
        for (size_t ti = 0; ti < textCount; ++ti) {
            const uint32_t strOff =
                (textSectionNames[ti].size() > 8) ? addToStrTab(textSectionNames[ti]) : 0;
            writeSymbol(symtabBytes,
                        textSectionNames[ti],
                        strOff,
                        0,
                        static_cast<int16_t>(secIdxText[ti]),
                        0,
                        kImageSymClassStatic);
            textAnchorIdx[ti] = coffSymCount++;
        }
    }

    uint32_t rodataAnchorIdx = UINT32_MAX;
    const bool needRodataAnchor =
        hasRodata &&
        (relocationNeedsAnchor(rodata, SymbolSection::Rodata) ||
         std::any_of(textSections.begin(), textSections.end(), [&](const CodeSection &sec) {
             return relocationNeedsAnchor(sec, SymbolSection::Rodata);
         }));
    if (needRodataAnchor) {
        writeSymbol(symtabBytes,
                    ".rdata",
                    0,
                    0,
                    static_cast<int16_t>(secIdxRdata),
                    0,
                    kImageSymClassStatic);
        rodataAnchorIdx = coffSymCount++;
    }

    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External) {
                pendingExternals.push_back({false, SIZE_MAX, i, s.name, 0});
                continue;
            }

            const int16_t secNum = static_cast<int16_t>(secIdxRdata);
            uint32_t value = 0;
            if (!checkedPhysicalSymbolValue(rodata, s, ".rdata", err, value))
                return false;
            const uint8_t storageClass =
                (s.binding == SymbolBinding::Local) ? kImageSymClassStatic : kImageSymClassExternal;
            if (!rememberDefinedGlobal(definedGlobalNames, s, ".rdata", err))
                return false;
            const uint32_t strOff = (s.name.size() > 8) ? addToStrTab(s.name) : 0;

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            rodataSymMap[i] = coffIdx;
            definedNameMap[s.name] = coffIdx;
            if (s.binding == SymbolBinding::Global)
                definedGlobalNameMap[s.name] = coffIdx;
        }
    }

    std::unordered_map<std::string, uint32_t> definedRodataByName;
    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External)
                continue;
            auto it = rodataSymMap.find(i);
            if (it != rodataSymMap.end()) {
                auto [nameIt, inserted] = definedRodataByName.emplace(s.name, it->second);
                if (!inserted)
                    nameIt->second = UINT32_MAX;
            }
        }
    }

    if (hasXdata) {
        for (const auto &sym : xdataSymbols) {
            const uint32_t strOff = (sym.name.size() > 8) ? addToStrTab(sym.name) : 0;
            writeSymbol(symtabBytes,
                        sym.name,
                        strOff,
                        sym.value,
                        static_cast<int16_t>(secIdxXdata),
                        sym.type,
                        sym.storageClass);
            definedNameMap[sym.name] = coffSymCount++;
        }
    }

    std::vector<std::unordered_set<uint32_t>> crossSectionTextAliases(textCount);
    std::vector<std::unordered_set<uint32_t>> undefinedTextRefs(textCount);
    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        for (const auto &rel : text.relocations()) {
            if (rel.symbolIndex >= text.symbols().count())
                continue;
            if (rel.targetSection != SymbolSection::Undefined)
                crossSectionTextAliases[ti].insert(rel.symbolIndex);
            else
                undefinedTextRefs[ti].insert(rel.symbolIndex);
        }
    }

    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        for (uint32_t i = 1; i < text.symbols().count(); ++i) {
            const Symbol &s = text.symbols().at(i);
            if (s.binding == SymbolBinding::External) {
                if (crossSectionTextAliases[ti].count(i) != 0 &&
                    undefinedTextRefs[ti].count(i) == 0) {
                    continue;
                }
                pendingExternals.push_back({true, ti, i, s.name, 0x20});
                continue;
            }

            const int16_t secNum = static_cast<int16_t>(secIdxText[ti]);
            uint32_t value = 0;
            if (!checkedPhysicalSymbolValue(text, s, textSectionNames[ti].c_str(), err, value))
                return false;
            const uint8_t storageClass =
                (s.binding == SymbolBinding::Local) ? kImageSymClassStatic : kImageSymClassExternal;
            if (!rememberDefinedGlobal(definedGlobalNames, s, textSectionNames[ti].c_str(), err))
                return false;
            const uint32_t strOff = (s.name.size() > 8) ? addToStrTab(s.name) : 0;

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0x20, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            textSymMaps[ti][i] = coffIdx;
            definedNameMap[s.name] = coffIdx;
            if (s.binding == SymbolBinding::Global)
                definedGlobalNameMap[s.name] = coffIdx;
        }
    }

    std::unordered_map<std::string, uint32_t> definedTextByName;
    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        for (uint32_t i = 1; i < text.symbols().count(); ++i) {
            const Symbol &s = text.symbols().at(i);
            if (s.binding == SymbolBinding::External)
                continue;
            auto it = textSymMaps[ti].find(i);
            if (it == textSymMaps[ti].end())
                continue;
            auto [nameIt, inserted] = definedTextByName.emplace(s.name, it->second);
            if (!inserted)
                nameIt->second = UINT32_MAX;
        }
    }

    for (const auto &ext : pendingExternals) {
        const auto definedIt = definedGlobalNameMap.find(ext.name);
        if (definedIt != definedGlobalNameMap.end()) {
            if (ext.fromText)
                textSymMaps[ext.textIdx][ext.origIdx] = definedIt->second;
            else
                rodataSymMap[ext.origIdx] = definedIt->second;
            continue;
        }

        auto extIt = externalNameMap.find(ext.name);
        if (extIt == externalNameMap.end()) {
            const uint32_t strOff = (ext.name.size() > 8) ? addToStrTab(ext.name) : 0;
            writeSymbol(symtabBytes,
                        ext.name,
                        strOff,
                        0,
                        kImageSymUndefined,
                        ext.type,
                        kImageSymClassExternal);
            extIt = externalNameMap.emplace(ext.name, coffSymCount++).first;
        }

        if (ext.fromText)
            textSymMaps[ext.textIdx][ext.origIdx] = extIt->second;
        else
            rodataSymMap[ext.origIdx] = extIt->second;
    }

    uint32_t strtabSize = 0;
    if (!checkedU32(strtabBytes.size(), "string table size", err, strtabSize))
        return false;
    strtabBytes[0] = static_cast<uint8_t>(strtabSize);
    strtabBytes[1] = static_cast<uint8_t>(strtabSize >> 8);
    strtabBytes[2] = static_cast<uint8_t>(strtabSize >> 16);
    strtabBytes[3] = static_cast<uint8_t>(strtabSize >> 24);

    std::vector<std::vector<uint8_t>> patchedTextBytes(textCount);
    for (size_t ti = 0; ti < textCount; ++ti)
        patchedTextBytes[ti] = textSections[ti].bytes();
    std::vector<uint8_t> patchedRodataBytes = rodata.bytes();
    std::vector<std::vector<uint8_t>> textRelocBytes(textCount);
    std::vector<uint32_t> numTextRelocs(textCount, 0);
    auto resolveRelocSym = [&](const Relocation &rel,
                               const CodeSection &source,
                               const std::unordered_map<uint32_t, uint32_t> &sourceMap,
                               const char *sectionName,
                               size_t sourceTextIndex,
                               uint32_t &coffSymIdx,
                               int64_t &effectiveAddend) -> bool {
        coffSymIdx = 0;
        effectiveAddend = rel.addend;
        if (rel.targetSection != SymbolSection::Undefined) {
            if (rel.targetOffsetValid) {
                if (!checkedSectionOffsetAddend(rel.addend,
                                                rel.targetOffset,
                                                "CoffWriter",
                                                sectionName,
                                                rel.offset,
                                                err,
                                                effectiveAddend))
                    return false;
                if (rel.targetSection == SymbolSection::Rodata) {
                    if (rodataAnchorIdx == UINT32_MAX) {
                        err << "CoffWriter: missing section anchor for .rdata\n";
                        return false;
                    }
                    if (rel.targetOffset > rodata.bytes().size()) {
                        err << "CoffWriter: relocation in " << sectionName << " at offset "
                            << rel.offset << " references .rdata offset " << rel.targetOffset
                            << " beyond section contents\n";
                        return false;
                    }
                    coffSymIdx = rodataAnchorIdx;
                    return true;
                }

                size_t textIdx = sourceTextIndex;
                if (rel.targetSectionIdentityValid) {
                    textIdx = SIZE_MAX;
                    size_t matches = 0;
                    for (size_t ti = 0; ti < textCount; ++ti) {
                        if (textSections[ti].matchesSectionIdentity(rel.targetSectionIdentity)) {
                            textIdx = ti;
                            ++matches;
                        }
                    }
                    if (matches > 1) {
                        err << "CoffWriter: relocation in " << sectionName << " at offset "
                            << rel.offset << " references duplicate .text section identity\n";
                        return false;
                    }
                } else if (textIdx == SIZE_MAX || textIdx >= textCount ||
                           rel.targetOffset > textSections[textIdx].bytes().size()) {
                    textIdx = SIZE_MAX;
                    size_t matches = 0;
                    for (size_t ti = 0; ti < textCount; ++ti) {
                        if (rel.targetOffset <= textSections[ti].bytes().size()) {
                            textIdx = ti;
                            ++matches;
                        }
                    }
                    if (matches > 1) {
                        err << "CoffWriter: relocation in " << sectionName << " at offset "
                            << rel.offset << " references ambiguous .text offset "
                            << rel.targetOffset
                            << "; use section-identity relocation overload\n";
                        return false;
                    }
                }
                if (textIdx == SIZE_MAX || textIdx >= textCount ||
                    rel.targetOffset > textSections[textIdx].bytes().size()) {
                    err << "CoffWriter: relocation in " << sectionName << " at offset "
                        << rel.offset << " references .text offset " << rel.targetOffset
                        << " beyond section contents\n";
                    return false;
                }
                if (textAnchorIdx[textIdx] == UINT32_MAX) {
                    err << "CoffWriter: missing section anchor for " << textSectionNames[textIdx]
                        << "\n";
                    return false;
                }
                coffSymIdx = textAnchorIdx[textIdx];
                return true;
            }
            if (rel.symbolIndex >= source.symbols().count()) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references unknown symbol index " << rel.symbolIndex << "\n";
                return false;
            }
            const Symbol &sym = source.symbols().at(rel.symbolIndex);
            const auto &targetByName = (rel.targetSection == SymbolSection::Rodata)
                                           ? definedRodataByName
                                           : definedTextByName;
            auto nameIt = targetByName.find(sym.name);
            if (nameIt == targetByName.end()) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references missing cross-section target '" << sym.name << "'\n";
                return false;
            }
            if (nameIt->second == UINT32_MAX) {
                err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                    << " references ambiguous cross-section target '" << sym.name << "'\n";
                return false;
            }
            coffSymIdx = nameIt->second;
            return true;
        }

        auto it = sourceMap.find(rel.symbolIndex);
        if (it == sourceMap.end()) {
            err << "CoffWriter: relocation in " << sectionName << " at offset " << rel.offset
                << " references unknown symbol index " << rel.symbolIndex << "\n";
            return false;
        }
        coffSymIdx = it->second;
        return true;
    };

    auto appendRelocBytes = [&](const CodeSection &source,
                                const std::unordered_map<uint32_t, uint32_t> &sourceMap,
                                const char *sectionName,
                                size_t sourceTextIndex,
                                std::vector<uint8_t> &patchedBytes,
                                std::vector<uint8_t> &relocBytes) -> bool {
        for (const auto &rel : source.relocations()) {
            if (!validateRelocationShape("CoffWriter", arch_, source, rel, sectionName, err))
                return false;
            uint32_t coffSymIdx = 0;
            int64_t effectiveAddend = rel.addend;
            if (!resolveRelocSym(
                    rel, source, sourceMap, sectionName, sourceTextIndex, coffSymIdx, effectiveAddend))
                return false;
            if (!patchCoffRelocationAddend(source, rel, effectiveAddend, patchedBytes, err))
                return false;
            const size_t physicalRelOffset = rel.offset - source.logicalOffsetBias();
            uint32_t relocOff = 0;
            if (!checkedU32(physicalRelOffset, "relocation offset", err, relocOff))
                return false;
            writeReloc(relocBytes, relocOff, coffSymIdx, coffRelocType(rel.kind, arch_));
        }
        return true;
    };

    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        auto &relocBytes = textRelocBytes[ti];
        if (!appendRelocBytes(text, textSymMaps[ti], ".text", ti, patchedTextBytes[ti], relocBytes))
            return false;
        if (!checkedU32(
                text.relocations().size(), "text relocation count", err, numTextRelocs[ti]))
            return false;
        addRelocationOverflowRecord(relocBytes, numTextRelocs[ti]);
    }

    std::vector<uint8_t> rdataRelocBytes;
    if (!appendRelocBytes(rodata, rodataSymMap, ".rdata", SIZE_MAX, patchedRodataBytes, rdataRelocBytes))
        return false;
    uint32_t numRdataRelocs = 0;
    if (!checkedU32(rodata.relocations().size(), ".rdata relocation count", err, numRdataRelocs))
        return false;
    addRelocationOverflowRecord(rdataRelocBytes, numRdataRelocs);

    std::vector<uint8_t> pdataRelocBytes;
    for (const auto &rel : pdataRelocs) {
        if (!validatePdataRelocOffset(rel.offset, pdataBytes.size(), err))
            return false;
        uint32_t targetIdx = 0;
        if (rel.hasSymbolRef) {
            size_t textIdx = SIZE_MAX;
            size_t matches = 0;
            for (size_t ti = 0; ti < textCount; ++ti) {
                if (textSections[ti].matchesSectionIdentity(rel.symbolSectionIdentity)) {
                    textIdx = ti;
                    ++matches;
                }
            }
            if (matches > 1) {
                err << "CoffWriter: .pdata relocation for '" << rel.symbolName
                    << "' references duplicate text section identity\n";
                return false;
            }
            if (textIdx == SIZE_MAX) {
                err << "CoffWriter: .pdata relocation for '" << rel.symbolName
                    << "' references missing text section identity\n";
                return false;
            }
            auto symIt = textSymMaps[textIdx].find(rel.symbolIndex);
            if (symIt == textSymMaps[textIdx].end()) {
                err << "CoffWriter: missing symbol '" << rel.symbolName
                    << "' for .pdata relocation\n";
                return false;
            }
            targetIdx = symIt->second;
        } else {
            auto it = definedNameMap.find(rel.symbolName);
            if (it == definedNameMap.end()) {
                err << "CoffWriter: missing symbol '" << rel.symbolName
                    << "' for .pdata relocation\n";
                return false;
            }
            targetIdx = it->second;
        }
        writeReloc(pdataRelocBytes, rel.offset, targetIdx, rel.type);
    }
    uint32_t numPdataRelocs = 0;
    if (!checkedU32(pdataRelocs.size(), ".pdata relocation count", err, numPdataRelocs))
        return false;
    addRelocationOverflowRecord(pdataRelocBytes, numPdataRelocs);

    std::vector<uint32_t> textSizes(textCount, 0);
    for (size_t ti = 0; ti < textCount; ++ti) {
        if (!checkedU32(patchedTextBytes[ti].size(), ".text size", err, textSizes[ti]))
            return false;
    }
    uint32_t rdataSize = 0;
    uint32_t xdataSize = 0;
    uint32_t pdataSize = 0;
    uint32_t debugLineDataSize = 0;
    if (!checkedU32(hasRodata ? patchedRodataBytes.size() : 0, ".rdata size", err, rdataSize) ||
        !checkedU32(hasXdata ? xdataBytes.size() : 0, ".xdata size", err, xdataSize) ||
        !checkedU32(hasPdata ? pdataBytes.size() : 0, ".pdata size", err, pdataSize) ||
        !checkedU32(
            hasDebugLine ? debugLineData_.size() : 0, ".debug_line size", err, debugLineDataSize))
        return false;

    const uint32_t headerAreaSize = kCoffHeaderSize + numSections * kSectionHeaderSize;
    uint32_t cursor = 0;
    if (!alignU32Checked(headerAreaSize, 4, "header area", err, cursor))
        return false;

    std::vector<uint32_t> textDataOff(textCount, 0);
    std::vector<uint32_t> textRelocOff(textCount, 0);
    for (size_t ti = 0; ti < textCount; ++ti) {
        textDataOff[ti] = cursor;
        if (!addU32Checked(cursor, textSizes[ti], ".text data", err, cursor))
            return false;
        if (numTextRelocs[ti] > 0) {
            textRelocOff[ti] = cursor;
            uint32_t textRelocSize = 0;
            if (!coffRelocTableSize(numTextRelocs[ti], ".text", err, textRelocSize))
                return false;
            if (!addU32Checked(cursor,
                               textRelocSize,
                               ".text relocation table",
                               err,
                               cursor))
                return false;
        }
        if (!alignU32Checked(cursor, 4, ".text data", err, cursor))
            return false;
    }

    uint32_t rdataDataOff = 0;
    uint32_t rdataRelocOff = 0;
    if (hasRodata) {
        rdataDataOff = cursor;
        if (!addU32Checked(rdataDataOff, rdataSize, ".rdata relocation offset", err, rdataRelocOff))
            return false;
        uint32_t rdataRelocEnd = 0;
        uint32_t rdataRelocSize = 0;
        if (!coffRelocTableSize(numRdataRelocs, ".rdata", err, rdataRelocSize))
            return false;
        if (!addU32Checked(
                rdataRelocOff, rdataRelocSize, ".rdata relocation table", err, rdataRelocEnd))
            return false;
        if (!alignU32Checked(rdataRelocEnd, 4, ".rdata relocation table", err, cursor))
            return false;
    }
    uint32_t xdataDataOff = 0;
    if (hasXdata) {
        xdataDataOff = cursor;
        uint32_t end = 0;
        if (!addU32Checked(xdataDataOff, xdataSize, ".xdata data", err, end))
            return false;
        if (!alignU32Checked(end, 4, ".xdata data", err, cursor))
            return false;
    }
    uint32_t pdataDataOff = 0;
    uint32_t pdataRelocOff = 0;
    if (hasPdata) {
        pdataDataOff = cursor;
        if (!addU32Checked(pdataDataOff, pdataSize, ".pdata relocation offset", err, pdataRelocOff))
            return false;
        uint32_t pdataRelocEnd = 0;
        uint32_t pdataRelocSize = 0;
        if (!coffRelocTableSize(numPdataRelocs, ".pdata", err, pdataRelocSize))
            return false;
        if (!addU32Checked(
                pdataRelocOff, pdataRelocSize, ".pdata relocation table", err, pdataRelocEnd))
            return false;
        if (!alignU32Checked(pdataRelocEnd, 4, ".pdata relocation table", err, cursor))
            return false;
    }
    uint32_t debugLineDataOff = 0;
    if (hasDebugLine) {
        debugLineDataOff = cursor;
        uint32_t end = 0;
        if (!addU32Checked(debugLineDataOff, debugLineDataSize, ".debug_line data", err, end))
            return false;
        if (!alignU32Checked(end, 4, ".debug_line data", err, cursor))
            return false;
    }
    const uint32_t symtabOff = cursor;

    std::vector<uint8_t> file;
    size_t reserveSize = 0;
    if (!checkedCoffReserveSize(symtabOff, symtabBytes.size(), strtabBytes.size(), err, reserveSize))
        return false;
    file.reserve(reserveSize);

    const uint16_t machine = (arch_ == ObjArch::X86_64) ? kMachineAMD64 : kMachineARM64;
    appendLE16(file, machine);
    appendLE16(file, numSections);
    appendLE32(file, 0);
    appendLE32(file, symtabOff);
    appendLE32(file, coffSymCount);
    appendLE16(file, 0);
    appendLE16(file, 0);

    for (size_t ti = 0; ti < textCount; ++ti) {
        uint32_t textChars = kImageScnCntCode | kImageScnMemExecute | kImageScnMemRead;
        textChars |= (arch_ == ObjArch::X86_64) ? kImageScnAlignText : kImageScnAlign4;
        if (numTextRelocs[ti] > kCoffMaxStandardRelocs)
            textChars |= kImageScnLnkNrelocOvfl;
        writeSectionHeader(file,
                           textHeaderNames[ti].c_str(),
                           0,
                           0,
                           textSizes[ti],
                           textDataOff[ti],
                           (numTextRelocs[ti] > 0) ? textRelocOff[ti] : 0,
                           coffHeaderRelocCount(numTextRelocs[ti]),
                           textChars);
    }

    if (hasRodata) {
        uint32_t rdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign8;
        if (numRdataRelocs > kCoffMaxStandardRelocs)
            rdataChars |= kImageScnLnkNrelocOvfl;
        writeSectionHeader(file,
                           ".rdata",
                           0,
                           0,
                           rdataSize,
                           rdataDataOff,
                           (numRdataRelocs > 0) ? rdataRelocOff : 0,
                           coffHeaderRelocCount(numRdataRelocs),
                           rdataChars);
    }
    if (hasXdata) {
        const uint32_t xdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file, ".xdata", 0, 0, xdataSize, xdataDataOff, 0, 0, xdataChars);
    }
    if (hasPdata) {
        const uint32_t pdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        uint32_t pdataHeaderChars = pdataChars;
        if (numPdataRelocs > kCoffMaxStandardRelocs)
            pdataHeaderChars |= kImageScnLnkNrelocOvfl;
        writeSectionHeader(file,
                           ".pdata",
                           0,
                           0,
                           pdataSize,
                           pdataDataOff,
                           (numPdataRelocs > 0) ? pdataRelocOff : 0,
                           coffHeaderRelocCount(numPdataRelocs),
                           pdataHeaderChars);
    }
    if (hasDebugLine) {
        const uint32_t debugChars =
            kImageScnCntInitData | kImageScnMemDiscardable | kImageScnMemRead | kImageScnAlign1;
        writeSectionHeader(file,
                           debugHeaderName.c_str(),
                           0,
                           0,
                           debugLineDataSize,
                           debugLineDataOff,
                           0,
                           0,
                           debugChars);
    }

    for (size_t ti = 0; ti < textCount; ++ti) {
        padTo(file, textDataOff[ti]);
        file.insert(file.end(), patchedTextBytes[ti].begin(), patchedTextBytes[ti].end());
        if (!textRelocBytes[ti].empty()) {
            padTo(file, textRelocOff[ti]);
            file.insert(file.end(), textRelocBytes[ti].begin(), textRelocBytes[ti].end());
        }
    }

    if (hasRodata) {
        padTo(file, rdataDataOff);
        file.insert(file.end(), patchedRodataBytes.begin(), patchedRodataBytes.end());
        if (!rdataRelocBytes.empty()) {
            padTo(file, rdataRelocOff);
            file.insert(file.end(), rdataRelocBytes.begin(), rdataRelocBytes.end());
        }
    }
    if (hasXdata) {
        padTo(file, xdataDataOff);
        file.insert(file.end(), xdataBytes.begin(), xdataBytes.end());
    }
    if (hasPdata) {
        padTo(file, pdataDataOff);
        file.insert(file.end(), pdataBytes.begin(), pdataBytes.end());
        if (!pdataRelocBytes.empty()) {
            padTo(file, pdataRelocOff);
            file.insert(file.end(), pdataRelocBytes.begin(), pdataRelocBytes.end());
        }
    }
    if (hasDebugLine) {
        padTo(file, debugLineDataOff);
        file.insert(file.end(), debugLineData_.begin(), debugLineData_.end());
    }

    padTo(file, symtabOff);
    file.insert(file.end(), symtabBytes.begin(), symtabBytes.end());
    file.insert(file.end(), strtabBytes.begin(), strtabBytes.end());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        err << "CoffWriter: cannot open " << path << " for writing\n";
        return false;
    }
    if (!checkedWriteAll(ofs, file, "CoffWriter", path, err))
        return false;
    return true;
    } catch (const std::exception &ex) {
        err << "CoffWriter: " << ex.what() << "\n";
        return false;
    }
}

} // namespace viper::codegen::objfile
