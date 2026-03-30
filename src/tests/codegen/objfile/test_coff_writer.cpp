//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_coff_writer.cpp
// Purpose: Regression tests for COFF object emission, including Win64 unwind
//          metadata for x86_64 native objects.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/objfile/CoffWriter.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

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
#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        check((cond), #cond, __LINE__);                                                            \
        if (!(cond))                                                                               \
            return EXIT_FAILURE;                                                                   \
    } while (0)

static const ObjSection *findSection(const ObjFile &obj, const std::string &name) {
    for (size_t i = 1; i < obj.sections.size(); ++i) {
        if (obj.sections[i].name == name)
            return &obj.sections[i];
    }
    return nullptr;
}

static uint32_t findSymbolIndex(const ObjFile &obj, const std::string &name) {
    for (uint32_t i = 1; i < obj.symbols.size(); ++i) {
        if (obj.symbols[i].name == name)
            return i;
    }
    return 0;
}

int main() {
    std::filesystem::create_directories("build/test-out");

    CodeSection text;
    CodeSection rodata;

    const uint32_t mainSym =
        text.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
    text.emit8(0x55);             // push rbp
    text.emit8(0x48);
    text.emit8(0x89);
    text.emit8(0xE5);             // mov rbp, rsp
    text.emit8(0x48);
    text.emit8(0x83);
    text.emit8(0xEC);
    text.emit8(0x20);             // sub rsp, 32
    text.emit8(0xC3);             // ret

    Win64UnwindEntry unwind{};
    unwind.symbolIndex = mainSym;
    unwind.functionLength = 9;
    unwind.prologueSize = 8;
    unwind.codes.push_back(
        {Win64UnwindCode::Kind::PushNonVol, 1, 5, 0});   // RBP
    unwind.codes.push_back(
        {Win64UnwindCode::Kind::AllocStack, 8, 0, 32});  // sub rsp, 32
    text.addWin64UnwindEntry(std::move(unwind));

    std::ostringstream err;
    CoffWriter writer(ObjArch::X86_64);
    const std::string path = "build/test-out/coff_unwind.obj";
    ASSERT(writer.write(path, text, rodata, err));

    ObjFile obj;
    ASSERT(readObjFile(path, obj, err));

    const ObjSection *textSec = findSection(obj, ".text");
    const ObjSection *xdataSec = findSection(obj, ".xdata");
    const ObjSection *pdataSec = findSection(obj, ".pdata");

    ASSERT(textSec != nullptr);
    ASSERT(xdataSec != nullptr);
    ASSERT(pdataSec != nullptr);

    CHECK(xdataSec->data.size() >= 8);
    CHECK(xdataSec->data[0] == 0x01); // version 1, flags 0
    CHECK(xdataSec->data[1] == 8);    // prologue size
    CHECK(xdataSec->data[2] == 2);    // two unwind slots

    CHECK(pdataSec->data.size() == 12);
    CHECK(pdataSec->relocs.size() == 3);
    CHECK(pdataSec->relocs[0].type == 3);
    CHECK(pdataSec->relocs[1].type == 3);
    CHECK(pdataSec->relocs[2].type == 3);

    const uint32_t mainIdx = findSymbolIndex(obj, "main");
    const uint32_t xdataIdx = findSymbolIndex(obj, "$xdata$0");
    CHECK(mainIdx != 0);
    CHECK(xdataIdx != 0);
    CHECK(pdataSec->relocs[0].symIndex == mainIdx);
    CHECK(pdataSec->relocs[1].symIndex == mainIdx);
    CHECK(pdataSec->relocs[2].symIndex == xdataIdx);

    if (gFail == 0) {
        std::cout << "All CoffWriter tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " CoffWriter test(s) FAILED.\n";
    return EXIT_FAILURE;
}
