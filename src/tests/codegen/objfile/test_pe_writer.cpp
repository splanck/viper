//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_pe_writer.cpp
// Purpose: Unit tests for PeExeWriter — verifies that linked sections are
//          serialized into a valid PE executable with correct DOS stub,
//          PE signature, COFF header, and section layout.
// Key invariants:
//   - DOS stub starts with "MZ" magic
//   - PE signature "PE\0\0" at offset pointed to by e_lfanew
//   - COFF header machine type matches architecture
//   - Optional header magic is 0x020b (PE32+)
//   - Section count and layout are consistent
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/PeExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"

#include "tests/TestHarness.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::linker;

namespace {

/// Read a complete file into a byte vector.
std::vector<uint8_t> readBinaryFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), {}};
}

uint16_t readU16(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t readU32(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t readU64(const std::vector<uint8_t> &data, size_t offset) {
    return static_cast<uint64_t>(readU32(data, offset)) |
           (static_cast<uint64_t>(readU32(data, offset + 4)) << 32);
}

size_t rvaToOffset(const std::vector<uint8_t> &data, uint32_t rva) {
    uint32_t peOffset = readU32(data, 0x3C);
    uint16_t numSections = readU16(data, peOffset + 6);
    uint16_t optSize = readU16(data, peOffset + 20);
    size_t secOff = peOffset + 24 + optSize;

    for (uint16_t i = 0; i < numSections; ++i) {
        size_t sh = secOff + static_cast<size_t>(i) * 40;
        uint32_t virtualSize = readU32(data, sh + 8);
        uint32_t virtualAddress = readU32(data, sh + 12);
        uint32_t rawSize = readU32(data, sh + 16);
        uint32_t rawPtr = readU32(data, sh + 20);
        uint32_t span = std::max(virtualSize, rawSize);
        if (rva >= virtualAddress && rva < virtualAddress + span)
            return rawPtr + (rva - virtualAddress);
    }
    return static_cast<size_t>(-1);
}

/// Create a minimal link layout with one text section.
LinkLayout makeMinimalLayout() {
    LinkLayout layout;
    constexpr uint64_t kImageBase = 0x140000000ULL;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    // Minimal code: x86_64 ret (0xC3)
    text.data = {0xC3};
    text.virtualAddr = kImageBase + 0x1000;
    layout.sections.push_back(std::move(text));

    layout.entryAddr = kImageBase + 0x1000;
    return layout;
}

LinkLayout makeMinimalArm64Layout() {
    LinkLayout layout;
    constexpr uint64_t kImageBase = 0x140000000ULL;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    text.data = {0xC0, 0x03, 0x5F, 0xD6}; // arm64 ret
    text.virtualAddr = kImageBase + 0x1000;
    layout.sections.push_back(std::move(text));

    layout.entryAddr = kImageBase + 0x1000;
    return layout;
}

LinkLayout makeTlsLayout() {
    constexpr uint64_t kImageBase = 0x140000000ULL;

    LinkLayout layout;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    text.data = {0x31, 0xC0, 0xC3};
    text.virtualAddr = kImageBase + 0x1000;
    layout.sections.push_back(std::move(text));

    OutputSection data;
    data.name = ".data";
    data.alloc = true;
    data.writable = true;
    data.data.resize(8, 0);
    data.virtualAddr = kImageBase + 0x2000;
    layout.sections.push_back(std::move(data));

    OutputSection tls;
    tls.name = ".tdata_template";
    tls.alloc = true;
    tls.writable = true;
    tls.tls = true;
    tls.alignment = 16;
    tls.data = {0x11, 0x22, 0x00, 0x00};
    tls.virtualAddr = kImageBase + 0x3000;
    layout.sections.push_back(std::move(tls));

    GlobalSymEntry tlsIndex;
    tlsIndex.name = "_tls_index";
    tlsIndex.binding = GlobalSymEntry::Global;
    tlsIndex.resolvedAddr = kImageBase + 0x2000;
    layout.globalSyms["_tls_index"] = std::move(tlsIndex);

    layout.entryAddr = kImageBase + 0x1000;
    return layout;
}

LinkLayout makeExceptionLayout() {
    constexpr uint64_t kImageBase = 0x140000000ULL;

    LinkLayout layout;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    text.data = {0xC3};
    text.virtualAddr = kImageBase + 0x1000;
    layout.sections.push_back(std::move(text));

    OutputSection xdata;
    xdata.name = ".xdata";
    xdata.alloc = true;
    xdata.data = {0x01, 0x00, 0x00, 0x00};
    xdata.virtualAddr = kImageBase + 0x2000;
    layout.sections.push_back(std::move(xdata));

    OutputSection pdata;
    pdata.name = ".pdata";
    pdata.alloc = true;
    pdata.data.resize(12, 0);
    pdata.virtualAddr = kImageBase + 0x3000;
    layout.sections.push_back(std::move(pdata));

    layout.entryAddr = kImageBase + 0x1000;
    return layout;
}

LinkLayout makeExternalIatLayout() {
    constexpr uint64_t kImageBase = 0x140000000ULL;

    LinkLayout layout;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    text.data = {0xC3};
    text.virtualAddr = kImageBase + 0x1000;
    layout.sections.push_back(std::move(text));

    OutputSection data;
    data.name = ".data";
    data.alloc = true;
    data.writable = true;
    data.data.resize(16, 0);
    data.virtualAddr = kImageBase + 0x2000;
    layout.sections.push_back(std::move(data));

    layout.entryAddr = kImageBase + 0x1000;
    return layout;
}

} // namespace

TEST(PeWriter, ProducesDosStub) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_dos.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    ASSERT_GT(data.size(), 64U);

    // DOS header: MZ magic
    EXPECT_EQ(data[0], 'M');
    EXPECT_EQ(data[1], 'Z');
}

