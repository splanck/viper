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

/// Create a minimal link layout with one text section.
LinkLayout makeMinimalLayout() {
    LinkLayout layout;

    OutputSection text;
    text.name = ".text";
    text.alloc = true;
    text.executable = true;
    // Minimal code: x86_64 ret (0xC3)
    text.data = {0xC3};
    text.virtualAddr = 0x1000;
    layout.sections.push_back(std::move(text));

    layout.entryAddr = 0x1000;
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

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
