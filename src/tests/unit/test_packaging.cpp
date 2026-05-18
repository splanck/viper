//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_packaging.cpp
// Purpose: Unit tests for the VAPS packaging subsystem — verifies binary
//          format correctness for ZIP, tar, ar, PE, DEFLATE, ICO/ICNS,
//          .lnk, .desktop, and Info.plist generators.
//
// Key invariants:
//   - Each test verifies structural correctness via magic bytes and offsets.
//   - Round-trip tests ensure compress->decompress produces original data.
//   - No file I/O — all tests use in-memory APIs.
//
// Ownership/Lifetime:
//   - Self-contained test binary.
//
// Links: src/tools/common/packaging/*
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "ArWriter.hpp"
#include "DesktopEntryGenerator.hpp"
#include "IconGenerator.hpp"
#include "InstallerStub.hpp"
#include "InstallerStubGen.hpp"
#include "LinuxPackageBuilder.hpp"
#include "LnkWriter.hpp"
#include "MacOSPackageBuilder.hpp"
#include "PEBuilder.hpp"
#include "PackageConfig.hpp"
#include "PkgDeflate.hpp"
#include "PkgGzip.hpp"
#include "PkgPNG.hpp"
#include "PkgUtils.hpp"
#include "PkgVerify.hpp"
#include "PlistGenerator.hpp"
#include "TarWriter.hpp"
#include "ToolchainInstallManifest.hpp"
#include "WindowsPackageBuilder.hpp"
#include "ZipReader.hpp"
#include "ZipWriter.hpp"
#include "viper/platform/Capabilities.hpp"
#include "viper/runtime/RuntimeComponentManifest.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

using namespace viper::pkg;

extern "C" {
uint32_t rt_crc32_compute(const uint8_t *data, size_t len);
}

// ============================================================================
// Helper: read a little-endian uint16 from a byte buffer
// ============================================================================
static uint16_t readLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static void writeLE32(uint8_t *p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static uint64_t readLE64(const uint8_t *p) {
    return static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) | (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) | (static_cast<uint64_t>(p[7]) << 56);
}