TEST(PeWriter, PeSignatureAtElfanew) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_sig.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    ASSERT_GT(data.size(), 128U);

    // e_lfanew at offset 0x3C points to PE signature
    uint32_t peOffset = readU32(data, 0x3C);
    ASSERT_LT(peOffset + 4, data.size());
    EXPECT_EQ(data[peOffset], 'P');
    EXPECT_EQ(data[peOffset + 1], 'E');
    EXPECT_EQ(data[peOffset + 2], 0);
    EXPECT_EQ(data[peOffset + 3], 0);
}

TEST(PeWriter, CoffHeaderMachineX64) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_coff.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    ASSERT_LT(peOffset + 24, data.size());

    // COFF header starts after PE\0\0 (4 bytes)
    uint16_t machine = readU16(data, peOffset + 4);
    // IMAGE_FILE_MACHINE_AMD64 = 0x8664
    EXPECT_EQ(machine, 0x8664);
}

TEST(PeWriter, CoffHeaderMachineArm64) {
    auto layout = makeMinimalArm64Layout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_coff_arm64.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::AArch64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    ASSERT_LT(peOffset + 24, data.size());

    uint16_t machine = readU16(data, peOffset + 4);
    EXPECT_EQ(machine, 0xAA64);
}

TEST(PeWriter, OptionalHeaderPE32Plus) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_opt.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    // Optional header starts at peOffset + 4 (sig) + 20 (COFF header)
    size_t optOffset = peOffset + 24;
    ASSERT_LT(optOffset + 2, data.size());

    uint16_t magic = readU16(data, optOffset);
    // PE32+ magic = 0x020b
    EXPECT_EQ(magic, 0x020b);
}

TEST(PeWriter, EmptyImportsSucceeds) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_empty_imports.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.str().empty());
}

TEST(PeWriter, ImportDirectoryIsWritten) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_imports.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path,
                         layout,
                         LinkArch::X86_64,
                         {DllImport{"kernel32.dll", {"ExitProcess"}, {}}},
                         err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    size_t optOffset = peOffset + 24;
    uint32_t importRva = readU32(data, optOffset + 112 + 8);
    uint32_t importSize = readU32(data, optOffset + 112 + 12);
    uint32_t iatRva = readU32(data, optOffset + 112 + 12 * 8);
    uint32_t iatSize = readU32(data, optOffset + 112 + 12 * 8 + 4);

    EXPECT_GT(importRva, 0U);
    EXPECT_GT(importSize, 0U);
    EXPECT_GT(iatRva, 0U);
    EXPECT_GT(iatSize, 0U);

    std::string fileText(data.begin(), data.end());
    EXPECT_TRUE(fileText.find("kernel32.dll") != std::string::npos);
    EXPECT_TRUE(fileText.find("ExitProcess") != std::string::npos);
}

