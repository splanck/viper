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
#include "LnkWriter.hpp"
#include "PEBuilder.hpp"
#include "PkgDeflate.hpp"
#include "PkgGzip.hpp"
#include "PkgPNG.hpp"
#include "PkgVerify.hpp"
#include "PlistGenerator.hpp"
#include "TarWriter.hpp"
#include "ZipReader.hpp"
#include "ZipWriter.hpp"

#include <cstring>
#include <sstream>

using namespace viper::pkg;

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

static uint32_t readBE32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
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
    EXPECT_TRUE(std::memcmp(data.data(), "./usr/bin/test", 14) == 0);
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

TEST(DesktopEntry, CategoryField) {
    DesktopEntryParams params;
    params.name = "Game";
    params.execPath = "/usr/bin/game";
    params.iconName = "game";
    params.categories = "Game;ArcadeGame;";

    auto content = generateDesktopEntry(params);
    EXPECT_CONTAINS(content, "Categories=Game;ArcadeGame;");
}

// ============================================================================
// MIME Type XML Tests
// ============================================================================

TEST(MimeXml, ContainsGlobPattern) {
    std::vector<FileAssoc> assocs;
    assocs.push_back({".zia", "Zia Source File", "text/x-zia"});

    auto xml = generateMimeTypeXml("mypackage", assocs);
    EXPECT_CONTAINS(xml, "text/x-zia");
    EXPECT_CONTAINS(xml, "*.zia");
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

TEST(Verify, DebValid) {
    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberString("control.tar.gz", "ctrl");
    ar.addMemberString("data.tar.gz", "data");
    auto data = ar.finish();

    std::ostringstream err;
    EXPECT_TRUE(verifyDeb(data, err));
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

// ============================================================================
// InstallerStub Integration Tests
// ============================================================================

TEST(InstallerStub, GeneratesValidPE) {
    auto stub = buildInstallerStub("TestApp", "TestApp", "x64");

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

TEST(InstallerStub, UninstallerGeneratesValidPE) {
    auto stub = buildUninstallerStub("TestApp", "x64");

    EXPECT_GT(stub.textSection.size(), static_cast<size_t>(10));
    EXPECT_FALSE(stub.imports.empty());

    PEBuildParams pe;
    pe.textSection = stub.textSection;
    pe.imports = stub.imports;
    pe.manifest = generateAsInvokerManifest();
    auto peBytes = buildPE(pe);

    std::ostringstream err;
    EXPECT_TRUE(verifyPE(peBytes, err));
}

TEST(InstallerStub, ARM64ReturnsPlaceholder) {
    auto stub = buildInstallerStub("TestApp", "TestApp", "arm64");
    // ARM64 placeholder is just `ret` = 4 bytes
    EXPECT_EQ(stub.textSection.size(), static_cast<size_t>(4));
    EXPECT_EQ(stub.textSection[0], static_cast<uint8_t>(0xC0));
    EXPECT_EQ(stub.textSection[3], static_cast<uint8_t>(0xD6));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
