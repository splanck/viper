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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
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
static constexpr uint32_t kImageScnMemExecute = 0x20000000;
static constexpr uint32_t kImageScnMemDiscardable = 0x02000000;
static constexpr uint32_t kImageScnMemRead = 0x40000000;
static constexpr uint32_t kImageScnAlign1 = 0x00100000;

static constexpr uint8_t kImageSymClassExternal = 2;
static constexpr uint8_t kImageSymClassStatic = 3;

static constexpr int16_t kImageSymUndefined = 0;

static constexpr uint32_t kCoffRelocSize = 10;

static constexpr uint16_t kImageRelAMD64_Addr64 = 1;
static constexpr uint16_t kImageRelAMD64_Addr32Nb = 3;
static constexpr uint16_t kImageRelAMD64_Rel32 = 4;

static constexpr uint16_t kImageRelARM64_Branch26 = 3;
static constexpr uint16_t kImageRelARM64_PagebaseRel21 = 4;
static constexpr uint16_t kImageRelARM64_Pageoffset12A = 6;
static constexpr uint16_t kImageRelARM64_Pageoffset12L = 7;
static constexpr uint16_t kImageRelARM64_Branch19 = 8;

static uint16_t coffRelocType(RelocKind kind) {
    switch (kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
            return kImageRelAMD64_Rel32;
        case RelocKind::Abs64:
            return kImageRelAMD64_Addr64;
        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
            return kImageRelARM64_Branch26;
        case RelocKind::A64AdrpPage21:
            return kImageRelARM64_PagebaseRel21;
        case RelocKind::A64AddPageOff12:
            return kImageRelARM64_Pageoffset12A;
        case RelocKind::A64LdSt64Off12:
            return kImageRelARM64_Pageoffset12L;
        case RelocKind::A64CondBr19:
            return kImageRelARM64_Branch19;
    }
    return 0;
}

static bool validateCoffRelocationAddend(const CodeSection &section,
                                         const Relocation &rel,
                                         std::ostream &err) {
    switch (rel.kind) {
        case RelocKind::PCRel32:
        case RelocKind::Branch32:
            if (rel.addend != 0 && rel.addend != -4) {
                err << "CoffWriter: unsupported addend " << rel.addend
                    << " for x86_64 rel32 relocation at offset " << rel.offset << "\n";
                return false;
            }
            return true;

        case RelocKind::Abs64: {
            if (rel.addend == 0)
                return true;
            if (rel.offset + 8 > section.bytes().size()) {
                err << "CoffWriter: 64-bit addend relocation at offset " << rel.offset
                    << " is out of bounds\n";
                return false;
            }

            uint64_t encoded = 0;
            const auto &bytes = section.bytes();
            for (size_t i = 0; i < 8; ++i)
                encoded |= (static_cast<uint64_t>(bytes[rel.offset + i]) << (i * 8));
            if (encoded != static_cast<uint64_t>(rel.addend)) {
                err << "CoffWriter: Abs64 addend " << rel.addend
                    << " must already be embedded in section bytes at offset " << rel.offset
                    << "\n";
                return false;
            }
            return true;
        }

        case RelocKind::A64Call26:
        case RelocKind::A64Jump26:
        case RelocKind::A64AdrpPage21:
        case RelocKind::A64AddPageOff12:
        case RelocKind::A64LdSt64Off12:
        case RelocKind::A64CondBr19:
            if (rel.addend != 0) {
                err << "CoffWriter: unsupported non-zero AArch64 addend " << rel.addend
                    << " at offset " << rel.offset << "\n";
                return false;
            }
            return true;
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
};

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

static void buildWin64UnwindSections(const CodeSection &text,
                                     uint32_t xdataNameBase,
                                     std::vector<uint8_t> &xdataBytes,
                                     std::vector<PendingCoffSymbol> &xdataSymbols,
                                     std::vector<uint8_t> &pdataBytes,
                                     std::vector<PendingCoffReloc> &pdataRelocs) {
    const auto &entries = text.win64UnwindEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        const uint32_t xdataOffset = static_cast<uint32_t>(xdataBytes.size());
        const std::string xdataName = "$xdata$" + std::to_string(xdataNameBase + i);
        xdataSymbols.push_back({xdataName, xdataOffset, 0, kImageSymClassStatic});

        std::vector<Win64UnwindCode> codes = entry.codes;
        std::stable_sort(codes.begin(), codes.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.codeOffset > rhs.codeOffset;
        });

        size_t codeSlots = 0;
        for (const auto &code : codes)
            codeSlots += win64UnwindSlotCount(code);

        xdataBytes.push_back(1);
        xdataBytes.push_back(entry.prologueSize);
        xdataBytes.push_back(static_cast<uint8_t>(codeSlots));
        xdataBytes.push_back(0);
        for (const auto &code : codes)
            emitWin64UnwindNodes(xdataBytes, code);
        padTo(xdataBytes, alignUp(xdataBytes.size(), 4));

        const uint32_t pdataOffset = static_cast<uint32_t>(pdataBytes.size());
        appendLE32(pdataBytes, 0);
        appendLE32(pdataBytes, entry.functionLength);
        appendLE32(pdataBytes, 0);

        const auto &funcSym = text.symbols().at(entry.symbolIndex);
        pdataRelocs.push_back({pdataOffset, funcSym.name, kImageRelAMD64_Addr32Nb});
        pdataRelocs.push_back({pdataOffset + 4, funcSym.name, kImageRelAMD64_Addr32Nb});
        pdataRelocs.push_back({pdataOffset + 8, xdataName, kImageRelAMD64_Addr32Nb});
    }
}

