//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ElfExeWriter.cpp
// Purpose: Writes ELF executables with native static and dynamic-link support.
// Key invariants:
//   - ET_EXEC with fixed image base and page-aligned PT_LOAD segments
//   - PT_GNU_STACK is always emitted as non-executable
//   - Shared-library imports are emitted via PT_INTERP/PT_DYNAMIC plus
//     loader-resolved GOT/data relocations instead of the system linker
// Links: codegen/common/linker/ElfExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ElfExeWriter.hpp"

#include "codegen/common/linker/AlignUtil.hpp"
#include "codegen/common/linker/ExeWriterUtil.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace viper::codegen::linker {

namespace {

static constexpr uint16_t ET_EXEC = 2;
static constexpr uint16_t EM_X86_64 = 62;
static constexpr uint16_t EM_AARCH64 = 183;

// Program header types.
static constexpr uint32_t PT_LOAD = 1;
static constexpr uint32_t PT_DYNAMIC = 2;
static constexpr uint32_t PT_INTERP = 3;
static constexpr uint32_t PT_GNU_STACK = 0x6474E551;

// Segment flags.
static constexpr uint32_t PF_X = 1;
static constexpr uint32_t PF_W = 2;
static constexpr uint32_t PF_R = 4;

// Section header types.
static constexpr uint32_t SHT_NULL = 0;
static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_RELA = 4;
static constexpr uint32_t SHT_HASH = 5;
static constexpr uint32_t SHT_DYNAMIC = 6;
static constexpr uint32_t SHT_NOBITS = 8;
static constexpr uint32_t SHT_DYNSYM = 11;

static constexpr uint32_t SHF_WRITE = 0x1;
static constexpr uint32_t SHF_ALLOC = 0x2;
static constexpr uint32_t SHF_EXECINSTR = 0x4;

// Dynamic tags.
static constexpr int64_t DT_NULL = 0;
static constexpr int64_t DT_NEEDED = 1;
static constexpr int64_t DT_HASH = 4;
static constexpr int64_t DT_STRTAB = 5;
static constexpr int64_t DT_SYMTAB = 6;
static constexpr int64_t DT_RELA = 7;
static constexpr int64_t DT_RELASZ = 8;
static constexpr int64_t DT_RELAENT = 9;
static constexpr int64_t DT_STRSZ = 10;
static constexpr int64_t DT_SYMENT = 11;
static constexpr int64_t DT_TEXTREL = 22;

// Dynamic relocation types.
static constexpr uint32_t R_X86_64_64 = 1;
static constexpr uint32_t R_X86_64_GLOB_DAT = 6;

static constexpr const char *kLinuxX8664Interpreter = "/lib64/ld-linux-x86-64.so.2";

struct Elf64_Ehdr {
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

struct Elf64_Phdr {
    uint32_t p_type = 0;
    uint32_t p_flags = 0;
    uint64_t p_offset = 0;
    uint64_t p_vaddr = 0;
    uint64_t p_paddr = 0;
    uint64_t p_filesz = 0;
    uint64_t p_memsz = 0;
    uint64_t p_align = 0;
};

struct Elf64_Shdr {
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

struct Elf64_Sym {
    uint32_t st_name = 0;
    uint8_t st_info = 0;
    uint8_t st_other = 0;
    uint16_t st_shndx = 0;
    uint64_t st_value = 0;
    uint64_t st_size = 0;
};

struct Elf64_Rela {
    uint64_t r_offset = 0;
    uint64_t r_info = 0;
    int64_t r_addend = 0;
};

struct Elf64_Dyn {
    int64_t d_tag = 0;
    uint64_t d_val = 0;
};

struct SegmentInfo {
    size_t layoutIdx = 0;
    size_t fileOffset = 0;
    uint64_t vaddr = 0;
    size_t fileSize = 0;
    size_t memSize = 0;
    uint32_t flags = 0;
};

struct NonAllocInfo {
    size_t layoutIdx = 0;
    size_t fileOffset = 0;
};

struct DynamicInfo {
    bool enabled = false;
    bool hasTextRel = false;
    std::vector<std::string> neededLibs;
    std::vector<std::string> dynSymbols;
    std::unordered_map<std::string, uint32_t> dynSymIndex;
    std::unordered_map<std::string, uint32_t> dynStrOff;
    std::unordered_map<std::string, uint32_t> neededNameOff;

