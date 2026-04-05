//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_native_linker.cpp
// Purpose: Coverage for top-level native linker target gating and diagnostics.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"
#include "codegen/common/objfile/CoffWriter.hpp"
#include "codegen/common/objfile/MachOWriter.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace viper::codegen::linker;
using namespace viper::codegen::objfile;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static std::string tmpPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/native-linker"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return {};
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

static uint16_t readLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t readLE64(const uint8_t *p) {
    return static_cast<uint64_t>(readLE32(p)) | (static_cast<uint64_t>(readLE32(p + 4)) << 32);
}

static bool containsAscii(const std::vector<uint8_t> &data, const std::string &needle) {
    if (needle.empty() || data.size() < needle.size())
        return false;
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

static std::string readMachOName(const uint8_t *p, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && p[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char *>(p), len);
}

static bool findMachOSection(const std::vector<uint8_t> &data,
                             const std::string &segmentName,
                             const std::string &sectionName,
                             uint32_t &alignLog2,
                             uint32_t &flags) {
    static constexpr uint32_t LC_SEGMENT_64 = 0x19;
    if (data.size() < 32)
        return false;

    const uint32_t ncmds = readLE32(data.data() + 16);
    size_t off = 32;
    for (uint32_t i = 0; i < ncmds; ++i) {
        if (off + 8 > data.size())
            return false;
        const uint32_t cmd = readLE32(data.data() + off);
        const uint32_t cmdsize = readLE32(data.data() + off + 4);
        if (cmd == LC_SEGMENT_64 && off + 72 <= data.size()) {
            const uint32_t nsects = readLE32(data.data() + off + 64);
            size_t secOff = off + 72;
            for (uint32_t si = 0; si < nsects; ++si) {
                if (secOff + 80 > data.size())
                    return false;
                const std::string sect = readMachOName(data.data() + secOff, 16);
                const std::string seg = readMachOName(data.data() + secOff + 16, 16);
                if (seg == segmentName && sect == sectionName) {
                    alignLog2 = readLE32(data.data() + secOff + 52);
                    flags = readLE32(data.data() + secOff + 64);
                    return true;
                }
                secOff += 80;
            }
        }
        if (cmdsize == 0)
            return false;
        off += cmdsize;
    }
    return false;
}

static bool countMachOSymbols(const std::vector<uint8_t> &data,
                              const std::string &symbolName,
                              uint32_t &definedCount,
                              uint32_t &undefinedCount) {
    static constexpr uint32_t LC_SYMTAB = 0x02;
    static constexpr uint8_t N_TYPE = 0x0e;
    static constexpr uint8_t N_UNDF = 0x00;
    static constexpr uint8_t N_SECT = 0x0e;

    definedCount = 0;
    undefinedCount = 0;
    if (data.size() < 32)
        return false;

    uint32_t symoff = 0;
    uint32_t nsyms = 0;
    uint32_t stroff = 0;
    uint32_t strsize = 0;
    const uint32_t ncmds = readLE32(data.data() + 16);
    size_t off = 32;
    for (uint32_t i = 0; i < ncmds; ++i) {
        if (off + 8 > data.size())
            return false;
        const uint32_t cmd = readLE32(data.data() + off);
        const uint32_t cmdsize = readLE32(data.data() + off + 4);
        if (cmd == LC_SYMTAB) {
            if (off + 24 > data.size())
                return false;
            symoff = readLE32(data.data() + off + 8);
            nsyms = readLE32(data.data() + off + 12);
            stroff = readLE32(data.data() + off + 16);
            strsize = readLE32(data.data() + off + 20);
            break;
        }
        if (cmdsize == 0)
            return false;
        off += cmdsize;
    }

    if (symoff == 0 || nsyms == 0 || stroff == 0 || stroff + strsize > data.size())
        return false;
    if (symoff + static_cast<size_t>(nsyms) * 16 > data.size())
        return false;

    for (uint32_t i = 0; i < nsyms; ++i) {
        const uint8_t *entry = data.data() + symoff + static_cast<size_t>(i) * 16;
        const uint32_t strx = readLE32(entry);
        if (strx >= strsize)
            return false;
        const char *namePtr = reinterpret_cast<const char *>(data.data() + stroff + strx);
        const size_t maxLen = strsize - strx;
        const std::string name = readMachOName(reinterpret_cast<const uint8_t *>(namePtr), maxLen);
        if (name != symbolName)
            continue;
        const uint8_t type = entry[4] & N_TYPE;
        if (type == N_SECT)
            ++definedCount;
        else if (type == N_UNDF)
            ++undefinedCount;
    }
    return true;
}

int main() {
    {
        NativeLinkerOptions opts;
        opts.platform = LinkPlatform::Windows;
        opts.arch = LinkArch::AArch64;
        opts.objPath = "does-not-matter.obj";
        opts.exePath = "does-not-matter.exe";

        std::ostringstream out;
        std::ostringstream err;
        const int rc = nativeLink(opts, out, err);

        CHECK(rc != 0);
        CHECK(err.str().find("not implemented yet") == std::string::npos);
        CHECK(err.str().find("failed to read object file") != std::string::npos);
    }

    {
        CodeSection text;
        CodeSection rodata;
        text.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t chkstkIdx = text.findOrDeclareSymbol("__chkstk");
        text.addRelocation(RelocKind::A64Call26, chkstkIdx, 0);
        text.emit32LE(0x94000000U); // bl __chkstk
        text.emit32LE(0xD2800020U); // mov x0, #1
        text.emit32LE(0xD65F03C0U); // ret

        const std::string objPath = tmpPath("arm64_chkstk.obj");
        const std::string exePath = tmpPath("arm64_chkstk.exe");

        std::ostringstream writerErr;
        CoffWriter writer(ObjArch::AArch64);
        CHECK(writer.write(objPath, text, rodata, writerErr));
        CHECK(writerErr.str().empty());

        NativeLinkerOptions opts;
        opts.platform = LinkPlatform::Windows;
        opts.arch = LinkArch::AArch64;
        opts.objPath = objPath;
        opts.exePath = exePath;
        opts.entrySymbol = "main";
        opts.stackSize = 0x200000;

        std::ostringstream out;
        std::ostringstream err;
        const int rc = nativeLink(opts, out, err);
        CHECK(rc == 0);
        CHECK(err.str().find("error:") == std::string::npos);
        CHECK(std::filesystem::exists(exePath));

        const std::vector<uint8_t> exe = readFile(exePath);
        CHECK(exe.size() > 0x40);
        CHECK(exe[0] == 'M');
        CHECK(exe[1] == 'Z');
        if (exe.size() > 0x40) {
            const uint32_t peOff = readLE32(exe.data() + 0x3C);
            CHECK(peOff + 6 <= exe.size());
            if (peOff + 6 <= exe.size()) {
                CHECK(exe[peOff + 0] == 'P');
                CHECK(exe[peOff + 1] == 'E');
                CHECK(exe[peOff + 2] == 0);
                CHECK(exe[peOff + 3] == 0);
                CHECK(readLE16(exe.data() + peOff + 4) == 0xAA64);
                const size_t optHeaderOff = peOff + 24;
                CHECK(optHeaderOff + 80 <= exe.size());
                if (optHeaderOff + 80 <= exe.size())
                    CHECK(readLE64(exe.data() + optHeaderOff + 72) == 0x200000ULL);
            }
        }
    }

    {
        CodeSection text;
        CodeSection rodata;
        text.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
        text.findOrDeclareSymbol("MTLCreateSystemDefaultDevice");
        text.findOrDeclareSymbol("OBJC_CLASS_$_CAMetalLayer");
        text.findOrDeclareSymbol("objc_msgSend$newCommandQueue");
        text.emit32LE(0xD2800000U); // mov x0, #0
        text.emit32LE(0xD65F03C0U); // ret

        const std::string objPath = tmpPath("macos_objc_frameworks.o");
        const std::string exePath = tmpPath("macos_objc_frameworks");

        std::ostringstream writerErr;
        MachOWriter writer(ObjArch::AArch64);
        CHECK(writer.write(objPath, text, rodata, writerErr));
        CHECK(writerErr.str().empty());

        NativeLinkerOptions opts;
        opts.platform = LinkPlatform::macOS;
        opts.arch = LinkArch::AArch64;
        opts.objPath = objPath;
        opts.exePath = exePath;
        opts.entrySymbol = "main";

        std::ostringstream out;
        std::ostringstream err;
        const int rc = nativeLink(opts, out, err);
        CHECK(rc == 0);
        CHECK(err.str().find("error:") == std::string::npos);
        CHECK(err.str().find("undefined symbol 'main'") == std::string::npos);
        CHECK(std::filesystem::exists(exePath));

        const std::vector<uint8_t> exe = readFile(exePath);
        CHECK(containsAscii(exe, "Metal.framework"));
        CHECK(containsAscii(exe, "QuartzCore.framework"));

        uint32_t alignLog2 = 0;
        uint32_t flags = 0;
        CHECK(findMachOSection(exe, "__DATA", "__objc_selrefs", alignLog2, flags));
        CHECK(alignLog2 == 3);
        CHECK(flags == 0x10000005U);

        uint32_t definedMain = 0;
        uint32_t undefinedMain = 0;
        CHECK(countMachOSymbols(exe, "_main", definedMain, undefinedMain));
        CHECK(definedMain == 1);
        CHECK(undefinedMain == 0);
    }

    if (gFail == 0) {
        std::cout << "All NativeLinker tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " NativeLinker test(s) FAILED.\n";
    return EXIT_FAILURE;
}
