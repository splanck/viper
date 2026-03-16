//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_elf_writer.cpp
// Purpose: Unit tests for ElfWriter — verifies that CodeSection data is
//          serialized into a valid ELF relocatable object file with correct
//          header fields, section layout, symbol table, and relocations.
// Key invariants:
//   - ELF magic, class, data encoding, and file type are correct
//   - Section count and header offsets are consistent
//   - Symbol table has null entry at index 0 and section symbols
//   - Relocations map to correct ELF relocation types
//   - File is well-formed for both x86_64 and AArch64
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/ElfWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/common/objfile/ElfWriter.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace viper::codegen::objfile;

static int gFail = 0;

static void check(bool cond, const char *msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Read a complete file into a byte vector.
static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
        return {};
    auto sz = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    ifs.read(reinterpret_cast<char *>(data.data()), sz);
    return data;
}

/// Read a little-endian uint16_t from a byte vector.
static uint16_t readLE16(const std::vector<uint8_t> &d, size_t off)
{
    return static_cast<uint16_t>(d[off]) | (static_cast<uint16_t>(d[off + 1]) << 8);
}

/// Read a little-endian uint32_t.
static uint32_t readLE32(const std::vector<uint8_t> &d, size_t off)
{
    return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) | (static_cast<uint32_t>(d[off + 3]) << 24);
}

/// Read a little-endian uint64_t.
static uint64_t readLE64(const std::vector<uint8_t> &d, size_t off)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i)
        val |= (static_cast<uint64_t>(d[off + i]) << (i * 8));
    return val;
}

// =============================================================================
// Test: Minimal ELF x86_64
// =============================================================================

static void testMinimalX64Elf()
{
    // Create a minimal .text section with a few bytes.
    CodeSection text, rodata;
    text.emit8(0xC3); // ret
    text.defineSymbol("test_func", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_minimal_x64.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() > 64); // At least the ELF header

    // ELF magic
    CHECK(data[0] == 0x7F);
    CHECK(data[1] == 'E');
    CHECK(data[2] == 'L');
    CHECK(data[3] == 'F');

    // ELFCLASS64
    CHECK(data[4] == 2);

    // ELFDATA2LSB
    CHECK(data[5] == 1);

    // EV_CURRENT
    CHECK(data[6] == 1);

    // e_type = ET_REL (1)
    CHECK(readLE16(data, 16) == 1);

    // e_machine = EM_X86_64 (62)
    CHECK(readLE16(data, 18) == 62);

    // e_ehsize = 64
    CHECK(readLE16(data, 52) == 64);

    // e_shentsize = 64
    CHECK(readLE16(data, 58) == 64);

    // e_shnum = 8 (null, .text, .rodata, .rela.text, .symtab, .strtab, .shstrtab, .note.GNU-stack)
    CHECK(readLE16(data, 60) == 8);

    // e_shstrndx = 6 (.shstrtab)
    CHECK(readLE16(data, 62) == 6);

    // Verify .text data at the expected offset (should be aligned to 16 for x86_64)
    size_t textOff =
        64; // First section after 64-byte header, aligned to 16 (64 is already aligned)
    CHECK(data[textOff] == 0xC3); // ret instruction

    std::remove(path.c_str());
}

// =============================================================================
// Test: Minimal ELF AArch64
// =============================================================================