static void writeBytes(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

static void writeTestWindowsPe(const std::filesystem::path &path,
                               const std::string &arch = "x64",
                               std::vector<PEImport> imports = {}) {
    PEBuildParams pe;
    pe.arch = arch;
    pe.textSection = {0xC3};
    pe.imports = std::move(imports);
    writeBytes(path, buildPE(pe));
}

static uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

static void appendBE32(std::vector<uint8_t> &buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static uint32_t testAdler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void appendPngChunk(std::vector<uint8_t> &buf,
                           const char type[4],
                           const std::vector<uint8_t> &payload) {
    appendBE32(buf, static_cast<uint32_t>(payload.size()));
    const size_t typeOff = buf.size();
    buf.insert(buf.end(), reinterpret_cast<const uint8_t *>(type),
               reinterpret_cast<const uint8_t *>(type) + 4);
    buf.insert(buf.end(), payload.begin(), payload.end());
    appendBE32(buf, rt_crc32_compute(buf.data() + typeOff, 4 + payload.size()));
}

static std::vector<uint8_t> makeIndexedPng2x1WithTransparency() {
    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    appendBE32(ihdr, 2);
    appendBE32(ihdr, 1);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(3); // indexed color
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendPngChunk(png, "IHDR", ihdr);
    appendPngChunk(png, "PLTE", {255, 0, 0, 0, 0, 255});
    appendPngChunk(png, "tRNS", {255, 0});

    std::vector<uint8_t> raw = {0, 0, 1};
    auto deflated = deflate(raw.data(), raw.size());
    std::vector<uint8_t> zlib = {0x78, 0x01};
    zlib.insert(zlib.end(), deflated.begin(), deflated.end());
    appendBE32(zlib, testAdler32(raw.data(), raw.size()));
    appendPngChunk(png, "IDAT", zlib);
    appendPngChunk(png, "IEND", {});
    return png;
}

static bool containsUtf16LE(const std::vector<uint8_t> &data, const std::string &text) {
    const auto needle = utf8ToUtf16LEBytes(text, true);
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

static bool containsUtf16LEStringData(const std::vector<uint8_t> &data, const std::string &text) {
    const auto needle = utf8ToUtf16LEBytes(text, false);
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

static bool containsAscii(const std::vector<uint8_t> &data, const std::string &text) {
    return std::search(data.begin(), data.end(), text.begin(), text.end()) != data.end();
}

static std::vector<uint8_t> extractFirstZipOverlay(const std::vector<uint8_t> &pe) {
    const std::array<uint8_t, 4> localHeader{{'P', 'K', 0x03, 0x04}};
    auto it = std::search(pe.begin(), pe.end(), localHeader.begin(), localHeader.end());
    if (it == pe.end())
        return {};
    return std::vector<uint8_t>(it, pe.end());
}

static uint32_t alignUpTest(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t testIatBaseRvaForStub(const StubResult &stub) {
    uint32_t idtSize = static_cast<uint32_t>((stub.imports.size() + 1) * 20);
    uint32_t iltSize = 0;
    for (const auto &imp : stub.imports)
        iltSize += static_cast<uint32_t>((imp.functions.size() + 1) * 8);
    uint32_t hintNameSize = 0;
    for (const auto &imp : stub.imports) {
        for (const auto &fn : imp.functions) {
            const uint32_t entryLen = static_cast<uint32_t>(2 + fn.size() + 1);
            hintNameSize += (entryLen + 1) & ~1u;
        }
    }
    uint32_t dllNameSize = 0;
    for (const auto &imp : stub.imports)
        dllNameSize += static_cast<uint32_t>(imp.dllName.size() + 1);

    const uint32_t iatOff = alignUpTest(idtSize + iltSize + hintNameSize + dllNameSize, 8);
    const uint32_t rdataRVA =
        0x2000u + (alignUpTest(static_cast<uint32_t>(stub.textSection.size()), 0x1000u) - 0x1000u);
    return rdataRVA + iatOff;
}

static size_t countIatCallsTo(const StubResult &stub,
                              const std::string &dllName,
                              const std::string &functionName) {
    const uint32_t iatBaseRva = testIatBaseRvaForStub(stub);
    uint32_t slotOffset = 0;
    bool found = false;
    for (const auto &imp : stub.imports) {
        for (const auto &fn : imp.functions) {
            if (imp.dllName == dllName && fn == functionName) {
                found = true;
                break;
            }
            slotOffset += 8;
        }
        if (found)
            break;
        slotOffset += 8;
    }
    if (!found)
        return 0;

    const int64_t targetRva = static_cast<int64_t>(iatBaseRva + slotOffset);
    size_t calls = 0;
    for (size_t i = 0; i + 6 <= stub.textSection.size(); ++i) {
        if (stub.textSection[i] != 0xFF || stub.textSection[i + 1] != 0x15)
            continue;
        const int32_t disp = static_cast<int32_t>(readLE32(stub.textSection.data() + i + 2));
        const int64_t rip = 0x1000ll + static_cast<int64_t>(i) + 6;
        if (rip + disp == targetRva)
            ++calls;
    }
    return calls;
}

static std::vector<uint8_t> inflateGzipPayload(const std::vector<uint8_t> &gzipData) {
    return gunzip(gzipData.data(), gzipData.size());
}

static std::string tarFirstEntryName(const std::vector<uint8_t> &tarBytes) {
    if (tarBytes.size() < 512)
        return {};
    const char *name = reinterpret_cast<const char *>(tarBytes.data());
    size_t len = 0;
    while (len < 100 && name[len] != '\0')
        ++len;
    return std::string(name, name + len);
}

static uint64_t parseTarOctal(const uint8_t *field, size_t len) {
    uint64_t value = 0;
    size_t i = 0;
    while (i < len && (field[i] == ' ' || field[i] == '\0'))
        ++i;
    for (; i < len && field[i] >= '0' && field[i] <= '7'; ++i)
        value = (value << 3) + static_cast<uint64_t>(field[i] - '0');
    return value;
}

static bool tarEntryMode(const std::vector<uint8_t> &tarBytes,
                         const std::string &entryName,
                         uint32_t &modeOut) {
    for (size_t off = 0; off + 512 <= tarBytes.size();) {
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (tarBytes[off + i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero)
            return false;

        const char *name = reinterpret_cast<const char *>(tarBytes.data() + off);
        size_t nameLen = 0;
        while (nameLen < 100 && name[nameLen] != '\0')
            ++nameLen;
        if (std::string(name, name + nameLen) == entryName) {
            modeOut = static_cast<uint32_t>(parseTarOctal(tarBytes.data() + off + 100, 8));
            return true;
        }

        const uint64_t size = parseTarOctal(tarBytes.data() + off + 124, 12);
        off += 512 + static_cast<size_t>(((size + 511) / 512) * 512);
    }
    return false;
}

static bool tarEntryData(const std::vector<uint8_t> &tarBytes,
                         const std::string &entryName,
                         std::vector<uint8_t> &dataOut) {
    for (size_t off = 0; off + 512 <= tarBytes.size();) {
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (tarBytes[off + i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero)
            return false;

        const char *name = reinterpret_cast<const char *>(tarBytes.data() + off);
        size_t nameLen = 0;
        while (nameLen < 100 && name[nameLen] != '\0')
            ++nameLen;
        const uint64_t size = parseTarOctal(tarBytes.data() + off + 124, 12);
        const size_t dataOff = off + 512;
        if (size > tarBytes.size() - dataOff)
            return false;
        if (std::string(name, name + nameLen) == entryName) {
            dataOut.assign(tarBytes.begin() + static_cast<std::ptrdiff_t>(dataOff),
                           tarBytes.begin() + static_cast<std::ptrdiff_t>(dataOff + size));
            return true;
        }
        off += 512 + static_cast<size_t>(((size + 511) / 512) * 512);
    }
    return false;
}

static bool tarEntryLinkTarget(const std::vector<uint8_t> &tarBytes,
                               const std::string &entryName,
                               std::string &targetOut) {
    for (size_t off = 0; off + 512 <= tarBytes.size();) {
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (tarBytes[off + i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero)
            return false;

        const char *name = reinterpret_cast<const char *>(tarBytes.data() + off);
        size_t nameLen = 0;
        while (nameLen < 100 && name[nameLen] != '\0')
            ++nameLen;
        const uint64_t size = parseTarOctal(tarBytes.data() + off + 124, 12);
        if (std::string(name, name + nameLen) == entryName && tarBytes[off + 156] == '2') {
            const char *link = reinterpret_cast<const char *>(tarBytes.data() + off + 157);
            size_t linkLen = 0;
            while (linkLen < 100 && link[linkLen] != '\0')
                ++linkLen;
            targetOut.assign(link, link + linkLen);
            return true;
        }
        off += 512 + static_cast<size_t>(((size + 511) / 512) * 512);
    }
    return false;
}

static bool arMemberData(const std::vector<uint8_t> &arBytes,
                         const std::string &memberName,
                         std::vector<uint8_t> &dataOut) {
    if (arBytes.size() < 8 || std::memcmp(arBytes.data(), "!<arch>\n", 8) != 0)
        return false;
    size_t pos = 8;
    while (pos + 60 <= arBytes.size()) {
        std::string name(reinterpret_cast<const char *>(arBytes.data() + pos), 16);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        const size_t slash = name.find('/');
        if (slash != std::string::npos)
            name = name.substr(0, slash);
        size_t size = 0;
        for (size_t i = 0; i < 10; ++i) {
            const char c = static_cast<char>(arBytes[pos + 48 + i]);
            if (c == ' ')
                break;
            if (c < '0' || c > '9')
                return false;
            size = size * 10 + static_cast<size_t>(c - '0');
        }
        const size_t dataOff = pos + 60;
        if (size > arBytes.size() - dataOff)
            return false;
        if (name == memberName) {
            dataOut.assign(arBytes.begin() + static_cast<std::ptrdiff_t>(dataOff),
                           arBytes.begin() + static_cast<std::ptrdiff_t>(dataOff + size));
            return true;
        }
        pos = dataOff + size + (size % 2);
    }
    return false;
}

static std::string debControlText(const std::vector<uint8_t> &debBytes) {
    std::vector<uint8_t> controlGz;
    if (!arMemberData(debBytes, "control.tar.gz", controlGz))
        return {};
    const auto controlTar = inflateGzipPayload(controlGz);
    std::vector<uint8_t> control;
    if (!tarEntryData(controlTar, "control", control) &&
        !tarEntryData(controlTar, "./control", control))
        return {};
    return std::string(control.begin(), control.end());
}

static std::string debControlEntryText(const std::vector<uint8_t> &debBytes,
                                       const std::string &entryName) {
    std::vector<uint8_t> controlGz;
    if (!arMemberData(debBytes, "control.tar.gz", controlGz))
        return {};
    const auto controlTar = inflateGzipPayload(controlGz);
    std::vector<uint8_t> data;
    if (!tarEntryData(controlTar, entryName, data) &&
        !tarEntryData(controlTar, "./" + entryName, data))
        return {};
    return std::string(data.begin(), data.end());
}

static std::vector<uint8_t> debDataTar(const std::vector<uint8_t> &debBytes) {
    std::vector<uint8_t> dataGz;
    if (!arMemberData(debBytes, "data.tar.gz", dataGz))
        return {};
    return inflateGzipPayload(dataGz);
}

static uint64_t peOptionalHeaderField64(const std::vector<uint8_t> &pe, uint32_t fieldOff) {
    if (pe.size() < 0x40)
        return 0;
    const uint32_t peOff = readLE32(pe.data() + 0x3c);
    const size_t optOff = static_cast<size_t>(peOff) + 4 + 20;
    if (optOff + fieldOff + 8 > pe.size())
        return 0;
    return readLE64(pe.data() + optOff + fieldOff);
}

static std::vector<uint8_t> makeTarGz(std::initializer_list<std::pair<std::string, std::string>> files) {
    TarWriter tar;
    tar.addDirectory("./", 0755);
    for (const auto &file : files)
        tar.addFileString(file.first, file.second);
    auto tarBytes = tar.finish();
    return gzip(tarBytes.data(), tarBytes.size());
}

static std::filesystem::path createMockToolchainStage(const std::filesystem::path &tmpRoot) {
    namespace fs = std::filesystem;
    const fs::path stage = tmpRoot / "stage";
    fs::create_directories(stage / "bin");
    fs::create_directories(stage / "lib" / "cmake" / "Viper");
    fs::create_directories(stage / "include" / "viper");
    fs::create_directories(stage / "share" / "man" / "man1");
    fs::create_directories(stage / "share" / "doc" / "viper");

#if defined(_WIN32)
    const std::string exeName = "viper.exe";
    auto archiveName = [](std::string_view base) { return std::string(base) + ".lib"; };
#else
    const std::string exeName = "viper";
    auto archiveName = [](std::string_view base) { return "lib" + std::string(base) + ".a"; };
#endif
    {
        const fs::path exePath = stage / "bin" / exeName;
        std::ofstream out(exePath, std::ios::binary);
        out << "stub";
        out.close();
        fs::permissions(exePath,
                        fs::perms::owner_read | fs::perms::owner_write |
                            fs::perms::owner_exec | fs::perms::group_read |
                            fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        fs::perm_options::replace);
    }
    for (const char *tool : {"zia", "vbasic", "ilrun", "il-verify", "il-dis", "zia-server",
                             "vbasic-server", "basic-ast-dump", "basic-lex-dump"}) {
        const fs::path toolPath = stage / "bin" / tool;
        std::ofstream out(toolPath, std::ios::binary);
        out << "stub";
        out.close();
        fs::permissions(toolPath,
                        fs::perms::owner_read | fs::perms::owner_write |
                            fs::perms::owner_exec | fs::perms::group_read |
                            fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        fs::perm_options::replace);
    }
    {
        std::ofstream out(stage / "lib" / "cmake" / "Viper" / "ViperConfig.cmake");
        out << "# mock\n";
    }
    {
        std::ofstream out(stage / "lib" / "cmake" / "Viper" / "ViperTargets.cmake");
        out << "# mock\n";
    }
    {
        std::ofstream out(stage / "lib" / "cmake" / "Viper" / "ViperConfigVersion.cmake");
        out << "set(PACKAGE_VERSION \"9.8.7\")\n";
    }
    {
        std::ofstream out(stage / "include" / "viper" / "version.hpp");
        out << "#define VIPER_VERSION_STR \"9.8.7\"\n";
    }
    {
        std::ofstream out(stage / "share" / "man" / "man1" / "viper.1");
        out << ".TH viper 1\n";
    }
    {
        std::ofstream out(stage / "share" / "doc" / "viper" / "README.md");
        out << "Viper\n";
    }
    {
        std::ofstream out(stage / "LICENSE");
        out << "GPL\n";
    }

    fs::create_directories(stage / "lib");
    for (std::string_view archive : viper::runtime_manifest::kRuntimeComponentArchives) {
        std::ofstream out(stage / "lib" / archiveName(archive), std::ios::binary);
        out << "ar";
    }
#if VIPER_BUILD_HAS_GRAPHICS
    {
        std::ofstream out(stage / "lib" / archiveName("vipergfx"), std::ios::binary);
        out << "gfx";
    }
#endif
#if VIPER_BUILD_HAS_GUI
    {
        std::ofstream out(stage / "lib" / archiveName("vipergui"), std::ios::binary);
        out << "gui";
    }
#endif
#if VIPER_BUILD_HAS_AUDIO
    {
        std::ofstream out(stage / "lib" / archiveName("viperaud"), std::ios::binary);
        out << "aud";
    }
#endif
    return stage;
}

// ============================================================================
// DEFLATE Tests
// ============================================================================

TEST(Deflate, RoundTripEmpty) {
    std::vector<uint8_t> input;
    auto compressed = deflate(input.data(), input.size());
    auto decompressed = inflate(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed.size(), static_cast<size_t>(0));
}

TEST(Gzip, RoundTripEmptyNullInput) {
    auto compressed = gzip(nullptr, 0);
    auto decompressed = gunzip(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed.size(), static_cast<size_t>(0));
}

TEST(Deflate, RoundTripSmall) {
    const char *msg = "Hello, VAPS packaging!";
    auto len = std::strlen(msg);
    auto compressed = deflate(reinterpret_cast<const uint8_t *>(msg), len);
    auto decompressed = inflate(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed.size(), len);
    EXPECT_TRUE(std::memcmp(decompressed.data(), msg, len) == 0);
}

TEST(Deflate, RoundTripMixedDataLevel6) {
    // Mixed data at compression level 6 (LZ77+Huffman)
    std::vector<uint8_t> input;
    for (int i = 0; i < 500; ++i)
        input.push_back(static_cast<uint8_t>(i % 256));
    auto compressed = deflate(input.data(), input.size(), 6);
    auto decompressed = inflate(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed.size(), input.size());
    EXPECT_TRUE(std::memcmp(decompressed.data(), input.data(), input.size()) == 0);
}

TEST(Deflate, RoundTripRepetitiveLevel6) {
    // Highly repetitive data — exercises LZ77 match finding
    std::vector<uint8_t> input(1000, 'A');
    auto compressed = deflate(input.data(), input.size(), 6);
    EXPECT_LT(compressed.size(), input.size()); // Should compress well
    auto decompressed = inflate(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed.size(), input.size());
    EXPECT_TRUE(std::memcmp(decompressed.data(), input.data(), input.size()) == 0);
}

TEST(Deflate, AllLevels) {
    const char *msg = "Test data for compression levels 1 through 9 !!!";
    auto len = std::strlen(msg);
    for (int level = 1; level <= 9; ++level) {
        auto compressed = deflate(reinterpret_cast<const uint8_t *>(msg), len, level);
        auto decompressed = inflate(compressed.data(), compressed.size());
        EXPECT_EQ(decompressed.size(), len);
        EXPECT_TRUE(std::memcmp(decompressed.data(), msg, len) == 0);
    }
}

TEST(Deflate, TruncatedInputThrows) {
    const char *msg = "truncated deflate stream";
    auto compressed = deflate(reinterpret_cast<const uint8_t *>(msg), std::strlen(msg));
    ASSERT_GT(compressed.size(), static_cast<size_t>(1));
    compressed.pop_back();
    EXPECT_THROWS(inflate(compressed.data(), compressed.size()), DeflateError);
}

TEST(Deflate, RejectsTrailingCompressedData) {
    const char *msg = "trailing deflate stream";
    auto compressed = deflate(reinterpret_cast<const uint8_t *>(msg), std::strlen(msg));
    compressed.push_back(0);
    EXPECT_THROWS(inflate(compressed.data(), compressed.size()), DeflateError);
}

TEST(Deflate, ExplicitOutputLimit) {
    const char *msg = "bounded inflate";
    const size_t len = std::strlen(msg);
    auto compressed = deflate(reinterpret_cast<const uint8_t *>(msg), len);
    EXPECT_THROWS(inflate(compressed.data(), compressed.size(), len - 1), DeflateError);
    auto decompressed = inflate(compressed.data(), compressed.size(), len);
    EXPECT_EQ(std::string(decompressed.begin(), decompressed.end()), std::string(msg));
}

// ============================================================================
// ZIP Tests
// ============================================================================

TEST(Zip, EmptyArchive) {
    ZipWriter zip;
    auto data = zip.finishToVector();
    // Minimal ZIP: end-of-central-directory record (22 bytes)
    EXPECT_GE(data.size(), static_cast<size_t>(22));
    // End-of-central-directory signature at the end
    size_t eocdOff = data.size() - 22;
    EXPECT_EQ(readLE32(data.data() + eocdOff), static_cast<uint32_t>(0x06054B50));
}

TEST(Zip, SingleFileLocalHeader) {
    ZipWriter zip;
    const char *content = "Hello ZIP";
    zip.addFile("test.txt", reinterpret_cast<const uint8_t *>(content), std::strlen(content));
    auto data = zip.finishToVector();

    // Local file header signature at offset 0
    EXPECT_EQ(readLE32(data.data()), static_cast<uint32_t>(0x04034B50));

    // Version needed = 20
    EXPECT_EQ(readLE16(data.data() + 4), static_cast<uint16_t>(20));

    // Filename length at offset 26
    uint16_t nameLen = readLE16(data.data() + 26);
    EXPECT_EQ(nameLen, static_cast<uint16_t>(8)); // "test.txt"

    // Verify filename
    EXPECT_TRUE(std::memcmp(data.data() + 30, "test.txt", 8) == 0);
}

TEST(Zip, DirectoryEntry) {
    ZipWriter zip;
    zip.addDirectory("mydir/");
    auto data = zip.finishToVector();

    // Local header present
    EXPECT_EQ(readLE32(data.data()), static_cast<uint32_t>(0x04034B50));

    // Filename should be "mydir/"
    uint16_t nameLen = readLE16(data.data() + 26);
    EXPECT_EQ(nameLen, static_cast<uint16_t>(6));
    EXPECT_TRUE(std::memcmp(data.data() + 30, "mydir/", 6) == 0);

    // Compressed/uncompressed size should be 0 for directory
    EXPECT_EQ(readLE32(data.data() + 18), static_cast<uint32_t>(0)); // compressed
    EXPECT_EQ(readLE32(data.data() + 22), static_cast<uint32_t>(0)); // uncompressed
}

TEST(Zip, RootDirectoryEntryIsNoop) {
    ZipWriter zip;
    zip.addDirectory("./");
    auto data = zip.finishToVector();

    ZipReader reader(data.data(), data.size());
    EXPECT_EQ(reader.entries().size(), static_cast<size_t>(0));
}

TEST(Zip, CentralDirectoryPresent) {
    ZipWriter zip;
    zip.addFileString("a.txt", "alpha");
    zip.addFileString("b.txt", "bravo");
    auto data = zip.finishToVector();

    // Find central directory headers (signature 0x02014B50)
    int centralCount = 0;
    for (size_t i = 0; i + 4 <= data.size(); ++i) {
        if (readLE32(data.data() + i) == 0x02014B50)
            centralCount++;
    }
    EXPECT_EQ(centralCount, 2);

    // End-of-central-directory should report 2 entries
    size_t eocdOff = data.size() - 22;
    EXPECT_EQ(readLE32(data.data() + eocdOff), static_cast<uint32_t>(0x06054B50));
    // Total entries on disk (offset 10 in EOCD)
    EXPECT_EQ(readLE16(data.data() + eocdOff + 10), static_cast<uint16_t>(2));
}

TEST(Zip, UnixPermissionsEncoded) {
    ZipWriter zip;
    zip.addFile("exec", reinterpret_cast<const uint8_t *>("x"), 1, 0100755);
    auto data = zip.finishToVector();

    // Find central directory header
    size_t cdOff = 0;
    for (size_t i = 0; i + 4 <= data.size(); ++i) {
        if (readLE32(data.data() + i) == 0x02014B50) {
            cdOff = i;
            break;
        }
    }
    ASSERT_TRUE(cdOff > 0);

    // version_made_by at offset 4: should be Unix (high byte = 3)
    uint16_t versionMadeBy = readLE16(data.data() + cdOff + 4);
    EXPECT_EQ(versionMadeBy >> 8, 3); // Unix

    // External attributes at offset 38: upper 16 bits = Unix mode
    uint32_t extAttrs = readLE32(data.data() + cdOff + 38);
    uint16_t unixMode = static_cast<uint16_t>(extAttrs >> 16);
    EXPECT_EQ(unixMode, static_cast<uint16_t>(0100755));
}

TEST(Zip, RejectsTooLongNames) {
    ZipWriter zip;
    std::string longName(70000, 'a');
    EXPECT_THROWS(zip.addFileString(longName, "x"), std::runtime_error);
}

TEST(Zip, WriterRejectsTraversalNames) {
    ZipWriter zip;
    EXPECT_THROWS(zip.addFileString("../escape.txt", "x"), std::runtime_error);
    EXPECT_THROWS(zip.addDirectory("/absolute"), std::runtime_error);
}

TEST(Zip, NormalizesBackslashEntryNames) {
    ZipWriter zip;
    zip.addFileString("assets\\config.txt", "x");
    auto data = zip.finishToVector();

    ZipReader reader(data.data(), data.size());
    EXPECT_TRUE(reader.find("assets/config.txt") != nullptr);
    EXPECT_TRUE(reader.find("assets\\config.txt") == nullptr);
}

TEST(Zip, WriterRejectsDuplicateEntryNamesAfterNormalization) {
    ZipWriter zip;
    zip.addFileString("assets/config.txt", "x");
    EXPECT_THROWS(zip.addFileString("assets\\config.txt", "y"), std::runtime_error);
}

TEST(Zip, SupportsZeroByteNullDataFile) {
    ZipWriter zip;
    zip.addFile("empty.bin", nullptr, 0);
    auto data = zip.finishToVector();

    ZipReader reader(data.data(), data.size());
    const auto *entry = reader.find("empty.bin");
    ASSERT_TRUE(entry != nullptr);
    auto extracted = reader.extract(*entry);
    EXPECT_EQ(extracted.size(), static_cast<size_t>(0));
}

TEST(Zip, RejectsUseAfterFinish) {
    ZipWriter zip;
    zip.addFileString("a.txt", "a");
    (void)zip.finishToVector();
    EXPECT_THROWS(zip.addFileString("b.txt", "b"), std::runtime_error);
    EXPECT_THROWS(zip.finishToVector(), std::runtime_error);
}

TEST(Zip, RejectsEscapingSymlinkTargets) {
    ZipWriter zip;
    zip.addDirectory("bin");
    EXPECT_THROWS(zip.addSymlink("bin/tool", "../../outside"), std::runtime_error);

    ZipWriter absolute;
    EXPECT_THROWS(absolute.addSymlink("bin/tool", "/usr/bin/tool"), std::runtime_error);
}

TEST(Zip, AllowsInternalSymlinkTargets) {
    ZipWriter zip;
    zip.addDirectory("bin");
    zip.addDirectory("lib");
    zip.addFileString("lib/tool-real", "x");
    zip.addSymlink("bin/tool", "../lib/tool-real");
    auto data = zip.finishToVector();

    ZipReader reader(data.data(), data.size());
    EXPECT_TRUE(reader.find("bin/tool") != nullptr);
}

TEST(Zip, StoresNormalizedSymlinkTarget) {
    ZipWriter zip;
    zip.addDirectory("bin");
    zip.addDirectory("lib");
    zip.addFileString("lib/tool-real", "x");
    zip.addSymlink("bin/tool", "..\\lib\\tool-real");
    auto data = zip.finishToVector();

    ZipReader reader(data.data(), data.size());
    const ZipEntry *entry = reader.find("bin/tool");
    ASSERT_TRUE(entry != nullptr);
    const auto target = reader.extract(*entry);
    EXPECT_EQ(std::string(target.begin(), target.end()), std::string("../lib/tool-real"));
}

// ============================================================================
// Tar Tests
// ============================================================================

TEST(Tar, EmptyArchive) {
    TarWriter tar;
    auto data = tar.finish();
    // Empty tar: just two 512-byte zero blocks = 1024 bytes
    EXPECT_EQ(data.size(), static_cast<size_t>(1024));
    // All zeros
    bool allZero = true;
    for (auto b : data)
        if (b != 0)
            allZero = false;
    EXPECT_TRUE(allZero);
}

TEST(Tar, FileHeaderMagic) {
    TarWriter tar;
    tar.addFileString("./hello.txt", "Hello");
    auto data = tar.finish();

    // USTAR magic at offset 257: "ustar\0" (6 bytes)
    EXPECT_TRUE(std::memcmp(data.data() + 257, "ustar", 5) == 0);
    EXPECT_EQ(data[257 + 5], static_cast<uint8_t>(0)); // NUL-terminated magic
    // Version at offset 263: "00"
    EXPECT_EQ(data[263], '0');
    EXPECT_EQ(data[264], '0');
}

TEST(Tar, FileNameInHeader) {
    TarWriter tar;
    tar.addFileString("./usr/bin/test", "content");
    auto data = tar.finish();

    // Name field at offset 0, 100 bytes
    EXPECT_TRUE(std::memcmp(data.data(), "usr/bin/test", 12) == 0);
}

TEST(Tar, FileModeField) {
    TarWriter tar;
    tar.addFileString("./file", "data", 0755);
    auto data = tar.finish();

    // Mode field at offset 100, 8 bytes octal
    // 0755 octal = "0000755\0"
    EXPECT_TRUE(std::memcmp(data.data() + 100, "0000755", 7) == 0);
}

TEST(Tar, DataAlignment) {
    TarWriter tar;
    // Add a file with content not a multiple of 512
    std::string content = "Short";
    tar.addFileString("./test", content);
    auto data = tar.finish();

    // Total: 512 (header) + 512 (data padded) + 1024 (end blocks) = 2048
    EXPECT_EQ(data.size(), static_cast<size_t>(2048));
}

TEST(Tar, DirectoryTypeflag) {
    TarWriter tar;
    tar.addDirectory("./mydir/", 0755);
    auto data = tar.finish();

    // Typeflag at offset 156: '5' for directory
    EXPECT_EQ(data[156], '5');
}

TEST(Tar, RejectsOversizedPath) {
    TarWriter tar;
    tar.addFileString(std::string("./") + std::string(180, 'a'), "data");
    EXPECT_THROWS(tar.finish(), std::runtime_error);
}

TEST(Tar, RejectsUnsafePath) {
    TarWriter tar;
    EXPECT_THROWS(tar.addFileString("../escape", "data"), std::runtime_error);
}

TEST(Tar, RejectsDuplicateEntryPath) {
    TarWriter tar;
    tar.addFileString("./dup.txt", "one");
    EXPECT_THROWS(tar.addFileString("dup.txt", "two"), std::runtime_error);
}

TEST(Tar, SupportsZeroByteNullDataFile) {
    TarWriter tar;
    tar.addFile("./empty", nullptr, 0);
    auto data = tar.finish();
    std::vector<uint8_t> extracted;
    EXPECT_TRUE(tarEntryData(data, "empty", extracted));
    EXPECT_EQ(extracted.size(), static_cast<size_t>(0));
}

TEST(Tar, SplitsLongDirectoryPathWithoutEmptyName) {
    TarWriter tar;
    const std::string longDir = "root/" + std::string(95, 'a') + "/leaf";
    tar.addDirectory(longDir);
    auto data = tar.finish();

    const char *name = reinterpret_cast<const char *>(data.data());
    size_t nameLen = 0;
    while (nameLen < 100 && name[nameLen] != '\0')
        ++nameLen;
    EXPECT_EQ(std::string(name, name + nameLen), std::string("leaf/"));
}

TEST(Tar, AllowsInternalRelativeSymlinkTarget) {
    TarWriter tar;
    tar.addDirectory("./bin", 0755);
    tar.addDirectory("./lib", 0755);
    tar.addFileString("./lib/tool-real", "x", 0755);
    tar.addSymlink("./bin/tool", "../lib/tool-real");
    auto data = tar.finish();

    std::ostringstream err;
    auto gz = gzip(data.data(), data.size());
    EXPECT_TRUE(verifyTarGz(gz, err));
}

TEST(Tar, StoresNormalizedSymlinkTarget) {
    TarWriter tar;
    tar.addDirectory("./bin", 0755);
    tar.addDirectory("./lib", 0755);
    tar.addFileString("./lib/tool-real", "x", 0755);
    tar.addSymlink("./bin/tool", "..\\lib\\tool-real");
    auto data = tar.finish();

    std::string target;
    ASSERT_TRUE(tarEntryLinkTarget(data, "bin/tool", target));
    EXPECT_EQ(target, std::string("../lib/tool-real"));
}

TEST(Tar, RejectsEscapingSymlinkTarget) {
    TarWriter tar;
    EXPECT_THROWS(tar.addSymlink("./bin/tool", "../../outside"), std::runtime_error);
}

TEST(Tar, VerifierRejectsDuplicateEntryPath) {
    TarWriter tar;
    tar.addFileString("./dup.txt", "x");
    auto one = tar.finish();
    ASSERT_GE(one.size(), static_cast<size_t>(2048));

    std::vector<uint8_t> dup;
    dup.insert(dup.end(), one.begin(), one.begin() + 1024);
    dup.insert(dup.end(), one.begin(), one.begin() + 1024);
    dup.insert(dup.end(), one.end() - 1024, one.end());
    auto gz = gzip(dup.data(), dup.size());

    std::ostringstream err;
    EXPECT_FALSE(verifyTarGz(gz, err));
    EXPECT_CONTAINS(err.str(), "duplicate entry");
}

// ============================================================================
// Ar Tests
// ============================================================================

TEST(Ar, GlobalMagic) {
    ArWriter ar;
    ar.addMemberString("test", "data");
    auto data = ar.finish();

    // ar magic: "!<arch>\n" (8 bytes)
    EXPECT_GE(data.size(), static_cast<size_t>(8));
    EXPECT_TRUE(std::memcmp(data.data(), "!<arch>\n", 8) == 0);
}

TEST(Ar, MemberHeader) {
    ArWriter ar;
    ar.addMemberString("hello", "world");
    auto data = ar.finish();

    // Member header starts at offset 8, 60 bytes
    // Name field: "hello/          " (16 bytes, "/" terminated, space padded)
    EXPECT_TRUE(std::memcmp(data.data() + 8, "hello/", 6) == 0);

    // File magic at end of header (offset 8+58): "`\n"
    EXPECT_EQ(data[8 + 58], '`');
    EXPECT_EQ(data[8 + 59], '\n');
}

TEST(Ar, DebianBinaryOrdering) {
    // .deb files must have debian-binary as first member
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberString("control.tar.gz", "ctrl");
    ar.addMemberString("data.tar.gz", "data");
    auto data = ar.finish();

    // First member name: "debian-binary"
    EXPECT_TRUE(std::memcmp(data.data() + 8, "debian-binary/", 14) == 0);
}

TEST(Ar, RejectsTooLongMemberName) {
    ArWriter ar;
    ar.addMemberString("this-name-is-far-too-long", "data");
    EXPECT_THROWS(ar.finish(), std::runtime_error);
}

TEST(Ar, SupportsZeroByteNullDataMember) {
    ArWriter ar;
    ar.addMember("empty", nullptr, 0);
    auto data = ar.finish();

    std::vector<uint8_t> member;
    ASSERT_TRUE(arMemberData(data, "empty", member));
    EXPECT_EQ(member.size(), static_cast<size_t>(0));
}

TEST(Ar, RejectsNonEmptyNullDataMember) {
    ArWriter ar;
    EXPECT_THROWS(ar.addMember("bad", nullptr, 1), std::runtime_error);
}

// ============================================================================
// PE Tests
// ============================================================================

TEST(PE, DosHeaderMagic) {
    PEBuildParams params;
    params.textSection = {0xC3}; // ret
    auto pe = buildPE(params);

    // DOS header: "MZ" at offset 0
    EXPECT_GE(pe.size(), static_cast<size_t>(2));
    EXPECT_EQ(pe[0], 'M');
    EXPECT_EQ(pe[1], 'Z');
}

TEST(PE, PESignature) {
    PEBuildParams params;
    params.textSection = {0xC3};
    auto pe = buildPE(params);

    // PE signature at offset 0x80: "PE\0\0"
    ASSERT_GE(pe.size(), static_cast<size_t>(0x84));
    EXPECT_EQ(pe[0x80], 'P');
    EXPECT_EQ(pe[0x81], 'E');
    EXPECT_EQ(pe[0x82], 0);
    EXPECT_EQ(pe[0x83], 0);
}

TEST(PE, MachineAMD64) {
    PEBuildParams params;
    params.textSection = {0xC3};
    params.arch = "x64";
    auto pe = buildPE(params);

    // COFF Machine at offset 0x84: 0x8664 = AMD64
    uint16_t machine = readLE16(pe.data() + 0x84);
    EXPECT_EQ(machine, static_cast<uint16_t>(0x8664));
}

TEST(PE, MachineARM64) {
    PEBuildParams params;
    params.textSection = {0xC0, 0x03, 0x5F, 0xD6}; // ARM64 ret
    params.arch = "arm64";
    auto pe = buildPE(params);

    // COFF Machine at offset 0x84: 0xAA64 = ARM64
    uint16_t machine = readLE16(pe.data() + 0x84);
    EXPECT_EQ(machine, static_cast<uint16_t>(0xAA64));
}

TEST(PE, RejectsEmptyTextSection) {
    PEBuildParams params;
    EXPECT_THROWS(buildPE(params), std::runtime_error);
}

TEST(PE, RejectsEntryPointOutsideTextSection) {
    PEBuildParams params;
    params.textSection = {0x90, 0xC3};
    params.entryPointOffset = 2;
    EXPECT_THROWS(buildPE(params), std::runtime_error);
}

TEST(PE, OptionalHeaderMagic) {
    PEBuildParams params;
    params.textSection = {0xC3};
    auto pe = buildPE(params);

    // Optional header at offset 0x98: PE32+ magic = 0x020B
    uint16_t magic = readLE16(pe.data() + 0x98);
    EXPECT_EQ(magic, static_cast<uint16_t>(0x020B));
}

TEST(PE, OverlayAppended) {
    PEBuildParams params;
    params.textSection = {0xC3};
    std::vector<uint8_t> overlay = {'O', 'V', 'L', 'Y'};
    params.overlay = overlay;
    auto pe = buildPE(params);

    // Overlay should be at the very end of the PE
    ASSERT_GE(pe.size(), static_cast<size_t>(4));
    EXPECT_EQ(pe[pe.size() - 4], 'O');
    EXPECT_EQ(pe[pe.size() - 3], 'V');
    EXPECT_EQ(pe[pe.size() - 2], 'L');
    EXPECT_EQ(pe[pe.size() - 1], 'Y');
}

TEST(PE, DefaultDllCharacteristicsEnableRelocationAslr) {
    PEBuildParams params;
    params.textSection = {0xC3};
    auto pe = buildPE(params);

    const uint16_t dllChars = readLE16(pe.data() + 0x98 + 70);
    EXPECT_EQ(dllChars, static_cast<uint16_t>(0x8160));
    EXPECT_TRUE((dllChars & 0x0040) != 0); // DYNAMIC_BASE
    EXPECT_TRUE((dllChars & 0x0020) != 0); // HIGH_ENTROPY_VA
    EXPECT_TRUE((dllChars & 0x0100) != 0); // NX_COMPAT

    const uint32_t relocRva = readLE32(pe.data() + 0x98 + 112 + 5 * 8);
    const uint32_t relocSize = readLE32(pe.data() + 0x98 + 112 + 5 * 8 + 4);
    EXPECT_NE(relocRva, static_cast<uint32_t>(0));
    EXPECT_EQ(relocSize, static_cast<uint32_t>(12));
}

// ============================================================================
// LNK Tests
// ============================================================================

TEST(Lnk, HeaderSize) {
    LnkParams params;
    params.targetPath = "C:\\test.exe";
    params.workingDir = "C:\\";
    params.description = "Test";
    auto data = generateLnk(params);

    // ShellLinkHeader is always 76 bytes
    ASSERT_GE(data.size(), static_cast<size_t>(76));
    // HeaderSize field at offset 0: 0x0000004C (76)
    EXPECT_EQ(readLE32(data.data()), static_cast<uint32_t>(0x4C));
}

TEST(Lnk, LinkCLSID) {
    LnkParams params;
    params.targetPath = "C:\\test.exe";
    params.workingDir = "C:\\";
    params.description = "Test";
    auto data = generateLnk(params);

    // LinkCLSID at offset 4: {00021401-0000-0000-C000-000000000046}
    // In little-endian binary: 01 14 02 00 00 00 00 00 C0 00 00 00 00 00 00 46
    EXPECT_EQ(data[4], 0x01);
    EXPECT_EQ(data[5], 0x14);
    EXPECT_EQ(data[6], 0x02);
    EXPECT_EQ(data[7], 0x00);
    EXPECT_EQ(data[19], 0x46);
}

TEST(Lnk, HasLinkInfoFlag) {
    LnkParams params;
    params.targetPath = "C:\\Program Files\\App\\app.exe";
    params.workingDir = "C:\\Program Files\\App";
    params.description = "My App";
    auto data = generateLnk(params);

    // LinkFlags at offset 20: should have HasLinkInfo (bit 1 = 0x02)
    uint32_t flags = readLE32(data.data() + 20);
    EXPECT_TRUE((flags & 0x02) != 0); // HasLinkInfo
    EXPECT_TRUE((flags & 0x04) != 0); // HasName
    EXPECT_TRUE((flags & 0x08) != 0); // HasRelativePath
    EXPECT_TRUE((flags & 0x80) != 0); // IsUnicode
}

TEST(Lnk, EnvironmentTargetAddsExpStringBlock) {
    LnkParams params;
    params.targetPath = "%ProgramFiles%\\Test App\\testapp.exe";
    params.workingDir = "%ProgramFiles%\\Test App";
    params.description = "Test App";
    auto data = generateLnk(params);

    const uint32_t flags = readLE32(data.data() + 20);
    EXPECT_TRUE((flags & 0x00000200) != 0); // HasExpString
    bool foundBlock = false;
    for (size_t i = 0; i + 8 <= data.size(); ++i) {
        if (readLE32(data.data() + i) == 0x00000314 &&
            readLE32(data.data() + i + 4) == 0xA0000001) {
            foundBlock = true;
            break;
        }
    }
    EXPECT_TRUE(foundBlock);
}

TEST(Lnk, LinkInfoContainsLocalBasePath) {
    LnkParams params;
    params.targetPath = "C:\\MyApp\\app.exe";
    params.workingDir = "C:\\MyApp";
    params.description = "Test";
    auto data = generateLnk(params);

    // LinkInfo starts right after the 76-byte header
    // LinkInfoSize at offset 76
    ASSERT_GE(data.size(), static_cast<size_t>(80));
    uint32_t liSize = readLE32(data.data() + 76);
    EXPECT_GT(liSize, static_cast<uint32_t>(28)); // At least header + data

    // LinkInfoFlags at offset 76+8: should have VolumeIDAndLocalBasePath (bit 0)
    uint32_t liFlags = readLE32(data.data() + 76 + 8);
    EXPECT_TRUE((liFlags & 0x01) != 0);

    // LocalBasePath should contain the target path somewhere in the LinkInfo
    // LocalBasePathOffset at offset 76+16
    uint32_t lbpOffset = readLE32(data.data() + 76 + 16);
    ASSERT_LT(static_cast<size_t>(76 + lbpOffset + 15), data.size());
    // Check that it starts with "C:\MyApp\app.exe"
    std::string lbp(reinterpret_cast<const char *>(data.data() + 76 + lbpOffset));
    EXPECT_CONTAINS(lbp, "C:\\MyApp\\app.exe");
}

TEST(Lnk, Utf8StringDataUsesUtf16) {
    LnkParams params;
    params.targetPath = "C:\\Program Files\\Cafe\\cafe.exe";
    params.workingDir = "C:\\Program Files\\Cafe";
    params.description = "Cafe \xC3\xA9";
    auto data = generateLnk(params);

    bool foundEAcute = false;
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0xE9 && data[i + 1] == 0x00) {
            foundEAcute = true;
            break;
        }
    }
    EXPECT_TRUE(foundEAcute);
}

TEST(Lnk, EndsWithExtraDataTerminalBlock) {
    LnkParams params;
    params.targetPath = "C:\\test.exe";
    auto data = generateLnk(params);
    ASSERT_GE(data.size(), static_cast<size_t>(4));
    EXPECT_EQ(readLE32(data.data() + data.size() - 4), static_cast<uint32_t>(0));
}

// ============================================================================
// Icon Tests (DEFLATE double-free fixed — full tests now enabled)
// ============================================================================

TEST(Icon, ImageResizeBasic) {
    PkgImage img;
    img.width = 64;
    img.height = 64;
    img.pixels.resize(64 * 64 * 4, 128);

    auto resized = imageResize(img, 32, 32);
    EXPECT_EQ(resized.width, static_cast<uint32_t>(32));
    EXPECT_EQ(resized.height, static_cast<uint32_t>(32));
    EXPECT_EQ(resized.pixels.size(), static_cast<size_t>(32 * 32 * 4));
}

TEST(Icon, IcnsMagic) {
    PkgImage img;
    img.width = 32;
    img.height = 32;
    img.pixels.resize(32 * 32 * 4, 128);

    auto icns = generateIcns(img);
    ASSERT_GE(icns.size(), static_cast<size_t>(8));

    // ICNS magic: "icns"
    EXPECT_EQ(icns[0], 'i');
    EXPECT_EQ(icns[1], 'c');
    EXPECT_EQ(icns[2], 'n');
    EXPECT_EQ(icns[3], 's');

    // Total size field (big-endian) at offset 4
    uint32_t totalSize = readBE32(icns.data() + 4);
    EXPECT_EQ(totalSize, static_cast<uint32_t>(icns.size()));
}

TEST(Icon, IcoHeader) {
    PkgImage img;
    img.width = 32;
    img.height = 32;
    img.pixels.resize(32 * 32 * 4, 200);

    auto ico = generateIco(img);
    ASSERT_GE(ico.size(), static_cast<size_t>(6));

    // ICO header: Reserved=0, Type=1 (ICO), Count>=1
    EXPECT_EQ(readLE16(ico.data()), static_cast<uint16_t>(0));     // Reserved
    EXPECT_EQ(readLE16(ico.data() + 2), static_cast<uint16_t>(1)); // Type = ICO
    uint16_t count = readLE16(ico.data() + 4);
    EXPECT_GE(count, static_cast<uint16_t>(1));
}

TEST(Icon, IcoEntryBitCount) {
    PkgImage img;
    img.width = 64;
    img.height = 64;
    img.pixels.resize(64 * 64 * 4, 100);

    auto ico = generateIco(img);
    uint16_t count = readLE16(ico.data() + 4);
    ASSERT_GE(count, static_cast<uint16_t>(1));

    // First ICONDIRENTRY at offset 6, BitCount at +6: should be 32 (RGBA)
    uint16_t bitCount = readLE16(ico.data() + 6 + 6);
    EXPECT_EQ(bitCount, static_cast<uint16_t>(32));
}

TEST(Icon, SmallSourceStillProducesStandardEntries) {
    PkgImage img;
    img.width = 1;
    img.height = 1;
    img.pixels.resize(4, 255);

    auto ico = generateIco(img);
    ASSERT_GE(ico.size(), static_cast<size_t>(6));
    EXPECT_EQ(readLE16(ico.data() + 4), static_cast<uint16_t>(7));

    auto icns = generateIcns(img);
    EXPECT_GT(icns.size(), static_cast<size_t>(8));

    auto pngs = generateMultiSizePngs(img);
    EXPECT_EQ(pngs.size(), static_cast<size_t>(5));
}

TEST(PNG, RejectsBadChunkCrc) {
    PkgImage img;
    img.width = 1;
    img.height = 1;
    img.pixels = {255, 0, 0, 255};
    auto png = pngEncode(img);
    ASSERT_GT(png.size(), static_cast<size_t>(32));
    png[29] ^= 0xFF; // IHDR CRC
    EXPECT_THROWS(pngReadMemory(png.data(), png.size()), PNGError);
}

TEST(PNG, RejectsNullInputBuffer) {
    EXPECT_THROWS(pngReadMemory(nullptr, 8), PNGError);
}

TEST(PNG, RejectsMismatchedPixelBuffer) {
    PkgImage img;
    img.width = 2;
    img.height = 2;
    img.pixels.resize(4);
    EXPECT_THROWS(pngEncode(img), PNGError);
}

TEST(PNG, RejectsTrailingBytesAfterIEND) {
    PkgImage img;
    img.width = 1;
    img.height = 1;
    img.pixels = {0, 0, 0, 255};
    auto png = pngEncode(img);
    png.push_back(0);
    EXPECT_THROWS(pngReadMemory(png.data(), png.size()), PNGError);
}

TEST(PNG, ResizeRejectsOverflowDimensions) {
    PkgImage img;
    img.width = 1;
    img.height = 1;
    img.pixels = {0, 0, 0, 255};
    EXPECT_THROWS(imageResize(img, 0xFFFFFFFFu, 2), PNGError);
}

TEST(PNG, ReadsIndexedPaletteTransparency) {
    const auto png = makeIndexedPng2x1WithTransparency();
    const auto img = pngReadMemory(png.data(), png.size());
    ASSERT_EQ(img.width, static_cast<uint32_t>(2));
    ASSERT_EQ(img.height, static_cast<uint32_t>(1));
    ASSERT_EQ(img.pixels.size(), static_cast<size_t>(8));
    EXPECT_EQ(img.pixels[0], static_cast<uint8_t>(255));
    EXPECT_EQ(img.pixels[1], static_cast<uint8_t>(0));
    EXPECT_EQ(img.pixels[2], static_cast<uint8_t>(0));
    EXPECT_EQ(img.pixels[3], static_cast<uint8_t>(255));
    EXPECT_EQ(img.pixels[4], static_cast<uint8_t>(0));
    EXPECT_EQ(img.pixels[5], static_cast<uint8_t>(0));
    EXPECT_EQ(img.pixels[6], static_cast<uint8_t>(255));
    EXPECT_EQ(img.pixels[7], static_cast<uint8_t>(0));
}

// ============================================================================
// Plist Tests
// ============================================================================

TEST(Plist, ContainsBundleIdentifier) {
    PlistParams params;
    params.executableName = "myapp";
    params.bundleId = "com.example.myapp";
    params.bundleName = "MyApp";
    params.version = "1.0.0";

    auto xml = generatePlist(params);
    EXPECT_CONTAINS(xml, "CFBundleIdentifier");
    EXPECT_CONTAINS(xml, "com.example.myapp");
}

TEST(Plist, ContainsBundleExecutable) {
    PlistParams params;
    params.executableName = "testbin";
    params.bundleId = "com.test";
    params.bundleName = "Test";
    params.version = "2.0";

    auto xml = generatePlist(params);
    EXPECT_CONTAINS(xml, "CFBundleExecutable");
    EXPECT_CONTAINS(xml, "testbin");
}

TEST(Plist, ContainsVersion) {
    PlistParams params;
    params.executableName = "app";
    params.bundleId = "com.test";
    params.bundleName = "App";
    params.version = "3.14.159";

    auto xml = generatePlist(params);
    EXPECT_CONTAINS(xml, "CFBundleVersion");
    EXPECT_CONTAINS(xml, "3.14.159");
}

TEST(Plist, PkgInfoContent) {
    auto pkgInfo = generatePkgInfo();
    EXPECT_EQ(pkgInfo, std::string("APPL????"));
}

// ============================================================================
// Desktop Entry Tests
// ============================================================================

TEST(DesktopEntry, ContainsDesktopEntrySection) {
    DesktopEntryParams params;
    params.name = "MyApp";
    params.execPath = "/usr/bin/myapp";
    params.iconName = "myapp";

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "[Desktop Entry]");
    EXPECT_CONTAINS(content, "Type=Application");
}

TEST(DesktopEntry, ContainsExecAndName) {
    DesktopEntryParams params;
    params.name = "Test App";
    params.execPath = "/usr/bin/testapp";
    params.iconName = "testapp";
    params.comment = "A test application";

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "Name=Test App");
    EXPECT_CONTAINS(content, "Exec=/usr/bin/testapp");
    EXPECT_CONTAINS(content, "Icon=testapp");
    EXPECT_CONTAINS(content, "Comment=A test application");
}

TEST(DesktopEntry, MimeTypeField) {
    DesktopEntryParams params;
    params.name = "Editor";
    params.execPath = "/usr/bin/editor";
    params.iconName = "editor";
    FileAssoc assoc;
    assoc.extension = ".zia";
    assoc.description = "Zia Source";
    assoc.mimeType = "text/x-zia";
    params.fileAssociations.push_back(assoc);

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "MimeType=text/x-zia;");
}

TEST(DesktopEntry, FileAssociationExecAcceptsFileArgument) {
    DesktopEntryParams params;
    params.name = "Editor";
    params.execPath = "/usr/bin/editor";
    params.iconName = "editor";
    FileAssoc assoc;
    assoc.extension = ".zia";
    assoc.description = "Zia Source";
    assoc.mimeType = "text/x-zia";
    params.fileAssociations.push_back(assoc);

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "Exec=/usr/bin/editor %f");
}

TEST(DesktopEntry, CategoryField) {
    DesktopEntryParams params;
    params.name = "Game";
    params.execPath = "/usr/bin/game";
    params.iconName = "game";
    params.categories = "Game;ArcadeGame;";

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "Categories=Game;ArcadeGame;");
}

TEST(DesktopEntry, NormalizesSingleCategoryWithTerminatingSemicolon) {
    DesktopEntryParams params;
    params.name = "Game";
    params.execPath = "/usr/bin/game";
    params.iconName = "game";
    params.categories = "Game";

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "Categories=Game;");
}