TEST(PeWriter, StartupStubBecomesEntryPoint) {
    auto layout = makeMinimalLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_stub_entry.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path,
                         layout,
                         LinkArch::X86_64,
                         {DllImport{"kernel32.dll", {"ExitProcess"}, {}}},
                         err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    size_t optOffset = peOffset + 24;
    uint32_t entryRva = readU32(data, optOffset + 16);

    EXPECT_NE(entryRva, 0x1000U);
    EXPECT_GT(entryRva, 0x1000U);
}

TEST(PeWriter, ExternalIatSlotsAreSeededWithLookupEntries) {
    auto layout = makeExternalIatLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_external_iat.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path,
                         layout,
                         LinkArch::X86_64,
                         {DllImport{"kernel32.dll", {"ExitProcess"}, {}}},
                         {{"ExitProcess", 0x2000}},
                         false,
                         0,
                         err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    size_t optOffset = peOffset + 24;
    uint32_t importRva = readU32(data, optOffset + 112 + 8);
    uint32_t iatRva = readU32(data, optOffset + 112 + 12 * 8);
    uint32_t iatSize = readU32(data, optOffset + 112 + 12 * 8 + 4);

    EXPECT_EQ(iatRva, 0x2000U);
    EXPECT_EQ(iatSize, 16U);

    size_t importOff = rvaToOffset(data, importRva);
    ASSERT_NE(importOff, static_cast<size_t>(-1));
    uint32_t firstThunk = readU32(data, importOff + 16);
    uint32_t originalFirstThunk = readU32(data, importOff + 0);
    EXPECT_EQ(firstThunk, 0x2000U);

    size_t iltOff = rvaToOffset(data, originalFirstThunk);
    size_t iatOff = rvaToOffset(data, firstThunk);
    ASSERT_NE(iltOff, static_cast<size_t>(-1));
    ASSERT_NE(iatOff, static_cast<size_t>(-1));

    const uint64_t iltEntry = readU64(data, iltOff);
    const uint64_t iatEntry = readU64(data, iatOff);
    EXPECT_NE(iltEntry, 0U);
    EXPECT_EQ(iatEntry, iltEntry);
    EXPECT_EQ(readU64(data, iatOff + 8), 0U);
}

TEST(PeWriter, TlsDirectoryIsWritten) {
    auto layout = makeTlsLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_tls.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    size_t optOffset = peOffset + 24;
    uint32_t tlsRva = readU32(data, optOffset + 112 + 9 * 8);
    uint32_t tlsSize = readU32(data, optOffset + 112 + 9 * 8 + 4);

    EXPECT_GT(tlsRva, 0U);
    EXPECT_EQ(tlsSize, 40U);

    size_t tlsOff = rvaToOffset(data, tlsRva);
    ASSERT_NE(tlsOff, static_cast<size_t>(-1));
    EXPECT_EQ(readU64(data, tlsOff + 0), 0x140003000ULL);
    EXPECT_EQ(readU64(data, tlsOff + 8), 0x140003004ULL);
    EXPECT_EQ(readU64(data, tlsOff + 16), 0x140002000ULL);
    EXPECT_EQ(readU32(data, tlsOff + 36), 0x00500000U);
}

TEST(PeWriter, ExceptionDirectoryPointsAtPdata) {
    auto layout = makeExceptionLayout();
    std::ostringstream err;
    std::string path = "build/test-out/pe_test_exception.exe";
    std::filesystem::create_directories("build/test-out");

    bool ok = writePeExe(path, layout, LinkArch::X86_64, {}, err);
    ASSERT_TRUE(ok);

    auto data = readBinaryFile(path);
    uint32_t peOffset = readU32(data, 0x3C);
    size_t optOffset = peOffset + 24;
    uint32_t exceptionRva = readU32(data, optOffset + 112 + 3 * 8);
    uint32_t exceptionSize = readU32(data, optOffset + 112 + 3 * 8 + 4);

    EXPECT_EQ(exceptionRva, 0x3000U);
    EXPECT_EQ(exceptionSize, 12U);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
