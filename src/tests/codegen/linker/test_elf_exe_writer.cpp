//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_elf_exe_writer.cpp
// Purpose: Unit tests for the ELF executable writer — verifies correct ELF
//          header, program headers, section headers, segment layout, W^X
//          enforcement, and multi-section output.
// Key invariants:
//   - ELF magic bytes and header fields match ELF64 spec
//   - One PT_LOAD per non-empty output section
//   - PT_GNU_STACK present with non-executable flags
//   - Section data written at correct file offsets
//   - W^X violation produces error
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/ElfExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/LinkTypes.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ─── ELF structures for parsing written files ────────────────────────────

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

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
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

// ELF constants for verification.
static constexpr uint16_t ET_EXEC = 2;
static constexpr uint16_t EM_X86_64 = 62;
static constexpr uint16_t EM_AARCH64 = 183;
static constexpr uint32_t PT_LOAD = 1;
static constexpr uint32_t PT_GNU_STACK = 0x6474E551;
static constexpr uint32_t PF_X = 1;
static constexpr uint32_t PF_W = 2;
static constexpr uint32_t PF_R = 4;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_NOBITS = 8;
static constexpr uint32_t SHF_ALLOC = 0x2;
static constexpr uint32_t SHF_WRITE = 0x1;
static constexpr uint32_t SHF_EXECINSTR = 0x4;

// ─── Helpers ─────────────────────────────────────────────────────────────

/// Read entire file into a byte vector.
static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char *>(data.data()), sz);
    return data;
}

/// Create a temporary file path for test output.
static std::string tmpPath(const std::string &name) {
    auto dir = std::filesystem::temp_directory_path() / "viper_elf_test";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

/// Clean up temp directory.
static void cleanupTmp() {
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "viper_elf_test", ec);
}

/// Build a minimal LinkLayout with the given sections.
static LinkLayout makeLayout(const std::vector<OutputSection> &sections,
                             uint64_t entryAddr = 0x401000,
                             size_t pageSize = 0x1000) {
    LinkLayout layout;
    layout.sections = sections;
    layout.entryAddr = entryAddr;
    layout.pageSize = pageSize;
    return layout;
}

/// Create an OutputSection with the given properties.
static OutputSection makeSec(const std::string &name,
                             size_t size,
                             uint64_t va,
                             bool exec,
                             bool writable,
                             uint8_t fillByte = 0xCC,
                             bool zeroFill = false) {
    OutputSection sec;
    sec.name = name;
    sec.data.resize(size, fillByte);
    sec.virtualAddr = va;
    sec.executable = exec;
    sec.writable = writable;
    sec.zeroFill = zeroFill;
    sec.alignment = 1;
    return sec;
}

// ─── Tests ───────────────────────────────────────────────────────────────

