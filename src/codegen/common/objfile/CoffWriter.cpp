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
//     .text data | .text relocs | .rdata data | symbol table | string table
//   - Symbol names <= 8 chars are stored inline; longer names use string table
//   - String table starts with 4-byte size (includes the size field itself)
//   - COFF relocations are 10 bytes: offset(4) + symIdx(4) + type(2)
//   - No explicit addend field — addends are embedded in instruction bytes
// Ownership/Lifetime:
//   - Stateless between write() calls
// Links: codegen/common/objfile/CoffWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/CoffWriter.hpp"
#include "codegen/common/objfile/ObjFileWriterUtil.hpp"
#include "codegen/common/objfile/StringTable.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace viper::codegen::objfile
{

// =============================================================================
// COFF Constants
// =============================================================================

// Machine types
static constexpr uint16_t kMachineAMD64 = 0x8664;
static constexpr uint16_t kMachineARM64 = 0xAA64;

// COFF header size (no optional header for .obj files)
static constexpr uint16_t kCoffHeaderSize = 20;

// Section header size
static constexpr uint16_t kSectionHeaderSize = 40;

// Section flags
static constexpr uint32_t kImageScnCntCode = 0x00000020;
static constexpr uint32_t kImageScnCntInitData = 0x00000040;
static constexpr uint32_t kImageScnAlignText = 0x00600000; // 16-byte align for x86_64
static constexpr uint32_t kImageScnAlign4 = 0x00300000;    // 4-byte align for AArch64
static constexpr uint32_t kImageScnAlign8 = 0x00400000;    // 8-byte align
static constexpr uint32_t kImageScnMemExecute = 0x20000000;
static constexpr uint32_t kImageScnMemRead = 0x40000000;

// Symbol storage class
static constexpr uint8_t kImageSymClassExternal = 2;
static constexpr uint8_t kImageSymClassStatic = 3;

// Symbol section numbers
static constexpr int16_t kImageSymUndefined = 0;

// COFF relocation size
static constexpr uint32_t kCoffRelocSize = 10;

// Symbol entry size
static constexpr uint32_t kCoffSymSize = 18;

// x86_64 COFF relocation types
static constexpr uint16_t kImageRelAMD64_Addr64 = 1;
static constexpr uint16_t kImageRelAMD64_Rel32 = 4;

// AArch64 COFF relocation types
static constexpr uint16_t kImageRelARM64_Branch26 = 3;
static constexpr uint16_t kImageRelARM64_PagebaseRel21 = 4;
static constexpr uint16_t kImageRelARM64_Pageoffset12A = 6;
static constexpr uint16_t kImageRelARM64_Pageoffset12L = 7;
static constexpr uint16_t kImageRelARM64_Branch19 = 8;

// Helpers: appendLE16/32, alignUp, padTo are provided by ObjFileWriterUtil.hpp.

/// Map RelocKind to COFF relocation type.
static uint16_t coffRelocType(RelocKind kind)
{
    switch (kind)
    {
        // x86_64
        case RelocKind::PCRel32:
            return kImageRelAMD64_Rel32;
        case RelocKind::Branch32:
            return kImageRelAMD64_Rel32; // COFF uses same type for both
        case RelocKind::Abs64:
            return kImageRelAMD64_Addr64;
        // AArch64
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

/// Write a COFF section header (40 bytes).
static void writeSectionHeader(std::vector<uint8_t> &out,
                               const char *name,
                               uint32_t virtualSize,
                               uint32_t virtualAddr,
                               uint32_t rawDataSize,
                               uint32_t rawDataPtr,
                               uint32_t relocPtr,
                               uint32_t numRelocs,
                               uint32_t characteristics)
{
    // Name: 8 bytes, padded with zeros.
    for (int i = 0; i < 8; ++i)
    {
        if (name[i] != '\0')
            out.push_back(static_cast<uint8_t>(name[i]));
        else
        {
            // Fill remaining bytes with zeros.
            for (int j = i; j < 8; ++j)
                out.push_back(0);
            break;
        }
    }

    appendLE32(out, virtualSize);                      // VirtualSize (0 for .obj)
    appendLE32(out, virtualAddr);                      // VirtualAddress (0 for .obj)
    appendLE32(out, rawDataSize);                      // SizeOfRawData
    appendLE32(out, rawDataPtr);                       // PointerToRawData
    appendLE32(out, relocPtr);                         // PointerToRelocations
    appendLE32(out, 0);                                // PointerToLineNumbers
    appendLE16(out, static_cast<uint16_t>(numRelocs)); // NumberOfRelocations
    appendLE16(out, 0);                                // NumberOfLinenumbers
    appendLE32(out, characteristics);
}

/// Write a COFF symbol entry (18 bytes).
/// If the name is <= 8 chars, it's stored inline.
/// Otherwise, the first 4 bytes are zero and the next 4 bytes are the
/// string table offset.
static void writeSymbol(std::vector<uint8_t> &out,
                        const std::string &name,
                        uint32_t strTabOffset,
                        uint32_t value,
                        int16_t sectionNumber,
                        uint16_t type,
                        uint8_t storageClass)
{
    if (name.size() <= 8)
    {
        // Inline name (8 bytes, zero-padded).
        for (size_t i = 0; i < 8; ++i)
        {
            if (i < name.size())
                out.push_back(static_cast<uint8_t>(name[i]));
            else
                out.push_back(0);
        }
    }
    else
    {
        // Long name: 4 zero bytes + 4-byte string table offset.
        appendLE32(out, 0);
        appendLE32(out, strTabOffset);
    }

    appendLE32(out, value);
    appendLE16(out, static_cast<uint16_t>(sectionNumber));
    appendLE16(out, type);
    out.push_back(storageClass);
    out.push_back(0); // NumberOfAuxSymbols
}

/// Write a COFF relocation entry (10 bytes).
static void writeReloc(std::vector<uint8_t> &out,
                       uint32_t virtualAddr,
                       uint32_t symbolTableIndex,
                       uint16_t type)
{
    appendLE32(out, virtualAddr);
    appendLE32(out, symbolTableIndex);
    appendLE16(out, type);
}

// =============================================================================
// CoffWriter::write
// =============================================================================

bool CoffWriter::write(const std::string &path,
                       const CodeSection &text,
                       const CodeSection &rodata,
                       std::ostream &err)
{
    // Determine section count: always .text; .rdata only if rodata has content.
    const bool hasRodata = !rodata.empty();
    const uint16_t numSections = hasRodata ? 2 : 1;
    const uint16_t secIdxText = 1; // COFF sections are 1-based
    const uint16_t secIdxRdata = hasRodata ? 2 : 0;

    // --- 1. Build symbol table and string table ---
    // COFF string table starts after the symbol table.
    // The string table begins with a 4-byte size field (includes itself).

    // Build a string table for long symbol names.
    // COFF string table offset starts at 4 (after the 4-byte size field).
    std::vector<uint8_t> symtabBytes;
    std::vector<uint8_t> strtabBytes;

    // Reserve the 4-byte size prefix.
    strtabBytes.resize(4, 0);

    // Track COFF symbol indices for relocation remapping.
    std::unordered_map<uint32_t, uint32_t> textSymMap;
    std::unordered_map<uint32_t, uint32_t> rodataSymMap;
    uint32_t coffSymCount = 0;

    // Helper to add a string to the COFF string table and get its offset.
    auto addToStrTab = [&](const std::string &s) -> uint32_t
    {
        uint32_t offset = static_cast<uint32_t>(strtabBytes.size());
        strtabBytes.insert(strtabBytes.end(), s.begin(), s.end());
        strtabBytes.push_back(0); // NUL terminator
        return offset;
    };

    // Process text section symbols.
    for (uint32_t i = 1; i < text.symbols().count(); ++i)
    {
        const Symbol &s = text.symbols().at(i);
        int16_t secNum = 0;
        uint32_t value = 0;
        uint8_t storageClass = kImageSymClassExternal;

        if (s.binding == SymbolBinding::External)
        {
            secNum = kImageSymUndefined;
        }
        else if (s.binding == SymbolBinding::Local)
        {
            secNum = static_cast<int16_t>(secIdxText);
            value = static_cast<uint32_t>(s.offset);
            storageClass = kImageSymClassStatic;
        }
        else
        {
            // Global defined symbol.
            secNum = static_cast<int16_t>(secIdxText);
            value = static_cast<uint32_t>(s.offset);
        }

        uint32_t strOff = 0;
        if (s.name.size() > 8)
            strOff = addToStrTab(s.name);

        writeSymbol(symtabBytes,
                    s.name,
                    strOff,
                    value,
                    secNum,
                    0x20, // type: 0x20 = function for code symbols
                    storageClass);
        textSymMap[i] = coffSymCount++;
    }

    // Process rodata section symbols.
    if (hasRodata)
    {
        for (uint32_t i = 1; i < rodata.symbols().count(); ++i)
        {
            const Symbol &s = rodata.symbols().at(i);
            int16_t secNum = 0;
            uint32_t value = 0;
            uint8_t storageClass = kImageSymClassExternal;

            if (s.binding == SymbolBinding::External)
            {
                secNum = kImageSymUndefined;
            }
            else if (s.binding == SymbolBinding::Local)
            {
                secNum = static_cast<int16_t>(secIdxRdata);
                value = static_cast<uint32_t>(s.offset);
                storageClass = kImageSymClassStatic;
            }
            else
            {
                secNum = static_cast<int16_t>(secIdxRdata);
                value = static_cast<uint32_t>(s.offset);
            }

            uint32_t strOff = 0;
            if (s.name.size() > 8)
                strOff = addToStrTab(s.name);

            writeSymbol(symtabBytes,
                        s.name,
                        strOff,
                        value,
                        secNum,
                        0, // type: 0 = not a function (data)
                        storageClass);
            rodataSymMap[i] = coffSymCount++;
        }
    }

    // Finalize string table size.
    {
        uint32_t strtabSize = static_cast<uint32_t>(strtabBytes.size());
        strtabBytes[0] = static_cast<uint8_t>(strtabSize);
        strtabBytes[1] = static_cast<uint8_t>(strtabSize >> 8);
        strtabBytes[2] = static_cast<uint8_t>(strtabSize >> 16);
        strtabBytes[3] = static_cast<uint8_t>(strtabSize >> 24);
    }

    // --- 2. Build relocations for .text ---
    std::vector<uint8_t> textRelocBytes;
    for (const auto &rel : text.relocations())
    {
        uint32_t coffSymIdx = 0;
        auto it = textSymMap.find(rel.symbolIndex);
        if (it != textSymMap.end())
        {
            coffSymIdx = it->second;
        }
        else if (hasRodata)
        {
            auto rit = rodataSymMap.find(rel.symbolIndex);
            if (rit != rodataSymMap.end())
                coffSymIdx = rit->second;
        }

        uint16_t relocType = coffRelocType(rel.kind);
        writeReloc(textRelocBytes, static_cast<uint32_t>(rel.offset), coffSymIdx, relocType);
    }

    uint32_t numTextRelocs = static_cast<uint32_t>(text.relocations().size());

    // --- 3. Compute file layout ---
    // Layout: COFF header | section headers | .text data | .text relocs |
    //         .rdata data | symbol table | string table

    uint32_t textSize = static_cast<uint32_t>(text.bytes().size());
    uint32_t rdataSize = hasRodata ? static_cast<uint32_t>(rodata.bytes().size()) : 0;

    uint32_t headerAreaSize = kCoffHeaderSize + numSections * kSectionHeaderSize;

    // .text section data (align to 4 for uniformity)
    uint32_t textDataOff = static_cast<uint32_t>(alignUp(headerAreaSize, 4));
    // .text relocations immediately after .text data
    uint32_t textRelocOff = textDataOff + textSize;
    uint32_t textRelocTotalSize = numTextRelocs * kCoffRelocSize;

    // .rdata section data
    uint32_t rdataDataOff = 0;
    if (hasRodata)
    {
        rdataDataOff = static_cast<uint32_t>(alignUp(textRelocOff + textRelocTotalSize, 4));
    }

    // Symbol table follows all section data + relocs
    uint32_t symtabOff;
    if (hasRodata)
        symtabOff = static_cast<uint32_t>(alignUp(rdataDataOff + rdataSize, 4));
    else
        symtabOff = static_cast<uint32_t>(alignUp(textRelocOff + textRelocTotalSize, 4));

    // --- 4. Build the file ---
    std::vector<uint8_t> file;
    file.reserve(symtabOff + symtabBytes.size() + strtabBytes.size());

    // COFF header (20 bytes)
    uint16_t machine = (arch_ == ObjArch::X86_64) ? kMachineAMD64 : kMachineARM64;
    appendLE16(file, machine);      // Machine
    appendLE16(file, numSections);  // NumberOfSections
    appendLE32(file, 0);            // TimeDateStamp (0 for reproducibility)
    appendLE32(file, symtabOff);    // PointerToSymbolTable
    appendLE32(file, coffSymCount); // NumberOfSymbols
    appendLE16(file, 0);            // SizeOfOptionalHeader (0 for .obj)
    appendLE16(file, 0);            // Characteristics

    // Section headers
    // .text
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

    // .rdata
    if (hasRodata)
    {
        uint32_t rdataChars = kImageScnCntInitData | kImageScnMemRead | kImageScnAlign8;
        writeSectionHeader(file, ".rdata", 0, 0, rdataSize, rdataDataOff, 0, 0, rdataChars);
    }

    // .text section data
    padTo(file, textDataOff);
    file.insert(file.end(), text.bytes().begin(), text.bytes().end());

    // .text relocations
    if (!textRelocBytes.empty())
    {
        padTo(file, textRelocOff);
        file.insert(file.end(), textRelocBytes.begin(), textRelocBytes.end());
    }

    // .rdata section data
    if (hasRodata)
    {
        padTo(file, rdataDataOff);
        file.insert(file.end(), rodata.bytes().begin(), rodata.bytes().end());
    }

    // Symbol table
    padTo(file, symtabOff);
    file.insert(file.end(), symtabBytes.begin(), symtabBytes.end());

    // String table (immediately after symbol table)
    file.insert(file.end(), strtabBytes.begin(), strtabBytes.end());

    // --- 5. Write to disk ---
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        err << "CoffWriter: cannot open " << path << " for writing\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char *>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!ofs)
    {
        err << "CoffWriter: write failed for " << path << "\n";
        return false;
    }
    return true;
}

} // namespace viper::codegen::objfile
