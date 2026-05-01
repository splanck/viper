//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ElfReader.cpp
// Purpose: ELF 64-bit relocatable object file reader.
// Key invariants:
//   - Handles both x86_64 (EM_X86_64=62) and AArch64 (EM_AARCH64=183)
//   - Uses explicit addend from .rela sections (not .rel)
//   - Section name from .shstrtab, symbol names from .strtab
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocConstants.hpp"

#include <cstring>
#include <limits>

namespace viper::codegen::linker {

// ELF64 structures — defined inline to avoid system header dependencies.
namespace elf {
static constexpr uint16_t ET_REL = 1;
static constexpr uint16_t EM_X86_64 = 62;
static constexpr uint16_t EM_AARCH64 = 183;

static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_RELA = 4;
static constexpr uint32_t SHT_REL = 9;
static constexpr uint32_t SHT_NOBITS = 8;

static constexpr uint32_t SHF_WRITE = 0x1;
static constexpr uint32_t SHF_ALLOC = 0x2;
static constexpr uint32_t SHF_EXECINSTR = 0x4;
static constexpr uint32_t SHF_TLS = 0x400;

static constexpr uint8_t STB_LOCAL = 0;
static constexpr uint8_t STB_GLOBAL = 1;
static constexpr uint8_t STB_WEAK = 2;

static constexpr uint16_t SHN_UNDEF = 0;
static constexpr uint16_t SHN_XINDEX = 0xFFFF;
static constexpr uint16_t SHN_ABS = 0xFFF1;
static constexpr uint16_t SHN_COMMON = 0xFFF2;

struct Elf64_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};

struct Elf64_Rel {
    uint64_t r_offset;
    uint64_t r_info;
};

} // namespace elf

