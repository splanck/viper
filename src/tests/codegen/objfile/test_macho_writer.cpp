//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_macho_writer.cpp
// Purpose: Unit tests for MachOWriter — verifies that CodeSection data is
//          serialized into a valid Mach-O relocatable object file with correct
//          header fields, load commands, symbol table, and relocations.
// Key invariants:
//   - Mach-O magic, CPU type, file type are correct
//   - Load commands are present and correctly sized
//   - Symbol names have Darwin underscore prefix
//   - Relocations use correct Mach-O types and descending address order
//   - x86_64 addends are embedded in instruction bytes
//   - File is well-formed for both x86_64 and AArch64
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/MachOWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/objfile/MachOWriter.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace viper::codegen::objfile;
using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Read a complete file into a byte vector.
static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
        return {};
    auto sz = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    ifs.read(reinterpret_cast<char *>(data.data()), sz);
    return data;
}

static uint16_t readLE16(const std::vector<uint8_t> &d, size_t off) {
    return static_cast<uint16_t>(d[off]) | (static_cast<uint16_t>(d[off + 1]) << 8);
}

static uint32_t readLE32(const std::vector<uint8_t> &d, size_t off) {
    return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) | (static_cast<uint32_t>(d[off + 3]) << 24);
}

static uint64_t readLE64(const std::vector<uint8_t> &d, size_t off) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i)
        val |= (static_cast<uint64_t>(d[off + i]) << (i * 8));
    return val;
}

/// Extract a C string from the data at given offset.
static std::string readCStr(const std::vector<uint8_t> &d, size_t off) {
    std::string s;
    while (off < d.size() && d[off] != 0) {
        s.push_back(static_cast<char>(d[off]));
        ++off;
    }
    return s;
}

static const ObjSymbol *findSymbol(const ObjFile &obj, const std::string &name) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        if (obj.symbols[i].name == name)
            return &obj.symbols[i];
    }
    return nullptr;
}

// Known offsets for the fixed load command layout:
// Header: 32 bytes
// LC_SEGMENT_64: offset 32, size 232 (72 + 2*80)
//   __text section header: offset 32 + 72 = 104
//   __const section header: offset 104 + 80 = 184
// LC_BUILD_VERSION: offset 264, size 24
// LC_SYMTAB: offset 288, size 24
// LC_DYSYMTAB: offset 312, size 80
// End of load commands: 392

static constexpr size_t kOffLcSegment = 32;
static constexpr size_t kOffTextSect = 104;
static constexpr size_t kOffConstSect = 184;
static constexpr size_t kOffLcBuildVer = 264;
static constexpr size_t kOffLcSymtab = 288;
static constexpr size_t kOffLcDysymtab = 312;

// =============================================================================
// Test: Minimal Mach-O x86_64
// =============================================================================