bool CoffWriter::write(const std::string &path,
                       const CodeSection &text,
                       const CodeSection &rodata,
                       std::ostream &err) {
    std::vector<uint8_t> xdataBytes;
    std::vector<PendingCoffSymbol> xdataSymbols;
    std::vector<uint8_t> pdataBytes;
    std::vector<PendingCoffReloc> pdataRelocs;
    if (arch_ == ObjArch::X86_64 && !text.win64UnwindEntries().empty())
        buildWin64UnwindSections(text, 0, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs);

    const bool hasRodata = !rodata.empty();
    const bool hasXdata = !xdataBytes.empty();
    const bool hasPdata = !pdataBytes.empty();
    const bool hasDebugLine = !debugLineData_.empty();

    uint16_t nextSecIndex = 1;
    const uint16_t secIdxText = nextSecIndex++;
    const uint16_t secIdxRdata = hasRodata ? nextSecIndex++ : 0;
    const uint16_t secIdxXdata = hasXdata ? nextSecIndex++ : 0;
    const uint16_t secIdxPdata = hasPdata ? nextSecIndex++ : 0;
    const uint16_t numSections = static_cast<uint16_t>((nextSecIndex - 1) + (hasDebugLine ? 1 : 0));

    std::vector<uint8_t> symtabBytes;
    std::vector<uint8_t> strtabBytes(4, 0);
    std::unordered_map<uint32_t, uint32_t> textSymMap;
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;
    std::unordered_map<std::string, uint32_t> definedNameMap;
    uint32_t coffSymCount = 0;

    auto addToStrTab = [&](const std::string &s) -> uint32_t {
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
                secNum = kImageSymUndefined;
            } else if (s.binding == SymbolBinding::Local) {
                secNum = static_cast<int16_t>(secIdxRdata);
                value = static_cast<uint32_t>(s.offset);
                storageClass = kImageSymClassStatic;
            } else {
                secNum = static_cast<int16_t>(secIdxRdata);
                value = static_cast<uint32_t>(s.offset);
            }

            uint32_t strOff = 0;
            if (s.name.size() > 8)
                strOff = addToStrTab(s.name);

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            rodataSymMap[i] = coffIdx;
            if (s.binding != SymbolBinding::External)
                definedNameMap[s.name] = coffIdx;
        }
    }

    std::unordered_map<std::string, uint32_t> definedRodataByName;
    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External)
                continue;
            auto it = rodataSymMap.find(i);
            if (it != rodataSymMap.end())
                definedRodataByName[s.name] = it->second;
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

    for (uint32_t i = 1; i < text.symbols().count(); ++i) {
        const Symbol &s = text.symbols().at(i);
        if (s.binding == SymbolBinding::External) {
            auto existing = definedNameMap.find(s.name);
            if (existing != definedNameMap.end()) {
                textSymMap[i] = existing->second;
                continue;
            }
        }

        int16_t secNum = 0;
        uint32_t value = 0;
        uint8_t storageClass = kImageSymClassExternal;

        if (s.binding == SymbolBinding::External) {
            secNum = kImageSymUndefined;
        } else if (s.binding == SymbolBinding::Local) {
            secNum = static_cast<int16_t>(secIdxText);
            value = static_cast<uint32_t>(s.offset);
            storageClass = kImageSymClassStatic;
        } else {
            secNum = static_cast<int16_t>(secIdxText);
            value = static_cast<uint32_t>(s.offset);
        }

        uint32_t strOff = 0;
        if (s.name.size() > 8)
            strOff = addToStrTab(s.name);

        writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0x20, storageClass);
        const uint32_t coffIdx = coffSymCount++;
        textSymMap[i] = coffIdx;
        if (s.binding != SymbolBinding::External)
            definedNameMap[s.name] = coffIdx;
    }

    uint32_t debugLineStrOff = 0;
    if (hasDebugLine)
        debugLineStrOff = addToStrTab(".debug_line");

    const uint32_t strtabSize = static_cast<uint32_t>(strtabBytes.size());
    strtabBytes[0] = static_cast<uint8_t>(strtabSize);
    strtabBytes[1] = static_cast<uint8_t>(strtabSize >> 8);
    strtabBytes[2] = static_cast<uint8_t>(strtabSize >> 16);
    strtabBytes[3] = static_cast<uint8_t>(strtabSize >> 24);

    std::vector<uint8_t> textRelocBytes;
    for (const auto &rel : text.relocations()) {
        if (!validateCoffRelocationAddend(text, rel, err))
            return false;
        uint32_t coffSymIdx = 0;
        auto it = textSymMap.find(rel.symbolIndex);
        if (rel.targetSection == SymbolSection::Rodata) {
            if (rel.symbolIndex < text.symbols().count()) {
                const Symbol &sym = text.symbols().at(rel.symbolIndex);
                auto rodIt = definedRodataByName.find(sym.name);
                if (rodIt != definedRodataByName.end())
                    coffSymIdx = rodIt->second;
            }
            if (coffSymIdx == 0) {
                auto rit = rodataSymMap.find(rel.symbolIndex);
                if (rit != rodataSymMap.end())
                    coffSymIdx = rit->second;
            }
            if (coffSymIdx == 0 && it != textSymMap.end())
                coffSymIdx = it->second;
        } else {
            coffSymIdx = (it != textSymMap.end()) ? it->second : 0;
        }
        writeReloc(
            textRelocBytes, static_cast<uint32_t>(rel.offset), coffSymIdx, coffRelocType(rel.kind));
    }
    const uint32_t numTextRelocs = static_cast<uint32_t>(text.relocations().size());

    std::vector<uint8_t> pdataRelocBytes;
    for (const auto &rel : pdataRelocs) {
        auto it = definedNameMap.find(rel.symbolName);
        if (it == definedNameMap.end()) {
            err << "CoffWriter: missing symbol '" << rel.symbolName << "' for .pdata relocation\n";
            return false;
        }
        writeReloc(pdataRelocBytes, rel.offset, it->second, rel.type);
    }
    const uint32_t numPdataRelocs = static_cast<uint32_t>(pdataRelocs.size());

    const uint32_t textSize = static_cast<uint32_t>(text.bytes().size());
    const uint32_t rdataSize = hasRodata ? static_cast<uint32_t>(rodata.bytes().size()) : 0;
    const uint32_t xdataSize = hasXdata ? static_cast<uint32_t>(xdataBytes.size()) : 0;
    const uint32_t pdataSize = hasPdata ? static_cast<uint32_t>(pdataBytes.size()) : 0;
    const uint32_t debugLineDataSize =
        hasDebugLine ? static_cast<uint32_t>(debugLineData_.size()) : 0;

    const uint32_t headerAreaSize = kCoffHeaderSize + numSections * kSectionHeaderSize;
    const uint32_t textDataOff = static_cast<uint32_t>(alignUp(headerAreaSize, 4));
    const uint32_t textRelocOff = textDataOff + textSize;
    const uint32_t textRelocTotalSize = numTextRelocs * kCoffRelocSize;
    uint32_t cursor = static_cast<uint32_t>(alignUp(textRelocOff + textRelocTotalSize, 4));

    uint32_t rdataDataOff = 0;
    if (hasRodata) {
        rdataDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(rdataDataOff + rdataSize, 4));
    }
    uint32_t xdataDataOff = 0;
    if (hasXdata) {
        xdataDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(xdataDataOff + xdataSize, 4));
    }
    uint32_t pdataDataOff = 0;
    uint32_t pdataRelocOff = 0;
    if (hasPdata) {
        pdataDataOff = cursor;
        pdataRelocOff = pdataDataOff + pdataSize;
        cursor = static_cast<uint32_t>(alignUp(pdataRelocOff + numPdataRelocs * kCoffRelocSize, 4));
    }
    uint32_t debugLineDataOff = 0;
    if (hasDebugLine) {
        debugLineDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(debugLineDataOff + debugLineDataSize, 4));
    }
    const uint32_t symtabOff = cursor;

    std::vector<uint8_t> file;
    file.reserve(symtabOff + symtabBytes.size() + strtabBytes.size());

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
    writeSectionHeader(file,
                       ".text",
                       0,
                       0,
                       textSize,
                       textDataOff,
                       (numTextRelocs > 0) ? textRelocOff : 0,
                       numTextRelocs,
                       textChars);

    if (hasRodata) {
        const uint32_t rdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign8;
        writeSectionHeader(file, ".rdata", 0, 0, rdataSize, rdataDataOff, 0, 0, rdataChars);
    }
    if (hasXdata) {
        const uint32_t xdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file, ".xdata", 0, 0, xdataSize, xdataDataOff, 0, 0, xdataChars);
    }
    if (hasPdata) {
        const uint32_t pdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file,
                           ".pdata",
                           0,
                           0,
                           pdataSize,
                           pdataDataOff,
                           (numPdataRelocs > 0) ? pdataRelocOff : 0,
                           numPdataRelocs,
                           pdataChars);
    }
    if (hasDebugLine) {
        const std::string debugSecName = "/" + std::to_string(debugLineStrOff);
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
    file.insert(file.end(), text.bytes().begin(), text.bytes().end());

    if (!textRelocBytes.empty()) {
        padTo(file, textRelocOff);
        file.insert(file.end(), textRelocBytes.begin(), textRelocBytes.end());
    }
    if (hasRodata) {
        padTo(file, rdataDataOff);
        file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());
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
    ofs.write(reinterpret_cast<const char *>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!ofs) {
        err << "CoffWriter: write failed for " << path << "\n";
        return false;
    }
    return true;
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
                buildWin64UnwindSections(
                    text, xdataNameBase, xdataBytes, xdataSymbols, pdataBytes, pdataRelocs);
                xdataNameBase += static_cast<uint32_t>(text.win64UnwindEntries().size());
            }
        }
    }

    const bool hasRodata = !rodata.empty();
    const bool hasXdata = !xdataBytes.empty();
    const bool hasPdata = !pdataBytes.empty();
    const bool hasDebugLine = !debugLineData_.empty();

    const size_t textCount = textSections.size();
    std::vector<uint16_t> secIdxText(textCount, 0);
    uint16_t nextSecIndex = 1;
    for (size_t i = 0; i < textCount; ++i)
        secIdxText[i] = nextSecIndex++;
    const uint16_t secIdxRdata = hasRodata ? nextSecIndex++ : 0;
    const uint16_t secIdxXdata = hasXdata ? nextSecIndex++ : 0;
    const uint16_t secIdxPdata = hasPdata ? nextSecIndex++ : 0;
    const uint16_t numSections = static_cast<uint16_t>((nextSecIndex - 1) + (hasDebugLine ? 1 : 0));

    std::vector<uint8_t> symtabBytes;
    std::vector<uint8_t> strtabBytes(4, 0);
    std::vector<std::unordered_map<uint32_t, uint32_t>> textSymMaps(textCount);
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;
    std::unordered_map<std::string, uint32_t> definedNameMap;
    std::unordered_map<std::string, uint32_t> externalNameMap;
    uint32_t coffSymCount = 0;

    auto addToStrTab = [&](const std::string &s) -> uint32_t {
        const uint32_t offset = static_cast<uint32_t>(strtabBytes.size());
        strtabBytes.insert(strtabBytes.end(), s.begin(), s.end());
        strtabBytes.push_back(0);
        return offset;
    };

    auto encodeSectionHeaderName = [&](const std::string &name) -> std::string {
        if (name.size() <= 8)
            return name;
        return "/" + std::to_string(addToStrTab(name));
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
    }
    std::string debugHeaderName;
    if (hasDebugLine)
        debugHeaderName = encodeSectionHeaderName(".debug_line");

    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External) {
                pendingExternals.push_back({false, SIZE_MAX, i, s.name, 0});
                continue;
            }

            const int16_t secNum = static_cast<int16_t>(secIdxRdata);
            const uint32_t value = static_cast<uint32_t>(s.offset);
            const uint8_t storageClass =
                (s.binding == SymbolBinding::Local) ? kImageSymClassStatic : kImageSymClassExternal;
            const uint32_t strOff = (s.name.size() > 8) ? addToStrTab(s.name) : 0;

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            rodataSymMap[i] = coffIdx;
            definedNameMap[s.name] = coffIdx;
        }
    }

    std::unordered_map<std::string, uint32_t> definedRodataByName;
    if (hasRodata) {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i) {
            const Symbol &s = rodata.symbols().at(i);
            if (s.binding == SymbolBinding::External)
                continue;
            auto it = rodataSymMap.find(i);
            if (it != rodataSymMap.end())
                definedRodataByName[s.name] = it->second;
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

    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        for (uint32_t i = 1; i < text.symbols().count(); ++i) {
            const Symbol &s = text.symbols().at(i);
            if (s.binding == SymbolBinding::External) {
                pendingExternals.push_back({true, ti, i, s.name, 0x20});
                continue;
            }

            const int16_t secNum = static_cast<int16_t>(secIdxText[ti]);
            const uint32_t value = static_cast<uint32_t>(s.offset);
            const uint8_t storageClass =
                (s.binding == SymbolBinding::Local) ? kImageSymClassStatic : kImageSymClassExternal;
            const uint32_t strOff = (s.name.size() > 8) ? addToStrTab(s.name) : 0;

            writeSymbol(symtabBytes, s.name, strOff, value, secNum, 0x20, storageClass);
            const uint32_t coffIdx = coffSymCount++;
            textSymMaps[ti][i] = coffIdx;
            definedNameMap[s.name] = coffIdx;
        }
    }

    for (const auto &ext : pendingExternals) {
        const auto definedIt = definedNameMap.find(ext.name);
        if (definedIt != definedNameMap.end()) {
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

    const uint32_t strtabSize = static_cast<uint32_t>(strtabBytes.size());
    strtabBytes[0] = static_cast<uint8_t>(strtabSize);
    strtabBytes[1] = static_cast<uint8_t>(strtabSize >> 8);
    strtabBytes[2] = static_cast<uint8_t>(strtabSize >> 16);
    strtabBytes[3] = static_cast<uint8_t>(strtabSize >> 24);

    std::vector<std::vector<uint8_t>> textRelocBytes(textCount);
    std::vector<uint32_t> numTextRelocs(textCount, 0);
    for (size_t ti = 0; ti < textCount; ++ti) {
        const auto &text = textSections[ti];
        auto &relocBytes = textRelocBytes[ti];
        for (const auto &rel : text.relocations()) {
            if (!validateCoffRelocationAddend(text, rel, err))
                return false;
            uint32_t coffSymIdx = 0;
            auto it = textSymMaps[ti].find(rel.symbolIndex);
            if (rel.targetSection == SymbolSection::Rodata) {
                if (rel.symbolIndex < text.symbols().count()) {
                    const Symbol &sym = text.symbols().at(rel.symbolIndex);
                    auto rodIt = definedRodataByName.find(sym.name);
                    if (rodIt != definedRodataByName.end())
                        coffSymIdx = rodIt->second;
                }
                if (coffSymIdx == 0) {
                    auto rit = rodataSymMap.find(rel.symbolIndex);
                    if (rit != rodataSymMap.end())
                        coffSymIdx = rit->second;
                }
                if (coffSymIdx == 0 && it != textSymMaps[ti].end())
                    coffSymIdx = it->second;
            } else {
                coffSymIdx = (it != textSymMaps[ti].end()) ? it->second : 0;
            }
            writeReloc(
                relocBytes, static_cast<uint32_t>(rel.offset), coffSymIdx, coffRelocType(rel.kind));
        }
        numTextRelocs[ti] = static_cast<uint32_t>(text.relocations().size());
    }

    std::vector<uint8_t> pdataRelocBytes;
    for (const auto &rel : pdataRelocs) {
        auto it = definedNameMap.find(rel.symbolName);
        if (it == definedNameMap.end()) {
            err << "CoffWriter: missing symbol '" << rel.symbolName << "' for .pdata relocation\n";
            return false;
        }
        writeReloc(pdataRelocBytes, rel.offset, it->second, rel.type);
    }
    const uint32_t numPdataRelocs = static_cast<uint32_t>(pdataRelocs.size());

    std::vector<uint32_t> textSizes(textCount, 0);
    for (size_t ti = 0; ti < textCount; ++ti)
        textSizes[ti] = static_cast<uint32_t>(textSections[ti].bytes().size());
    const uint32_t rdataSize = hasRodata ? static_cast<uint32_t>(rodata.bytes().size()) : 0;
    const uint32_t xdataSize = hasXdata ? static_cast<uint32_t>(xdataBytes.size()) : 0;
    const uint32_t pdataSize = hasPdata ? static_cast<uint32_t>(pdataBytes.size()) : 0;
    const uint32_t debugLineDataSize =
        hasDebugLine ? static_cast<uint32_t>(debugLineData_.size()) : 0;

    const uint32_t headerAreaSize = kCoffHeaderSize + numSections * kSectionHeaderSize;
    uint32_t cursor = static_cast<uint32_t>(alignUp(headerAreaSize, 4));

    std::vector<uint32_t> textDataOff(textCount, 0);
    std::vector<uint32_t> textRelocOff(textCount, 0);
    for (size_t ti = 0; ti < textCount; ++ti) {
        textDataOff[ti] = cursor;
        cursor += textSizes[ti];
        if (numTextRelocs[ti] > 0) {
            textRelocOff[ti] = cursor;
            cursor += numTextRelocs[ti] * kCoffRelocSize;
        }
        cursor = static_cast<uint32_t>(alignUp(cursor, 4));
    }

    uint32_t rdataDataOff = 0;
    if (hasRodata) {
        rdataDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(rdataDataOff + rdataSize, 4));
    }
    uint32_t xdataDataOff = 0;
    if (hasXdata) {
        xdataDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(xdataDataOff + xdataSize, 4));
    }
    uint32_t pdataDataOff = 0;
    uint32_t pdataRelocOff = 0;
    if (hasPdata) {
        pdataDataOff = cursor;
        pdataRelocOff = pdataDataOff + pdataSize;
        cursor = static_cast<uint32_t>(alignUp(pdataRelocOff + numPdataRelocs * kCoffRelocSize, 4));
    }
    uint32_t debugLineDataOff = 0;
    if (hasDebugLine) {
        debugLineDataOff = cursor;
        cursor = static_cast<uint32_t>(alignUp(debugLineDataOff + debugLineDataSize, 4));
    }
    const uint32_t symtabOff = cursor;

    std::vector<uint8_t> file;
    file.reserve(symtabOff + symtabBytes.size() + strtabBytes.size());

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
        writeSectionHeader(file,
                           textHeaderNames[ti].c_str(),
                           0,
                           0,
                           textSizes[ti],
                           textDataOff[ti],
                           (numTextRelocs[ti] > 0) ? textRelocOff[ti] : 0,
                           numTextRelocs[ti],
                           textChars);
    }

    if (hasRodata) {
        const uint32_t rdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign8;
        writeSectionHeader(file, ".rdata", 0, 0, rdataSize, rdataDataOff, 0, 0, rdataChars);
    }
    if (hasXdata) {
        const uint32_t xdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file, ".xdata", 0, 0, xdataSize, xdataDataOff, 0, 0, xdataChars);
    }
    if (hasPdata) {
        const uint32_t pdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign4;
        writeSectionHeader(file,
                           ".pdata",
                           0,
                           0,
                           pdataSize,
                           pdataDataOff,
                           (numPdataRelocs > 0) ? pdataRelocOff : 0,
                           numPdataRelocs,
                           pdataChars);
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
        file.insert(file.end(), textSections[ti].bytes().begin(), textSections[ti].bytes().end());
        if (!textRelocBytes[ti].empty()) {
            padTo(file, textRelocOff[ti]);
            file.insert(file.end(), textRelocBytes[ti].begin(), textRelocBytes[ti].end());
        }
    }

    if (hasRodata) {
        padTo(file, rdataDataOff);
        file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());
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
    ofs.write(reinterpret_cast<const char *>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!ofs) {
        err << "CoffWriter: write failed for " << path << "\n";
        return false;
    }
    return true;
}

} // namespace viper::codegen::objfile
