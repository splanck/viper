//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ElfExeWriter.cpp
// Purpose: Writes a minimal static ELF executable.
// Key invariants:
//   - ELF header → program headers → .text → .rodata → .data → .bss →
//     section header table
//   - PT_LOAD per segment, PT_GNU_STACK for non-exec stack
//   - Static linking only (no .dynamic, no PT_INTERP)
// Links: codegen/common/linker/ElfExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ElfExeWriter.hpp"

#include <cstring>
#include <fstream>

namespace viper::codegen::linker
{

namespace
{

static constexpr uint16_t ET_EXEC = 2;
static constexpr uint16_t EM_X86_64 = 62;
static constexpr uint16_t EM_AARCH64 = 183;

// Program header types.
static constexpr uint32_t PT_LOAD = 1;
static constexpr uint32_t PT_GNU_STACK = 0x6474E551;

// Segment flags.
static constexpr uint32_t PF_X = 1;
static constexpr uint32_t PF_W = 2;
static constexpr uint32_t PF_R = 4;

// Section header types.
static constexpr uint32_t SHT_NULL = 0;
static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_NOBITS = 8;

static constexpr uint32_t SHF_ALLOC = 0x2;
static constexpr uint32_t SHF_WRITE = 0x1;
static constexpr uint32_t SHF_EXECINSTR = 0x4;

struct Elf64_Ehdr
{
    uint8_t e_ident[16] = {};
    uint16_t e_type = 0;
    uint16_t e_machine = 0;
    uint32_t e_version = 1;
    uint64_t e_entry = 0;
    uint64_t e_phoff = 0;
    uint64_t e_shoff = 0;
    uint32_t e_flags = 0;
    uint16_t e_ehsize = 64;
    uint16_t e_phentsize = 56;
    uint16_t e_phnum = 0;
    uint16_t e_shentsize = 64;
    uint16_t e_shnum = 0;
    uint16_t e_shstrndx = 0;
};

struct Elf64_Phdr
{
    uint32_t p_type = 0;
    uint32_t p_flags = 0;
    uint64_t p_offset = 0;
    uint64_t p_vaddr = 0;
    uint64_t p_paddr = 0;
    uint64_t p_filesz = 0;
    uint64_t p_memsz = 0;
    uint64_t p_align = 0;
};

struct Elf64_Shdr
{
    uint32_t sh_name = 0;
    uint32_t sh_type = 0;
    uint64_t sh_flags = 0;
    uint64_t sh_addr = 0;
    uint64_t sh_offset = 0;
    uint64_t sh_size = 0;
    uint32_t sh_link = 0;
    uint32_t sh_info = 0;
    uint64_t sh_addralign = 1;
    uint64_t sh_entsize = 0;
};

size_t alignUp(size_t val, size_t align)
{
    if (align == 0)
        return val;
    return (val + align - 1) & ~(align - 1);
}

} // anonymous namespace

bool writeElfExe(const std::string &path, const LinkLayout &layout, LinkArch arch,
                 std::ostream &err)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        err << "error: cannot open '" << path << "' for writing\n";
        return false;
    }

    const size_t pageSize = layout.pageSize;
    const uint16_t machine = (arch == LinkArch::AArch64) ? EM_AARCH64 : EM_X86_64;

    // Build program headers and file layout.
    // Layout: ELF header | program headers | [page-aligned segments...] | section headers

    // Determine number of LOAD segments (one per output section with data).
    std::vector<size_t> loadableIndices;
    for (size_t i = 0; i < layout.sections.size(); ++i)
    {
        if (!layout.sections[i].data.empty())
            loadableIndices.push_back(i);
    }

    const uint16_t numPhdrs = static_cast<uint16_t>(loadableIndices.size() + 1); // +1 for GNU_STACK
    const size_t ehdrSize = sizeof(Elf64_Ehdr);
    const size_t phdrTableSize = numPhdrs * sizeof(Elf64_Phdr);

    // Section headers: null + each output section + .shstrtab.
    const uint16_t numShdrs = static_cast<uint16_t>(layout.sections.size() + 2);

    // Compute file offsets for each segment.
    struct SegmentInfo
    {
        size_t layoutIdx;
        size_t fileOffset;
        uint64_t vaddr;
        size_t fileSize;
        size_t memSize;
        uint32_t flags;
    };
    std::vector<SegmentInfo> segments;

    size_t filePos = alignUp(ehdrSize + phdrTableSize, pageSize);

    for (size_t idx : loadableIndices)
    {
        const auto &sec = layout.sections[idx];
        filePos = alignUp(filePos, pageSize);

        uint32_t flags = PF_R;
        if (sec.executable)
            flags |= PF_X;
        if (sec.writable)
            flags |= PF_W;

        segments.push_back({idx, filePos, sec.virtualAddr, sec.data.size(), sec.data.size(), flags});
        filePos += sec.data.size();
    }

    // Build .shstrtab.
    std::string shstrtab;
    shstrtab.push_back('\0'); // Null entry.
    std::vector<uint32_t> secNameOffsets;
    for (const auto &sec : layout.sections)
    {
        secNameOffsets.push_back(static_cast<uint32_t>(shstrtab.size()));
        shstrtab += sec.name;
        shstrtab.push_back('\0');
    }
    const uint32_t shstrtabNameOff = static_cast<uint32_t>(shstrtab.size());
    shstrtab += ".shstrtab";
    shstrtab.push_back('\0');

    // Section headers placed after all segment data.
    const size_t shstrtabOff = alignUp(filePos, 8);
    const size_t shdrsOff = alignUp(shstrtabOff + shstrtab.size(), 8);

    // Build ELF header.
    Elf64_Ehdr ehdr{};
    ehdr.e_ident[0] = 0x7F;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; // ELFCLASS64
    ehdr.e_ident[5] = 1; // ELFDATA2LSB
    ehdr.e_ident[6] = 1; // EV_CURRENT
    ehdr.e_ident[7] = 0; // ELFOSABI_NONE
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = machine;
    ehdr.e_entry = layout.entryAddr;
    ehdr.e_phoff = ehdrSize;
    ehdr.e_shoff = shdrsOff;
    ehdr.e_phnum = numPhdrs;
    ehdr.e_shnum = numShdrs;
    ehdr.e_shstrndx = numShdrs - 1;

    // Write ELF header.
    f.write(reinterpret_cast<const char *>(&ehdr), sizeof(ehdr));

    // Write program headers.
    for (const auto &seg : segments)
    {
        Elf64_Phdr phdr{};
        phdr.p_type = PT_LOAD;
        phdr.p_flags = seg.flags;
        phdr.p_offset = seg.fileOffset;
        phdr.p_vaddr = seg.vaddr;
        phdr.p_paddr = seg.vaddr;
        phdr.p_filesz = seg.fileSize;
        phdr.p_memsz = seg.memSize;
        phdr.p_align = pageSize;
        f.write(reinterpret_cast<const char *>(&phdr), sizeof(phdr));
    }

    // GNU_STACK: non-executable stack.
    {
        Elf64_Phdr phdr{};
        phdr.p_type = PT_GNU_STACK;
        phdr.p_flags = PF_R | PF_W;
        f.write(reinterpret_cast<const char *>(&phdr), sizeof(phdr));
    }

    // Write segment data (page-aligned).
    for (const auto &seg : segments)
    {
        // Pad to file offset.
        auto cur = static_cast<size_t>(f.tellp());
        if (cur < seg.fileOffset)
        {
            std::vector<char> pad(seg.fileOffset - cur, 0);
            f.write(pad.data(), static_cast<std::streamsize>(pad.size()));
        }
        const auto &secData = layout.sections[seg.layoutIdx].data;
        f.write(reinterpret_cast<const char *>(secData.data()),
                static_cast<std::streamsize>(secData.size()));
    }

    // Write .shstrtab.
    {
        auto cur = static_cast<size_t>(f.tellp());
        if (cur < shstrtabOff)
        {
            std::vector<char> pad(shstrtabOff - cur, 0);
            f.write(pad.data(), static_cast<std::streamsize>(pad.size()));
        }
        f.write(shstrtab.data(), static_cast<std::streamsize>(shstrtab.size()));
    }

    // Write section headers.
    {
        auto cur = static_cast<size_t>(f.tellp());
        if (cur < shdrsOff)
        {
            std::vector<char> pad(shdrsOff - cur, 0);
            f.write(pad.data(), static_cast<std::streamsize>(pad.size()));
        }
    }

    // Null section header.
    {
        Elf64_Shdr shdr{};
        f.write(reinterpret_cast<const char *>(&shdr), sizeof(shdr));
    }

    // Section headers for each output section.
    for (size_t i = 0; i < layout.sections.size(); ++i)
    {
        const auto &sec = layout.sections[i];
        Elf64_Shdr shdr{};
        shdr.sh_name = secNameOffsets[i];
        shdr.sh_type = sec.data.empty() ? SHT_NOBITS : SHT_PROGBITS;
        shdr.sh_flags = SHF_ALLOC;
        if (sec.executable)
            shdr.sh_flags |= SHF_EXECINSTR;
        if (sec.writable)
            shdr.sh_flags |= SHF_WRITE;
        shdr.sh_addr = sec.virtualAddr;

        // Find file offset.
        for (const auto &seg : segments)
        {
            if (seg.layoutIdx == i)
            {
                shdr.sh_offset = seg.fileOffset;
                break;
            }
        }
        shdr.sh_size = sec.data.size();
        shdr.sh_addralign = sec.alignment;
        f.write(reinterpret_cast<const char *>(&shdr), sizeof(shdr));
    }

    // .shstrtab section header.
    {
        Elf64_Shdr shdr{};
        shdr.sh_name = shstrtabNameOff;
        shdr.sh_type = SHT_STRTAB;
        shdr.sh_offset = shstrtabOff;
        shdr.sh_size = shstrtab.size();
        shdr.sh_addralign = 1;
        f.write(reinterpret_cast<const char *>(&shdr), sizeof(shdr));
    }

    if (!f)
    {
        err << "error: write failed to '" << path << "'\n";
        return false;
    }

    // Make executable on Unix.
#if !defined(_WIN32)
    std::error_code ec;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write | std::filesystem::perms::group_read |
                                     std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                     std::filesystem::perms::others_exec,
                                 ec);
#endif

    return true;
}

} // namespace viper::codegen::linker