TEST(DesktopEntry, RejectsUnknownCategory) {
    DesktopEntryParams params;
    params.name = "Mystery";
    params.execPath = "/usr/bin/mystery";
    params.iconName = "mystery";
    params.categories = "DefinitelyNotARegisteredCategory";
    EXPECT_THROWS(generateDesktopEntry(params), std::runtime_error);
}

TEST(DesktopEntry, RejectsInvalidCategoryInjection) {
    DesktopEntryParams params;
    params.name = "Game";
    params.execPath = "/usr/bin/game";
    params.iconName = "game";
    params.categories = "Game;\nExec=bad";
    EXPECT_THROWS(generateDesktopEntry(params), std::runtime_error);
}

TEST(DesktopEntry, RejectsLineBreakInjection) {
    DesktopEntryParams params;
    params.name = "Bad\nName";
    params.execPath = "/usr/bin/bad";
    params.iconName = "bad";
    EXPECT_THROWS(generateDesktopEntry(params), std::runtime_error);
}

TEST(DesktopEntry, SupportsHiddenMimeHandlerEntry) {
    DesktopEntryParams params;
    params.name = "Editor";
    params.execPath = "/usr/bin/editor";
    params.iconName = "editor";
    params.noDisplay = true;
    params.fileAssociations.push_back({".zia", "Zia Source", "text/x-zia", ""});

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "NoDisplay=true");
    EXPECT_CONTAINS(content, "MimeType=text/x-zia;");
    EXPECT_CONTAINS(content, "Exec=/usr/bin/editor %f");
}

