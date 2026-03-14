//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/PeExeWriter.cpp
// Purpose: Writes a minimal PE executable (PE32+).
// Key invariants:
//   - DOS header with e_lfanew pointing to PE signature
//   - COFF header + PE32+ Optional Header (240 bytes)
//   - Sections page-aligned (0x1000 VA, 0x200 file)
//   - Console subsystem (IMAGE_SUBSYSTEM_WINDOWS_CUI = 3)
// Links: codegen/common/linker/PeExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/PeExeWriter.hpp"

#include "codegen/common/linker/AlignUtil.hpp"
#include "codegen/common/linker/ExeWriterUtil.hpp"

#include <cstring>
#include <fstream>

namespace viper::codegen::linker
{

using encoding::writeLE16;
using encoding::writeLE32;
using encoding::writeLE64;
using encoding::writePad;
using encoding::padTo;

namespace
{

static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

// DllCharacteristics flags (PE Optional Header).
static constexpr uint16_t kDllCharHighEntropyVA = 0x0020;
static constexpr uint16_t kDllCharDynamicBase = 0x0040;
static constexpr uint16_t kDllCharNXCompat = 0x0100;
static constexpr uint16_t kDllCharTermServerAware = 0x8000;
static constexpr uint16_t kDllCharacteristics =
    kDllCharHighEntropyVA | kDllCharDynamicBase | kDllCharNXCompat | kDllCharTermServerAware;

} // anonymous namespace

bool writePeExe(const std::string &path,
                const LinkLayout &layout,
                LinkArch arch,
                const std::vector<DllImport> & /*imports*/,
                std::ostream &err)
{
    const uint16_t machine =
        (arch == LinkArch::AArch64) ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_AMD64;
    const uint64_t imageBase = 0x140000000ULL;
    const uint32_t sectionAlignment = 0x1000;
    const uint32_t fileAlignment = 0x200;

    std::vector<uint8_t> file;

    // === DOS Header (64 bytes) ===
    file.resize(64, 0);
    file[0] = 'M';
    file[1] = 'Z';
    // e_lfanew at offset 60.
    const uint32_t peOffset = 64;
    file[60] = static_cast<uint8_t>(peOffset);
    file[61] = 0;
    file[62] = 0;
    file[63] = 0;

    // === PE Signature (4 bytes) ===
    writeLE32(file, 0x00004550); // "PE\0\0"

    // === COFF Header (20 bytes) ===
    uint16_t numSections = 0;
    for (const auto &sec : layout.sections)
        if (!sec.data.empty())
            ++numSections;

    writeLE16(file, machine);
    writeLE16(file, numSections);
    writeLE32(file, 0);      // TimeDateStamp
    writeLE32(file, 0);      // PointerToSymbolTable
    writeLE32(file, 0);      // NumberOfSymbols
    writeLE16(file, 240);    // SizeOfOptionalHeader (PE32+)
    writeLE16(file, 0x0022); // Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    // === Optional Header (PE32+, 240 bytes) ===
    const size_t optHeaderStart = file.size();
    writeLE16(file, 0x020B); // Magic: PE32+

    writeLE16(file, 0); // LinkerVersion
    writeLE32(file, 0); // SizeOfCode (filled later)
    writeLE32(file, 0); // SizeOfInitializedData
    writeLE32(file, 0); // SizeOfUninitializedData

    // AddressOfEntryPoint.
    uint64_t mainAddr = resolveMainAddress(layout);
    uint32_t entryRVA = mainAddr ? static_cast<uint32_t>(mainAddr - imageBase) : 0;
    writeLE32(file, entryRVA);
    writeLE32(file, 0); // BaseOfCode

    writeLE64(file, imageBase);
    writeLE32(file, sectionAlignment);
    writeLE32(file, fileAlignment);
    writeLE16(file, 6); // MajorOperatingSystemVersion
    writeLE16(file, 0);
    writeLE16(file, 0); // MajorImageVersion
    writeLE16(file, 0);
    writeLE16(file, 6); // MajorSubsystemVersion
    writeLE16(file, 0);
    writeLE32(file, 0); // Win32VersionValue

    // SizeOfImage: VA of last section + its aligned size.
    uint32_t sizeOfImage = sectionAlignment; // At least first page.
    for (const auto &sec : layout.sections)
    {
        if (sec.data.empty())
            continue;
        uint32_t secEnd = static_cast<uint32_t>(sec.virtualAddr - imageBase +
                                                alignUp(sec.data.size(), sectionAlignment));
        if (secEnd > sizeOfImage)
            sizeOfImage = secEnd;
    }
    writeLE32(file, sizeOfImage);

    // SizeOfHeaders: headers + section table, file-aligned.
    const size_t headersEnd = optHeaderStart + 240 + numSections * 40;
    const uint32_t sizeOfHeaders = static_cast<uint32_t>(alignUp(headersEnd, fileAlignment));
    writeLE32(file, sizeOfHeaders);

    writeLE32(file, 0);                   // CheckSum
    writeLE16(file, 3);                   // Subsystem: IMAGE_SUBSYSTEM_WINDOWS_CUI
    writeLE16(file, kDllCharacteristics); // HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT |
                                          // TERMINAL_SERVER_AWARE
    writeLE64(file, 0x100000);            // SizeOfStackReserve (1MB)
    writeLE64(file, 0x1000);              // SizeOfStackCommit
    writeLE64(file, 0x100000);            // SizeOfHeapReserve
    writeLE64(file, 0x1000);              // SizeOfHeapCommit
    writeLE32(file, 0);                   // LoaderFlags
    writeLE32(file, 16);                  // NumberOfRvaAndSizes

    // Data directories (16 entries × 8 bytes = 128 bytes).
    for (int i = 0; i < 16; ++i)
    {
        writeLE32(file, 0); // VirtualAddress
        writeLE32(file, 0); // Size
    }

    // === Section Headers ===
    struct PeSection
    {
        size_t layoutIdx;
        uint32_t virtualAddress;
        uint32_t virtualSize;
        uint32_t sizeOfRawData;
        uint32_t pointerToRawData;
        uint32_t characteristics;
    };

    std::vector<PeSection> peSections;

    uint32_t currentFileOff = sizeOfHeaders;
    for (size_t i = 0; i < layout.sections.size(); ++i)
    {
        const auto &sec = layout.sections[i];
        if (sec.data.empty())
            continue;

        PeSection ps;
        ps.layoutIdx = i;
        ps.virtualAddress = static_cast<uint32_t>(sec.virtualAddr - imageBase);
        ps.virtualSize = static_cast<uint32_t>(sec.data.size());
        ps.sizeOfRawData = static_cast<uint32_t>(alignUp(sec.data.size(), fileAlignment));
        ps.pointerToRawData = currentFileOff;

        uint32_t chars = 0;
        if (sec.executable)
            chars |= 0x60000020; // MEM_EXECUTE | MEM_READ | CNT_CODE
        else if (sec.writable)
            chars |= 0xC0000040; // MEM_READ | MEM_WRITE | CNT_INITIALIZED_DATA
        else
            chars |= 0x40000040; // MEM_READ | CNT_INITIALIZED_DATA
        ps.characteristics = chars;

        peSections.push_back(ps);
        currentFileOff += ps.sizeOfRawData;
    }

    // Write section headers (40 bytes each).
    for (const auto &ps : peSections)
    {
        const auto &sec = layout.sections[ps.layoutIdx];
        // Section name (8 bytes, NUL-padded).
        char secName[8] = {};
        const char *sn = sec.executable ? ".text" : (sec.writable ? ".data" : ".rdata");
        std::strncpy(secName, sn, 8);
        file.insert(file.end(), secName, secName + 8);

        writeLE32(file, ps.virtualSize);
        writeLE32(file, ps.virtualAddress);
        writeLE32(file, ps.sizeOfRawData);
        writeLE32(file, ps.pointerToRawData);
        writeLE32(file, 0); // PointerToRelocations
        writeLE32(file, 0); // PointerToLinenumbers
        writeLE16(file, 0); // NumberOfRelocations
        writeLE16(file, 0); // NumberOfLinenumbers
        writeLE32(file, ps.characteristics);
    }

    // Pad to sizeOfHeaders.
    padTo(file, sizeOfHeaders);

    // Write section data (file-aligned).
    for (const auto &ps : peSections)
    {
        const auto &sec = layout.sections[ps.layoutIdx];
        padTo(file, ps.pointerToRawData);
        file.insert(file.end(), sec.data.begin(), sec.data.end());
        // Pad to file alignment.
        padTo(file, alignUp(file.size(), fileAlignment));
    }

    // Write file.
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        err << "error: cannot open '" << path << "' for writing\n";
        return false;
    }
    f.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!f)
    {
        err << "error: write failed to '" << path << "'\n";
        return false;
    }

    return true;
}

} // namespace viper::codegen::linker