static void testMinimalA64Elf()
{
    CodeSection text, rodata;
    // ret → 0xD65F03C0 (little-endian: C0 03 5F D6)
    text.emit32LE(0xD65F03C0);
    text.defineSymbol("test_func", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_minimal_a64.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::AArch64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() > 64);

    // e_machine = EM_AARCH64 (183)
    CHECK(readLE16(data, 18) == 183);

    // .text aligned to 4 for AArch64 — offset should be 64 (already aligned)
    CHECK(data[64] == 0xC0);
    CHECK(data[65] == 0x03);
    CHECK(data[66] == 0x5F);
    CHECK(data[67] == 0xD6);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Relocations in ELF x86_64
// =============================================================================

static void testX64Relocations()
{
    CodeSection text, rodata;

    // Emit a CALL instruction placeholder (5 bytes: E8 + 4 zero bytes)
    text.emit8(0xE8);
    // Declare external symbol and add relocation.
    uint32_t symIdx = text.findOrDeclareSymbol("rt_print_i64");
    text.addRelocation(RelocKind::Branch32, symIdx, -4);
    text.emit32LE(0); // placeholder rel32
    text.emit8(0xC3); // ret

    text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_x64_relocs.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() > 64);

    // Find .rela.text section: index 3, header at e_shoff + 3*64
    uint64_t shoff = readLE64(data, 40);
    size_t relaHdr = static_cast<size_t>(shoff) + 3 * 64;

    // sh_type should be SHT_RELA (4)
    CHECK(readLE32(data, relaHdr + 4) == 4);

    // sh_size should be 24 (one Elf64_Rela entry)
    uint64_t relaSize = readLE64(data, relaHdr + 32);
    CHECK(relaSize == 24);

    // Read the rela entry
    uint64_t relaOff = readLE64(data, relaHdr + 24);
    uint64_t rOffset = readLE64(data, static_cast<size_t>(relaOff));
    uint64_t rInfo = readLE64(data, static_cast<size_t>(relaOff) + 8);
    int64_t rAddend = static_cast<int64_t>(readLE64(data, static_cast<size_t>(relaOff) + 16));

    // r_offset should point to the call displacement (byte 1, after E8)
    CHECK(rOffset == 1);

    // r_info low 32 bits should be R_X86_64_PLT32 (4)
    CHECK((rInfo & 0xFFFFFFFF) == 4);

    // r_addend should be -4
    CHECK(rAddend == -4);

    std::remove(path.c_str());
}

// =============================================================================
// Test: AArch64 Relocations
// =============================================================================

