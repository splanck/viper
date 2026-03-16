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

#include <cstring>

namespace viper::codegen::linker
{

// ELF64 structures — defined inline to avoid system header dependencies.
namespace elf
{
static constexpr uint16_t ET_REL = 1;
static constexpr uint16_t EM_X86_64 = 62;
static constexpr uint16_t EM_AARCH64 = 183;

static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_RELA = 4;
static constexpr uint32_t SHT_NOBITS = 8;

static constexpr uint32_t SHF_WRITE = 0x1;
static constexpr uint32_t SHF_ALLOC = 0x2;
static constexpr uint32_t SHF_EXECINSTR = 0x4;
static constexpr uint32_t SHF_TLS = 0x400;

static constexpr uint8_t STB_LOCAL = 0;
static constexpr uint8_t STB_GLOBAL = 1;
static constexpr uint8_t STB_WEAK = 2;

static constexpr uint16_t SHN_UNDEF = 0;
static constexpr uint16_t SHN_ABS = 0xFFF1;
static constexpr uint16_t SHN_COMMON = 0xFFF2;

struct Elf64_Ehdr
{
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

struct Elf64_Shdr
{
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

struct Elf64_Sym
{
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64_Rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};

} // namespace elf

template <typename T> static const T *readStruct(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + sizeof(T) > size)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

static const char *readString(
    const uint8_t *data, size_t size, size_t strTabOff, size_t strTabSize, uint32_t nameOff)
{
    size_t pos = strTabOff + nameOff;
    if (pos >= size)
        return "";
    return reinterpret_cast<const char *>(data + pos);
}

bool readElfObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err)
{
    const auto *ehdr = readStruct<elf::Elf64_Ehdr>(data, size, 0);
    if (!ehdr)
    {
        err << "error: " << name << ": truncated ELF header\n";
        return false;
    }

    obj.format = ObjFileFormat::ELF;
    obj.is64bit = true;
    obj.isLittleEndian = (ehdr->e_ident[5] == 1);
    obj.machine = ehdr->e_machine;
    obj.name = name;

    const uint16_t shnum = ehdr->e_shnum;
    const uint16_t shstrndx = ehdr->e_shstrndx;

    if (shnum == 0)
        return true;

    // Read section headers.
    std::vector<const elf::Elf64_Shdr *> shdrs(shnum);
    for (uint16_t i = 0; i < shnum; ++i)
    {
        shdrs[i] = readStruct<elf::Elf64_Shdr>(data, size, ehdr->e_shoff + i * ehdr->e_shentsize);
        if (!shdrs[i])
        {
            err << "error: " << name << ": truncated section header " << i << "\n";
            return false;
        }
    }

    // Locate .shstrtab for section names.
    size_t shstrOff = 0, shstrSize = 0;
    if (shstrndx < shnum)
    {
        shstrOff = static_cast<size_t>(shdrs[shstrndx]->sh_offset);
        shstrSize = static_cast<size_t>(shdrs[shstrndx]->sh_size);
    }

    // Build sections (index 0 = null).
    // Map from ELF section index → ObjFile section index.
    std::vector<uint32_t> secMap(shnum, 0);
    obj.sections.resize(1); // Null section at index 0.
    obj.sections[0].name = "";

    for (uint16_t i = 1; i < shnum; ++i)
    {
        const auto *sh = shdrs[i];
        if (sh->sh_type == elf::SHT_SYMTAB || sh->sh_type == elf::SHT_STRTAB ||
            sh->sh_type == elf::SHT_RELA)
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

        if (sh->sh_type == elf::SHT_NOBITS)
        {
            sec.data.resize(static_cast<size_t>(sh->sh_size), 0);
        }
        else if (sh->sh_size > 0 && sh->sh_offset + sh->sh_size <= size)
        {
            auto off = static_cast<size_t>(sh->sh_offset);
            auto sz = static_cast<size_t>(sh->sh_size);
            sec.data.assign(data + off, data + off + sz);
        }

        secMap[i] = static_cast<uint32_t>(obj.sections.size());
        obj.sections.push_back(std::move(sec));
    }

    // Find symbol table.
    const elf::Elf64_Shdr *symSh = nullptr;
    for (uint16_t i = 1; i < shnum; ++i)
    {
        if (shdrs[i]->sh_type == elf::SHT_SYMTAB)
        {
            symSh = shdrs[i];
            break;
        }
    }

    // Locate string table for symbols.
    size_t strOff = 0, strSize = 0;
    if (symSh && symSh->sh_link < shnum)
    {
        strOff = static_cast<size_t>(shdrs[symSh->sh_link]->sh_offset);
        strSize = static_cast<size_t>(shdrs[symSh->sh_link]->sh_size);
    }

    // Read symbols.
    std::vector<uint32_t> symMap; // ELF sym index → ObjFile sym index.
    if (symSh)
    {
        const uint32_t symCount = static_cast<uint32_t>(symSh->sh_size / sizeof(elf::Elf64_Sym));
        if (symCount > kMaxObjSymbols)
        {
            err << "error: " << name << ": symbol count " << symCount << " exceeds limit\n";
            return false;
        }
        symMap.resize(symCount, 0);

        obj.symbols.resize(1); // Null symbol at index 0.
        obj.symbols[0] = ObjSymbol{};

        for (uint32_t i = 1; i < symCount; ++i)
        {
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

            if (sym->st_shndx < shnum && sym->st_shndx != elf::SHN_UNDEF &&
                sym->st_shndx != elf::SHN_ABS && sym->st_shndx != elf::SHN_COMMON)
                os.sectionIndex = secMap[sym->st_shndx];

            os.offset = static_cast<size_t>(sym->st_value);
            os.size = static_cast<size_t>(sym->st_size);

            symMap[i] = static_cast<uint32_t>(obj.symbols.size());
            obj.symbols.push_back(std::move(os));
        }
    }

    // Read relocations from .rela sections.
    for (uint16_t i = 1; i < shnum; ++i)
    {
        if (shdrs[i]->sh_type != elf::SHT_RELA)
            continue;

        // sh_info points to the section these relocs apply to.
        const uint32_t targetSecElf = shdrs[i]->sh_info;
        if (targetSecElf >= shnum || secMap[targetSecElf] == 0)
            continue;

        auto &targetSec = obj.sections[secMap[targetSecElf]];
        const uint32_t relCount =
            static_cast<uint32_t>(shdrs[i]->sh_size / sizeof(elf::Elf64_Rela));

        for (uint32_t r = 0; r < relCount; ++r)
        {
            const auto *rela = readStruct<elf::Elf64_Rela>(
                data, size, static_cast<size_t>(shdrs[i]->sh_offset) + r * sizeof(elf::Elf64_Rela));
            if (!rela)
                break;

            ObjReloc rel;
            rel.offset = static_cast<size_t>(rela->r_offset);
            rel.type = static_cast<uint32_t>(rela->r_info & 0xFFFFFFFF);
            const uint32_t elfSymIdx = static_cast<uint32_t>(rela->r_info >> 32);
            rel.symIndex = (elfSymIdx < symMap.size()) ? symMap[elfSymIdx] : 0;
            rel.addend = rela->r_addend;

            targetSec.relocs.push_back(rel);
        }
    }

    return true;
}

} // namespace viper::codegen::linker