// ============================================================================
// MIME Type XML Tests
// ============================================================================

TEST(MimeXml, ContainsGlobPattern) {
    std::vector<FileAssoc> assocs;
    assocs.push_back({".zia", "Zia Source File", "text/x-zia", ""});

    auto xml = generateMimeTypeXml("mypackage", assocs);
    EXPECT_CONTAINS(xml, "text/x-zia");
    EXPECT_CONTAINS(xml, "*.zia");
}

TEST(MimeXml, EscapesXmlFields) {
    std::vector<FileAssoc> assocs;
    assocs.push_back({".vp", "A & B <C>", "application/x-viper", ""});

    auto xml = generateMimeTypeXml("mypackage", assocs);
    EXPECT_CONTAINS(xml, "A &amp; B &lt;C&gt;");
}

// ============================================================================
// Verification Tests
// ============================================================================

TEST(Verify, ZipValid) {
    ZipWriter zip;
    zip.addFileString("hello.txt", "Hello");
    auto data = zip.finishToVector();

    std::ostringstream err;
    EXPECT_TRUE(verifyZip(data, err));
}

TEST(Verify, ZipInvalidTooSmall) {
    std::vector<uint8_t> data = {0x50, 0x4B}; // Just "PK" — too small
    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "too small");
}

TEST(Verify, ZipRejectsZip64Sentinel) {
    std::vector<uint8_t> data(22, 0);
    data[0] = 0x50;
    data[1] = 0x4B;
    data[2] = 0x05;
    data[3] = 0x06;
    data[8] = 0xFF;
    data[9] = 0xFF;
    data[10] = 0xFF;
    data[11] = 0xFF;
    data[12] = 0xFF;
    data[13] = 0xFF;
    data[14] = 0xFF;
    data[15] = 0xFF;
    data[16] = 0xFF;
    data[17] = 0xFF;
    data[18] = 0xFF;
    data[19] = 0xFF;

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "ZIP64");
}

TEST(Verify, ZipRejectsTraversalEntry) {
    ZipWriter zip;
    zip.addFileString("safe.txt", "Hello");
    auto data = zip.finishToVector();

    const std::string from = "safe.txt";
    const std::string to = "../x.txt";
    ASSERT_EQ(from.size(), to.size());
    for (size_t i = 0; i + from.size() <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, from.data(), from.size()) == 0)
            std::memcpy(data.data() + i, to.data(), to.size());
    }

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "unsafe entry path");
}

TEST(Verify, ZipRejectsCrcMismatch) {
    ZipWriter zip;
    zip.addFileString("hello.txt", "Hello");
    auto data = zip.finishToVector();

    const uint16_t nameLen = readLE16(data.data() + 26);
    const size_t dataOff = 30 + nameLen;
    ASSERT_LT(dataOff, data.size());
    data[dataOff] ^= 0xFF;

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "CRC");
}

TEST(Verify, ZipRejectsDuplicateNormalizedEntryPath) {
    ZipWriter zip;
    zip.addDirectory("foo/");
    zip.addFileString("bar", "x");
    auto data = zip.finishToVector();

    const std::string from = "bar";
    const std::string to = "foo";
    for (size_t i = 0; i + from.size() <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, from.data(), from.size()) == 0)
            std::memcpy(data.data() + i, to.data(), to.size());
    }

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "duplicate normalized entry");
}

TEST(Verify, ZipRejectsLocalHeaderSizeMismatch) {
    ZipWriter zip;
    zip.addFileString("hello.txt", "Hello");
    auto data = zip.finishToVector();

    ASSERT_GE(data.size(), static_cast<size_t>(26));
    data[22] = 6; // local uncompressed size, central directory still says 5
    data[23] = 0;
    data[24] = 0;
    data[25] = 0;

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(data, err));
    EXPECT_CONTAINS(err.str(), "local sizes");
}

TEST(Verify, MacOSAppZipRequiresBundlePayload) {
    ZipWriter zip;
    zip.addFileString("App.app/Contents/Info.plist", "<plist/>");
    zip.addFileString("App.app/Contents/PkgInfo", "APPL????");
    zip.addFileString("App.app/Contents/MacOS/app", "bin");
    auto data = zip.finishToVector();

    std::ostringstream err;
    EXPECT_TRUE(verifyMacOSAppZip(data, "App.app", "app", err));
    std::ostringstream missingErr;
    EXPECT_FALSE(verifyMacOSAppZip(data, "App.app", "missing", missingErr));
}

TEST(Verify, DebValid) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("control.tar.gz", makeTarGz({{"./control", "Package: test\n"}}));
    ar.addMemberVec("data.tar.gz", makeTarGz({{"./usr/bin/test", "data"}}));
    auto data = ar.finish();

    std::ostringstream err;
    const bool ok = verifyDeb(data, err);
    if (!ok)
        std::cerr << err.str();
    EXPECT_TRUE(ok);
}

TEST(Verify, DebPayloadRequiresExpectedPath) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("control.tar.gz", makeTarGz({{"./control", "Package: test\n"}}));
    ar.addMemberVec("data.tar.gz", makeTarGz({{"./usr/bin/test", "data"}}));
    auto data = ar.finish();

    std::ostringstream err;
    EXPECT_TRUE(verifyDebPayload(data, {"usr/bin/test"}, err));
    std::ostringstream missingErr;
    EXPECT_FALSE(verifyDebPayload(data, {"usr/bin/missing"}, missingErr));
    EXPECT_CONTAINS(missingErr.str(), "missing required payload path");
}

TEST(Verify, DebRejectsMisorderedMembers) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("data.tar.gz", makeTarGz({{"./usr/bin/test", "data"}}));
    ar.addMemberVec("control.tar.gz", makeTarGz({{"./control", "Package: test\n"}}));
    auto data = ar.finish();

    std::ostringstream err;
    EXPECT_FALSE(verifyDeb(data, err));
    EXPECT_CONTAINS(err.str(), "expected 'control.tar.gz'");
}

TEST(Verify, DebRejectsExtraMembers) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("control.tar.gz", makeTarGz({{"./control", "Package: test\n"}}));
    ar.addMemberVec("data.tar.gz", makeTarGz({{"./usr/bin/test", "data"}}));
    ar.addMemberString("extra", "unexpected");
    auto data = ar.finish();

    std::ostringstream err;
    EXPECT_FALSE(verifyDeb(data, err));
    EXPECT_CONTAINS(err.str(), "unexpected extra ar member");
}

TEST(Verify, DebRejectsUngzippedMembers) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberString("control.tar.gz", "ctrl");
    ar.addMemberString("data.tar.gz", "data");
    auto data = ar.finish();

    std::ostringstream err;
    EXPECT_FALSE(verifyDeb(data, err));
    EXPECT_CONTAINS(err.str(), "compressed tar");
}

TEST(Verify, TarGzValid) {
    auto tarGz = makeTarGz({{"./hello.txt", "hello"}});
    std::ostringstream err;
    EXPECT_TRUE(verifyTarGz(tarGz, err));
}

TEST(Verify, TarGzPayloadRequiresExpectedPath) {
    auto tarGz = makeTarGz({{"./hello.txt", "hello"}});
    std::ostringstream err;
    EXPECT_TRUE(verifyTarGzPayload(tarGz, {"hello.txt"}, err));
    std::ostringstream missingErr;
    EXPECT_FALSE(verifyTarGzPayload(tarGz, {"missing.txt"}, missingErr));
}

TEST(Verify, DebMissingMagic) {
    std::vector<uint8_t> data = {0, 0, 0, 0, 0, 0, 0, 0};
    std::ostringstream err;
    EXPECT_FALSE(verifyDeb(data, err));
    EXPECT_CONTAINS(err.str(), "ar magic");
}

TEST(Verify, PEValid) {
    PEBuildParams params;
    params.textSection = {0xC3};
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(pe, err));
}

TEST(Verify, PEInvalidMagic) {
    std::vector<uint8_t> data(200, 0);
    data[0] = 'X'; // Not "MZ"
    std::ostringstream err;
    EXPECT_FALSE(verifyPE(data, err));
    EXPECT_CONTAINS(err.str(), "MZ");
}

TEST(Verify, PERejectsOverflowingELfanew) {
    std::vector<uint8_t> data(128, 0);
    data[0] = 'M';
    data[1] = 'Z';
    data[60] = 0xFF;
    data[61] = 0xFF;
    data[62] = 0xFF;
    data[63] = 0xFF;

    std::ostringstream err;
    EXPECT_FALSE(verifyPE(data, err));
    EXPECT_CONTAINS(err.str(), "points past end");
}

TEST(Verify, PEZipOverlayValid) {
    ZipWriter zip;
    zip.addFileString("hello.txt", "Hello");

    PEBuildParams params;
    params.textSection = {0xC3};
    params.overlay = zip.finishToVector();
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlay(pe, err));
}

TEST(Verify, PEZipOverlayIgnoresAuthenticodeCertificateTable) {
    ZipWriter zip;
    zip.addFileString("hello.txt", "Hello");

    PEBuildParams params;
    params.textSection = {0xC3};
    params.overlay = zip.finishToVector();
    auto pe = buildPE(params);

    const uint32_t certOff = static_cast<uint32_t>(pe.size());
    const uint32_t certSize = 8;
    pe.push_back(8);
    pe.push_back(0);
    pe.push_back(0);
    pe.push_back(0);
    pe.push_back(0);
    pe.push_back(2);
    pe.push_back(2);
    pe.push_back(0);

    const size_t peOff = readLE32(pe.data() + 60);
    const size_t certDirOff = peOff + 4 + 20 + 112 + 4 * 8;
    ASSERT_LT(certDirOff + 8, pe.size());
    writeLE32(pe.data() + certDirOff, certOff);
    writeLE32(pe.data() + certDirOff + 4, certSize);

    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlay(pe, err));
}

TEST(Verify, PEZipOverlayPayloadRequiresExpectedEntry) {
    ZipWriter zip;
    zip.addFileString("app/app.exe", "MZ");
    zip.addFileString("app/uninstall.exe", "MZ");

    PEBuildParams params;
    params.textSection = {0xC3};
    params.overlay = zip.finishToVector();
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlayPayload(pe, {"app/app.exe", "app/uninstall.exe"}, err));
    std::ostringstream missingErr;
    EXPECT_FALSE(verifyPEZipOverlayPayload(pe, {"app/missing.exe"}, missingErr));
}

TEST(Verify, ZipSha256ManifestAllowsDirectoryEntries) {
    ZipWriter zip;
    zip.addDirectory("app/");
    zip.addFileString("app/app.exe", "Hello");
    zip.addFileString(
        "meta/manifest.sha256",
        "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969  app/app.exe\n");

    std::ostringstream err;
    EXPECT_TRUE(verifyZip(zip.finishToVector(), err));
}

TEST(Verify, ZipSha256ManifestRejectsUncoveredFiles) {
    ZipWriter zip;
    zip.addFileString("covered.txt", "Hello");
    zip.addFileString("orphan.txt", "not listed");
    zip.addFileString(
        "meta/manifest.sha256",
        "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969  covered.txt\n");

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(zip.finishToVector(), err));
    EXPECT_CONTAINS(err.str(), "does not cover entry");
}

TEST(Verify, ZipSha256ManifestRejectsDuplicateEntries) {
    ZipWriter zip;
    zip.addFileString("covered.txt", "Hello");
    zip.addFileString(
        "meta/manifest.sha256",
        "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969  covered.txt\n"
        "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969  covered.txt\n");

    std::ostringstream err;
    EXPECT_FALSE(verifyZip(zip.finishToVector(), err));
    EXPECT_CONTAINS(err.str(), "duplicate entry");
}

TEST(Verify, PEZipOverlayMissing) {
    PEBuildParams params;
    params.textSection = {0xC3};
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_FALSE(verifyPEZipOverlay(pe, err));
    EXPECT_CONTAINS(err.str(), "overlay");
}

TEST(PE, ImportsAndCustomRdataCoexist) {
    PEBuildParams params;
    params.textSection = {0xC3};
    params.imports = {{"kernel32.dll", {"ExitProcess"}}};
    params.rdataSection = {'V', 'I', 'P', 'E', 'R'};
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(pe, err));

    bool foundMarker = false;
    for (size_t i = 0; i + 5 <= pe.size(); ++i) {
        if (std::memcmp(pe.data() + i, "VIPER", 5) == 0) {
            foundMarker = true;
            break;
        }
    }
    EXPECT_TRUE(foundMarker);
}