    std::vector<uint8_t> interp;
    std::vector<uint8_t> dynstr;
    std::vector<uint8_t> dynsym;
    std::vector<uint8_t> hash;
    std::vector<uint8_t> rela;
    std::vector<uint8_t> roBlob;
    std::vector<uint8_t> dynamic;

    size_t interpOff = 0;
    size_t dynstrOff = 0;
    size_t dynsymOff = 0;
    size_t hashOff = 0;
    size_t relaOff = 0;

    size_t roFileOff = 0;
    size_t rwFileOff = 0;
    uint64_t roVaddr = 0;
    uint64_t rwVaddr = 0;
};

struct SyntheticSectionRef {
    const char *name = nullptr;
    uint32_t type = 0;
    uint64_t flags = 0;
    uint64_t addr = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t link = 0;
    uint32_t info = 0;
    uint64_t addralign = 1;
    uint64_t entsize = 0;
};

template <typename T> void writeStruct(std::vector<uint8_t> &buf, size_t off, const T &value) {
    if (off + sizeof(T) > buf.size())
        buf.resize(off + sizeof(T), 0);
    std::memcpy(buf.data() + off, &value, sizeof(T));
}

template <typename T>
void appendStruct(std::vector<uint8_t> &buf, const T &value, uint64_t align = alignof(T)) {
    buf.resize(alignUp(buf.size(), align), 0);
    const size_t off = buf.size();
    buf.resize(off + sizeof(T), 0);
    std::memcpy(buf.data() + off, &value, sizeof(T));
}

void appendBytes(std::vector<uint8_t> &buf,
                 const std::vector<uint8_t> &src,
                 size_t &outOff,
                 uint64_t align) {
    buf.resize(alignUp(buf.size(), align), 0);
    outOff = buf.size();
    buf.insert(buf.end(), src.begin(), src.end());
}

uint32_t addString(std::vector<uint8_t> &strtab, const std::string &s) {
    const uint32_t off = static_cast<uint32_t>(strtab.size());
    strtab.insert(strtab.end(), s.begin(), s.end());
    strtab.push_back(0);
    return off;
}

uint32_t dynInfoForSym(uint32_t symIndex, uint32_t type) {
    return (static_cast<uint64_t>(symIndex) << 32) | type;
}

uint32_t elfHash(std::string_view name) {
    uint32_t h = 0;
    for (unsigned char c : name) {
        h = (h << 4) + c;
        const uint32_t g = h & 0xF0000000U;
        if (g != 0)
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

uint64_t maxAllocEndAddr(const LinkLayout &layout) {
    uint64_t maxEnd = 0;
    for (const auto &sec : layout.sections) {
        if (!sec.alloc || sec.data.empty())
            continue;
        maxEnd = std::max(maxEnd, sec.virtualAddr + static_cast<uint64_t>(sec.data.size()));
    }
    return maxEnd;
}

bool buildDynamicInfo(const LinkLayout &layout,
                      LinkArch arch,
                      const std::vector<std::string> &neededLibs,
                      const std::unordered_set<std::string> &dynSyms,
                      size_t pageSize,
                      DynamicInfo &info,
                      std::ostream &err) {
    if (dynSyms.empty())
        return true;

    if (arch != LinkArch::X86_64) {
        err << "error: ELF dynamic imports are only implemented for Linux x86_64\n";
        return false;
    }
    if (neededLibs.empty()) {
        err << "error: ELF dynamic imports require at least one DT_NEEDED library\n";
        return false;
    }

    info.enabled = true;
    info.neededLibs = neededLibs;
    info.dynSymbols.assign(dynSyms.begin(), dynSyms.end());
    std::sort(info.dynSymbols.begin(), info.dynSymbols.end());

    info.interp.assign(kLinuxX8664Interpreter,
                       kLinuxX8664Interpreter + std::strlen(kLinuxX8664Interpreter) + 1);

    info.dynstr.push_back(0);
    for (const auto &lib : info.neededLibs)
        info.neededNameOff[lib] = addString(info.dynstr, lib);
    for (const auto &sym : info.dynSymbols)
        info.dynStrOff[sym] = addString(info.dynstr, sym);

    appendStruct(info.dynsym, Elf64_Sym{}, 8);
    for (size_t i = 0; i < info.dynSymbols.size(); ++i) {
        Elf64_Sym sym{};
        sym.st_name = info.dynStrOff[info.dynSymbols[i]];
        sym.st_info = (1u << 4); // STB_GLOBAL | STT_NOTYPE
        appendStruct(info.dynsym, sym, 8);
        info.dynSymIndex[info.dynSymbols[i]] = static_cast<uint32_t>(i + 1);
    }

    const uint32_t dynSymCount = static_cast<uint32_t>(info.dynSymbols.size() + 1);
    const uint32_t bucketCount = std::max<uint32_t>(1, dynSymCount - 1);
    std::vector<uint32_t> buckets(bucketCount, 0);
    std::vector<uint32_t> chains(dynSymCount, 0);
    for (uint32_t i = 1; i < dynSymCount; ++i) {
        const uint32_t hash = elfHash(info.dynSymbols[i - 1]);
        const uint32_t bucket = hash % bucketCount;
        if (buckets[bucket] == 0) {
            buckets[bucket] = i;
            continue;
        }
        uint32_t chain = buckets[bucket];
        while (chains[chain] != 0)
            chain = chains[chain];
        chains[chain] = i;
    }
    encoding::writeLE32(info.hash, bucketCount);
    encoding::writeLE32(info.hash, dynSymCount);
    for (uint32_t bucket : buckets)
        encoding::writeLE32(info.hash, bucket);
    for (uint32_t chain : chains)
        encoding::writeLE32(info.hash, chain);

    auto emitRela = [&](uint64_t offset, uint32_t symIndex, uint32_t type) {
        Elf64_Rela rela{};
        rela.r_offset = offset;
        rela.r_info = dynInfoForSym(symIndex, type);
        appendStruct(info.rela, rela, 8);
    };

    for (const auto &got : layout.gotEntries) {
        auto it = info.dynSymIndex.find(got.symbolName);
        if (it == info.dynSymIndex.end()) {
            err << "error: missing .dynsym entry for GOT symbol '" << got.symbolName << "'\n";
            return false;
        }
        emitRela(got.gotAddr, it->second, R_X86_64_GLOB_DAT);
    }

    std::vector<BindEntry> bindEntries = layout.bindEntries;
    std::sort(bindEntries.begin(), bindEntries.end(), [](const BindEntry &a, const BindEntry &b) {
        if (a.sectionIndex != b.sectionIndex)
            return a.sectionIndex < b.sectionIndex;
        if (a.offset != b.offset)
            return a.offset < b.offset;
        return a.symbolName < b.symbolName;
    });

    for (const auto &bind : bindEntries) {
        if (bind.sectionIndex >= layout.sections.size()) {
            err << "error: dynamic bind entry references invalid section index " << bind.sectionIndex
                << "\n";
            return false;
        }
        auto it = info.dynSymIndex.find(bind.symbolName);
        if (it == info.dynSymIndex.end()) {
            err << "error: missing .dynsym entry for bind symbol '" << bind.symbolName << "'\n";
            return false;
        }
        const auto &sec = layout.sections[bind.sectionIndex];
        if (!sec.writable)
            info.hasTextRel = true;
        emitRela(sec.virtualAddr + bind.offset, it->second, R_X86_64_64);
    }

    appendBytes(info.roBlob, info.interp, info.interpOff, 1);
    appendBytes(info.roBlob, info.dynstr, info.dynstrOff, 1);
    appendBytes(info.roBlob, info.dynsym, info.dynsymOff, 8);
    appendBytes(info.roBlob, info.hash, info.hashOff, 8);
    appendBytes(info.roBlob, info.rela, info.relaOff, 8);

    const uint64_t roBase = alignUp(maxAllocEndAddr(layout), pageSize);
    info.roVaddr = roBase;
    info.rwVaddr = alignUp(roBase + info.roBlob.size(), pageSize);

    auto dynSectionVA = [&](size_t off) { return info.roVaddr + off; };

    std::vector<Elf64_Dyn> entries;
    entries.reserve(info.neededLibs.size() + 8);
    for (const auto &lib : info.neededLibs)
        entries.push_back({DT_NEEDED, info.neededNameOff[lib]});
    entries.push_back({DT_HASH, dynSectionVA(info.hashOff)});
    entries.push_back({DT_STRTAB, dynSectionVA(info.dynstrOff)});
    entries.push_back({DT_SYMTAB, dynSectionVA(info.dynsymOff)});
    entries.push_back({DT_RELA, dynSectionVA(info.relaOff)});
    entries.push_back({DT_RELASZ, info.rela.size()});
    entries.push_back({DT_RELAENT, sizeof(Elf64_Rela)});
    entries.push_back({DT_STRSZ, info.dynstr.size()});
    entries.push_back({DT_SYMENT, sizeof(Elf64_Sym)});
    if (info.hasTextRel)
        entries.push_back({DT_TEXTREL, 0});
    entries.push_back({DT_NULL, 0});

    for (const auto &entry : entries)
        appendStruct(info.dynamic, entry, 8);

    return true;
}

} // anonymous namespace

bool writeElfExe(const std::string &path,
                 const LinkLayout &layout,
                 LinkArch arch,
                 const std::vector<std::string> &neededLibs,
                 const std::unordered_set<std::string> &dynSyms,
                 std::size_t stackSize,
                 std::ostream &err) {
    const size_t pageSize = layout.pageSize;
    const uint16_t machine = (arch == LinkArch::AArch64) ? EM_AARCH64 : EM_X86_64;

    std::vector<size_t> loadableIndices;
    std::vector<size_t> nonAllocIndices;
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        const auto &sec = layout.sections[i];
        if (sec.data.empty())
            continue;
        if (!sec.alloc) {
            nonAllocIndices.push_back(i);
            continue;
        }
        if (sec.executable && sec.writable) {
            err << "error: section '" << sec.name
                << "' is both writable and executable (W^X violation)\n";
            return false;
        }
        loadableIndices.push_back(i);
    }

    DynamicInfo dynInfo;
    if (!buildDynamicInfo(layout, arch, neededLibs, dynSyms, pageSize, dynInfo, err))
        return false;

    std::vector<SegmentInfo> segments;
    const size_t ehdrSize = sizeof(Elf64_Ehdr);
    const size_t baseLoadCount = loadableIndices.size();
    const bool hasDynRo = dynInfo.enabled && !dynInfo.roBlob.empty();
    const bool hasDynRw = dynInfo.enabled && !dynInfo.dynamic.empty();
    const uint16_t numPhdrs = static_cast<uint16_t>(baseLoadCount + (hasDynRo ? 1 : 0) +
                                                    (hasDynRw ? 1 : 0) + (hasDynRo ? 1 : 0) +
                                                    (hasDynRw ? 1 : 0) + 1);
    const size_t phdrTableSize = numPhdrs * sizeof(Elf64_Phdr);

    size_t filePos = alignUp(ehdrSize + phdrTableSize, pageSize);
    for (size_t idx : loadableIndices) {
        const auto &sec = layout.sections[idx];
        filePos = alignUp(filePos, pageSize);
        const size_t fileSize = sec.zeroFill ? 0 : sec.data.size();
        uint32_t flags = PF_R;
        if (sec.executable)
            flags |= PF_X;
        if (sec.writable)
            flags |= PF_W;
        segments.push_back({idx, filePos, sec.virtualAddr, fileSize, sec.data.size(), flags});
        filePos += fileSize;
    }

    if (hasDynRo) {
        filePos = alignUp(filePos, pageSize);
        dynInfo.roFileOff = filePos;
        filePos += dynInfo.roBlob.size();
    }
    if (hasDynRw) {
        filePos = alignUp(filePos, pageSize);
        dynInfo.rwFileOff = filePos;
        filePos += dynInfo.dynamic.size();
    }

    std::vector<NonAllocInfo> nonAllocInfo;
    for (size_t idx : nonAllocIndices) {
        const auto &sec = layout.sections[idx];
        filePos = alignUp(filePos, sec.alignment);
        nonAllocInfo.push_back({idx, filePos});
        filePos += sec.data.size();
    }

    std::string shstrtab;
    shstrtab.push_back('\0');

    std::vector<uint32_t> loadableNameOffsets;
    for (size_t idx : loadableIndices) {
        loadableNameOffsets.push_back(static_cast<uint32_t>(shstrtab.size()));
        shstrtab += layout.sections[idx].name;
        shstrtab.push_back('\0');
    }

    std::vector<uint32_t> nonAllocNameOffsets;
    for (size_t idx : nonAllocIndices) {
        nonAllocNameOffsets.push_back(static_cast<uint32_t>(shstrtab.size()));
        shstrtab += layout.sections[idx].name;
        shstrtab.push_back('\0');
    }

    std::vector<uint32_t> syntheticNameOffsets;
    if (dynInfo.enabled) {
        for (const char *name : {".interp", ".dynstr", ".dynsym", ".hash", ".rela.dyn", ".dynamic"}) {
            syntheticNameOffsets.push_back(static_cast<uint32_t>(shstrtab.size()));
            shstrtab += name;
            shstrtab.push_back('\0');
        }
    }

    const uint32_t gnuStackNameOff = static_cast<uint32_t>(shstrtab.size());
    shstrtab += ".note.GNU-stack";
    shstrtab.push_back('\0');
    const uint32_t shstrtabNameOff = static_cast<uint32_t>(shstrtab.size());
    shstrtab += ".shstrtab";
    shstrtab.push_back('\0');

    const size_t shstrtabOff = alignUp(filePos, 8);
    const size_t shdrsOff = alignUp(shstrtabOff + shstrtab.size(), 8);

    const uint16_t syntheticCount = dynInfo.enabled ? 6 : 0;
    const uint16_t numShdrs = static_cast<uint16_t>(loadableIndices.size() + nonAllocIndices.size() +
                                                    syntheticCount + 3);

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

    std::vector<Elf64_Phdr> phdrs;
    phdrs.reserve(numPhdrs);
    for (const auto &seg : segments) {
        Elf64_Phdr phdr{};
        phdr.p_type = PT_LOAD;
        phdr.p_flags = seg.flags;
        phdr.p_offset = seg.fileOffset;
        phdr.p_vaddr = seg.vaddr;
        phdr.p_paddr = seg.vaddr;
        phdr.p_filesz = seg.fileSize;
        phdr.p_memsz = seg.memSize;
        phdr.p_align = pageSize;
        phdrs.push_back(phdr);
    }
    if (hasDynRo) {
        Elf64_Phdr load{};
        load.p_type = PT_LOAD;
        load.p_flags = PF_R;
        load.p_offset = dynInfo.roFileOff;
        load.p_vaddr = dynInfo.roVaddr;
        load.p_paddr = dynInfo.roVaddr;
        load.p_filesz = dynInfo.roBlob.size();
        load.p_memsz = dynInfo.roBlob.size();
        load.p_align = pageSize;
        phdrs.push_back(load);

        Elf64_Phdr interp{};
        interp.p_type = PT_INTERP;
        interp.p_flags = PF_R;
        interp.p_offset = dynInfo.roFileOff + dynInfo.interpOff;
        interp.p_vaddr = dynInfo.roVaddr + dynInfo.interpOff;
        interp.p_paddr = interp.p_vaddr;
        interp.p_filesz = dynInfo.interp.size();
        interp.p_memsz = dynInfo.interp.size();
        interp.p_align = 1;
        phdrs.push_back(interp);
    }
    if (hasDynRw) {
        Elf64_Phdr load{};
        load.p_type = PT_LOAD;
        load.p_flags = PF_R | PF_W;
        load.p_offset = dynInfo.rwFileOff;
        load.p_vaddr = dynInfo.rwVaddr;
        load.p_paddr = dynInfo.rwVaddr;
        load.p_filesz = dynInfo.dynamic.size();
        load.p_memsz = dynInfo.dynamic.size();
        load.p_align = pageSize;
        phdrs.push_back(load);

        Elf64_Phdr dynamic{};
        dynamic.p_type = PT_DYNAMIC;
        dynamic.p_flags = PF_R | PF_W;
        dynamic.p_offset = dynInfo.rwFileOff;
        dynamic.p_vaddr = dynInfo.rwVaddr;
        dynamic.p_paddr = dynInfo.rwVaddr;
        dynamic.p_filesz = dynInfo.dynamic.size();
        dynamic.p_memsz = dynInfo.dynamic.size();
        dynamic.p_align = 8;
        phdrs.push_back(dynamic);
    }
    {
        Elf64_Phdr phdr{};
        phdr.p_type = PT_GNU_STACK;
        phdr.p_flags = PF_R | PF_W;
        phdr.p_memsz = stackSize;
        phdrs.push_back(phdr);
    }

    std::vector<SyntheticSectionRef> syntheticSections;
    if (dynInfo.enabled) {
        const uint16_t dynstrShndx =
            static_cast<uint16_t>(1 + loadableIndices.size() + nonAllocIndices.size() + 1);
        const uint16_t dynsymShndx = static_cast<uint16_t>(dynstrShndx + 1);
        syntheticSections = {
            {".interp",
             SHT_PROGBITS,
             SHF_ALLOC,
             dynInfo.roVaddr + dynInfo.interpOff,
             dynInfo.roFileOff + dynInfo.interpOff,
             dynInfo.interp.size(),
             0,
             0,
             1,
             0},
            {".dynstr",
             SHT_STRTAB,
             SHF_ALLOC,
             dynInfo.roVaddr + dynInfo.dynstrOff,
             dynInfo.roFileOff + dynInfo.dynstrOff,
             dynInfo.dynstr.size(),
             0,
             0,
             1,
             0},
            {".dynsym",
             SHT_DYNSYM,
             SHF_ALLOC,
             dynInfo.roVaddr + dynInfo.dynsymOff,
             dynInfo.roFileOff + dynInfo.dynsymOff,
             dynInfo.dynsym.size(),
             dynstrShndx,
             1,
             8,
             sizeof(Elf64_Sym)},
            {".hash",
             SHT_HASH,
             SHF_ALLOC,
             dynInfo.roVaddr + dynInfo.hashOff,
             dynInfo.roFileOff + dynInfo.hashOff,
             dynInfo.hash.size(),
             dynsymShndx,
             0,
             8,
             4},
            {".rela.dyn",
             SHT_RELA,
             SHF_ALLOC,
             dynInfo.roVaddr + dynInfo.relaOff,
             dynInfo.roFileOff + dynInfo.relaOff,
             dynInfo.rela.size(),
             dynsymShndx,
             0,
             8,
             sizeof(Elf64_Rela)},
            {".dynamic",
             SHT_DYNAMIC,
             SHF_ALLOC | SHF_WRITE,
             dynInfo.rwVaddr,
             dynInfo.rwFileOff,
             dynInfo.dynamic.size(),
             dynstrShndx,
             0,
             8,
             sizeof(Elf64_Dyn)},
        };
    }

    std::vector<uint8_t> fileData(shdrsOff + static_cast<size_t>(numShdrs) * sizeof(Elf64_Shdr), 0);
    writeStruct(fileData, 0, ehdr);

    size_t phdrOff = ehdrSize;
    for (const auto &phdr : phdrs) {
        writeStruct(fileData, phdrOff, phdr);
        phdrOff += sizeof(Elf64_Phdr);
    }

    for (const auto &seg : segments) {
        const auto &sec = layout.sections[seg.layoutIdx];
        if (seg.fileSize == 0)
            continue;
        std::memcpy(fileData.data() + seg.fileOffset, sec.data.data(), seg.fileSize);
    }
    if (hasDynRo)
        std::memcpy(fileData.data() + dynInfo.roFileOff, dynInfo.roBlob.data(), dynInfo.roBlob.size());
    if (hasDynRw)
        std::memcpy(fileData.data() + dynInfo.rwFileOff, dynInfo.dynamic.data(), dynInfo.dynamic.size());

    for (size_t i = 0; i < nonAllocInfo.size(); ++i) {
        const auto &sec = layout.sections[nonAllocInfo[i].layoutIdx];
        std::memcpy(
            fileData.data() + nonAllocInfo[i].fileOffset, sec.data.data(), sec.data.size());
    }

    std::memcpy(fileData.data() + shstrtabOff, shstrtab.data(), shstrtab.size());

    size_t shdrOff = shdrsOff;
    writeStruct(fileData, shdrOff, Elf64_Shdr{});
    shdrOff += sizeof(Elf64_Shdr);

    for (size_t i = 0; i < loadableIndices.size(); ++i) {
        const auto &sec = layout.sections[loadableIndices[i]];
        Elf64_Shdr shdr{};
        shdr.sh_name = loadableNameOffsets[i];
        shdr.sh_type = sec.zeroFill ? SHT_NOBITS : SHT_PROGBITS;
        shdr.sh_flags = SHF_ALLOC;
        if (sec.writable)
            shdr.sh_flags |= SHF_WRITE;
        if (sec.executable)
            shdr.sh_flags |= SHF_EXECINSTR;
        shdr.sh_addr = sec.virtualAddr;
        shdr.sh_offset = segments[i].fileOffset;
        shdr.sh_size = sec.data.size();
        shdr.sh_addralign = std::max<uint32_t>(sec.alignment, 1u);
        writeStruct(fileData, shdrOff, shdr);
        shdrOff += sizeof(Elf64_Shdr);
    }

    for (size_t i = 0; i < nonAllocIndices.size(); ++i) {
        const auto &sec = layout.sections[nonAllocIndices[i]];
        Elf64_Shdr shdr{};
        shdr.sh_name = nonAllocNameOffsets[i];
        shdr.sh_type = SHT_PROGBITS;
        shdr.sh_offset = nonAllocInfo[i].fileOffset;
        shdr.sh_size = sec.data.size();
        shdr.sh_addralign = std::max<uint32_t>(sec.alignment, 1u);
        writeStruct(fileData, shdrOff, shdr);
        shdrOff += sizeof(Elf64_Shdr);
    }

    for (size_t i = 0; i < syntheticSections.size(); ++i) {
        const auto &sec = syntheticSections[i];
        Elf64_Shdr shdr{};
        shdr.sh_name = syntheticNameOffsets[i];
        shdr.sh_type = sec.type;
        shdr.sh_flags = sec.flags;
        shdr.sh_addr = sec.addr;
        shdr.sh_offset = sec.offset;
        shdr.sh_size = sec.size;
        shdr.sh_link = sec.link;
        shdr.sh_info = sec.info;
        shdr.sh_addralign = sec.addralign;
        shdr.sh_entsize = sec.entsize;
        writeStruct(fileData, shdrOff, shdr);
        shdrOff += sizeof(Elf64_Shdr);
    }

    {
        Elf64_Shdr shdr{};
        shdr.sh_name = gnuStackNameOff;
        shdr.sh_type = SHT_PROGBITS;
        writeStruct(fileData, shdrOff, shdr);
        shdrOff += sizeof(Elf64_Shdr);
    }
    {
        Elf64_Shdr shdr{};
        shdr.sh_name = shstrtabNameOff;
        shdr.sh_type = SHT_STRTAB;
        shdr.sh_offset = shstrtabOff;
        shdr.sh_size = shstrtab.size();
        shdr.sh_addralign = 1;
        writeStruct(fileData, shdrOff, shdr);
    }

    return writeBinaryFileAtomically(path, fileData, true, err);
}

} // namespace viper::codegen::linker
