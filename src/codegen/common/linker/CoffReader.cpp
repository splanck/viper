//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/CoffReader.cpp
// Purpose: COFF/PE object file reader for the native linker.
// Key invariants:
//   - Machine field at offset 0: 0x8664 (AMD64), 0xAA64 (ARM64)
//   - COFF relocations are 10 bytes: offset(4) + symIndex(4) + type(2)
//   - No explicit addend — extracted from instruction bytes at reloc site
//   - Symbol names ≤8 chars inline; longer names use string table offset
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"

#include <cstring>

namespace viper::codegen::linker {

namespace coff {
static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

static constexpr uint16_t IMAGE_SYM_CLASS_EXTERNAL = 2;
static constexpr uint16_t IMAGE_SYM_CLASS_STATIC = 3;
static constexpr uint16_t IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105;

static constexpr uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
static constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
static constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
static constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
static constexpr uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
static constexpr uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;

#pragma pack(push, 1)

struct CoffHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct SectionHeader {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct CoffReloc {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
};

struct CoffSymbol {
    union {
        char ShortName[8];

        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;

    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};

#pragma pack(pop)
} // namespace coff

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t readLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

template <typename T> static const T *coffAt(const uint8_t *data, size_t size, size_t offset) {
    if (offset + sizeof(T) > size)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

bool readCoffObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err) {
    if (size < sizeof(coff::CoffHeader)) {
        err << "error: " << name << ": too small for COFF header\n";
        return false;
    }

    const auto *hdr = coffAt<coff::CoffHeader>(data, size, 0);
    if (!hdr)
        return false;

    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.name = name;
    obj.machine = hdr->Machine;

    // Locate string table (immediately after symbol table).
    size_t strTabOff = hdr->PointerToSymbolTable +
                       static_cast<size_t>(hdr->NumberOfSymbols) * sizeof(coff::CoffSymbol);
    uint32_t strTabSize = 0;
    if (strTabOff + 4 <= size)
        strTabSize = readLE32(data + strTabOff);

    auto readSymName = [&](const coff::CoffSymbol *sym) -> std::string {
        if (sym->Name.LongName.Zeros == 0) {
            // Long name: offset into string table.
            size_t off = strTabOff + sym->Name.LongName.Offset;
            if (off < size)
                return std::string(reinterpret_cast<const char *>(data + off));
            return "";
        }
        // Short name: up to 8 chars, NUL-padded.
        size_t len = 0;
        while (len < 8 && sym->Name.ShortName[len] != '\0')
            ++len;
        return std::string(sym->Name.ShortName, len);
    };

    // Parse sections.
    obj.sections.resize(1); // Null section at index 0.
    obj.sections[0].name = "";

    const size_t secOff = sizeof(coff::CoffHeader) + hdr->SizeOfOptionalHeader;
    for (uint16_t i = 0; i < hdr->NumberOfSections; ++i) {
        const auto *sh =
            coffAt<coff::SectionHeader>(data, size, secOff + i * sizeof(coff::SectionHeader));
        if (!sh)
            break;

        ObjSection sec;

        // Parse section name (may reference string table if starts with '/').
        if (sh->Name[0] == '/') {
            size_t off = 0;
            for (int c = 1; c < 8 && sh->Name[c] >= '0' && sh->Name[c] <= '9'; ++c)
                off = off * 10 + (sh->Name[c] - '0');
            size_t pos = strTabOff + off;
            if (pos < size)
                sec.name = reinterpret_cast<const char *>(data + pos);
        } else {
            size_t len = 0;
            while (len < 8 && sh->Name[len] != '\0')
                ++len;
            sec.name.assign(sh->Name, len);
        }

        sec.executable = (sh->Characteristics & coff::IMAGE_SCN_MEM_EXECUTE) != 0;
        sec.writable = (sh->Characteristics & coff::IMAGE_SCN_MEM_WRITE) != 0;
        sec.alloc = (sh->Characteristics &
                     (coff::IMAGE_SCN_CNT_CODE | coff::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      coff::IMAGE_SCN_CNT_UNINITIALIZED_DATA)) != 0;

        // Extract alignment from COFF characteristics (bits 20-23).
        const uint32_t alignBits = (sh->Characteristics >> 20) & 0xF;
        sec.alignment = (alignBits > 0) ? (1u << (alignBits - 1)) : 1;

        // Read section data.
        if (sh->SizeOfRawData > 0 && sh->PointerToRawData + sh->SizeOfRawData <= size) {
            sec.data.assign(data + sh->PointerToRawData,
                            data + sh->PointerToRawData + sh->SizeOfRawData);
        } else if (sh->Characteristics & coff::IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
            sec.data.resize(sh->VirtualSize, 0);
        }

        // Read relocations.
        for (uint16_t r = 0; r < sh->NumberOfRelocations; ++r) {
            const auto *cr = coffAt<coff::CoffReloc>(
                data, size, sh->PointerToRelocations + r * sizeof(coff::CoffReloc));
            if (!cr)
                break;

            ObjReloc rel;
            rel.offset = cr->VirtualAddress;
            rel.type = cr->Type;
            rel.symIndex = cr->SymbolTableIndex + 1; // +1 because ObjFile has null sym at 0.

            // Extract addend from instruction bytes.
            if (rel.offset + 4 <= sec.data.size()) {
                int32_t val;
                std::memcpy(&val, sec.data.data() + rel.offset, 4);
                rel.addend = val;
            } else {
                rel.addend = 0;
            }

            sec.relocs.push_back(rel);
        }

        obj.sections.push_back(std::move(sec));
    }

    // Parse symbols.
    if (hdr->NumberOfSymbols > kMaxObjSymbols) {
        err << "error: " << name << ": symbol count " << hdr->NumberOfSymbols << " exceeds limit\n";
        return false;
    }
    obj.symbols.resize(1); // Null symbol at index 0.
    obj.symbols[0] = ObjSymbol{};

    for (uint32_t i = 0; i < hdr->NumberOfSymbols;) {
        const auto *sym = coffAt<coff::CoffSymbol>(
            data, size, hdr->PointerToSymbolTable + i * sizeof(coff::CoffSymbol));
        if (!sym)
            break;

        ObjSymbol os;
        os.name = readSymName(sym);

        if (sym->SectionNumber <= 0) {
            os.binding = ObjSymbol::Undefined;
            if (sym->StorageClass == coff::IMAGE_SYM_CLASS_WEAK_EXTERNAL)
                os.binding = ObjSymbol::Weak;
        } else if (sym->StorageClass == coff::IMAGE_SYM_CLASS_EXTERNAL) {
            os.binding = ObjSymbol::Global;
        } else if (sym->StorageClass == coff::IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
            os.binding = ObjSymbol::Weak;
        } else {
            os.binding = ObjSymbol::Local;
        }

        // Map 1-based COFF section number to our section index.
        if (sym->SectionNumber > 0 &&
            static_cast<uint16_t>(sym->SectionNumber) <= hdr->NumberOfSections)
            os.sectionIndex = static_cast<uint32_t>(sym->SectionNumber);
        os.offset = sym->Value;

        obj.symbols.push_back(std::move(os));

        // Skip aux symbols.
        i += 1 + sym->NumberOfAuxSymbols;
        // Pad the symbol array for skipped aux entries.
        for (uint8_t a = 0; a < sym->NumberOfAuxSymbols; ++a)
            obj.symbols.push_back(ObjSymbol{});
    }

    return true;
}

} // namespace viper::codegen::linker