static void testMinimalX64Macho() {
    CodeSection text, rodata;
    text.emit8(0xC3); // ret
    text.defineSymbol("test_func", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_minimal_x64.macho.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() > 32); // At least the Mach-O header

    // Magic
    CHECK(readLE32(data, 0) == 0xFEEDFACF);

    // CPU type = x86_64
    CHECK(readLE32(data, 4) == 0x01000007);

    // CPU subtype
    CHECK(readLE32(data, 8) == 3);

    // File type = MH_OBJECT
    CHECK(readLE32(data, 12) == 1);

    // ncmds = 4
    CHECK(readLE32(data, 16) == 4);

    // sizeofcmds = 232 + 24 + 24 + 80 = 360
    CHECK(readLE32(data, 20) == 360);

    // flags = MH_SUBSECTIONS_VIA_SYMBOLS
    CHECK(readLE32(data, 24) == 0x2000);

    // reserved = 0
    CHECK(readLE32(data, 28) == 0);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Minimal Mach-O AArch64
// =============================================================================

static void testMinimalA64Macho() {
    CodeSection text, rodata;
    text.emit32LE(0xD65F03C0); // ret
    text.defineSymbol("test_func", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_minimal_a64.macho.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::AArch64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);
    CHECK(data.size() > 32);

    // CPU type = ARM64
    CHECK(readLE32(data, 4) == 0x0100000C);

    // CPU subtype = ARM64_ALL
    CHECK(readLE32(data, 8) == 0);

    // File type = MH_OBJECT
    CHECK(readLE32(data, 12) == 1);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Load Commands
// =============================================================================

static void testLoadCommands() {
    CodeSection text, rodata;
    text.emit8(0xC3);

    std::string path = "/tmp/viper_test_macho_lc.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // LC_SEGMENT_64 at offset 32
    CHECK(readLE32(data, kOffLcSegment) == 0x19);
    CHECK(readLE32(data, kOffLcSegment + 4) == 232); // cmdsize

    // nsects = 2 (within segment cmd at offset +64)
    CHECK(readLE32(data, kOffLcSegment + 64) == 2);

    // LC_BUILD_VERSION at offset 264
    CHECK(readLE32(data, kOffLcBuildVer) == 0x32);
    CHECK(readLE32(data, kOffLcBuildVer + 4) == 24); // cmdsize

    // platform = PLATFORM_MACOS (1)
    CHECK(readLE32(data, kOffLcBuildVer + 8) == 1);

    // LC_SYMTAB at offset 288
    CHECK(readLE32(data, kOffLcSymtab) == 0x02);
    CHECK(readLE32(data, kOffLcSymtab + 4) == 24);

    // LC_DYSYMTAB at offset 312
    CHECK(readLE32(data, kOffLcDysymtab) == 0x0B);
    CHECK(readLE32(data, kOffLcDysymtab + 4) == 80);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Section Headers
// =============================================================================

static void testSectionHeaders() {
    CodeSection text, rodata;
    text.emit8(0xC3);
    text.emit8(0x90); // nop (2 bytes total)

    const char *str = "hello";
    rodata.emitBytes(str, std::strlen(str) + 1);

    std::string path = "/tmp/viper_test_macho_sects.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // __text section name at kOffTextSect
    std::string textName(reinterpret_cast<const char *>(data.data() + kOffTextSect), 6);
    CHECK(textName == "__text");

    // __text segment name at kOffTextSect + 16
    std::string textSeg(reinterpret_cast<const char *>(data.data() + kOffTextSect + 16), 6);
    CHECK(textSeg == "__TEXT");

    // __text size = 2
    CHECK(readLE64(data, kOffTextSect + 40) == 2);

    // __text flags: S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    CHECK(readLE32(data, kOffTextSect + 64) == (0x80000000 | 0x00000400));

    // __const section name at kOffConstSect
    std::string constName(reinterpret_cast<const char *>(data.data() + kOffConstSect), 7);
    CHECK(constName == "__const");

    // __const segment name
    std::string constSeg(reinterpret_cast<const char *>(data.data() + kOffConstSect + 16), 6);
    CHECK(constSeg == "__TEXT");

    // __const size = 6 ("hello\0")
    CHECK(readLE64(data, kOffConstSect + 40) == 6);

    // __const flags = 0 (S_REGULAR)
    CHECK(readLE32(data, kOffConstSect + 64) == 0);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Symbol Mangling (underscore prefix)
// =============================================================================

static void testSymbolMangling() {
    CodeSection text, rodata;
    text.emit8(0xC3);
    text.defineSymbol("my_func", SymbolBinding::Global, SymbolSection::Text);
    text.findOrDeclareSymbol("external_func");

    std::string path = "/tmp/viper_test_macho_mangle.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Read symoff and stroff from LC_SYMTAB
    uint32_t symOff = readLE32(data, kOffLcSymtab + 8);
    uint32_t nsyms = readLE32(data, kOffLcSymtab + 12);
    uint32_t strOff = readLE32(data, kOffLcSymtab + 16);

    CHECK(nsyms >= 2); // at least my_func and external_func

    // Check each symbol name has underscore prefix
    bool foundMyFunc = false;
    bool foundExternalFunc = false;
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t nlistOff = symOff + i * 16;
        uint32_t strx = readLE32(data, nlistOff);
        std::string name = readCStr(data, strOff + strx);

        if (name == "_my_func") {
            foundMyFunc = true;
            // n_type should be N_SECT | N_EXT = 0x0F
            CHECK(data[nlistOff + 4] == 0x0F);
            // n_sect should be 1 (__text)
            CHECK(data[nlistOff + 5] == 1);
        } else if (name == "_external_func") {
            foundExternalFunc = true;
            // n_type should be N_UNDF | N_EXT = 0x01
            CHECK(data[nlistOff + 4] == 0x01);
            // n_sect should be NO_SECT (0)
            CHECK(data[nlistOff + 5] == 0);
        }
    }
    CHECK(foundMyFunc);
    CHECK(foundExternalFunc);

    std::remove(path.c_str());
}

// =============================================================================
// Test: x86_64 Relocations
// =============================================================================

static void testX64Relocations() {
    CodeSection text, rodata;

    // CALL instruction: E8 + 4 zero bytes
    text.emit8(0xE8);
    uint32_t symIdx = text.findOrDeclareSymbol("rt_print_i64");
    text.addRelocation(RelocKind::Branch32, symIdx, -4);
    text.emit32LE(0); // placeholder
    text.emit8(0xC3); // ret

    text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_macho_x64_relocs.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Find __text section header to get reloff and nreloc
    uint32_t reloff = readLE32(data, kOffTextSect + 56);
    uint32_t nreloc = readLE32(data, kOffTextSect + 60);

    CHECK(nreloc == 1);
    CHECK(reloff > 0);

    // Read the relocation entry (8 bytes)
    uint32_t rAddress = readLE32(data, reloff);
    uint32_t rPacked = readLE32(data, reloff + 4);

    // r_address should be 1 (offset of displacement after E8)
    CHECK(rAddress == 1);

    // Unpack: symbolnum[23:0] | pcrel[24] | length[26:25] | extern[27] | type[31:28]
    uint8_t rPcrel = (rPacked >> 24) & 1;
    uint8_t rLength = (rPacked >> 25) & 3;
    uint8_t rExtern = (rPacked >> 27) & 1;
    uint8_t rType = (rPacked >> 28) & 0xF;

    // X86_64_RELOC_BRANCH = 2, pcrel=1, length=2 (32-bit), extern=1
    CHECK(rType == 2);
    CHECK(rPcrel == 1);
    CHECK(rLength == 2);
    CHECK(rExtern == 1);

    // Verify addend is patched into instruction bytes (-4 = FC FF FF FF)
    uint32_t textOff = readLE32(data, kOffTextSect + 48);
    CHECK(data[textOff + 1] == 0xFC);
    CHECK(data[textOff + 2] == 0xFF);
    CHECK(data[textOff + 3] == 0xFF);
    CHECK(data[textOff + 4] == 0xFF);

    std::remove(path.c_str());
}

// =============================================================================
// Test: AArch64 Relocations
// =============================================================================

static void testA64Relocations() {
    CodeSection text, rodata;

    // BL instruction (0x94000000) with A64Call26 relocation.
    uint32_t symIdx = text.findOrDeclareSymbol("rt_print_i64");
    text.addRelocation(RelocKind::A64Call26, symIdx, 0);
    text.emit32LE(0x94000000); // bl placeholder
    text.emit32LE(0xD65F03C0); // ret

    text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_macho_a64_relocs.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::AArch64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    uint32_t reloff = readLE32(data, kOffTextSect + 56);
    uint32_t nreloc = readLE32(data, kOffTextSect + 60);

    CHECK(nreloc == 1);

    uint32_t rAddress = readLE32(data, reloff);
    uint32_t rPacked = readLE32(data, reloff + 4);

    // r_address = 0 (BL is at start)
    CHECK(rAddress == 0);

    uint8_t rPcrel = (rPacked >> 24) & 1;
    uint8_t rLength = (rPacked >> 25) & 3;
    uint8_t rExtern = (rPacked >> 27) & 1;
    uint8_t rType = (rPacked >> 28) & 0xF;

    // ARM64_RELOC_BRANCH26 = 2, pcrel=1, length=2, extern=1
    CHECK(rType == 2);
    CHECK(rPcrel == 1);
    CHECK(rLength == 2);
    CHECK(rExtern == 1);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Relocation Descending Order
// =============================================================================

static void testRelocDescendingOrder() {
    CodeSection text, rodata;

    // Emit two calls at different offsets.
    uint32_t sym1 = text.findOrDeclareSymbol("func_a");
    text.addRelocation(RelocKind::A64Call26, sym1, 0);
    text.emit32LE(0x94000000); // bl at offset 0

    uint32_t sym2 = text.findOrDeclareSymbol("func_b");
    text.addRelocation(RelocKind::A64Call26, sym2, 0);
    text.emit32LE(0x94000000); // bl at offset 4

    text.emit32LE(0xD65F03C0); // ret
    text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);

    std::string path = "/tmp/viper_test_macho_reloc_order.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::AArch64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    uint32_t reloff = readLE32(data, kOffTextSect + 56);
    uint32_t nreloc = readLE32(data, kOffTextSect + 60);
    CHECK(nreloc == 2);

    // First reloc should have higher address (descending order)
    uint32_t addr0 = readLE32(data, reloff);
    uint32_t addr1 = readLE32(data, reloff + 8);
    CHECK(addr0 > addr1); // 4 > 0

    std::remove(path.c_str());
}

// =============================================================================
// Test: Factory creates Mach-O writer
// =============================================================================

static void testFactory() {
    auto writer = createObjectFileWriter(ObjFormat::MachO, ObjArch::X86_64);
    CHECK(writer != nullptr);

    auto writer2 = createObjectFileWriter(ObjFormat::MachO, ObjArch::AArch64);
    CHECK(writer2 != nullptr);
}

// =============================================================================
// Test: Rodata Section
// =============================================================================

static void testRodataSection() {
    CodeSection text, rodata;
    text.emit8(0xC3);

    const char *str = "Hello, World!";
    rodata.emitBytes(str, std::strlen(str) + 1);

    std::string path = "/tmp/viper_test_macho_rodata.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // Read __const section offset and size
    uint32_t constOff = readLE32(data, kOffConstSect + 48);
    uint64_t constSize = readLE64(data, kOffConstSect + 40);

    CHECK(constSize == std::strlen(str) + 1);

    std::string content(reinterpret_cast<const char *>(data.data() + constOff),
                        static_cast<size_t>(constSize) - 1);
    CHECK(content == "Hello, World!");

    std::remove(path.c_str());
}

// =============================================================================
// Test: DYSYMTAB ranges
// =============================================================================

static void testDysymtabRanges() {
    CodeSection text, rodata;
    text.emit8(0xC3);
    text.defineSymbol("my_func", SymbolBinding::Global, SymbolSection::Text);
    text.findOrDeclareSymbol("ext_func");

    std::string path = "/tmp/viper_test_macho_dysym.o";
    std::ostringstream errStream;

    MachOWriter writer(ObjArch::X86_64);
    bool ok = writer.write(path, text, rodata, errStream);
    CHECK(ok);

    auto data = readFile(path);

    // LC_DYSYMTAB fields: ilocalsym, nlocalsym, iextdefsym, nextdefsym, iundefsym, nundefsym
    uint32_t ilocal = readLE32(data, kOffLcDysymtab + 8);
    uint32_t nlocal = readLE32(data, kOffLcDysymtab + 12);
    uint32_t iextdef = readLE32(data, kOffLcDysymtab + 16);
    uint32_t nextdef = readLE32(data, kOffLcDysymtab + 20);
    uint32_t iundef = readLE32(data, kOffLcDysymtab + 24);
    uint32_t nundef = readLE32(data, kOffLcDysymtab + 28);

    // No local symbols, 1 external defined (my_func), 1 undefined (ext_func)
    CHECK(ilocal == 0);
    CHECK(nlocal == 0);
    CHECK(iextdef == 0);
    CHECK(nextdef == 1);
    CHECK(iundef == 1);
    CHECK(nundef == 1);

    // Ranges should be contiguous and cover all symbols
    uint32_t nsyms = readLE32(data, kOffLcSymtab + 12);
    CHECK(nsyms == nlocal + nextdef + nundef);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Multi-section merge rebases non-zero symbol offsets
// =============================================================================

static void testMultiSectionMergeRebasesSymbolOffsets() {
    CodeSection textA, textB, rodata;
    textA.defineSymbol("func_a", SymbolBinding::Global, SymbolSection::Text);
    textA.emit8(0x90);
    textA.emit8(0xC3);

    textB.emit8(0x90);
    textB.defineSymbol("func_b", SymbolBinding::Global, SymbolSection::Text);
    textB.emit8(0xC3);

    std::string path = "/tmp/viper_test_macho_multitext_merge.o";
    std::ostringstream errStream;

    auto writer = createObjectFileWriter(ObjFormat::MachO, ObjArch::X86_64);
    CHECK(writer != nullptr);
    bool ok = writer != nullptr &&
              writer->write(path, std::vector<CodeSection>{textA, textB}, rodata, errStream);
    CHECK(ok);

    ObjFile obj;
    CHECK(readObjFile(path, obj, errStream));
    const ObjSymbol *funcB = findSymbol(obj, "func_b");
    CHECK(funcB != nullptr);
    if (funcB != nullptr)
        CHECK(funcB->offset == 3);

    std::remove(path.c_str());
}

// =============================================================================
// Test: Unsupported relocations fail instead of being dropped
// =============================================================================

static void testUnsupportedRelocationFails() {
    CodeSection text, rodata;
    uint32_t symIdx = text.findOrDeclareSymbol("target");
    text.addRelocation(RelocKind::A64CondBr19, symIdx, 0);
    text.emit32LE(0x54000000); // b.eq placeholder

    std::ostringstream errStream;
    MachOWriter writer(ObjArch::AArch64);
    const bool ok = writer.write("/tmp/viper_test_macho_bad_reloc.o", text, rodata, errStream);
    CHECK(!ok);
    CHECK(errStream.str().find("no Mach-O encoding") != std::string::npos);

    std::remove("/tmp/viper_test_macho_bad_reloc.o");
}

// =============================================================================
// Main
// =============================================================================

int main() {
    testMinimalX64Macho();
    testMinimalA64Macho();
    testLoadCommands();
    testSectionHeaders();
    testSymbolMangling();
    testX64Relocations();
    testA64Relocations();
    testRelocDescendingOrder();
    testFactory();
    testRodataSection();
    testDysymtabRanges();
    testMultiSectionMergeRebasesSymbolOffsets();
    testUnsupportedRelocationFails();

    if (gFail == 0)
        std::cout << "All Mach-O writer tests passed.\n";
    else
        std::cerr << gFail << " test(s) FAILED.\n";
    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