/// Test 1: Single .text section — verify ELF header fields.
static void testSingleTextSection() {
    auto path = tmpPath("single_text.elf");
    auto sec = makeSec(".text", 64, 0x401000, true, false, 0x90); // NOP-filled

    auto layout = makeLayout({sec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);
    CHECK(err.str().empty());

    auto data = readFile(path);
    CHECK(data.size() >= sizeof(Elf64_Ehdr));

    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    // ELF magic.
    CHECK(ehdr.e_ident[0] == 0x7F);
    CHECK(ehdr.e_ident[1] == 'E');
    CHECK(ehdr.e_ident[2] == 'L');
    CHECK(ehdr.e_ident[3] == 'F');
    CHECK(ehdr.e_ident[4] == 2); // ELFCLASS64
    CHECK(ehdr.e_ident[5] == 1); // ELFDATA2LSB
    CHECK(ehdr.e_ident[6] == 1); // EV_CURRENT

    // Header fields.
    CHECK(ehdr.e_type == ET_EXEC);
    CHECK(ehdr.e_machine == EM_X86_64);
    CHECK(ehdr.e_entry == 0x401000);
    CHECK(ehdr.e_ehsize == 64);
    CHECK(ehdr.e_phentsize == 56);
    CHECK(ehdr.e_shentsize == 64);

    // Program headers: 1 PT_LOAD + 1 PT_GNU_STACK = 2.
    CHECK(ehdr.e_phnum == 2);
    CHECK(ehdr.e_phoff == 64); // Immediately after ELF header.

    // Section headers: null + 1 section + .note.GNU-stack + .shstrtab = 4.
    CHECK(ehdr.e_shnum == 4);
    CHECK(ehdr.e_shstrndx == 3); // .shstrtab is last.
}

/// Test 2: Multi-section layout — .text, .rodata, .data.
static void testMultiSection() {
    auto path = tmpPath("multi_sec.elf");
    auto text = makeSec(".text", 128, 0x401000, true, false, 0xC3);     // RET-filled
    auto rodata = makeSec(".rodata", 64, 0x402000, false, false, 0x42); // 'B'-filled
    auto dataSec = makeSec(".data", 32, 0x403000, false, true, 0xDD);   // data-filled

    auto layout = makeLayout({text, rodata, dataSec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);
    CHECK(err.str().empty());

    auto data = readFile(path);
    CHECK(data.size() >= sizeof(Elf64_Ehdr));

    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    // 3 PT_LOAD + 1 PT_GNU_STACK = 4.
    CHECK(ehdr.e_phnum == 4);

    // Section headers: null + 3 sections + .note.GNU-stack + .shstrtab = 6.
    CHECK(ehdr.e_shnum == 6);

    // Verify program headers.
    CHECK(data.size() >= ehdr.e_phoff + ehdr.e_phnum * sizeof(Elf64_Phdr));
    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), data.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));

    // PT_LOAD[0]: .text — executable.
    CHECK(phdrs[0].p_type == PT_LOAD);
    CHECK(phdrs[0].p_vaddr == 0x401000);
    CHECK(phdrs[0].p_filesz == 128);
    CHECK((phdrs[0].p_flags & PF_R) != 0);
    CHECK((phdrs[0].p_flags & PF_X) != 0);
    CHECK((phdrs[0].p_flags & PF_W) == 0);
    CHECK(phdrs[0].p_align == 0x1000);

    // PT_LOAD[1]: .rodata — read-only.
    CHECK(phdrs[1].p_type == PT_LOAD);
    CHECK(phdrs[1].p_vaddr == 0x402000);
    CHECK(phdrs[1].p_filesz == 64);
    CHECK((phdrs[1].p_flags & PF_R) != 0);
    CHECK((phdrs[1].p_flags & PF_X) == 0);
    CHECK((phdrs[1].p_flags & PF_W) == 0);

    // PT_LOAD[2]: .data — writable.
    CHECK(phdrs[2].p_type == PT_LOAD);
    CHECK(phdrs[2].p_vaddr == 0x403000);
    CHECK(phdrs[2].p_filesz == 32);
    CHECK((phdrs[2].p_flags & PF_R) != 0);
    CHECK((phdrs[2].p_flags & PF_W) != 0);
    CHECK((phdrs[2].p_flags & PF_X) == 0);

    // PT_GNU_STACK: non-executable.
    CHECK(phdrs[3].p_type == PT_GNU_STACK);
    CHECK((phdrs[3].p_flags & PF_R) != 0);
    CHECK((phdrs[3].p_flags & PF_W) != 0);
    CHECK((phdrs[3].p_flags & PF_X) == 0);

    // Verify section data integrity — read .text bytes from file offset.
    CHECK(data.size() >= phdrs[0].p_offset + 128);
    for (size_t i = 0; i < 128; ++i)
        CHECK(data[phdrs[0].p_offset + i] == 0xC3);

    // Verify .rodata data.
    CHECK(data.size() >= phdrs[1].p_offset + 64);
    for (size_t i = 0; i < 64; ++i)
        CHECK(data[phdrs[1].p_offset + i] == 0x42);

    // Verify .data data.
    CHECK(data.size() >= phdrs[2].p_offset + 32);
    for (size_t i = 0; i < 32; ++i)
        CHECK(data[phdrs[2].p_offset + i] == 0xDD);
}

/// Test 3: W^X violation — section with both writable and executable flags.
static void testWxViolation() {
    auto path = tmpPath("wx_violation.elf");
    auto badSec = makeSec(".evil", 64, 0x401000, true, true); // W+X!

    auto layout = makeLayout({badSec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(!ok);
    CHECK(err.str().find("W^X violation") != std::string::npos);
}

/// Test 4: AArch64 architecture — verify EM_AARCH64 in header.
static void testAArch64Machine() {
    auto path = tmpPath("aarch64.elf");
    auto sec = makeSec(".text", 32, 0x401000, true, false);

    auto layout = makeLayout({sec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::AArch64, err);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() >= sizeof(Elf64_Ehdr));

    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));
    CHECK(ehdr.e_machine == EM_AARCH64);
}