TEST(PE, EmbedsVersionInfoResource) {
    PEBuildParams params;
    params.textSection = {0xC3};
    params.versionInfo.enabled = true;
    params.versionInfo.fileVersion = {1, 2, 3, 4};
    params.versionInfo.productVersion = {1, 2, 3, 4};
    params.versionInfo.companyName = "Viper Project";
    params.versionInfo.fileDescription = "Versioned Test Stub";
    params.versionInfo.fileVersionText = "1.2.3.4";
    params.versionInfo.internalName = "versioned.exe";
    params.versionInfo.originalFilename = "versioned.exe";
    params.versionInfo.productName = "Versioned Test";
    params.versionInfo.productVersionText = "1.2.3.4";
    auto pe = buildPE(params);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(pe, err));
    EXPECT_TRUE(containsUtf16LE(pe, "VS_VERSION_INFO"));
    EXPECT_TRUE(containsUtf16LE(pe, "FileDescription"));
    EXPECT_TRUE(containsUtf16LE(pe, "Versioned Test Stub"));
}

TEST(PackageUtils, SanitizesRelativePaths) {
    EXPECT_EQ(sanitizePackageRelativePath("themes\\\\dark"), std::string("themes/dark"));
    EXPECT_EQ(joinPackageRelativePath("usr/share/app", "themes/dark"),
              std::string("usr/share/app/themes/dark"));
}

TEST(PackageUtils, RejectsArchiveEscapes) {
    EXPECT_THROWS(sanitizePackageRelativePath("../escape"), std::runtime_error);
    EXPECT_THROWS(sanitizePackageRelativePath("/absolute"), std::runtime_error);
    EXPECT_THROWS(sanitizePackageRelativePath("C:/drive"), std::runtime_error);
    EXPECT_THROWS(sanitizePackageRelativePath(std::string("bad") + static_cast<char>(0x7F)),
                  std::runtime_error);
}

TEST(PackageUtils, RejectsInvalidPackageNames) {
    EXPECT_THROWS(normalizeExecName("bad/name"), std::runtime_error);
    EXPECT_THROWS(normalizeDebName("a"), std::runtime_error);
    EXPECT_THROWS(normalizeDebName("-bad"), std::runtime_error);
}

TEST(PackageUtils, RejectsInvalidFileAssociations) {
    EXPECT_NO_THROW(validateFileAssociation(".zia", "Zia Source", "text/x-zia"));
    EXPECT_THROWS(validateFileAssociation(".", "Bad", "text/plain"), std::runtime_error);
    EXPECT_THROWS(validateFileAssociation("zia", "Bad", "text/plain"), std::runtime_error);
    EXPECT_THROWS(validateFileAssociation(".bad", "Bad", "text/with space"), std::runtime_error);
    EXPECT_THROWS(validateFileAssociation("../bad", "Bad", "text/plain"), std::runtime_error);
}

TEST(PackageUtils, RejectsDuplicateFileAssociationExtensions) {
    std::vector<FileAssoc> assocs = {
        {".zia", "Zia Source", "text/x-zia", ""},
        {".ZIA", "Zia Source 2", "text/x-zia-2", ""},
    };
    EXPECT_THROWS(validatePackageFileAssociations(assocs), std::runtime_error);
}

TEST(PackageUtils, NormalizesWindowsSigningThumbprints) {
    EXPECT_EQ(normalizeWindowsCertificateThumbprint(
                  "ABCD EFFE 0011 2233 4455 6677 8899 AABB CCDD EEFF",
                  "thumbprint"),
              std::string("abcdeffe00112233445566778899aabbccddeeff"));
    EXPECT_THROWS(validateWindowsCertificateThumbprint("1234", "thumbprint"),
                  std::runtime_error);
    EXPECT_THROWS(validateWindowsCertificateThumbprint(
                      "abcdeffe00112233445566778899aabbccddeefg", "thumbprint"),
                  std::runtime_error);
}

TEST(PackageUtils, ValidatesMacOSSigningSemantics) {
    PackageConfig pkg;
    pkg.macosSignMode = "developer-id";
    EXPECT_THROWS(validateMacOSSigningConfig(pkg), std::runtime_error);
    pkg.macosSignIdentity = "Developer ID Application: Example";
    EXPECT_NO_THROW(validateMacOSSigningConfig(pkg));
    pkg.macosNotaryProfile = "notary-profile";
    EXPECT_NO_THROW(validateMacOSSigningConfig(pkg));
    pkg.macosStaple = true;
    EXPECT_NO_THROW(validateMacOSSigningConfig(pkg));

    pkg.macosSignMode = "adhoc";
    EXPECT_THROWS(validateMacOSSigningConfig(pkg), std::runtime_error);
    pkg.macosNotaryProfile.clear();
    EXPECT_THROWS(validateMacOSSigningConfig(pkg), std::runtime_error);
}

#if !defined(_WIN32)
TEST(PackageUtils, SafeDirectoryIterateFollowsInternalSymlinkDirectories) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_safe_iter_symlink_dir";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "project" / "assets");
    fs::create_directories(tmpRoot / "project" / "shared");
    {
        std::ofstream out(tmpRoot / "project" / "shared" / "data.txt");
        out << "data";
    }
    fs::create_directory_symlink("../shared", tmpRoot / "project" / "assets" / "linked");

    bool sawLinkedFile = false;
    safeDirectoryIterate(tmpRoot / "project" / "assets",
                         tmpRoot / "project",
                         [&](const fs::directory_entry &entry) {
                             if (fs::is_regular_file(entry.path()) &&
                                 entry.path()
                                         .lexically_relative(tmpRoot / "project" / "assets")
                                         .generic_string() == "linked/data.txt") {
                                 sawLinkedFile = true;
                             }
                         });
    EXPECT_TRUE(sawLinkedFile);
    fs::remove_all(tmpRoot);
}

TEST(PackageUtils, SafeDirectoryIterateResolvedReportsValidatedReadPath) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_safe_iter_resolved_path";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "project" / "assets");
    fs::create_directories(tmpRoot / "project" / "shared");
    {
        std::ofstream out(tmpRoot / "project" / "shared" / "data.txt");
        out << "data";
    }
    fs::create_symlink("../shared/data.txt", tmpRoot / "project" / "assets" / "link.txt");

    bool sawResolvedSymlink = false;
    safeDirectoryIterateResolved(
        tmpRoot / "project" / "assets",
        tmpRoot / "project",
        [&](const SafeDirectoryEntry &entry) {
            if (entry.logicalPath.filename() == "link.txt") {
                sawResolvedSymlink = entry.symlink && entry.regularFile &&
                                     fs::equivalent(entry.resolvedPath,
                                                    tmpRoot / "project" / "shared" /
                                                        "data.txt");
            }
        });
    EXPECT_TRUE(sawResolvedSymlink);
    fs::remove_all(tmpRoot);
}

TEST(PackageUtils, SafeDirectoryIterateRejectsEscapingSymlink) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_safe_iter_escape_symlink";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "project" / "assets");
    {
        std::ofstream out(tmpRoot / "outside.txt");
        out << "outside";
    }
    fs::create_symlink(tmpRoot / "outside.txt", tmpRoot / "project" / "assets" / "escape.txt");

    EXPECT_THROWS(safeDirectoryIterateResolved(
                      tmpRoot / "project" / "assets",
                      tmpRoot / "project",
                      [](const SafeDirectoryEntry &) {}),
                  std::runtime_error);
    fs::remove_all(tmpRoot);
}
#endif

TEST(PackageUtils, ValidatesPackageUrls) {
    EXPECT_NO_THROW(validatePackageUrl("https://example.com/app", "homepage"));
    EXPECT_NO_THROW(validatePackageUrl("http://localhost:8080", "homepage"));
    EXPECT_THROWS(validatePackageUrl("https:///missing-host", "homepage"), std::runtime_error);
    EXPECT_THROWS(validatePackageUrl("https://bad host.example", "homepage"), std::runtime_error);
    EXPECT_THROWS(validatePackageUrl("1https://example.com", "homepage"), std::runtime_error);
    EXPECT_THROWS(validatePackageUrl("https://example.com:", "homepage"), std::runtime_error);
}

TEST(PackageUtils, ValidatesToolchainArchitecture) {
    EXPECT_NO_THROW(validateToolchainArchitecture("x64"));
    EXPECT_NO_THROW(validateToolchainArchitecture("arm64"));
    EXPECT_THROWS(validateToolchainArchitecture("amd64"), std::runtime_error);
    EXPECT_THROWS(validateToolchainArchitecture(""), std::runtime_error);
}

TEST(PackageUtils, ValidatesToolchainPlatform) {
    EXPECT_NO_THROW(validateToolchainPlatform("windows"));
    EXPECT_NO_THROW(validateToolchainPlatform("macos"));
    EXPECT_NO_THROW(validateToolchainPlatform("linux"));
    EXPECT_THROWS(validateToolchainPlatform("darwin"), std::runtime_error);
    EXPECT_THROWS(validateToolchainPlatform(""), std::runtime_error);
}

TEST(PackageUtils, ValidatesDebDependencyGrammar) {
    EXPECT_NO_THROW(validateDebDependency("libc6 (>= 2.34)"));
    EXPECT_NO_THROW(validateDebDependency("libstdc++6 | libc++1"));
    EXPECT_NO_THROW(validateDebDependency("python3:any"));
    EXPECT_THROWS(validateDebDependency("BadPackage"), std::runtime_error);
    EXPECT_THROWS(validateDebDependency("libc6 (> 2.34)"), std::runtime_error);
    EXPECT_THROWS(validateDebDependency("libc6 | "), std::runtime_error);
}

TEST(PackageUtils, ValidatesDebianVersionGrammar) {
    EXPECT_NO_THROW(validateDebVersion("1.2.3-1", "version"));
    EXPECT_NO_THROW(validateDebVersion("2:1.0~beta+1", "version"));
    EXPECT_THROWS(validateDebVersion("-1.0", "version"), std::runtime_error);
    EXPECT_THROWS(validateDebVersion("1:2:3", "version"), std::runtime_error);
    EXPECT_THROWS(validateDebVersion("1.0-", "version"), std::runtime_error);
}

TEST(PackageUtils, ValidatesTargetSpecificIdentifiers) {
    EXPECT_NO_THROW(validateMacOSBundleIdentifier("org.viper.test-app"));
    EXPECT_THROWS(validateMacOSBundleIdentifier("org.viper.test_app"), std::runtime_error);
    EXPECT_THROWS(validateMacOSBundleIdentifier("org.-viper.test"), std::runtime_error);
    EXPECT_NO_THROW(validateWindowsProgIdBase("viper.test_app"));
    EXPECT_THROWS(validateWindowsProgIdBase("viper."), std::runtime_error);
}

TEST(PackageUtils, ValidatesDottedVersionComponentCountAndRange) {
    EXPECT_NO_THROW(validateDottedNumericVersion("10.0", "version"));
    EXPECT_NO_THROW(validateDottedNumericVersion("1.2.3.4", "version"));
    EXPECT_THROWS(validateDottedNumericVersion("1", "version"), std::runtime_error);
    EXPECT_THROWS(validateDottedNumericVersion("1.2.3.4.5", "version"), std::runtime_error);
    EXPECT_THROWS(validateDottedNumericVersion("1.70000", "version"), std::runtime_error);
}

