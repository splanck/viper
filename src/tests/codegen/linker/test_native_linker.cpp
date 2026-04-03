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
            }
        }
    }

    if (gFail == 0) {
        std::cout << "All NativeLinker tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " NativeLinker test(s) FAILED.\n";
    return EXIT_FAILURE;
}