/// Test 5: Entry point propagation — verify e_entry matches layout.entryAddr.
static void testEntryPoint() {
    auto path = tmpPath("entry.elf");
    auto sec = makeSec(".text", 256, 0x401000, true, false);

    auto layout = makeLayout({sec}, 0x401080); // Entry at offset 0x80 into text.
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));
    CHECK(ehdr.e_entry == 0x401080);
}

/// Test 6: Section header verification — names, types, and flags.
static void testSectionHeaders() {
    auto path = tmpPath("shdrs.elf");
    auto text = makeSec(".text", 64, 0x401000, true, false);
    auto dataSec = makeSec(".data", 32, 0x402000, false, true);

    auto layout = makeLayout({text, dataSec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    // Read section headers.
    CHECK(data.size() >= ehdr.e_shoff + ehdr.e_shnum * sizeof(Elf64_Shdr));
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    std::memcpy(shdrs.data(), data.data() + ehdr.e_shoff, ehdr.e_shnum * sizeof(Elf64_Shdr));

    // shdrs[0]: null section.
    CHECK(shdrs[0].sh_type == 0);
    CHECK(shdrs[0].sh_size == 0);

    // shdrs[1]: .text — SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR.
    CHECK(shdrs[1].sh_type == SHT_PROGBITS);
    CHECK((shdrs[1].sh_flags & SHF_ALLOC) != 0);
    CHECK((shdrs[1].sh_flags & SHF_EXECINSTR) != 0);
    CHECK((shdrs[1].sh_flags & SHF_WRITE) == 0);
    CHECK(shdrs[1].sh_addr == 0x401000);
    CHECK(shdrs[1].sh_size == 64);

    // shdrs[2]: .data — SHT_PROGBITS, SHF_ALLOC|SHF_WRITE.
    CHECK(shdrs[2].sh_type == SHT_PROGBITS);
    CHECK((shdrs[2].sh_flags & SHF_ALLOC) != 0);
    CHECK((shdrs[2].sh_flags & SHF_WRITE) != 0);
    CHECK((shdrs[2].sh_flags & SHF_EXECINSTR) == 0);
    CHECK(shdrs[2].sh_addr == 0x402000);
    CHECK(shdrs[2].sh_size == 32);

    // shdrs[last]: .shstrtab — SHT_STRTAB.
    CHECK(shdrs[ehdr.e_shstrndx].sh_type == SHT_STRTAB);
    CHECK(shdrs[ehdr.e_shstrndx].sh_size > 0);

    // Verify .shstrtab contains section names.
    auto &strtabShdr = shdrs[ehdr.e_shstrndx];
    CHECK(data.size() >= strtabShdr.sh_offset + strtabShdr.sh_size);
    const char *strtab = reinterpret_cast<const char *>(data.data() + strtabShdr.sh_offset);

    // .text section name should be in the string table.
    CHECK(shdrs[1].sh_name < strtabShdr.sh_size);
    CHECK(std::strcmp(strtab + shdrs[1].sh_name, ".text") == 0);

    // .data section name should be in the string table.
    CHECK(shdrs[2].sh_name < strtabShdr.sh_size);
    CHECK(std::strcmp(strtab + shdrs[2].sh_name, ".data") == 0);
}

/// Test 7: Page alignment — segments are page-aligned in file.
static void testPageAlignment() {
    auto path = tmpPath("page_align.elf");
    auto text = makeSec(".text", 100, 0x401000, true, false);
    auto dataSec = makeSec(".data", 50, 0x402000, false, true);

    auto layout = makeLayout({text, dataSec});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), data.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));

    // Each PT_LOAD segment file offset should be page-aligned.
    for (size_t i = 0; i < phdrs.size(); ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            CHECK(phdrs[i].p_offset % 0x1000 == 0);
        }
    }
}

/// Test 8: Empty sections skipped — sections with no data don't get PT_LOAD.
static void testEmptySectionSkipped() {
    auto path = tmpPath("empty_sec.elf");
    OutputSection empty;
    empty.name = ".bss";
    empty.virtualAddr = 0x402000;
    empty.writable = true;
    empty.alignment = 1;
    // data is empty — no PT_LOAD should be created.

    auto text = makeSec(".text", 64, 0x401000, true, false);
    auto layout = makeLayout({text, empty});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    // Only 1 PT_LOAD (for .text) + 1 PT_GNU_STACK = 2.
    CHECK(ehdr.e_phnum == 2);

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), data.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));
    CHECK(phdrs[0].p_type == PT_LOAD);
    CHECK(phdrs[0].p_vaddr == 0x401000);
    CHECK(phdrs[1].p_type == PT_GNU_STACK);
}

