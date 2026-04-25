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
#include "codegen/common/linker/RelocConstants.hpp"

#include <cstring>
#include <string>

namespace viper::codegen::linker {

namespace coff {
static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

static constexpr uint16_t IMAGE_SYM_CLASS_EXTERNAL = 2;
static constexpr uint16_t IMAGE_SYM_CLASS_STATIC = 3;
static constexpr uint16_t IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105;
static constexpr int16_t IMAGE_SYM_UNDEFINED = 0;
static constexpr int16_t IMAGE_SYM_ABSOLUTE = -1;
static constexpr int16_t IMAGE_SYM_DEBUG = -2;

static constexpr uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
static constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
static constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
static constexpr uint32_t IMAGE_SCN_LNK_COMDAT = 0x00001000;
static constexpr uint32_t IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000;
static constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
static constexpr uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
static constexpr uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;

static constexpr uint8_t IMAGE_COMDAT_SELECT_NODUPLICATES = 1;
static constexpr uint8_t IMAGE_COMDAT_SELECT_ANY = 2;
static constexpr uint8_t IMAGE_COMDAT_SELECT_ASSOCIATIVE = 5;

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

struct CoffAuxSectionDefinition {
    uint32_t Length;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t CheckSum;
    int16_t Number;
    uint8_t Selection;
    uint8_t Reserved;
    int16_t HighNumber;
};

struct CoffAuxWeakExternal {
    uint32_t TagIndex;
    uint32_t Characteristics;
    uint8_t Unused[10];
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

static uint64_t readLE64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

static bool checkedRange(size_t off, size_t len, size_t size) {
    return off <= size && len <= size - off;
}

static std::string readBoundedString(const uint8_t *data, size_t off, size_t len) {
    const uint8_t *begin = data + off;
    const void *nul = std::memchr(begin, '\0', len);
    if (!nul)
        return "";
    return std::string(reinterpret_cast<const char *>(begin),
                       static_cast<const char *>(nul));
}

static int64_t signExtend(uint64_t value, unsigned bits) {
    const uint64_t signBit = uint64_t{1} << (bits - 1);
    const uint64_t mask = (uint64_t{1} << bits) - 1;
    value &= mask;
    return static_cast<int64_t>((value ^ signBit) - signBit);
}

template <typename T> static const T *coffAt(const uint8_t *data, size_t size, size_t offset) {
    if (offset + sizeof(T) > size)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

static int64_t extractCoffAddend(uint16_t machine,
                                 uint16_t relocType,
                                 const std::vector<uint8_t> &sectionData,
                                 size_t offset) {
    if (machine == coff::IMAGE_FILE_MACHINE_AMD64) {
        if (relocType == coff_x64::kAddr64 && offset + 8 <= sectionData.size())
            return static_cast<int64_t>(readLE64(sectionData.data() + offset));
        if (relocType != coff_x64::kSection && offset + 4 <= sectionData.size()) {
            int32_t val = 0;
            std::memcpy(&val, sectionData.data() + offset, 4);
            return val;
        }
        return 0;
    }

    if (machine != coff::IMAGE_FILE_MACHINE_ARM64 || offset + 4 > sectionData.size())
        return 0;

    if (relocType == coff_a64::kAddr64) {
        if (offset + 8 <= sectionData.size())
            return static_cast<int64_t>(readLE64(sectionData.data() + offset));
        return 0;
    }

    const uint32_t insn = readLE32(sectionData.data() + offset);
    switch (relocType) {
        case coff_a64::kBranch26:
            return signExtend(insn & 0x03FFFFFFu, 26) << 2;
        case coff_a64::kBranch19:
            return signExtend((insn >> 5) & 0x7FFFFu, 19) << 2;
        case coff_a64::kPageRel21: {
            const uint32_t immlo = (insn >> 29) & 0x3u;
            const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
            return signExtend((immhi << 2) | immlo, 21) << 12;
        }
        case coff_a64::kPageOff12A:
            return static_cast<int64_t>((insn >> 10) & 0xFFFu);
        case coff_a64::kPageOff12L: {
            uint32_t shift = insn >> 30;
            if ((insn & 0x04800000u) == 0x04800000u)
                shift = 4;
            return static_cast<int64_t>(((insn >> 10) & 0xFFFu) << shift);
        }
        default:
            return 0;
    }
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

    if (hdr->Machine == 0 && hdr->NumberOfSections == 0xFFFF) {
        err << "error: " << name << ": COFF BigObj is not supported by the native linker\n";
        return false;
    }

    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.name = name;
    obj.machine = hdr->Machine;
    if (hdr->Machine != coff::IMAGE_FILE_MACHINE_AMD64 &&
        hdr->Machine != coff::IMAGE_FILE_MACHINE_ARM64) {
        err << "error: " << name << ": unsupported COFF machine\n";
        return false;
    }

    // Locate string table (immediately after symbol table).
    size_t strTabOff = hdr->PointerToSymbolTable +
                       static_cast<size_t>(hdr->NumberOfSymbols) * sizeof(coff::CoffSymbol);
    uint32_t strTabSize = 0;
    if (strTabOff + 4 <= size)
        strTabSize = readLE32(data + strTabOff);
    if (hdr->NumberOfSymbols > 0 &&
        !checkedRange(hdr->PointerToSymbolTable,
                      static_cast<size_t>(hdr->NumberOfSymbols) * sizeof(coff::CoffSymbol),
                      size)) {
        err << "error: " << name << ": COFF symbol table is out of bounds\n";
        return false;
    }

    auto readSymName = [&](const coff::CoffSymbol *sym) -> std::string {
        if (sym->Name.LongName.Zeros == 0) {
            // Long name: offset into string table.
            if (sym->Name.LongName.Offset < strTabSize) {
                size_t off = strTabOff + sym->Name.LongName.Offset;
                size_t remain = strTabSize - sym->Name.LongName.Offset;
                if (checkedRange(off, remain, size))
                    return readBoundedString(data, off, remain);
            }
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
    std::vector<uint32_t> sectionCharacteristics(hdr->NumberOfSections + 1, 0);

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
            if (off < strTabSize) {
                size_t pos = strTabOff + off;
                size_t remain = strTabSize - off;
                if (checkedRange(pos, remain, size))
                    sec.name = readBoundedString(data, pos, remain);
            }
        } else {
            size_t len = 0;
            while (len < 8 && sh->Name[len] != '\0')
                ++len;
            sec.name.assign(sh->Name, len);
        }

        sectionCharacteristics[i + 1] = sh->Characteristics;

        sec.executable = (sh->Characteristics & coff::IMAGE_SCN_MEM_EXECUTE) != 0;
        sec.writable = (sh->Characteristics & coff::IMAGE_SCN_MEM_WRITE) != 0;
        sec.tls = sec.name.rfind(".tls", 0) == 0;
        sec.alloc = (sh->Characteristics &
                     (coff::IMAGE_SCN_CNT_CODE | coff::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      coff::IMAGE_SCN_CNT_UNINITIALIZED_DATA)) != 0;

        // Extract alignment from COFF characteristics (bits 20-23).
        const uint32_t alignBits = (sh->Characteristics >> 20) & 0xF;
        sec.alignment = (alignBits > 0) ? (1u << (alignBits - 1)) : 1;

        // COFF uninitialized-data sections must be materialized as zero-filled
        // storage even if SizeOfRawData/PointerToRawData happen to point at
        // bytes in the object file. Some producers leave non-zero metadata in
        // those fields, and copying it verbatim corrupts zero-init globals.
        if (sh->Characteristics & coff::IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
            sec.zeroFill = true;
            const uint32_t zeroSize = sh->VirtualSize != 0 ? sh->VirtualSize : sh->SizeOfRawData;
            sec.data.resize(zeroSize, 0);
        } else if (sh->SizeOfRawData > 0 &&
                   checkedRange(sh->PointerToRawData, sh->SizeOfRawData, size)) {
            sec.data.assign(data + sh->PointerToRawData,
                            data + sh->PointerToRawData + sh->SizeOfRawData);
        }

        uint32_t relocCount = sh->NumberOfRelocations;
        uint32_t firstReloc = 0;
        if ((sh->Characteristics & coff::IMAGE_SCN_LNK_NRELOC_OVFL) != 0 &&
            sh->NumberOfRelocations == 0xFFFF) {
            const auto *overflow = coffAt<coff::CoffReloc>(data, size, sh->PointerToRelocations);
            if (!overflow) {
                err << "error: " << name << ": COFF relocation overflow record is out of bounds\n";
                return false;
            }
            relocCount = overflow->VirtualAddress;
            firstReloc = 1;
        }
        if (relocCount > 0 &&
            !checkedRange(sh->PointerToRelocations,
                          static_cast<size_t>(relocCount + firstReloc) *
                              sizeof(coff::CoffReloc),
                          size)) {
            err << "error: " << name << ": COFF relocation table is out of bounds\n";
            return false;
        }

        // Read relocations.
        for (uint32_t r = firstReloc; r < relocCount + firstReloc; ++r) {
            const auto *cr = coffAt<coff::CoffReloc>(
                data, size, sh->PointerToRelocations + r * sizeof(coff::CoffReloc));

            ObjReloc rel;
            rel.offset = cr->VirtualAddress;
            rel.type = cr->Type;
            if (cr->SymbolTableIndex >= hdr->NumberOfSymbols) {
                err << "error: " << name << ": COFF relocation references invalid symbol index "
                    << cr->SymbolTableIndex << "\n";
                return false;
            }
            rel.symIndex = cr->SymbolTableIndex + 1; // +1 because ObjFile has null sym at 0.
            rel.addend =
                extractCoffAddend(hdr->Machine, static_cast<uint16_t>(rel.type), sec.data, rel.offset);

            sec.relocs.push_back(rel);
        }

        obj.sections.push_back(std::move(sec));
    }

    // Parse symbols.
    if (hdr->NumberOfSymbols > kMaxObjSymbols) {
        err << "error: " << name << ": symbol count " << hdr->NumberOfSymbols << " exceeds limit\n";
        return false;
    }

    std::vector<uint8_t> comdatSelectionBySection(hdr->NumberOfSections + 1, 0);
    std::vector<uint32_t> associativeSectionBySection(hdr->NumberOfSections + 1, 0);
    for (uint32_t i = 0; i < hdr->NumberOfSymbols;) {
        const auto *sym = coffAt<coff::CoffSymbol>(
            data, size, hdr->PointerToSymbolTable + i * sizeof(coff::CoffSymbol));
        if (!sym)
            break;

        const bool hasSectionAux = sym->SectionNumber > 0 &&
                                   static_cast<uint16_t>(sym->SectionNumber) <=
                                       hdr->NumberOfSections &&
                                   sym->StorageClass == coff::IMAGE_SYM_CLASS_STATIC &&
                                   sym->NumberOfAuxSymbols > 0;
        if (hasSectionAux) {
            const uint16_t sectionNumber = static_cast<uint16_t>(sym->SectionNumber);
            const bool isComdat =
                (sectionCharacteristics[sectionNumber] & coff::IMAGE_SCN_LNK_COMDAT) != 0;
            if (isComdat) {
                const auto *aux = coffAt<coff::CoffAuxSectionDefinition>(
                    data,
                    size,
                    hdr->PointerToSymbolTable + (i + 1) * sizeof(coff::CoffSymbol));
                if (aux) {
                    comdatSelectionBySection[sectionNumber] = aux->Selection;
                    if (aux->Selection == coff::IMAGE_COMDAT_SELECT_ASSOCIATIVE &&
                        aux->Number > 0 &&
                        static_cast<uint16_t>(aux->Number) <= hdr->NumberOfSections)
                        associativeSectionBySection[sectionNumber] =
                            static_cast<uint32_t>(aux->Number);
                }
            }
        }

        i += 1 + sym->NumberOfAuxSymbols;
    }
    for (uint32_t secNo = 1; secNo < associativeSectionBySection.size(); ++secNo) {
        if (secNo < obj.sections.size())
            obj.sections[secNo].associativeSection = associativeSectionBySection[secNo];
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

        if (sym->SectionNumber == coff::IMAGE_SYM_UNDEFINED) {
            os.binding = ObjSymbol::Undefined;
            if (sym->StorageClass == coff::IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
                os.weakExternal = true;
                if (sym->NumberOfAuxSymbols > 0) {
                    const auto *aux = coffAt<coff::CoffAuxWeakExternal>(
                        data,
                        size,
                        hdr->PointerToSymbolTable + (i + 1) * sizeof(coff::CoffSymbol));
                    if (aux && aux->TagIndex < hdr->NumberOfSymbols) {
                        const auto *fallback = coffAt<coff::CoffSymbol>(
                            data,
                            size,
                            hdr->PointerToSymbolTable +
                                aux->TagIndex * sizeof(coff::CoffSymbol));
                        if (fallback)
                            os.weakDefaultName = readSymName(fallback);
                    }
                }
            }
        } else if (sym->SectionNumber == coff::IMAGE_SYM_ABSOLUTE ||
                   sym->SectionNumber == coff::IMAGE_SYM_DEBUG) {
            os.binding = ObjSymbol::Local;
            os.absolute = (sym->SectionNumber == coff::IMAGE_SYM_ABSOLUTE);
        } else if (sym->StorageClass == coff::IMAGE_SYM_CLASS_EXTERNAL) {
            os.binding = ObjSymbol::Global;
        } else if (sym->StorageClass == coff::IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
            os.binding = ObjSymbol::Weak;
        } else {
            os.binding = ObjSymbol::Local;
        }

        if (os.binding == ObjSymbol::Global && sym->SectionNumber > 0 &&
            static_cast<uint16_t>(sym->SectionNumber) <= hdr->NumberOfSections &&
            comdatSelectionBySection[static_cast<uint16_t>(sym->SectionNumber)] ==
                coff::IMAGE_COMDAT_SELECT_ANY) {
            os.binding = ObjSymbol::Weak;
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