static void testA64Relocations()
{
    CodeSection text, rodata;

    // Emit a BL instruction (0x94000000) with A64Call26 relocation.
    uint32_t symIdx = text.findOrDeclareSymbol("rt_print_i64");
    text.addRelocation(RelocKind::A64Call26, symIdx, 0);
    text.emit32LE(0x94000000); // bl placeholder
    text.emit32LE(0xD65F03C0); // ret

    text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_a64_relocs.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::AArch64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Find .rela.text section
    uint64_t shoff = readLE64(data, 40);
    size_t relaHdr = static_cast<size_t>(shoff) + 3 * 64;

    uint64_t relaSize = readLE64(data, relaHdr + 32);
    CHECK(relaSize == 24); // one rela entry

    uint64_t relaOff = readLE64(data, relaHdr + 24);
    uint64_t rInfo = readLE64(data, static_cast<size_t>(relaOff) + 8);

    // Low 32 bits = R_AARCH64_CALL26 (283)
    CHECK((rInfo & 0xFFFFFFFF) == 283);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Symbol Table Structure
// =============================================================================

static void testSymbolTable()
{
    CodeSection text, rodata;
    text.emit8(0xC3);
    text.defineSymbol("my_func", SymbolBinding::Global, SymbolSection::Text);
    text.findOrDeclareSymbol("external_func"); // external

    std::string path = "/tmp/viper_test_symtab.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Find .symtab section: index 4, header at e_shoff + 4*64
    uint64_t shoff = readLE64(data, 40);
    size_t symHdr = static_cast<size_t>(shoff) + 4 * 64;

    // sh_type = SHT_SYMTAB (2)
    CHECK(readLE32(data, symHdr + 4) == 2);

    // sh_entsize = 24 (sizeof Elf64_Sym)
    uint64_t entsize = readLE64(data, symHdr + 56);
    CHECK(entsize == 24);

    // sh_size / entsize gives symbol count
    uint64_t symSize = readLE64(data, symHdr + 32);
    uint32_t symCount = static_cast<uint32_t>(symSize / entsize);

    // At minimum: null + .text section sym + .rodata section sym + my_func + external_func = 5
    CHECK(symCount >= 5);

    // sh_info = index of first non-local symbol
    uint32_t firstGlobal = readLE32(data, symHdr + 44);
    CHECK(firstGlobal >= 3); // At least null + 2 section symbols are local

    // First symbol (index 0) should be null: all zeros
    uint64_t symOff = readLE64(data, symHdr + 24);
    size_t sym0 = static_cast<size_t>(symOff);
    CHECK(readLE32(data, sym0) == 0);     // st_name = 0
    CHECK(data[sym0 + 4] == 0);           // st_info = 0
    CHECK(readLE16(data, sym0 + 6) == 0); // st_shndx = SHN_UNDEF

    std::remove(path.c_str());
}

// =============================================================================
// Test: Factory creates ELF writer
// =============================================================================

static void testFactory()
{
    auto writer = createObjectFileWriter(ObjFormat::ELF, ObjArch::X86_64);
    CHECK(writer != nullptr);

    auto writer2 = createObjectFileWriter(ObjFormat::ELF, ObjArch::AArch64);
    CHECK(writer2 != nullptr);

    // Mach-O now implemented (Phase 5)
    auto writer3 = createObjectFileWriter(ObjFormat::MachO, ObjArch::X86_64);
    CHECK(writer3 != nullptr);

    // COFF writer (Phase 6)
    auto writer4 = createObjectFileWriter(ObjFormat::COFF, ObjArch::X86_64);
    CHECK(writer4 != nullptr);
}

// =============================================================================
// Test: Section header string table
// =============================================================================

static void testSectionNames()
{
    CodeSection text, rodata;
    text.emit8(0xC3);

    std::string path = "/tmp/viper_test_shstrtab.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Find .shstrtab section: index 6
    uint64_t shoff = readLE64(data, 40);
    size_t shstrHdr = static_cast<size_t>(shoff) + 6 * 64;

    uint64_t shstrOff = readLE64(data, shstrHdr + 24);
    uint64_t shstrSize = readLE64(data, shstrHdr + 32);

    // Extract the section name string table.
    std::string shstrtab(
        reinterpret_cast<const char *>(data.data() + static_cast<size_t>(shstrOff)),
        static_cast<size_t>(shstrSize));

    // Verify required section names are present.
    CHECK(shstrtab.find(".text") != std::string::npos);
    CHECK(shstrtab.find(".rodata") != std::string::npos);
    CHECK(shstrtab.find(".rela.text") != std::string::npos);
    CHECK(shstrtab.find(".symtab") != std::string::npos);
    CHECK(shstrtab.find(".strtab") != std::string::npos);
    CHECK(shstrtab.find(".shstrtab") != std::string::npos);
    CHECK(shstrtab.find(".note.GNU-stack") != std::string::npos);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Rodata section
// =============================================================================

static void testRodataSection()
{
    CodeSection text, rodata;
    text.emit8(0xC3);

    // Add some rodata content.
    const char *str = "Hello, World!";
    rodata.emitBytes(str, std::strlen(str) + 1); // include NUL

    std::string path = "/tmp/viper_test_rodata.o";
    std::ostringstream errStream;

    ElfWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Find .rodata section: index 2
    uint64_t shoff = readLE64(data, 40);
    size_t rodataHdr = static_cast<size_t>(shoff) + 2 * 64;

    uint64_t rodataOff = readLE64(data, rodataHdr + 24);
    uint64_t rodataSize = readLE64(data, rodataHdr + 32);

    CHECK(rodataSize == std::strlen(str) + 1);

    // Verify the string content.
    std::string content(
        reinterpret_cast<const char *>(data.data() + static_cast<size_t>(rodataOff)),
        static_cast<size_t>(rodataSize) - 1); // exclude NUL
    CHECK(content == "Hello, World!");

    std::remove(path.c_str());
}

int main()
{
    testMinimalX64Elf();
    testMinimalA64Elf();
    testX64Relocations();
    testA64Relocations();
    testSymbolTable();
    testFactory();
    testSectionNames();
    testRodataSection();

    if (gFail == 0)
        std::cout << "All ELF writer tests passed.\n";
    else
        std::cerr << gFail << " test(s) FAILED.\n";
    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