/// Test 9: Zero-fill sections use SHT_NOBITS and occupy memory, not file bytes.
static void testZeroFillSection() {
    auto path = tmpPath("zerofill.elf");
    auto text = makeSec(".text", 32, 0x401000, true, false, 0x90);
    auto bss = makeSec(".bss", 48, 0x402000, false, true, 0x00, true);

    auto layout = makeLayout({text, bss});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);
    CHECK(err.str().empty());

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), data.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));
    CHECK(ehdr.e_phnum == 3); // .text + .bss + GNU-stack
    CHECK(phdrs[1].p_type == PT_LOAD);
    CHECK(phdrs[1].p_vaddr == 0x402000);
    CHECK(phdrs[1].p_filesz == 0);
    CHECK(phdrs[1].p_memsz == 48);
    CHECK((phdrs[1].p_flags & PF_W) != 0);

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    std::memcpy(shdrs.data(), data.data() + ehdr.e_shoff, ehdr.e_shnum * sizeof(Elf64_Shdr));

    auto &strtabShdr = shdrs[ehdr.e_shstrndx];
    const char *strtab = reinterpret_cast<const char *>(data.data() + strtabShdr.sh_offset);

    bool foundBss = false;
    for (const auto &shdr : shdrs) {
        if (shdr.sh_name >= strtabShdr.sh_size)
            continue;
        if (std::strcmp(strtab + shdr.sh_name, ".bss") != 0)
            continue;
        foundBss = true;
        CHECK(shdr.sh_type == SHT_NOBITS);
        CHECK((shdr.sh_flags & SHF_ALLOC) != 0);
        CHECK((shdr.sh_flags & SHF_WRITE) != 0);
        CHECK(shdr.sh_size == 48);
        CHECK(shdr.sh_offset == 0x2000);
        break;
    }
    CHECK(foundBss);
}

/// Test 10: Large page size (16KB for macOS arm64-style layout).
static void testLargePageSize() {
    auto path = tmpPath("large_page.elf");
    auto text = makeSec(".text", 200, 0x401000, true, false);
    auto dataSec = makeSec(".data", 100, 0x405000, false, true);

    // Use 16KB page size.
    auto layout = makeLayout({text, dataSec}, 0x401000, 0x4000);
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::AArch64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), data.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));

    // PT_LOAD segments should be 16KB-aligned.
    for (size_t i = 0; i < phdrs.size(); ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            CHECK(phdrs[i].p_offset % 0x4000 == 0);
            CHECK(phdrs[i].p_align == 0x4000);
        }
    }
}

/// Test 11: GNU-stack section header exists in section headers.
static void testGnuStackSectionHeader() {
    auto path = tmpPath("gnu_stack.elf");
    auto text = makeSec(".text", 32, 0x401000, true, false);

    auto layout = makeLayout({text});
    std::ostringstream err;
    bool ok = writeElfExe(path, layout, LinkArch::X86_64, err);
    CHECK(ok);

    auto data = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, data.data(), sizeof(ehdr));

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    std::memcpy(shdrs.data(), data.data() + ehdr.e_shoff, ehdr.e_shnum * sizeof(Elf64_Shdr));

    // Read .shstrtab to resolve section names.
    auto &strtabShdr = shdrs[ehdr.e_shstrndx];
    const char *strtab = reinterpret_cast<const char *>(data.data() + strtabShdr.sh_offset);

    // Find .note.GNU-stack section header.
    bool found = false;
    for (size_t i = 0; i < shdrs.size(); ++i) {
        if (shdrs[i].sh_name < strtabShdr.sh_size &&
            std::strcmp(strtab + shdrs[i].sh_name, ".note.GNU-stack") == 0) {
            found = true;
            CHECK(shdrs[i].sh_type == SHT_PROGBITS);
            CHECK(shdrs[i].sh_size == 0); // No data.
            break;
        }
    }
    CHECK(found);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main() {
    testSingleTextSection();
    testMultiSection();
    testWxViolation();
    testAArch64Machine();
    testEntryPoint();
    testSectionHeaders();
    testPageAlignment();
    testEmptySectionSkipped();
    testZeroFillSection();
    testLargePageSize();
    testGnuStackSectionHeader();

    cleanupTmp();

    if (gFail > 0) {
        std::cerr << gFail << " check(s) FAILED\n";
        return 1;
    }
    std::cout << "All ELF exe writer tests passed.\n";
    return 0;
}