template <typename T> static const T *readStruct(const uint8_t *data, size_t size, size_t offset) {
    if (offset > size || sizeof(T) > size - offset)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

static bool checkedRange(size_t off, size_t len, size_t size) {
    return off <= size && len <= size - off;
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t readLE64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

static int64_t signExtend(uint64_t value, unsigned bits) {
    const uint64_t signBit = uint64_t{1} << (bits - 1);
    const uint64_t mask = (uint64_t{1} << bits) - 1;
    value &= mask;
    return static_cast<int64_t>((value ^ signBit) - signBit);
}

static int64_t extractRelAddend(uint16_t machine,
                                uint32_t relocType,
                                const std::vector<uint8_t> &sectionData,
                                size_t offset) {
    if (machine == elf::EM_X86_64) {
        if (relocType == elf_x64::kAbs64 && checkedRange(offset, 8, sectionData.size()))
            return static_cast<int64_t>(readLE64(sectionData.data() + offset));
        if (checkedRange(offset, 4, sectionData.size())) {
            int32_t val = 0;
            std::memcpy(&val, sectionData.data() + offset, 4);
            return val;
        }
        return 0;
    }

    if (machine != elf::EM_AARCH64 || !checkedRange(offset, 4, sectionData.size()))
        return 0;

    if (relocType == elf_a64::kAbs64) {
        if (checkedRange(offset, 8, sectionData.size()))
            return static_cast<int64_t>(readLE64(sectionData.data() + offset));
        return 0;
    }

    const uint32_t insn = readLE32(sectionData.data() + offset);
    switch (relocType) {
        case elf_a64::kCall26:
        case elf_a64::kJump26:
            return signExtend(insn & 0x03FFFFFFu, 26) << 2;
        case elf_a64::kCondBr19:
            return signExtend((insn >> 5) & 0x7FFFFu, 19) << 2;
        case elf_a64::kAdrPrelPgHi21: {
            const uint32_t immlo = (insn >> 29) & 0x3u;
            const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
            return signExtend((immhi << 2) | immlo, 21) << 12;
        }
        case elf_a64::kAddAbsLo12Nc:
            return static_cast<int64_t>((insn >> 10) & 0xFFFu);
        case elf_a64::kLdSt32Lo12Nc:
            return static_cast<int64_t>(((insn >> 10) & 0xFFFu) << 2);
        case elf_a64::kLdSt64Lo12Nc:
            return static_cast<int64_t>(((insn >> 10) & 0xFFFu) << 3);
        case elf_a64::kLdSt128Lo12Nc:
            return static_cast<int64_t>(((insn >> 10) & 0xFFFu) << 4);
        default:
            return 0;
    }
}

static std::string readString(
    const uint8_t *data, size_t size, size_t strTabOff, size_t strTabSize, uint32_t nameOff) {
    if (!checkedRange(strTabOff, strTabSize, size) || nameOff >= strTabSize)
        return "";
    size_t pos = strTabOff + nameOff;
    if (pos < strTabOff || pos >= strTabOff + strTabSize)
        return "";
    const uint8_t *begin = data + pos;
    const uint8_t *end = data + strTabOff + strTabSize;
    const void *nul = std::memchr(begin, '\0', static_cast<size_t>(end - begin));
    if (!nul)
        return "";
    return std::string(reinterpret_cast<const char *>(begin),
                       static_cast<const char *>(nul));
}

bool readElfObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err) {
    const auto *ehdr = readStruct<elf::Elf64_Ehdr>(data, size, 0);
    if (!ehdr) {
        err << "error: " << name << ": truncated ELF header\n";
        return false;
    }

    obj.format = ObjFileFormat::ELF;
    obj.is64bit = true;
    obj.isLittleEndian = (ehdr->e_ident[5] == 1);
    obj.machine = ehdr->e_machine;
    obj.name = name;
    obj.symbols.assign(1, ObjSymbol{});

    if (ehdr->e_ident[4] != 2 || ehdr->e_ident[5] != 1 || ehdr->e_ident[6] != 1 ||
        ehdr->e_type != 1 || ehdr->e_shentsize != sizeof(elf::Elf64_Shdr)) {
        err << "error: " << name << ": unsupported ELF object format\n";
        return false;
    }

    const auto *sh0 =
        readStruct<elf::Elf64_Shdr>(data, size, static_cast<size_t>(ehdr->e_shoff));
    if (!sh0) {
        err << "error: " << name << ": missing ELF section header 0\n";
        return false;
    }

    size_t shnum = ehdr->e_shnum;
    if (shnum == 0)
        shnum = static_cast<size_t>(sh0->sh_size);
    size_t shstrndx = ehdr->e_shstrndx;
    if (shstrndx == elf::SHN_XINDEX)
        shstrndx = sh0->sh_link;
    if (shnum == 0)
        return true;
    if (shnum > kMaxObjSections) {
        err << "error: " << name << ": section count " << shnum << " exceeds limit\n";
        return false;
    }

    // Read section headers.
    std::vector<const elf::Elf64_Shdr *> shdrs(shnum);
    if (!checkedRange(static_cast<size_t>(ehdr->e_shoff),
                      static_cast<size_t>(shnum) * ehdr->e_shentsize,
                      size)) {
        err << "error: " << name << ": section header table is out of bounds\n";
        return false;
    }
    for (size_t i = 0; i < shnum; ++i) {
        shdrs[i] = readStruct<elf::Elf64_Shdr>(data, size, ehdr->e_shoff + i * ehdr->e_shentsize);
        if (!shdrs[i]) {
            err << "error: " << name << ": truncated section header " << i << "\n";
            return false;
        }
    }

    // Locate .shstrtab for section names.
    size_t shstrOff = 0, shstrSize = 0;
    if (shstrndx < shnum) {
        shstrOff = static_cast<size_t>(shdrs[shstrndx]->sh_offset);
        shstrSize = static_cast<size_t>(shdrs[shstrndx]->sh_size);
    }

    // Build sections (index 0 = null).
    // Map from ELF section index → ObjFile section index.
    std::vector<uint32_t> secMap(shnum, 0);
    obj.sections.resize(1); // Null section at index 0.
    obj.sections[0].name = "";

    for (size_t i = 1; i < shnum; ++i) {
        const auto *sh = shdrs[i];
        if (sh->sh_type == elf::SHT_SYMTAB || sh->sh_type == elf::SHT_STRTAB ||
            sh->sh_type == elf::SHT_RELA || sh->sh_type == elf::SHT_REL)
            continue;

        ObjSection sec;
        sec.name = readString(data, size, shstrOff, shstrSize, sh->sh_name);
        sec.alignment = static_cast<uint32_t>(sh->sh_addralign);
        sec.executable = (sh->sh_flags & elf::SHF_EXECINSTR) != 0;
        sec.writable = (sh->sh_flags & elf::SHF_WRITE) != 0;
        sec.alloc = (sh->sh_flags & elf::SHF_ALLOC) != 0;
        sec.tls = (sh->sh_flags & elf::SHF_TLS) != 0;

        // ELF sections with SHF_STRINGS + SHF_MERGE contain NUL-terminated
        // string literals suitable for cross-module deduplication.
        // Also recognize by name: .rodata.str* sections are GCC/Clang-generated
        // mergeable string sections even when flag inspection is impractical.
        constexpr uint32_t SHF_MERGE = 0x10;
        constexpr uint32_t SHF_STRINGS = 0x20;
        sec.isCStringSection =
            ((sh->sh_flags & SHF_MERGE) != 0 && (sh->sh_flags & SHF_STRINGS) != 0) ||
            sec.name.find(".str") != std::string::npos;

        if (sh->sh_type == elf::SHT_NOBITS) {
            sec.zeroFill = true;
            sec.data.resize(static_cast<size_t>(sh->sh_size), 0);
        } else if (sh->sh_size > 0 &&
                   checkedRange(static_cast<size_t>(sh->sh_offset),
                                static_cast<size_t>(sh->sh_size),
                                size)) {
            auto off = static_cast<size_t>(sh->sh_offset);
            auto sz = static_cast<size_t>(sh->sh_size);
            sec.data.assign(data + off, data + off + sz);
        }

        secMap[i] = static_cast<uint32_t>(obj.sections.size());
        obj.sections.push_back(std::move(sec));
    }

    // Find symbol table.
    const elf::Elf64_Shdr *symSh = nullptr;
    for (size_t i = 1; i < shnum; ++i) {
        if (shdrs[i]->sh_type == elf::SHT_SYMTAB) {
            symSh = shdrs[i];
            break;
        }
    }

    // Locate string table for symbols.
    size_t strOff = 0, strSize = 0;
    if (symSh && symSh->sh_link < shnum) {
        strOff = static_cast<size_t>(shdrs[symSh->sh_link]->sh_offset);
        strSize = static_cast<size_t>(shdrs[symSh->sh_link]->sh_size);
    }

    // Read symbols.
    std::vector<uint32_t> symMap; // ELF sym index → ObjFile sym index.
    if (symSh) {
        if (!checkedRange(static_cast<size_t>(symSh->sh_offset),
                          static_cast<size_t>(symSh->sh_size),
                          size)) {
            err << "error: " << name << ": symbol table is out of bounds\n";
            return false;
        }
        const uint32_t symCount = static_cast<uint32_t>(symSh->sh_size / sizeof(elf::Elf64_Sym));
        if (symCount > kMaxObjSymbols) {
            err << "error: " << name << ": symbol count " << symCount << " exceeds limit\n";
            return false;
        }
        symMap.resize(symCount, 0);

        uint32_t commonSecIdx = 0;
        auto allocateCommon = [&](size_t sizeBytes, size_t alignment) -> size_t {
            if (commonSecIdx == 0) {
                ObjSection common;
                common.name = ".common";
                common.writable = true;
                common.alloc = true;
                common.zeroFill = true;
                common.alignment = 1;
                commonSecIdx = static_cast<uint32_t>(obj.sections.size());
                obj.sections.push_back(std::move(common));
            }
            auto &common = obj.sections[commonSecIdx];
            if (alignment == 0)
                alignment = 1;
            if (alignment > common.alignment)
                common.alignment = static_cast<uint32_t>(alignment);
            const size_t rem = common.data.size() % alignment;
            if (rem != 0)
                common.data.resize(common.data.size() + (alignment - rem), 0);
            const size_t off = common.data.size();
            common.data.resize(off + sizeBytes, 0);
            return off;
        };

        for (uint32_t i = 1; i < symCount; ++i) {
            const auto *sym = readStruct<elf::Elf64_Sym>(
                data, size, static_cast<size_t>(symSh->sh_offset) + i * sizeof(elf::Elf64_Sym));
            if (!sym)
                break;

            ObjSymbol os;
            os.name = readString(data, size, strOff, strSize, sym->st_name);

            const uint8_t bind = sym->st_info >> 4;
            if (sym->st_shndx == elf::SHN_UNDEF)
                os.binding = (bind == elf::STB_WEAK) ? ObjSymbol::Weak : ObjSymbol::Undefined;
            else if (bind == elf::STB_LOCAL)
                os.binding = ObjSymbol::Local;
            else if (bind == elf::STB_WEAK)
                os.binding = ObjSymbol::Weak;
            else
                os.binding = ObjSymbol::Global;

            if (sym->st_shndx == elf::SHN_ABS) {
                os.absolute = true;
            } else if (sym->st_shndx == elf::SHN_COMMON) {
                os.offset = allocateCommon(static_cast<size_t>(sym->st_size),
                                           static_cast<size_t>(sym->st_value));
                os.sectionIndex = commonSecIdx;
            } else if (sym->st_shndx < shnum && sym->st_shndx != elf::SHN_UNDEF) {
                os.sectionIndex = secMap[sym->st_shndx];
            }

            if (sym->st_shndx != elf::SHN_COMMON)
                os.offset = static_cast<size_t>(sym->st_value);
            os.size = static_cast<size_t>(sym->st_size);

            symMap[i] = static_cast<uint32_t>(obj.symbols.size());
            obj.symbols.push_back(std::move(os));
        }
    }

    // Read relocations from .rela/.rel sections.
    for (size_t i = 1; i < shnum; ++i) {
        if (shdrs[i]->sh_type != elf::SHT_RELA && shdrs[i]->sh_type != elf::SHT_REL)
            continue;

        // sh_info points to the section these relocs apply to.
        const uint32_t targetSecElf = shdrs[i]->sh_info;
        if (targetSecElf >= shnum || secMap[targetSecElf] == 0)
            continue;

        auto &targetSec = obj.sections[secMap[targetSecElf]];
        if (!checkedRange(static_cast<size_t>(shdrs[i]->sh_offset),
                          static_cast<size_t>(shdrs[i]->sh_size),
                          size)) {
            err << "error: " << name << ": relocation table is out of bounds\n";
            return false;
        }
        const bool isRela = shdrs[i]->sh_type == elf::SHT_RELA;
        const size_t relEntSize = isRela ? sizeof(elf::Elf64_Rela) : sizeof(elf::Elf64_Rel);
        if (shdrs[i]->sh_entsize != 0 && shdrs[i]->sh_entsize != relEntSize) {
            err << "error: " << name << ": unsupported ELF relocation entry size\n";
            return false;
        }
        const uint32_t relCount = static_cast<uint32_t>(shdrs[i]->sh_size / relEntSize);

        for (uint32_t r = 0; r < relCount; ++r) {
            ObjReloc rel;
            uint64_t rInfo = 0;
            if (isRela) {
                const auto *rela = readStruct<elf::Elf64_Rela>(
                    data,
                    size,
                    static_cast<size_t>(shdrs[i]->sh_offset) + r * sizeof(elf::Elf64_Rela));
                if (!rela)
                    break;
                rel.offset = static_cast<size_t>(rela->r_offset);
                rInfo = rela->r_info;
                rel.addend = rela->r_addend;
            } else {
                const auto *relNoAddend = readStruct<elf::Elf64_Rel>(
                    data,
                    size,
                    static_cast<size_t>(shdrs[i]->sh_offset) + r * sizeof(elf::Elf64_Rel));
                if (!relNoAddend)
                    break;
                rel.offset = static_cast<size_t>(relNoAddend->r_offset);
                rInfo = relNoAddend->r_info;
            }

            rel.type = static_cast<uint32_t>(rInfo & 0xFFFFFFFF);
            const uint32_t elfSymIdx = static_cast<uint32_t>(rInfo >> 32);
            if (elfSymIdx >= symMap.size()) {
                err << "error: " << name << ": relocation references invalid symbol index "
                    << elfSymIdx << "\n";
                return false;
            }
            rel.symIndex = symMap[elfSymIdx];
            if (!isRela)
                rel.addend = extractRelAddend(ehdr->e_machine, rel.type, targetSec.data, rel.offset);

            targetSec.relocs.push_back(rel);
        }
    }

    return true;
}

} // namespace viper::codegen::linker