TEST(PackageUtils, RejectsSourcePathEscape) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_pkg_source_escape_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "project");

    EXPECT_THROWS(resolvePackageSourcePath(tmpRoot / "project", "../outside.txt", "asset"),
                  std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(PackageConfig, DetectsNonDefaultPackageFields) {
    PackageConfig pkg;
    EXPECT_FALSE(pkg.hasPackageConfig());
    pkg.license = "GPL-3.0-only";
    EXPECT_TRUE(pkg.hasPackageConfig());

    PackageConfig pkg2;
    pkg2.targetArchitectures.push_back("arm64");
    EXPECT_TRUE(pkg2.hasPackageConfig());

    PackageConfig pkg3;
    pkg3.windowsSignThumbprint = "ABCDEFFE00112233445566778899AABBCCDDEEFF";
    EXPECT_TRUE(pkg3.hasPackageConfig());
}

// ============================================================================
// ZipReader Tests
// ============================================================================

TEST(ZipReader, RoundTripStoredEntries) {
    // Write a ZIP with stored entries, then read it back
    ZipWriter writer;
    writer.addFileString("hello.txt", "Hello, World!");
    writer.addFileString("dir/nested.txt", "Nested content");
    auto zipData = writer.finishToVector();

    ZipReader reader(zipData.data(), zipData.size());
    EXPECT_EQ(reader.entries().size(), static_cast<size_t>(2));

    auto *hello = reader.find("hello.txt");
    ASSERT_TRUE(hello != nullptr);
    auto helloData = reader.extract(*hello);
    EXPECT_EQ(helloData.size(), static_cast<size_t>(13));
    EXPECT_TRUE(std::memcmp(helloData.data(), "Hello, World!", 13) == 0);

    auto *nested = reader.find("dir/nested.txt");
    ASSERT_TRUE(nested != nullptr);
    auto nestedData = reader.extract(*nested);
    EXPECT_EQ(std::string(nestedData.begin(), nestedData.end()), std::string("Nested content"));
}

TEST(ZipReader, DeflateExtractionHonorsDeclaredOutputLimit) {
    ZipWriter writer;
    writer.addFileString("big.txt", std::string(4096, 'A'));
    auto zipData = writer.finishToVector();

    size_t cdOff = 0;
    for (size_t i = 0; i + 4 <= zipData.size(); ++i) {
        if (readLE32(zipData.data() + i) == 0x02014B50) {
            cdOff = i;
            break;
        }
    }
    ASSERT_TRUE(cdOff > 0);
    ASSERT_EQ(readLE16(zipData.data() + cdOff + 10), static_cast<uint16_t>(8));

    // Central-directory uncompressed size drives ZipReader's inflate cap.
    zipData[cdOff + 24] = 1;
    zipData[cdOff + 25] = 0;
    zipData[cdOff + 26] = 0;
    zipData[cdOff + 27] = 0;
    zipData[22] = 1;
    zipData[23] = 0;
    zipData[24] = 0;
    zipData[25] = 0;

    ZipReader reader(zipData.data(), zipData.size());
    const ZipEntry *entry = reader.find("big.txt");
    ASSERT_TRUE(entry != nullptr);
    EXPECT_THROWS(reader.extract(*entry), DeflateError);
}

TEST(ZipReader, FindMissingEntry) {
    ZipWriter writer;
    writer.addFileString("a.txt", "alpha");
    auto zipData = writer.finishToVector();

    ZipReader reader(zipData.data(), zipData.size());
    EXPECT_TRUE(reader.find("nonexistent.txt") == nullptr);
}

TEST(ZipReader, EmptyArchive) {
    ZipWriter writer;
    auto zipData = writer.finishToVector();

    ZipReader reader(zipData.data(), zipData.size());
    EXPECT_EQ(reader.entries().size(), static_cast<size_t>(0));
}

TEST(ZipReader, InvalidDataThrows) {
    std::vector<uint8_t> garbage = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool threw = false;
    try {
        ZipReader reader(garbage.data(), garbage.size());
    } catch (const ZipReadError &) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(ZipWriter, StoredModePreservesLocalDataLayout) {
    ZipWriter writer;
    writer.setCompressionEnabled(false);
    writer.addFileString("hello.txt", "Hello stored world");

    const auto &entries = writer.layoutEntries();
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    EXPECT_EQ(entries[0].method, static_cast<uint16_t>(0));
    EXPECT_EQ(entries[0].name, std::string("hello.txt"));
    EXPECT_GT(entries[0].localDataOffset, entries[0].localHeaderOffset);
    EXPECT_EQ(entries[0].compressedSize, entries[0].uncompressedSize);
}

// ============================================================================
// InstallerStubGen Tests
// ============================================================================

TEST(StubGen, PushPopEncoding) {
    InstallerStubGen gen;
    gen.push(X64Reg::RBP); // 55
    gen.push(X64Reg::R12); // 41 54
    gen.pop(X64Reg::R12);  // 41 5C
    gen.pop(X64Reg::RBP);  // 5D
    gen.ret();             // C3

    auto code = gen.finishText(0x1000, 0x2000, {}, 0x3000);
    ASSERT_EQ(code.size(), static_cast<size_t>(7));
    EXPECT_EQ(code[0], static_cast<uint8_t>(0x55)); // push rbp
    EXPECT_EQ(code[1], static_cast<uint8_t>(0x41)); // REX.B
    EXPECT_EQ(code[2], static_cast<uint8_t>(0x54)); // push r12
    EXPECT_EQ(code[3], static_cast<uint8_t>(0x41)); // REX.B
    EXPECT_EQ(code[4], static_cast<uint8_t>(0x5C)); // pop r12
    EXPECT_EQ(code[5], static_cast<uint8_t>(0x5D)); // pop rbp
    EXPECT_EQ(code[6], static_cast<uint8_t>(0xC3)); // ret
}

TEST(StubGen, XorRegReg) {
    InstallerStubGen gen;
    gen.xorRegReg(X64Reg::RCX, X64Reg::RCX); // 48 31 C9
    gen.ret();

    auto code = gen.finishText(0x1000, 0x2000, {}, 0x3000);
    ASSERT_GE(code.size(), static_cast<size_t>(4));
    EXPECT_EQ(code[0], static_cast<uint8_t>(0x48)); // REX.W
    EXPECT_EQ(code[1], static_cast<uint8_t>(0x31)); // XOR
    EXPECT_EQ(code[2], static_cast<uint8_t>(0xC9)); // ModRM: rcx, rcx
}

TEST(StubGen, LabelJumpForward) {
    InstallerStubGen gen;
    auto lbl = gen.newLabel();
    gen.jmp(lbl); // E9 XX XX XX XX (5 bytes)
    gen.nop();    // 90 (1 byte) — this should be skipped
    gen.bindLabel(lbl);
    gen.ret(); // C3

    auto code = gen.finishText(0x1000, 0x2000, {}, 0x3000);
    // jmp should jump over the nop: rel32 = 1 (target at offset 6, fixup at offset 1, +4 = 5,
    // 6-5=1)
    EXPECT_EQ(code[0], static_cast<uint8_t>(0xE9));
    int32_t disp = static_cast<int32_t>(code[1]) | (static_cast<int32_t>(code[2]) << 8) |
                   (static_cast<int32_t>(code[3]) << 16) | (static_cast<int32_t>(code[4]) << 24);
    EXPECT_EQ(disp, 1); // skip 1 byte (the nop)
}

TEST(StubGen, EmbedStringW) {
    InstallerStubGen gen;
    uint32_t off = gen.embedStringW("Hi");
    gen.ret();

    EXPECT_EQ(off, static_cast<uint32_t>(0));
    const auto &data = gen.dataSection();
    // "Hi" in UTF-16LE = 'H' 00 'i' 00 00 00
    ASSERT_EQ(data.size(), static_cast<size_t>(6));
    EXPECT_EQ(data[0], 'H');
    EXPECT_EQ(data[1], 0);
    EXPECT_EQ(data[2], 'i');
    EXPECT_EQ(data[3], 0);
    EXPECT_EQ(data[4], 0); // NUL terminator
    EXPECT_EQ(data[5], 0);
}

TEST(StubGen, EmbedStringWAcceptsUtf8) {
    InstallerStubGen gen;
    uint32_t off = gen.embedStringW("\xE2\x82\xAC");
    EXPECT_EQ(off, static_cast<uint32_t>(0));
    const auto &data = gen.dataSection();
    ASSERT_EQ(data.size(), static_cast<size_t>(4));
    EXPECT_EQ(data[0], static_cast<uint8_t>(0xAC));
    EXPECT_EQ(data[1], static_cast<uint8_t>(0x20));
    EXPECT_EQ(data[2], 0);
    EXPECT_EQ(data[3], 0);
}

TEST(StubGen, EmitsIndexedWordStore) {
    InstallerStubGen gen;
    gen.movMemIndexImm16(X64Reg::RBP, X64Reg::RAX, 1, -16, 0);
    const auto code = gen.finishText(0x1000, 0x2000, {}, 0x3000);
    const std::vector<uint8_t> expected = {
        0x66, 0xC7, 0x84, 0x45, 0xF0, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
    EXPECT_EQ(code, expected);
}

TEST(StubGen, EmitsIndexedWordLoadAndLea) {
    InstallerStubGen gen;
    gen.movzxRegMemIndex16(X64Reg::R11, X64Reg::RBP, X64Reg::RAX, 0, -32);
    gen.leaRegMemIndex(X64Reg::RDX, X64Reg::RBP, X64Reg::R10, 0, -64);
    const auto code = gen.finishText(0x1000, 0x2000, {}, 0x3000);
    const std::vector<uint8_t> expected = {
        0x44, 0x0F, 0xB7, 0x9C, 0x05, 0xE0, 0xFF, 0xFF, 0xFF,
        0x4A, 0x8D, 0x94, 0x15, 0xC0, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(code, expected);
}

// ============================================================================
// InstallerStub Integration Tests
// ============================================================================

TEST(InstallerStub, GeneratesValidPE) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.version = "1.0.0";
    layout.executableName = "testapp.exe";
    layout.publisher = "Viper";
    layout.identifier = "org.viper.testapp";
    layout.overlayFileOffset = 0x400;
    layout.installDirectories.push_back({WindowsInstallRoot::InstallDir, "themes"});
    layout.uninstallDirectories = layout.installDirectories;
    layout.installFiles.push_back({WindowsInstallRoot::InstallDir, "testapp.exe", 0x80, 16});
    auto stub = buildInstallerStub(layout, "x64");

    // Should produce non-empty .text and imports
    EXPECT_GT(stub.textSection.size(), static_cast<size_t>(10));
    EXPECT_FALSE(stub.imports.empty());

    // Build a PE with the stub and verify it
    PEBuildParams pe;
    pe.textSection = stub.textSection;
    pe.imports = stub.imports;
    pe.manifest = generateUacManifest();
    auto peBytes = buildPE(pe);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(peBytes, err));
}

TEST(InstallerStub, ImportsRuntimeCrcForOverlayIntegrity) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.overlayFileOffset = 0x400;
    layout.installFiles.push_back({WindowsInstallRoot::InstallDir, "testapp.exe", 0x80, 16, 0x12345678});
    auto stub = buildInstallerStub(layout, "x64");

    bool found = false;
    for (const auto &imp : stub.imports) {
        if (imp.dllName == "ntdll.dll" &&
            std::find(imp.functions.begin(), imp.functions.end(), "RtlComputeCrc32") !=
                imp.functions.end()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(InstallerStub, UsesSetFilePointerExForLargeOverlayOffsets) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.overlayFileOffset = 0x100000000ull;
    layout.installFiles.push_back(
        {WindowsInstallRoot::InstallDir, "testapp.exe", 0x80, 16, 0x12345678});
    auto stub = buildInstallerStub(layout, "x64");

    bool found = false;
    for (const auto &imp : stub.imports) {
        if (imp.dllName == "kernel32.dll" &&
            std::find(imp.functions.begin(), imp.functions.end(), "SetFilePointerEx") !=
                imp.functions.end()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(InstallerStub, EmitsWindowsAddRemoveProgramsMetadata) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.version = "1.2.3";
    layout.executableName = "testapp.exe";
    layout.publisher = "Viper";
    layout.identifier = "org.viper.testapp";
    layout.homepage = "https://example.invalid/testapp";
    layout.displayIconRelativePath = "testapp.ico";
    layout.estimatedSizeKb = 42;
    layout.installDate = "20260512";
    layout.overlayFileOffset = 0x400;
    auto stub = buildInstallerStub(layout, "x64");

    EXPECT_TRUE(containsUtf16LE(stub.stubData, "QuietUninstallString"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "DisplayIcon"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "EstimatedSize"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "InstallDate"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "URLInfoAbout"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "URLUpdateInfo"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "20260512"));
    EXPECT_TRUE(containsUtf16LE(stub.stubData, "https://example.invalid/testapp"));
}

TEST(InstallerStub, ImportsRegistryQueryForOwnedFileAssociationCleanup) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.identifier = "org.viper.testapp";
    layout.overlayFileOffset = 0x400;
    layout.fileAssociations.push_back(
        {".zia", "Zia Source", "text/x-zia", "org.viper.testapp.zia", {}});

    auto installer = buildInstallerStub(layout, "x64");
    auto uninstaller = buildUninstallerStub(layout, "x64");

    auto hasImport = [](const StubResult &stub, const std::string &dll, const std::string &fn) {
        return std::any_of(stub.imports.begin(), stub.imports.end(), [&](const PEImport &imp) {
            return imp.dllName == dll &&
                   std::find(imp.functions.begin(), imp.functions.end(), fn) !=
                       imp.functions.end();
        });
    };

    EXPECT_TRUE(hasImport(installer, "advapi32.dll", "RegQueryValueExW"));
    EXPECT_TRUE(hasImport(installer, "kernel32.dll", "lstrcmpW"));
    EXPECT_TRUE(hasImport(installer, "kernel32.dll", "lstrcmpiW"));
    EXPECT_TRUE(hasImport(uninstaller, "advapi32.dll", "RegQueryValueExW"));
    EXPECT_TRUE(hasImport(uninstaller, "kernel32.dll", "lstrcmpW"));
    EXPECT_TRUE(hasImport(uninstaller, "kernel32.dll", "lstrcmpiW"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, "VAPSContentTypeOwner"));
    EXPECT_TRUE(containsUtf16LE(uninstaller.stubData, "VAPSContentTypeOwner"));
}

TEST(InstallerStub, UsesModernKnownFoldersAndRecursiveRegistryCleanup) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.identifier = "org.viper.testapp";
    layout.createDesktopShortcut = true;
    layout.createStartMenuShortcut = true;
    layout.fileAssociations.push_back(
        {".zia", "Zia Source", "text/x-zia", "org.viper.testapp.zia", {}});

    auto installer = buildInstallerStub(layout, "x64");
    auto uninstaller = buildUninstallerStub(layout, "x64");

    auto hasImport = [](const StubResult &stub, const std::string &dll, const std::string &fn) {
        return std::any_of(stub.imports.begin(), stub.imports.end(), [&](const PEImport &imp) {
            return imp.dllName == dll &&
                   std::find(imp.functions.begin(), imp.functions.end(), fn) !=
                       imp.functions.end();
        });
    };

    EXPECT_TRUE(hasImport(installer, "shell32.dll", "SHGetKnownFolderPath"));
    EXPECT_TRUE(hasImport(installer, "ole32.dll", "CoTaskMemFree"));
    EXPECT_TRUE(hasImport(installer, "advapi32.dll", "RegDeleteTreeW"));
    EXPECT_TRUE(hasImport(installer, "kernel32.dll", "GetDateFormatW"));
    EXPECT_TRUE(hasImport(uninstaller, "shell32.dll", "SHGetKnownFolderPath"));
    EXPECT_TRUE(hasImport(uninstaller, "ole32.dll", "CoTaskMemFree"));
    EXPECT_TRUE(hasImport(uninstaller, "advapi32.dll", "RegDeleteTreeW"));
    EXPECT_FALSE(hasImport(installer, "shell32.dll", "SHGetFolderPathW"));
    EXPECT_FALSE(hasImport(uninstaller, "advapi32.dll", "RegDeleteKeyW"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, "yyyyMMdd"));
}

TEST(InstallerStub, ImportsPathAndDirectoryFailureChecks) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.overlayFileOffset = 0x400;
    layout.installDirectories.push_back({WindowsInstallRoot::InstallDir, "data"});
    auto stub = buildInstallerStub(layout, "x64");

    bool foundGetLastError = false;
    bool foundLstrlen = false;
    for (const auto &imp : stub.imports) {
        if (imp.dllName != "kernel32.dll")
            continue;
        foundGetLastError =
            foundGetLastError ||
            std::find(imp.functions.begin(), imp.functions.end(), "GetLastError") !=
                imp.functions.end();
        foundLstrlen = foundLstrlen ||
                       std::find(imp.functions.begin(), imp.functions.end(), "lstrlenW") !=
                           imp.functions.end();
    }
    EXPECT_TRUE(foundGetLastError);
    EXPECT_TRUE(foundLstrlen);
}

TEST(InstallerStub, SupportsQuietAutomationFlags) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.overlayFileOffset = 0x400;
    auto installer = buildInstallerStub(layout, "x64");
    auto uninstaller = buildUninstallerStub(layout, "x64");

    auto hasImport = [](const StubResult &stub, const std::string &dll, const std::string &fn) {
        return std::any_of(stub.imports.begin(), stub.imports.end(), [&](const PEImport &imp) {
            return imp.dllName == dll &&
                   std::find(imp.functions.begin(), imp.functions.end(), fn) !=
                       imp.functions.end();
        });
    };

    EXPECT_TRUE(hasImport(installer, "kernel32.dll", "GetCommandLineW"));
    EXPECT_TRUE(hasImport(installer, "shlwapi.dll", "StrStrIW"));
    EXPECT_TRUE(hasImport(uninstaller, "kernel32.dll", "GetCommandLineW"));
    EXPECT_TRUE(hasImport(uninstaller, "shlwapi.dll", "StrStrIW"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, "/quiet"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, "/silent"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, "/norestart"));
    EXPECT_TRUE(containsUtf16LE(installer.stubData, " /quiet"));
    EXPECT_FALSE(containsUtf16LE(installer.stubData, " /quiet /norestart"));
    EXPECT_TRUE(containsUtf16LE(uninstaller.stubData, "/quiet"));
    EXPECT_TRUE(containsUtf16LE(uninstaller.stubData, "/silent"));
    EXPECT_TRUE(containsUtf16LE(uninstaller.stubData, "/norestart"));
    EXPECT_TRUE(containsUtf16LE(uninstaller.stubData, "Choose OK to continue or Cancel to exit."));
}

TEST(InstallerStubGen, RejectsOutOfRangeIATSlotFixup) {
    InstallerStubGen gen;
    gen.callIATSlot(1);
    std::vector<PEImport> imports = {{"kernel32.dll", {"ExitProcess"}}};
    EXPECT_THROWS(gen.finishText(0x1000, 0x2000, imports, 0x3000), std::runtime_error);
}

TEST(InstallerStub, UninstallerGeneratesValidPE) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.version = "1.0.0";
    layout.executableName = "testapp.exe";
    layout.publisher = "Viper";
    layout.identifier = "org.viper.testapp";
    layout.uninstallDirectories.push_back({WindowsInstallRoot::InstallDir, "themes"});
    layout.uninstallFiles.push_back({WindowsInstallRoot::InstallDir, "testapp.exe", 0, 16});
    auto stub = buildUninstallerStub(layout, "x64");

    EXPECT_GT(stub.textSection.size(), static_cast<size_t>(10));
    EXPECT_FALSE(stub.imports.empty());
    bool foundGetLastError = false;
    for (const auto &imp : stub.imports) {
        if (imp.dllName == "kernel32.dll" &&
            std::find(imp.functions.begin(), imp.functions.end(), "GetLastError") !=
                imp.functions.end()) {
            foundGetLastError = true;
        }
    }
    EXPECT_TRUE(foundGetLastError);

    PEBuildParams pe;
    pe.textSection = stub.textSection;
    pe.imports = stub.imports;
    pe.manifest = generateAsInvokerManifest();
    auto peBytes = buildPE(pe);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(peBytes, err));
}

TEST(InstallerStub, UninstallerDoesNotScheduleInstallRootForRebootDeletion) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.version = "1.0.0";
    layout.executableName = "testapp.exe";
    layout.publisher = "Viper";
    layout.identifier = "org.viper.testapp";
    layout.uninstallDirectories.push_back({WindowsInstallRoot::InstallDir, "themes"});
    layout.uninstallFiles.push_back({WindowsInstallRoot::InstallDir, "testapp.exe", 0, 16});
    auto stub = buildUninstallerStub(layout, "x64");

    EXPECT_EQ(countIatCallsTo(stub, "kernel32.dll", "MoveFileExW"), static_cast<size_t>(1));
}

TEST(InstallerStub, ARM64UsesX64Bootstrap) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    auto stub = buildInstallerStub(layout, "arm64");
    EXPECT_EQ(stub.peArch, "x64");
    EXPECT_GT(stub.textSection.size(), static_cast<size_t>(10));
    EXPECT_FALSE(stub.imports.empty());
}

TEST(InstallerStub, AllowsZeroBytePayloadFile) {
    WindowsPackageLayout layout;
    layout.displayName = "TestApp";
    layout.installDirName = "TestApp";
    layout.executableName = "testapp.exe";
    layout.overlayFileOffset = 0x400;
    layout.installFiles.push_back({WindowsInstallRoot::InstallDir, "empty.dat", 0x80, 0});
    auto stub = buildInstallerStub(layout, "x64");
    EXPECT_GT(stub.textSection.size(), static_cast<size_t>(10));
}

