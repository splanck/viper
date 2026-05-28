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
#include "codegen/common/objfile/ObjFileWriterUtil.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
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
static constexpr uint32_t PT_TLS = 7;
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
static constexpr uint32_t SHF_TLS = 0x400;

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
static constexpr uint32_t R_AARCH64_ABS64 = 257;
static constexpr uint32_t R_AARCH64_GLOB_DAT = 1025;

static constexpr const char *kLinuxX8664Interpreter = "/lib64/ld-linux-x86-64.so.2";
static constexpr const char *kLinuxAArch64Interpreter = "/lib/ld-linux-aarch64.so.1";

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

struct TlsSegmentInfo {
    bool present = false;
    size_t fileOffset = 0;
    uint64_t vaddr = 0;
    size_t fileSize = 0;
    size_t memSize = 0;
    uint64_t align = 1;
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

struct StartupStubInfo {
    bool enabled = false;
    std::vector<uint8_t> bytes;
    size_t fileOffset = 0;
    uint64_t vaddr = 0;
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
    if (off > std::numeric_limits<size_t>::max() - sizeof(T))
        throw std::length_error("ELF write offset overflow");
    const size_t end = off + sizeof(T);
    if (end > buf.size())
        buf.resize(end, 0);
    std::memcpy(buf.data() + off, &value, sizeof(T));
}

/// @brief Pad @p buf to @p align then append a raw struct copy of @p value.
template <typename T>
void appendStruct(std::vector<uint8_t> &buf, const T &value, uint64_t align = alignof(T)) {
    buf.resize(alignUp(buf.size(), align), 0);
    const size_t off = buf.size();
    buf.resize(off + sizeof(T), 0);
    std::memcpy(buf.data() + off, &value, sizeof(T));
}

/// @brief Pad @p buf to @p align, write its size to @p outOff, then concat @p src.
void appendBytes(std::vector<uint8_t> &buf,
                 const std::vector<uint8_t> &src,
                 size_t &outOff,
                 uint64_t align) {
    buf.resize(alignUp(buf.size(), align), 0);
    outOff = buf.size();
    buf.insert(buf.end(), src.begin(), src.end());
}

bool checkedU32(uint64_t value, const char *what, std::ostream &err, uint32_t &out) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        err << "error: ELF " << what << " exceeds 32-bit file format limit\n";
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool checkedAddU64(uint64_t lhs, uint64_t rhs, const char *what, std::ostream &err, uint64_t &out) {
    if (lhs > std::numeric_limits<uint64_t>::max() - rhs) {
        err << "error: ELF " << what << " overflows 64-bit address range\n";
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool checkedAddSize(size_t lhs, size_t rhs, const char *what, std::ostream &err, size_t &out) {
    if (lhs > std::numeric_limits<size_t>::max() - rhs) {
        err << "error: ELF " << what << " overflows addressable size\n";
        return false;
    }
    out = lhs + rhs;
    return true;
}

bool checkedAlignUpSize(
    size_t value, size_t alignment, const char *what, std::ostream &err, size_t &out) {
    try {
        out = alignUp(value, alignment);
    } catch (const std::exception &ex) {
        err << "error: ELF " << what << " alignment failed: " << ex.what() << "\n";
        return false;
    }
    return true;
}

/// @brief Append a NUL-terminated string to a string table; return its offset.
bool addString(std::vector<uint8_t> &strtab,
               const std::string &s,
               uint32_t &out,
               std::ostream &err) {
    if (!checkedU32(strtab.size(), "string-table offset", err, out))
        return false;
    if (s.size() == std::numeric_limits<size_t>::max() ||
        strtab.size() > std::numeric_limits<size_t>::max() - (s.size() + 1)) {
        err << "error: ELF string table overflows addressable size\n";
        return false;
    }
    strtab.insert(strtab.end(), s.begin(), s.end());
    strtab.push_back(0);
    return true;
}

/// @brief Compose an ELF r_info value from the symbol index and reloc type.
/// @details Matches the ELF64 layout: high 32 bits = symbol index, low 32 = type.
uint64_t dynInfoForSym(uint32_t symIndex, uint32_t type) {
    return (static_cast<uint64_t>(symIndex) << 32) | type;
}

/// @brief Test whether @p value fits in a signed 32-bit field (PC-relative reach check).
bool fitsInt32(int64_t value) {
    return value >= -2147483648LL && value <= 2147483647LL;
}

using viper::codegen::objfile::putLE32;

/// @brief Compute the SVR4 ELF hash (`DT_HASH`) of a symbol name.
/// @details The original Bourne-shell-era PJW hash, specified by the System V
///          ABI as the function used to populate `.hash` (DT_HASH) sections.
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

/// @brief Find the highest virtual address occupied by any allocatable section.
/// @details Used as the placement floor for synthesised sections (.dynstr,
///          .dynsym, .hash, .rela.dyn) added after layout finalisation.
bool maxAllocEndAddr(const LinkLayout &layout, uint64_t &maxEnd, std::ostream &err) {
    maxEnd = 0;
    for (const auto &sec : layout.sections) {
        const size_t memSize = outputSectionMemSize(sec);
        if (!sec.alloc || memSize == 0)
            continue;
        uint64_t secEnd = 0;
        if (!checkedAddU64(sec.virtualAddr,
                           static_cast<uint64_t>(memSize),
                           "section address range",
                           err,
                           secEnd))
            return false;
        maxEnd = std::max(maxEnd, secEnd);
    }
    return true;
}

/// @brief Synthesise an x86_64 _start stub that calls @p entryAddr and exits.
/// @details Aligns RSP to 16, calls main, passes its return value as the exit
///          code via the SYS_exit_group syscall (60), and traps if the syscall
///          returns. Used when the linker is producing a fully static binary
///          without crt1.o / glibc startup.
std::vector<uint8_t> buildLinuxX64StartupStub(uint64_t stubVa,
                                              uint64_t entryAddr,
                                              std::ostream &err) {
    std::vector<uint8_t> stub = {
        0x48, 0x83, 0xE4, 0xF0,       // and rsp, -16
        0xE8, 0x00, 0x00, 0x00, 0x00, // call entry
        0x89, 0xC7,                   // mov edi, eax
        0xB8, 0x3C, 0x00, 0x00, 0x00, // mov eax, 60
        0x0F, 0x05,                   // syscall
        0x0F, 0x0B,                   // ud2
    };

    const int64_t callDisp = static_cast<int64_t>(entryAddr) - static_cast<int64_t>(stubVa + 9);
    if (!fitsInt32(callDisp)) {
        err << "error: ELF x86_64 startup stub cannot reach entry point\n";
        return {};
    }

    putLE32(stub, 5, static_cast<uint32_t>(static_cast<int32_t>(callDisp)));
    return stub;
}

/// @brief Synthesise an AArch64 _start stub: BL entry; MOV x8,#93; SVC #0; BRK.
/// @details Tail-calls main, then issues SYS_exit (93) with the return value
///          in x0. Bails if the entry point is not within ±128 MB BL range.
std::vector<uint8_t> buildLinuxAArch64StartupStub(uint64_t stubVa,
                                                  uint64_t entryAddr,
                                                  std::ostream &err) {
    const int64_t delta = static_cast<int64_t>(entryAddr) - static_cast<int64_t>(stubVa);
    if ((delta & 0x3) != 0) {
        err << "error: ELF AArch64 startup stub target is not instruction aligned\n";
        return {};
    }

    const int64_t imm26 = delta >> 2;
    if (imm26 < -(1 << 25) || imm26 > ((1 << 25) - 1)) {
        err << "error: ELF AArch64 startup stub branch is out of range\n";
        return {};
    }

    std::vector<uint8_t> stub;
    encoding::writeLE32(stub,
                        0x94000000U | (static_cast<uint32_t>(imm26) & 0x03FFFFFFU)); // bl entry
    encoding::writeLE32(stub, 0xD2800BA8U);                                          // movz x8, #93
    encoding::writeLE32(stub, 0xD4000001U);                                          // svc #0
    encoding::writeLE32(stub, 0xD4200000U);                                          // brk #0
    return stub;
}

/// @brief Construct the .dynamic / .dynsym / .dynstr / .rela.dyn / .hash blobs.
/// @details Emits one DT_NEEDED per @p neededLibs entry, one .dynsym + .dynstr
///          entry per @p dynSyms member, and one R_*_GLOB_DAT relocation per
///          GOT/import slot found in the layout. The synthesised buffers are
///          page-aligned and placed contiguously after the existing alloc
///          sections by the caller.
/// @return false on overflow / unrecoverable layout errors (writes to @p err).
bool buildDynamicInfo(const LinkLayout &layout,
                      LinkArch arch,
                      const std::vector<std::string> &neededLibs,
                      const std::unordered_set<std::string> &dynSyms,
                      size_t pageSize,
                      DynamicInfo &info,
                      std::ostream &err) {
    if (dynSyms.empty())
        return true;

    if (arch != LinkArch::X86_64 && arch != LinkArch::AArch64) {
        err << "error: ELF dynamic imports are only implemented for Linux x86_64/AArch64\n";
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

    const char *interp =
        (arch == LinkArch::AArch64) ? kLinuxAArch64Interpreter : kLinuxX8664Interpreter;
    info.interp.assign(interp, interp + std::strlen(interp) + 1);

    info.dynstr.push_back(0);
    for (const auto &lib : info.neededLibs)
        if (!addString(info.dynstr, lib, info.neededNameOff[lib], err))
            return false;
    for (const auto &sym : info.dynSymbols)
        if (!addString(info.dynstr, sym, info.dynStrOff[sym], err))
            return false;

    appendStruct(info.dynsym, Elf64_Sym{}, 8);
    if (info.dynSymbols.size() >= std::numeric_limits<uint32_t>::max()) {
        err << "error: ELF dynamic symbol count exceeds 32-bit file format limit\n";
        return false;
    }
    for (size_t i = 0; i < info.dynSymbols.size(); ++i) {
        Elf64_Sym sym{};
        sym.st_name = info.dynStrOff[info.dynSymbols[i]];
        sym.st_info = (1u << 4); // STB_GLOBAL | STT_NOTYPE
        appendStruct(info.dynsym, sym, 8);
        info.dynSymIndex[info.dynSymbols[i]] = static_cast<uint32_t>(i + 1);
    }

    uint32_t dynSymCount = 0;
    if (!checkedU32(info.dynSymbols.size() + 1, "dynamic symbol count", err, dynSymCount))
        return false;
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
        emitRela(got.gotAddr,
                 it->second,
                 arch == LinkArch::AArch64 ? R_AARCH64_GLOB_DAT : R_X86_64_GLOB_DAT);
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
            err << "error: dynamic bind entry references invalid section index "
                << bind.sectionIndex << "\n";
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
        uint64_t relocAddr = 0;
        if (!checkedAddU64(
                sec.virtualAddr, bind.offset, "dynamic relocation address", err, relocAddr))
            return false;
        emitRela(relocAddr, it->second, arch == LinkArch::AArch64 ? R_AARCH64_ABS64 : R_X86_64_64);
    }

    appendBytes(info.roBlob, info.interp, info.interpOff, 1);
    appendBytes(info.roBlob, info.dynstr, info.dynstrOff, 1);
    appendBytes(info.roBlob, info.dynsym, info.dynsymOff, 8);
    appendBytes(info.roBlob, info.hash, info.hashOff, 8);
    appendBytes(info.roBlob, info.rela, info.relaOff, 8);

    uint64_t maxAllocEnd = 0;
    if (!maxAllocEndAddr(layout, maxAllocEnd, err))
        return false;
    size_t maxAllocEndSize = 0;
    if (maxAllocEnd > std::numeric_limits<size_t>::max()) {
        err << "error: ELF dynamic section placement exceeds addressable size\n";
        return false;
    }
    size_t roBaseSize = 0;
    if (!checkedAlignUpSize(static_cast<size_t>(maxAllocEnd),
                            pageSize,
                            "dynamic section placement",
                            err,
                            roBaseSize))
        return false;
    const uint64_t roBase = roBaseSize;
    info.roVaddr = roBase;
    uint64_t roEnd = 0;
    if (!checkedAddU64(roBase, info.roBlob.size(), "dynamic read-only section range", err, roEnd))
        return false;
    if (roEnd > std::numeric_limits<size_t>::max()) {
        err << "error: ELF dynamic writable section placement exceeds addressable size\n";
        return false;
    }
    size_t rwBaseSize = 0;
    if (!checkedAlignUpSize(static_cast<size_t>(roEnd),
                            pageSize,
                            "dynamic writable section placement",
                            err,
                            rwBaseSize))
        return false;
    info.rwVaddr = rwBaseSize;

    auto dynSectionVA = [&](size_t off, const char *what, uint64_t &out) {
        return checkedAddU64(info.roVaddr, off, what, err, out);
    };
    uint64_t hashVA = 0;
    uint64_t dynstrVA = 0;
    uint64_t dynsymVA = 0;
    uint64_t relaVA = 0;
    if (!dynSectionVA(info.hashOff, ".hash virtual address", hashVA) ||
        !dynSectionVA(info.dynstrOff, ".dynstr virtual address", dynstrVA) ||
        !dynSectionVA(info.dynsymOff, ".dynsym virtual address", dynsymVA) ||
        !dynSectionVA(info.relaOff, ".rela.dyn virtual address", relaVA))
        return false;

    std::vector<Elf64_Dyn> entries;
    entries.reserve(info.neededLibs.size() + 8);
    for (const auto &lib : info.neededLibs)
        entries.push_back({DT_NEEDED, info.neededNameOff[lib]});
    entries.push_back({DT_HASH, hashVA});
    entries.push_back({DT_STRTAB, dynstrVA});
    entries.push_back({DT_SYMTAB, dynsymVA});
    entries.push_back({DT_RELA, relaVA});
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
                 bool emitStartupStub,
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

    StartupStubInfo startupStub;
    if (emitStartupStub) {
        if (layout.entryAddr == 0) {
            err << "error: ELF startup stub requested but entry address is missing\n";
            return false;
        }

        uint64_t maxEnd = 0;
        if (!maxAllocEndAddr(layout, maxEnd, err))
            return false;
        if (dynInfo.enabled) {
            if (!dynInfo.roBlob.empty()) {
                uint64_t roEnd = 0;
                if (!checkedAddU64(dynInfo.roVaddr,
                                   dynInfo.roBlob.size(),
                                   "dynamic read-only range",
                                   err,
                                   roEnd))
                    return false;
                maxEnd = std::max(maxEnd, roEnd);
            }
            if (!dynInfo.dynamic.empty()) {
                uint64_t rwEnd = 0;
                if (!checkedAddU64(dynInfo.rwVaddr,
                                   dynInfo.dynamic.size(),
                                   "dynamic writable range",
                                   err,
                                   rwEnd))
                    return false;
                maxEnd = std::max(maxEnd, rwEnd);
            }
        }
        if (maxEnd > std::numeric_limits<size_t>::max()) {
            err << "error: ELF startup stub placement exceeds addressable size\n";
            return false;
        }
        size_t stubVaddr = 0;
        if (!checkedAlignUpSize(
                static_cast<size_t>(maxEnd), pageSize, "startup stub placement", err, stubVaddr))
            return false;
        startupStub.vaddr = stubVaddr;

        if (arch == LinkArch::X86_64)
            startupStub.bytes = buildLinuxX64StartupStub(startupStub.vaddr, layout.entryAddr, err);
        else
            startupStub.bytes =
                buildLinuxAArch64StartupStub(startupStub.vaddr, layout.entryAddr, err);
        if (startupStub.bytes.empty())
            return false;
        startupStub.enabled = true;
    }

    std::vector<SegmentInfo> segments;
    const size_t ehdrSize = sizeof(Elf64_Ehdr);
    const size_t baseLoadCount = loadableIndices.size();
    const bool hasDynRo = dynInfo.enabled && !dynInfo.roBlob.empty();
    const bool hasDynRw = dynInfo.enabled && !dynInfo.dynamic.empty();
    const bool hasTls = std::any_of(loadableIndices.begin(),
                                    loadableIndices.end(),
                                    [&](size_t idx) { return layout.sections[idx].tls; });
    const size_t phdrCount = baseLoadCount + (hasDynRo ? 1 : 0) + (hasDynRw ? 1 : 0) +
                             (hasDynRo ? 1 : 0) + (hasDynRw ? 1 : 0) +
                             (startupStub.enabled ? 1 : 0) + (hasTls ? 1 : 0) + 1;
    if (phdrCount > std::numeric_limits<uint16_t>::max()) {
        err << "error: ELF program header count exceeds 16-bit file format limit\n";
        return false;
    }
    if (phdrCount > std::numeric_limits<size_t>::max() / sizeof(Elf64_Phdr)) {
        err << "error: ELF program header table size overflows address space\n";
        return false;
    }
    const uint16_t numPhdrs = static_cast<uint16_t>(phdrCount);
    const size_t phdrTableSize = numPhdrs * sizeof(Elf64_Phdr);

    size_t headerTablesSize = 0;
    if (!checkedAddSize(ehdrSize, phdrTableSize, "header table size", err, headerTablesSize))
        return false;
    size_t filePos = 0;
    if (!checkedAlignUpSize(headerTablesSize, pageSize, "first load segment offset", err, filePos))
        return false;
    for (size_t idx : loadableIndices) {
        const auto &sec = layout.sections[idx];
        if (!checkedAlignUpSize(filePos, pageSize, "load segment file offset", err, filePos))
            return false;
        const size_t fileSize = sec.zeroFill ? 0 : sec.data.size();
        const size_t memSize = outputSectionMemSize(sec);
        uint32_t flags = PF_R;
        if (sec.executable)
            flags |= PF_X;
        if (sec.writable)
            flags |= PF_W;
        segments.push_back({idx, filePos, sec.virtualAddr, fileSize, memSize, flags});
        if (!checkedAddSize(filePos, fileSize, "load segment file range", err, filePos))
            return false;
    }

    TlsSegmentInfo tlsInfo;
    for (const auto &seg : segments) {
        const auto &sec = layout.sections[seg.layoutIdx];
        if (!sec.tls)
            continue;

        if (!tlsInfo.present) {
            tlsInfo.present = true;
            tlsInfo.fileOffset = seg.fileOffset;
            tlsInfo.vaddr = seg.vaddr;
        }
        if (seg.vaddr < tlsInfo.vaddr) {
            err << "error: TLS section addresses are not monotonically ordered\n";
            return false;
        }
        const uint64_t rel64 = seg.vaddr - tlsInfo.vaddr;
        if (rel64 > std::numeric_limits<size_t>::max()) {
            err << "error: TLS segment span exceeds addressable size\n";
            return false;
        }
        const size_t rel = static_cast<size_t>(rel64);
        if (seg.memSize > std::numeric_limits<size_t>::max() - rel) {
            err << "error: TLS segment memory size overflows address space\n";
            return false;
        }
        tlsInfo.memSize = std::max(tlsInfo.memSize, rel + seg.memSize);
        if (!sec.zeroFill) {
            if (seg.fileSize > std::numeric_limits<size_t>::max() - rel) {
                err << "error: TLS segment file size overflows address space\n";
                return false;
            }
            tlsInfo.fileSize = std::max(tlsInfo.fileSize, rel + seg.fileSize);
        }
        tlsInfo.align = std::max<uint64_t>(tlsInfo.align, std::max<uint32_t>(sec.alignment, 1u));
    }

    if (hasDynRo) {
        if (!checkedAlignUpSize(filePos, pageSize, "dynamic read-only file offset", err, filePos))
            return false;
        dynInfo.roFileOff = filePos;
        if (!checkedAddSize(
                filePos, dynInfo.roBlob.size(), "dynamic read-only file range", err, filePos))
            return false;
    }
    if (hasDynRw) {
        if (!checkedAlignUpSize(filePos, pageSize, "dynamic writable file offset", err, filePos))
            return false;
        dynInfo.rwFileOff = filePos;
        if (!checkedAddSize(
                filePos, dynInfo.dynamic.size(), "dynamic writable file range", err, filePos))
            return false;
    }
    if (startupStub.enabled) {
        if (!checkedAlignUpSize(filePos, pageSize, "startup stub file offset", err, filePos))
            return false;
        startupStub.fileOffset = filePos;
        if (!checkedAddSize(
                filePos, startupStub.bytes.size(), "startup stub file range", err, filePos))
            return false;
    }

    std::vector<NonAllocInfo> nonAllocInfo;
    for (size_t idx : nonAllocIndices) {
        const auto &sec = layout.sections[idx];
        if (!checkedAlignUpSize(
                filePos, sec.alignment, "non-alloc section file offset", err, filePos))
            return false;
        nonAllocInfo.push_back({idx, filePos});
        if (!checkedAddSize(filePos, sec.data.size(), "non-alloc section file range", err, filePos))
            return false;
    }

    std::string shstrtab;
    shstrtab.push_back('\0');

    std::vector<uint32_t> loadableNameOffsets;
    for (size_t idx : loadableIndices) {
        uint32_t off = 0;
        if (!checkedU32(shstrtab.size(), "section-name string-table offset", err, off))
            return false;
        loadableNameOffsets.push_back(off);
        shstrtab += layout.sections[idx].name;
        shstrtab.push_back('\0');
    }

    std::vector<uint32_t> nonAllocNameOffsets;
    for (size_t idx : nonAllocIndices) {
        uint32_t off = 0;
        if (!checkedU32(shstrtab.size(), "section-name string-table offset", err, off))
            return false;
        nonAllocNameOffsets.push_back(off);
        shstrtab += layout.sections[idx].name;
        shstrtab.push_back('\0');
    }

    std::vector<uint32_t> syntheticNameOffsets;
    if (dynInfo.enabled) {
        for (const char *name :
             {".interp", ".dynstr", ".dynsym", ".hash", ".rela.dyn", ".dynamic"}) {
            uint32_t off = 0;
            if (!checkedU32(shstrtab.size(), "section-name string-table offset", err, off))
                return false;
            syntheticNameOffsets.push_back(off);
            shstrtab += name;
            shstrtab.push_back('\0');
        }
    }

    uint32_t gnuStackNameOff = 0;
    if (!checkedU32(shstrtab.size(), "section-name string-table offset", err, gnuStackNameOff))
        return false;
    shstrtab += ".note.GNU-stack";
    shstrtab.push_back('\0');
    uint32_t shstrtabNameOff = 0;
    if (!checkedU32(shstrtab.size(), "section-name string-table offset", err, shstrtabNameOff))
        return false;
    shstrtab += ".shstrtab";
    shstrtab.push_back('\0');

    size_t shstrtabOff = 0;
    if (!checkedAlignUpSize(filePos, 8, "section-name string-table offset", err, shstrtabOff))
        return false;
    size_t shstrtabEnd = 0;
    if (!checkedAddSize(
            shstrtabOff, shstrtab.size(), "section-name string-table range", err, shstrtabEnd))
        return false;
    size_t shdrsOff = 0;
    if (!checkedAlignUpSize(shstrtabEnd, 8, "section-header table offset", err, shdrsOff))
        return false;

    const size_t syntheticCount = dynInfo.enabled ? 6 : 0;
    const size_t shdrCount = loadableIndices.size() + nonAllocIndices.size() + syntheticCount + 3;
    if (shdrCount > std::numeric_limits<uint16_t>::max()) {
        err << "error: ELF section header count exceeds 16-bit file format limit\n";
        return false;
    }
    const uint16_t numShdrs = static_cast<uint16_t>(shdrCount);

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
    ehdr.e_entry = startupStub.enabled ? startupStub.vaddr : layout.entryAddr;
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
    if (tlsInfo.present) {
        Elf64_Phdr tls{};
        tls.p_type = PT_TLS;
        tls.p_flags = PF_R;
        tls.p_offset = tlsInfo.fileOffset;
        tls.p_vaddr = tlsInfo.vaddr;
        tls.p_paddr = tlsInfo.vaddr;
        tls.p_filesz = tlsInfo.fileSize;
        tls.p_memsz = tlsInfo.memSize;
        tls.p_align = tlsInfo.align;
        phdrs.push_back(tls);
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
    if (startupStub.enabled) {
        Elf64_Phdr load{};
        load.p_type = PT_LOAD;
        load.p_flags = PF_R | PF_X;
        load.p_offset = startupStub.fileOffset;
        load.p_vaddr = startupStub.vaddr;
        load.p_paddr = startupStub.vaddr;
        load.p_filesz = startupStub.bytes.size();
        load.p_memsz = startupStub.bytes.size();
        load.p_align = pageSize;
        phdrs.push_back(load);
    }
    if (dynInfo.enabled) {
        for (auto &phdr : phdrs) {
            if (phdr.p_type != PT_LOAD)
                continue;
            if (phdr.p_offset == 0)
                break;

            const size_t headerSlack = static_cast<size_t>(phdr.p_offset);
            if (phdr.p_vaddr < headerSlack || phdr.p_paddr < headerSlack) {
                err << "error: ELF header-mapping adjustment underflows segment base\n";
                return false;
            }
            size_t filesz = 0;
            size_t memsz = 0;
            if (!checkedAddSize(headerSlack,
                                static_cast<size_t>(phdr.p_filesz),
                                "header-mapped PT_LOAD file size",
                                err,
                                filesz) ||
                !checkedAddSize(headerSlack,
                                static_cast<size_t>(phdr.p_memsz),
                                "header-mapped PT_LOAD memory size",
                                err,
                                memsz))
                return false;

            phdr.p_offset = 0;
            phdr.p_vaddr -= headerSlack;
            phdr.p_paddr -= headerSlack;
            phdr.p_filesz = filesz;
            phdr.p_memsz = memsz;
            break;
        }
    }
    {
        Elf64_Phdr phdr{};
        phdr.p_type = PT_GNU_STACK;
        phdr.p_flags = PF_R | PF_W;
        phdr.p_memsz = stackSize;
        phdrs.push_back(phdr);
    }
    if (phdrs.size() != numPhdrs) {
        err << "error: internal ELF program header count mismatch\n";
        return false;
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

    if (static_cast<size_t>(numShdrs) > std::numeric_limits<size_t>::max() / sizeof(Elf64_Shdr)) {
        err << "error: ELF section header table size overflows address space\n";
        return false;
    }
    const size_t shdrBytes = static_cast<size_t>(numShdrs) * sizeof(Elf64_Shdr);
    if (shdrsOff > std::numeric_limits<size_t>::max() - shdrBytes) {
        err << "error: ELF file size overflows address space\n";
        return false;
    }
    std::vector<uint8_t> fileData(shdrsOff + shdrBytes, 0);
    try {
        writeStruct(fileData, 0, ehdr);
    } catch (const std::exception &ex) {
        err << "error: ELF header write failed: " << ex.what() << "\n";
        return false;
    }

    size_t phdrOff = ehdrSize;
    for (const auto &phdr : phdrs) {
        try {
            writeStruct(fileData, phdrOff, phdr);
        } catch (const std::exception &ex) {
            err << "error: ELF program header write failed: " << ex.what() << "\n";
            return false;
        }
        phdrOff += sizeof(Elf64_Phdr);
    }

    for (const auto &seg : segments) {
        const auto &sec = layout.sections[seg.layoutIdx];
        if (seg.fileSize == 0)
            continue;
        std::memcpy(fileData.data() + seg.fileOffset, sec.data.data(), seg.fileSize);
    }
    if (hasDynRo)
        std::memcpy(
            fileData.data() + dynInfo.roFileOff, dynInfo.roBlob.data(), dynInfo.roBlob.size());
    if (hasDynRw)
        std::memcpy(
            fileData.data() + dynInfo.rwFileOff, dynInfo.dynamic.data(), dynInfo.dynamic.size());
    if (startupStub.enabled)
        std::memcpy(fileData.data() + startupStub.fileOffset,
                    startupStub.bytes.data(),
                    startupStub.bytes.size());

    for (size_t i = 0; i < nonAllocInfo.size(); ++i) {
        const auto &sec = layout.sections[nonAllocInfo[i].layoutIdx];
        std::memcpy(fileData.data() + nonAllocInfo[i].fileOffset, sec.data.data(), sec.data.size());
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
        if (sec.tls)
            shdr.sh_flags |= SHF_TLS;
        shdr.sh_addr = sec.virtualAddr;
        shdr.sh_offset = segments[i].fileOffset;
        shdr.sh_size = outputSectionMemSize(sec);
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