TEST(WindowsPackageBuilder, BuildsInstallerWithStoredZipOverlay) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_builder_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "assets" / "themes");

    writeTestWindowsPe(tmpRoot / "app.exe");
    {
        std::ofstream asset(tmpRoot / "assets" / "themes" / "dark.json", std::ios::binary);
        asset << "{\"theme\":\"dark\"}";
    }
    {
        PkgImage img;
        img.width = 1;
        img.height = 1;
        img.pixels = {255, 0, 0, 255};
        const auto png = pngEncode(img);
        std::ofstream icon(tmpRoot / "icon.png", std::ios::binary);
        icon.write(reinterpret_cast<const char *>(png.data()),
                   static_cast<std::streamsize>(png.size()));
    }

    PackageConfig pkg;
    pkg.displayName = "Test App";
    pkg.identifier = "org.viper.testapp";
    pkg.author = "Viper";
    pkg.shortcutDesktop = true;
    pkg.shortcutMenu = true;
    pkg.iconPath = "icon.png";
    pkg.assets.push_back({"assets", "data"});
    pkg.fileAssociations.push_back({".zia", "Zia Source", "text/x-zia", ""});

    const fs::path outPath = tmpRoot / "test_setup.exe";
    WindowsBuildParams params;
    params.projectName = "testapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = outPath.string();
    params.archStr = "x64";

    buildWindowsPackage(params);

    auto pe = readFile(outPath.string());
    std::ostringstream err;
    const bool overlayOk = verifyPEZipOverlayPayload(
        pe,
        {"app/testapp.exe", "app/uninstall.exe", "app/testapp.ico", "meta/manifest.sha256"},
        err);
    if (!overlayOk)
        std::cerr << err.str();
    EXPECT_TRUE(overlayOk);
    EXPECT_TRUE(containsUtf16LE(pe, "%ProgramFiles%\\Test App\\testapp.exe"));
    EXPECT_TRUE(containsUtf16LEStringData(pe, "%ProgramFiles%\\Test App\\testapp.ico"));
    EXPECT_TRUE(containsUtf16LE(pe, "Software\\Classes\\.zia"));
    EXPECT_TRUE(containsUtf16LE(pe, "Software\\Classes\\.zia\\OpenWithProgids"));
    EXPECT_TRUE(containsUtf16LE(pe, "org.viper.testapp.zia"));
    EXPECT_TRUE(containsUtf16LE(pe, "DefaultIcon"));
    EXPECT_TRUE(containsUtf16LE(pe, "QuietUninstallString"));
    const std::string deleteValueImport = "RegDeleteValueW";
    EXPECT_TRUE(std::search(pe.begin(),
                            pe.end(),
                            deleteValueImport.begin(),
                            deleteValueImport.end()) != pe.end());

    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, PerUserInstallerUsesLocalAppDataAndBundlesAdjacentDlls) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_user_scope_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(tmpRoot / "app.exe", "x64", {{"helper.dll", {"helper_entry"}}});
    {
        std::ofstream dll(tmpRoot / "helper.dll", std::ios::binary);
        dll << "helper";
    }

    PackageConfig pkg;
    pkg.displayName = "User App";
    pkg.identifier = "org.viper.userapp";
    pkg.author = "Viper";
    pkg.homepage = "https://example.invalid/userapp";
    pkg.shortcutDesktop = true;
    pkg.shortcutMenu = true;
    pkg.windowsInstallScope = "user";

    const fs::path outPath = tmpRoot / "user_setup.exe";
    WindowsBuildParams params;
    params.projectName = "userapp";
    params.version = "2.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = outPath.string();
    params.archStr = "x64";

    buildWindowsPackage(params);

    auto pe = readFile(outPath.string());
    std::ostringstream err;
    const bool overlayOk = verifyPEZipOverlayPayload(
        pe, {"app/userapp.exe", "app/helper.dll", "app/uninstall.exe", "meta/manifest.sha256"}, err);
    if (!overlayOk)
        std::cerr << err.str();
    EXPECT_TRUE(overlayOk);
    EXPECT_TRUE(containsUtf16LE(pe, "%LocalAppData%\\User App\\userapp.exe"));
    EXPECT_TRUE(containsUtf16LE(pe, "Environment"));
    EXPECT_FALSE(containsUtf16LE(pe,
                                 "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"));
    EXPECT_TRUE(containsUtf16LE(pe, "https://example.invalid/userapp"));

    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, UsesCustomInstallDirOpenArgsManifestAndVersionInfo) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_custom_dir_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(tmpRoot / "app.exe");

    PackageConfig pkg;
    pkg.displayName = "Display App";
    pkg.identifier = "org.viper.displayapp";
    pkg.author = "Viper";
    pkg.windowsInstallDir = "DisplayAppCustom";
    pkg.minOsWindows = "10.0";
    pkg.fileAssociations.push_back(
        {".vap", "Viper App Project", "text/x-viper-app", "--open-project"});

    WindowsBuildParams params;
    params.projectName = "displayapp";
    params.version = "1.2.3-beta";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "display_setup.exe").string();
    params.archStr = "x64";

    buildWindowsPackage(params);

    auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlayPayload(
        pe, {"app/displayapp.exe", "app/uninstall.exe", "meta/manifest.sha256"}, err));
    EXPECT_TRUE(containsUtf16LE(pe, "%ProgramFiles%\\DisplayAppCustom\\displayapp.exe"));
    EXPECT_FALSE(containsUtf16LE(pe, "%ProgramFiles%\\Display App\\displayapp.exe"));
    EXPECT_TRUE(containsUtf16LE(pe, " --open-project"));
    EXPECT_TRUE(containsUtf16LE(pe, " \"%1\""));
    EXPECT_TRUE(containsUtf16LE(pe, "VS_VERSION_INFO"));
    EXPECT_TRUE(containsUtf16LE(pe, "Display App Setup"));
    EXPECT_TRUE(containsUtf16LE(pe, "1.2.3-beta"));
    EXPECT_TRUE(containsAscii(pe, "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"));

    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, BundlesRecursiveDllsAndSkipsSystemNamedLocals) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_recursive_dll_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(
        tmpRoot / "app.exe", "x64", {{"helper.dll", {"helper_entry"}}, {"kernel32.dll", {"Sleep"}}});
    writeTestWindowsPe(tmpRoot / "helper.dll", "x64", {{"plugin.dll", {"plugin_entry"}}});
    writeTestWindowsPe(tmpRoot / "plugin.dll");
    writeBytes(tmpRoot / "kernel32.dll", {'n', 'o', 't', ' ', 'b', 'u', 'n', 'd', 'l', 'e', 'd'});

    PackageConfig pkg;
    pkg.displayName = "Recursive DLL App";

    WindowsBuildParams params;
    params.projectName = "recursivedll";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "recursive_setup.exe").string();
    params.archStr = "x64";

    buildWindowsPackage(params);

    const auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlayPayload(
        pe,
        {"app/recursivedll.exe",
         "app/helper.dll",
         "app/plugin.dll",
         "app/uninstall.exe",
         "meta/manifest.sha256"},
        err));
    const auto overlay = extractFirstZipOverlay(pe);
    ASSERT_FALSE(overlay.empty());
    ZipReader reader(overlay.data(), overlay.size());
    EXPECT_TRUE(reader.find("app/helper.dll") != nullptr);
    EXPECT_TRUE(reader.find("app/plugin.dll") != nullptr);
    EXPECT_TRUE(reader.find("app/kernel32.dll") == nullptr);

    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, SkipsKnownWindowsGameRuntimeDlls) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_game_system_dll_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(tmpRoot / "app.exe",
                       "x64",
                       {{"xinput1_4.dll", {"XInputGetState"}},
                        {"iphlpapi.dll", {"GetAdaptersAddresses"}},
                        {"d3dcompiler_47.dll", {"D3DCompile"}}});
    writeBytes(tmpRoot / "xinput1_4.dll", {'l', 'o', 'c', 'a', 'l'});
    writeBytes(tmpRoot / "iphlpapi.dll", {'l', 'o', 'c', 'a', 'l'});
    writeBytes(tmpRoot / "d3dcompiler_47.dll", {'l', 'o', 'c', 'a', 'l'});

    PackageConfig pkg;
    pkg.displayName = "Game Runtime App";

    WindowsBuildParams params;
    params.projectName = "gameruntime";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "game_runtime_setup.exe").string();
    params.archStr = "x64";

    buildWindowsPackage(params);

    const auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlayPayload(
        pe, {"app/gameruntime.exe", "app/uninstall.exe", "meta/manifest.sha256"}, err));
    const auto overlay = extractFirstZipOverlay(pe);
    ASSERT_FALSE(overlay.empty());
    ZipReader reader(overlay.data(), overlay.size());
    EXPECT_TRUE(reader.find("app/gameruntime.exe") != nullptr);
    EXPECT_TRUE(reader.find("app/xinput1_4.dll") == nullptr);
    EXPECT_TRUE(reader.find("app/iphlpapi.dll") == nullptr);
    EXPECT_TRUE(reader.find("app/d3dcompiler_47.dll") == nullptr);

    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsMissingDebugRuntimeDllDependency) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_debug_runtime_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(tmpRoot / "app.exe", "x64", {{"vcruntime140d.dll", {"debug_entry"}}});

    PackageConfig pkg;
    pkg.displayName = "Debug Runtime App";

    WindowsBuildParams params;
    params.projectName = "debugruntime";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "debug_runtime_setup.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsMissingCustomDllWithMsvcPrefix) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_msvc_prefix_custom_dll_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    writeTestWindowsPe(tmpRoot / "app.exe", "x64", {{"msvcp_plugin.dll", {"plugin_entry"}}});

    PackageConfig pkg;
    pkg.displayName = "Custom Prefix DLL App";

    WindowsBuildParams params;
    params.projectName = "customprefixdll";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "custom_prefix_setup.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsMissingAdjacentDllDependency) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_missing_dll_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);

    PEBuildParams appPe;
    appPe.textSection = {0xC3};
    appPe.imports = {{"plugin_runtime.dll", {"plugin_entry"}}};
    const auto appBytes = buildPE(appPe);
    {
        std::ofstream exe(tmpRoot / "app.exe", std::ios::binary);
        exe.write(reinterpret_cast<const char *>(appBytes.data()),
                  static_cast<std::streamsize>(appBytes.size()));
    }

    PackageConfig pkg;
    pkg.displayName = "Missing DLL App";

    WindowsBuildParams params;
    params.projectName = "missingdll";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "missing_setup.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsNonPePayload) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_non_pe_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    writeBytes(tmpRoot / "app.exe", std::vector<uint8_t>{'M', 'Z', 's', 't', 'u', 'b'});

    PackageConfig pkg;
    pkg.displayName = "Valid Name";

    WindowsBuildParams params;
    params.projectName = "nonpe";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "nonpe_setup.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsPayloadArchitectureMismatch) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_arch_mismatch_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    writeTestWindowsPe(tmpRoot / "app.exe", "arm64");

    PackageConfig pkg;
    pkg.displayName = "Arch Mismatch";

    WindowsBuildParams params;
    params.projectName = "archmismatch";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "arch_setup.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, BuildsArm64PayloadPackageWithX64Bootstrap) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot =
        fs::temp_directory_path() / "viper_packaging_windows_builder_arm64_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    writeTestWindowsPe(tmpRoot / "app.exe", "arm64");

    PackageConfig pkg;
    pkg.displayName = "Test App";

    WindowsBuildParams params;
    params.projectName = "testapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "test_setup.exe").string();
    params.archStr = "arm64";

    buildWindowsPackage(params);

    auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlay(pe, err));
    EXPECT_EQ(readLE16(pe.data() + 0x84), static_cast<uint16_t>(0x8664));
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsUnsafeDisplayName) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_bad_name_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    {
        std::ofstream exe(tmpRoot / "app.exe", std::ios::binary);
        exe.write("MZstub", 6);
    }

    PackageConfig pkg;
    pkg.displayName = "Bad/Name";

    WindowsBuildParams params;
    params.projectName = "testapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "bad.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(WindowsPackageBuilder, RejectsMissingAsset) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_windows_missing_asset_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    {
        std::ofstream exe(tmpRoot / "app.exe", std::ios::binary);
        exe.write("MZstub", 6);
    }

    PackageConfig pkg;
    pkg.displayName = "Test App";
    pkg.assets.push_back({"missing", "data"});

    WindowsBuildParams params;
    params.projectName = "testapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app.exe").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "bad.exe").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildWindowsPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(LinuxPackageBuilder, TarballDoesNotValidateDebianOnlyMetadata) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_tarball_metadata_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }

    PackageConfig pkg;
    pkg.displayName = "Portable App";
    pkg.category = "DefinitelyNotARegisteredCategory";
    pkg.depends.push_back("BadPackage (> 1)");

    LinuxBuildParams params;
    params.projectName = "portableapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "portableapp.tar.gz").string();
    params.archStr = "not-a-deb-arch";

    buildTarball(params);
    const auto tarGz = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyTarGzPayload(tarGz, {"portableapp-1.0.0/portableapp"}, err));
    fs::remove_all(tmpRoot);
}

TEST(LinuxPackageBuilder, TarballNormalizesDebianEpochVersionInTopDirectory) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_tarball_epoch_version";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot);
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }

    LinuxBuildParams params;
    params.projectName = "portableapp";
    params.version = "2:1.0~beta+1";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig.displayName = "Portable App";
    params.outputPath = (tmpRoot / "portableapp.tar.gz").string();
    params.archStr = "x64";

    buildTarball(params);
    const auto tarGz = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyTarGzPayload(tarGz, {"portableapp-2_1.0~beta+1/portableapp"}, err));
    fs::remove_all(tmpRoot);
}

#if !defined(_WIN32)
TEST(LinuxPackageBuilder, TarballSingleFileSymlinkAssetKeepsLogicalName) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_tarball_symlink_asset";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "assets");
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }
    {
        std::ofstream real(tmpRoot / "assets" / "real.txt");
        real << "payload";
    }
    fs::create_symlink("real.txt", tmpRoot / "assets" / "link.txt");

    PackageConfig pkg;
    pkg.displayName = "Portable App";
    pkg.assets.push_back({"assets/link.txt", "data"});

    LinuxBuildParams params;
    params.projectName = "portableapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "portableapp.tar.gz").string();
    params.archStr = "x64";

    buildTarball(params);
    const auto tarBytes = inflateGzipPayload(readFile(params.outputPath));
    std::vector<uint8_t> data;
    EXPECT_TRUE(tarEntryData(tarBytes, "portableapp-1.0.0/data/link.txt", data));
    EXPECT_EQ(std::string(data.begin(), data.end()), std::string("payload"));
    std::vector<uint8_t> unexpected;
    EXPECT_FALSE(tarEntryData(tarBytes, "portableapp-1.0.0/data/real.txt", unexpected));
    fs::remove_all(tmpRoot);
}
#endif

TEST(LinuxPackageBuilder, TarballRejectsDuplicateAssetOutputPath) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_tarball_dup_asset_test";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "a");
    fs::create_directories(tmpRoot / "b");
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }
    {
        std::ofstream file(tmpRoot / "a" / "config.txt");
        file << "one";
    }
    {
        std::ofstream file(tmpRoot / "b" / "config.txt");
        file << "two";
    }

    PackageConfig pkg;
    pkg.displayName = "Portable App";
    pkg.assets.push_back({"a/config.txt", "data"});
    pkg.assets.push_back({"b/config.txt", "data"});

    LinuxBuildParams params;
    params.projectName = "portableapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "portableapp.tar.gz").string();
    params.archStr = "x64";

    EXPECT_THROWS(buildTarball(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(LinuxPackageBuilder, DebPreservesHiddenMimeDesktopEntryAndEmptyAssetDir) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_deb_mime_empty_dir";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "empty-assets");
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }

    PackageConfig pkg;
    pkg.displayName = "Empty App";
    pkg.shortcutMenu = false;
    pkg.shortcutDesktop = false;
    pkg.assets.push_back({"empty-assets", "data/empty"});
    pkg.fileAssociations.push_back({".zia", "Zia Source", "text/x-zia", ""});

    LinuxBuildParams params;
    params.projectName = "emptyapp";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig = pkg;
    params.outputPath = (tmpRoot / "emptyapp.deb").string();
    params.archStr = "amd64";

    buildDebPackage(params);
    const auto deb = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyDebPayload(deb,
                                 {"usr/bin/emptyapp",
                                  "usr/share/applications/emptyapp.desktop",
                                  "usr/share/mime/packages/emptyapp.xml",
                                  "usr/share/emptyapp/data/empty"},
                                 err));
    fs::remove_all(tmpRoot);
}

TEST(LinuxPackageBuilder, DebPreservesExecutableAssetMode) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_packaging_deb_asset_mode";
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "scripts");
    {
        std::ofstream exe(tmpRoot / "app", std::ios::binary);
        exe << "stub";
    }
    {
        std::ofstream script(tmpRoot / "scripts" / "helper.sh");
        script << "#!/bin/sh\n";
    }
    fs::permissions(tmpRoot / "scripts" / "helper.sh",
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                        fs::perms::others_exec,
                    fs::perm_options::replace);

    LinuxBuildParams params;
    params.projectName = "assetmode";
    params.version = "1.0.0";
    params.executablePath = (tmpRoot / "app").string();
    params.projectRoot = tmpRoot.string();
    params.pkgConfig.displayName = "Asset Mode";
    params.pkgConfig.assets.push_back({"scripts", "tools"});
    params.outputPath = (tmpRoot / "assetmode.deb").string();
    params.archStr = "amd64";

    buildDebPackage(params);
    const auto dataTar = debDataTar(readFile(params.outputPath));
    uint32_t mode = 0;
    EXPECT_TRUE(tarEntryMode(dataTar, "usr/share/assetmode/tools/helper.sh", mode));
    EXPECT_EQ(mode, static_cast<uint32_t>(0755));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, GatherAndValidateMockStage) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);

    const auto manifest = gatherToolchainInstallManifest(stage);
    EXPECT_EQ(manifest.version, std::string("9.8.7"));
    EXPECT_TRUE(manifest.platform == "windows" || manifest.platform == "macos" ||
                manifest.platform == "linux");
    EXPECT_TRUE(manifest.totalSizeBytes() > 0);
    EXPECT_TRUE(std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
        return entry.kind == ToolchainFileKind::Binary &&
               entry.stagedRelativePath.find("bin/") == 0;
    }));
    EXPECT_TRUE(std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
        return entry.kind == ToolchainFileKind::CMakeConfig &&
               entry.stagedRelativePath == "lib/cmake/Viper/ViperTargets.cmake";
    }));

    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, RequiresDetectedVersion) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_no_version";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    fs::remove(stage / "lib" / "cmake" / "Viper" / "ViperConfigVersion.cmake");
    fs::remove(stage / "include" / "viper" / "version.hpp");

    EXPECT_THROWS(gatherToolchainInstallManifest(stage), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, AllowsUniversalOnlyForMacOS) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_universal";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);

    manifest.platform = "macos";
    manifest.arch = "universal";
    EXPECT_NO_THROW(validateToolchainInstallManifest(manifest));

    manifest.platform = "linux";
    EXPECT_THROWS(validateToolchainInstallManifest(manifest), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, InstallPathMappingPreservesRelativeLayout) {
    ToolchainFileEntry file;
    file.kind = ToolchainFileKind::CMakeConfig;
    file.stagedRelativePath = "lib/cmake/Viper/ViperConfig.cmake";

    EXPECT_EQ(mapInstallPath(file, InstallPathPolicy::PortableArchive),
              std::string("lib/cmake/Viper/ViperConfig.cmake"));
    EXPECT_EQ(mapInstallPath(file, InstallPathPolicy::MacOSUsrLocalViperRoot),
              std::string("/usr/local/viper/lib/cmake/Viper/ViperConfig.cmake"));
    EXPECT_EQ(mapInstallPath(file, InstallPathPolicy::LinuxUsrRoot),
              std::string("/usr/lib/cmake/Viper/ViperConfig.cmake"));
    EXPECT_EQ(mapInstallPath(file, InstallPathPolicy::WindowsProgramFilesRoot),
              std::string("C:\\Program Files\\Viper\\lib\\cmake\\Viper\\ViperConfig.cmake"));

    ToolchainFileEntry doc;
    doc.kind = ToolchainFileKind::Doc;
    doc.stagedRelativePath = "LICENSE";
    EXPECT_EQ(mapInstallPath(doc, InstallPathPolicy::LinuxUsrRoot),
              std::string("/usr/share/doc/viper/LICENSE"));
}

TEST(ToolchainInstallManifest, ValidationAcceptsCaseVariantCMakeConfigPath) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_cmake_case";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    for (auto &entry : manifest.files) {
        if (entry.stagedRelativePath == "lib/cmake/Viper/ViperConfig.cmake")
            entry.stagedRelativePath = "lib/cmake/viper/ViperConfig.cmake";
        else if (entry.stagedRelativePath == "lib/cmake/Viper/ViperTargets.cmake")
            entry.stagedRelativePath = "lib/cmake/viper/ViperTargets.cmake";
    }
    EXPECT_NO_THROW(validateToolchainInstallManifest(manifest));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, TotalSizeDetectsOverflow) {
    ToolchainInstallManifest manifest;
    ToolchainFileEntry a;
    a.sizeBytes = std::numeric_limits<uint64_t>::max();
    ToolchainFileEntry b;
    b.sizeBytes = 1;
    manifest.files = {a, b};
    EXPECT_THROWS(manifest.totalSizeBytes(), std::overflow_error);
}

TEST(ToolchainInstallManifest, RejectsInvalidArchitecture) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_bad_arch";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.arch = "amd64";

    EXPECT_THROWS(validateToolchainInstallManifest(manifest), std::runtime_error);
    LinuxToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "bad.deb").string();
    EXPECT_THROWS(buildToolchainDebPackage(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, RequiresOptionalLibraryOnlyWhenStagedMetadataReferencesIt) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_optional_refs";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    {
        std::ofstream targets(stage / "lib" / "cmake" / "Viper" / "ViperTargets.cmake",
                              std::ios::app);
        targets << "vipergfx\n";
    }
    std::error_code ec;
    fs::remove(stage / "lib" / "libvipergfx.a", ec);
    ec.clear();
    fs::remove(stage / "lib" / "vipergfx.lib", ec);

    EXPECT_THROWS(gatherToolchainInstallManifest(stage), std::runtime_error);
    fs::remove_all(tmpRoot);
}

#if !defined(_WIN32)
TEST(ToolchainInstallManifest, RejectsSymlinkEscapingStage) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_symlink_escape";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    {
        std::ofstream outside(tmpRoot / "outside.txt");
        outside << "outside";
    }
    fs::create_symlink(tmpRoot / "outside.txt", stage / "share" / "doc" / "viper" / "escape.txt");

    EXPECT_THROWS(gatherToolchainInstallManifest(stage), std::runtime_error);
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, PreservesInternalSymlinkTargets) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_symlink_internal";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    fs::create_symlink("../share/doc/viper/README.md", stage / "bin" / "viper-readme");

    const auto manifest = gatherToolchainInstallManifest(stage);
    auto it = std::find_if(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
        return entry.stagedRelativePath == "bin/viper-readme";
    });
    ASSERT_TRUE(it != manifest.files.end());
    EXPECT_TRUE(it->symlink);
    EXPECT_EQ(it->symlinkTarget, std::string("../share/doc/viper/README.md"));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainInstallManifest, HandlesSymlinkedStageDirWithAbsoluteInstallManifest) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_manifest_stage_alias";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    const fs::path stageLink = tmpRoot / "stage-link";
    fs::create_directory_symlink(stage, stageLink);
    const fs::path installManifest = tmpRoot / "install_manifest.txt";
    {
        std::ofstream out(installManifest);
        for (fs::recursive_directory_iterator it(stageLink); it != fs::recursive_directory_iterator();
             ++it) {
            if (it->is_regular_file() || it->is_symlink()) {
                const fs::path rel = it->path().lexically_relative(stageLink);
                out << (stageLink / rel).string() << "\n";
            }
        }
    }

    const auto manifest = gatherToolchainInstallManifest(stageLink, installManifest);
    EXPECT_TRUE(std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
        return entry.stagedRelativePath == "bin/viper" || entry.stagedRelativePath == "bin/viper.exe";
    }));
    EXPECT_TRUE(std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
        return entry.stagedRelativePath == "lib/cmake/Viper/ViperConfig.cmake";
    }));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainWindowsPackageBuilder, RejectsDirectorySymlinkDereference) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_windows_dir_symlink";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    fs::create_directory_symlink("../share/doc", stage / "bin" / "doc-link");
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.arch = "x64";
    manifest.platform = "windows";

    WindowsToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "bad.exe").string();
    params.archStr = "x64";
    EXPECT_THROWS(buildWindowsToolchainInstaller(params), std::runtime_error);
    fs::remove_all(tmpRoot);
}
#endif

TEST(ToolchainLinuxPackageBuilder, BuildsDebFromManifest) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_deb_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.platform = "linux";

    LinuxToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper_9.8.7_amd64.deb").string();
    buildToolchainDebPackage(params);

    const auto debBytes = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyDeb(debBytes, err));
    std::ostringstream payloadErr;
    EXPECT_TRUE(verifyDebPayload(debBytes,
                                 {"usr/bin/viper",
                                  "usr/share/applications/viper-source.desktop",
                                  "usr/share/applications/viper-il.desktop",
                                  "usr/share/mime/packages/viper.xml"},
                                 payloadErr));
    const std::string control = debControlText(debBytes);
    EXPECT_CONTAINS(control, "Depends: libc6, libstdc++6 | libc++1");
    const std::string postrm = debControlEntryText(debBytes, "postrm");
    EXPECT_CONTAINS(postrm, "mandb");
    EXPECT_CONTAINS(postrm, "update-mime-database");
    fs::remove_all(tmpRoot);
}

TEST(ToolchainLinuxPackageBuilder, BuildsTarballFromManifest) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_tar_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    {
        const fs::path helperPath = stage / "share" / "doc" / "viper" / "helper.sh";
        std::ofstream helper(helperPath);
        helper << "#!/bin/sh\n";
        helper.close();
        fs::permissions(helperPath,
                        fs::perms::owner_read | fs::perms::owner_write |
                            fs::perms::owner_exec | fs::perms::group_read |
                            fs::perms::group_exec,
                        fs::perm_options::replace);
    }
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.platform = "linux";

    LinuxToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-9.8.7-linux-x64.tar.gz").string();
    buildToolchainTarball(params);

    const auto tarGz = readFile(params.outputPath);
    ASSERT_TRUE(tarGz.size() >= 2);
    EXPECT_EQ(tarGz[0], static_cast<uint8_t>(0x1F));
    EXPECT_EQ(tarGz[1], static_cast<uint8_t>(0x8B));
    const auto tarBytes = inflateGzipPayload(tarGz);
    const std::string topDir =
        std::string("viper-9.8.7-") + manifest.platform + "-" + manifest.arch + "/";
    EXPECT_EQ(tarFirstEntryName(tarBytes), topDir);
    std::ostringstream payloadErr;
    EXPECT_TRUE(verifyTarGzPayload(tarGz,
                                   {topDir + "bin/viper",
                                    topDir + "share/applications/viper-source.desktop",
                                    topDir + "share/applications/viper-il.desktop",
                                    topDir + "share/mime/packages/viper.xml"},
                                   payloadErr));
    std::vector<uint8_t> sourceDesktop;
    ASSERT_TRUE(tarEntryData(
        tarBytes, topDir + "share/applications/viper-source.desktop", sourceDesktop));
    EXPECT_CONTAINS(std::string(sourceDesktop.begin(), sourceDesktop.end()),
                    "Exec=viper run %f");
    std::vector<uint8_t> ilDesktop;
    ASSERT_TRUE(tarEntryData(tarBytes, topDir + "share/applications/viper-il.desktop", ilDesktop));
    EXPECT_CONTAINS(std::string(ilDesktop.begin(), ilDesktop.end()),
                    "Exec=viper -run %f");
    uint32_t helperMode = 0;
    EXPECT_TRUE(tarEntryMode(tarBytes, topDir + "share/doc/viper/helper.sh", helperMode));
    EXPECT_EQ(helperMode, static_cast<uint32_t>(0750));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainLinuxPackageBuilder, TarballNormalizesDebianEpochVersionInTopDirectory) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_tar_epoch_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.platform = "linux";
    manifest.version = "2:9.8.7";

    LinuxToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-epoch.tar.gz").string();
    buildToolchainTarball(params);

    const auto tarGz = readFile(params.outputPath);
    const auto tarBytes = inflateGzipPayload(tarGz);
    const std::string topDir =
        std::string("viper-2_9.8.7-") + manifest.platform + "-" + manifest.arch + "/";
    EXPECT_EQ(tarFirstEntryName(tarBytes), topDir);
    std::ostringstream err;
    EXPECT_TRUE(verifyTarGzPayload(tarGz, {topDir + "bin/viper"}, err));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainLinuxPackageBuilder, NonLinuxTarballOmitsLinuxDesktopMetadata) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_tar_nonlinux_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.platform = "macos";

    LinuxToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-9.8.7-macos-x64.tar.gz").string();
    buildToolchainTarball(params);

    const auto tarGz = readFile(params.outputPath);
    const auto tarBytes = inflateGzipPayload(tarGz);
    const std::string topDir =
        std::string("viper-9.8.7-") + manifest.platform + "-" + manifest.arch + "/";
    std::vector<uint8_t> ignored;
    EXPECT_FALSE(tarEntryData(tarBytes, topDir + "share/applications/viper-source.desktop", ignored));
    EXPECT_FALSE(tarEntryData(tarBytes, topDir + "share/mime/packages/viper.xml", ignored));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainWindowsPackageBuilder, BuildsInstallerFromManifest) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_windows_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
#if !defined(_WIN32)
    fs::copy_file(stage / "bin" / "viper", stage / "bin" / "viper.exe");
    fs::remove(stage / "bin" / "viper");
#endif
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.arch = "x64";
    manifest.platform = "windows";

    WindowsToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-toolchain-setup.exe").string();
    params.archStr = "x64";
    buildWindowsToolchainInstaller(params);

    const auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlay(pe, err));
    std::ostringstream payloadErr;
    EXPECT_TRUE(verifyPEZipOverlayPayload(
        pe,
        {"app/bin/viper.exe",
         "app/bin/viper-dev.cmd",
         "app/bin/viper-install-vscode-extension.cmd",
         "meta/viper_developer_prompt.lnk",
         "meta/viper_vscode_extension.lnk",
         "app/uninstall.exe",
         "meta/manifest.sha256"},
        payloadErr));
    EXPECT_GE(peOptionalHeaderField64(pe, 72), static_cast<uint64_t>(0x200000));
    EXPECT_GE(peOptionalHeaderField64(pe, 80), static_cast<uint64_t>(0x100000));
    EXPECT_TRUE(containsUtf16LE(pe, "Environment"));
    EXPECT_TRUE(containsUtf16LEStringData(pe, "%LocalAppData%\\Viper\\bin\\viper.exe"));
    EXPECT_TRUE(containsAscii(pe, "asInvoker"));
    EXPECT_TRUE(containsUtf16LE(pe, "VAPSOriginalPath"));
    EXPECT_TRUE(containsUtf16LE(pe, "VAPSPathEntry"));
    EXPECT_TRUE(containsUtf16LE(pe, "bin\\viper.exe"));
    EXPECT_TRUE(containsUtf16LEStringData(pe, "Viper Developer Prompt"));
    EXPECT_FALSE(containsUtf16LE(pe, "Software\\Classes\\.zia"));
    const std::string sendMessageImport = "SendMessageTimeoutW";
    EXPECT_TRUE(std::search(pe.begin(),
                            pe.end(),
                            sendMessageImport.begin(),
                            sendMessageImport.end()) != pe.end());
    const std::string shellFileOperationImport = "SHFileOperationW";
    EXPECT_TRUE(std::search(pe.begin(),
                            pe.end(),
                            shellFileOperationImport.begin(),
                            shellFileOperationImport.end()) != pe.end());
    fs::remove_all(tmpRoot);
}

TEST(ToolchainWindowsPackageBuilder, HonorsMachineScopeAndFileAssociations) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_windows_machine_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
#if !defined(_WIN32)
    fs::copy_file(stage / "bin" / "viper", stage / "bin" / "viper.exe");
    fs::remove(stage / "bin" / "viper");
#endif
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.arch = "x64";
    manifest.platform = "windows";

    WindowsToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-toolchain-machine-setup.exe").string();
    params.archStr = "x64";
    params.installScope = "machine";
    params.registerFileAssociations = true;
    params.createStartMenuShortcuts = false;
    buildWindowsToolchainInstaller(params);

    const auto pe = readFile(params.outputPath);
    std::ostringstream err;
    EXPECT_TRUE(verifyPEZipOverlay(pe, err));
    EXPECT_TRUE(containsUtf16LE(
        pe, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"));
    EXPECT_TRUE(containsAscii(pe, "requireAdministrator"));
    EXPECT_TRUE(containsUtf16LE(pe, "Software\\Classes\\.zia"));
    EXPECT_TRUE(containsUtf16LE(pe, "org.viper.toolchain.zia"));
    EXPECT_TRUE(containsUtf16LE(pe, " run"));
    EXPECT_TRUE(containsUtf16LE(pe, " -run"));
    EXPECT_FALSE(containsUtf16LE(pe, "Viper Developer Prompt"));
    fs::remove_all(tmpRoot);
}

TEST(ToolchainMacOSPackageBuilder, RejectsLossyManifestVersionWithoutOverride) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_pkg_version_lossy";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    auto manifest = gatherToolchainInstallManifest(stage);
    manifest.platform = "macos";
    manifest.arch = "x64";
    manifest.version = "1.2.3+build";

    MacOSToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-toolchain.pkg").string();
    EXPECT_THROWS(buildMacOSToolchainPackage(params), std::runtime_error);
    params.packageVersion = "1.2.3.4";
#if defined(__APPLE__)
    EXPECT_NO_THROW(buildMacOSToolchainPackage(params));
#else
    (void)params;
#endif
    fs::remove_all(tmpRoot);
}

#if defined(__APPLE__)
TEST(ToolchainMacOSPackageBuilder, BuildsPkgFromManifest) {
    namespace fs = std::filesystem;
    const fs::path tmpRoot = fs::temp_directory_path() / "viper_toolchain_pkg_stage";
    fs::remove_all(tmpRoot);
    const fs::path stage = createMockToolchainStage(tmpRoot);
    const auto manifest = gatherToolchainInstallManifest(stage);

    MacOSToolchainBuildParams params;
    params.manifest = manifest;
    params.outputPath = (tmpRoot / "viper-toolchain.pkg").string();
    buildMacOSToolchainPackage(params);

    const auto pkgBytes = readFile(params.outputPath);
    ASSERT_TRUE(pkgBytes.size() >= 4);
    EXPECT_EQ(pkgBytes[0], static_cast<uint8_t>('x'));
    EXPECT_EQ(pkgBytes[1], static_cast<uint8_t>('a'));
    EXPECT_EQ(pkgBytes[2], static_cast<uint8_t>('r'));
    EXPECT_EQ(pkgBytes[3], static_cast<uint8_t>('!'));
    fs::remove_all(tmpRoot);
}
#endif

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
