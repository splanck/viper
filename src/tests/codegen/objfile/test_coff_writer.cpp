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
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

static uint32_t readLE32(const std::vector<uint8_t> &bytes, size_t off) {
    return static_cast<uint32_t>(bytes[off]) | (static_cast<uint32_t>(bytes[off + 1]) << 8) |
           (static_cast<uint32_t>(bytes[off + 2]) << 16) |
           (static_cast<uint32_t>(bytes[off + 3]) << 24);
}

static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static void writeLE16(std::vector<uint8_t> &bytes, size_t off, uint16_t v) {
    bytes[off] = static_cast<uint8_t>(v);
    bytes[off + 1] = static_cast<uint8_t>(v >> 8);
}

static void writeLE32(std::vector<uint8_t> &bytes, size_t off, uint32_t v) {
    bytes[off] = static_cast<uint8_t>(v);
    bytes[off + 1] = static_cast<uint8_t>(v >> 8);
    bytes[off + 2] = static_cast<uint8_t>(v >> 16);
    bytes[off + 3] = static_cast<uint8_t>(v >> 24);
}

int main() {
    std::filesystem::create_directories("build/test-out");

    CodeSection text;
    CodeSection rodata;

    const uint32_t mainSym = text.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
    text.emit8(0x55); // push rbp
    text.emit8(0x48);
    text.emit8(0x89);
    text.emit8(0xE5); // mov rbp, rsp
    text.emit8(0x48);
    text.emit8(0x83);
    text.emit8(0xEC);
    text.emit8(0x20); // sub rsp, 32
    text.emit8(0xC3); // ret

    Win64UnwindEntry unwind{};
    unwind.symbolIndex = mainSym;
    unwind.functionLength = 9;
    unwind.prologueSize = 8;
    unwind.codes.push_back({Win64UnwindCode::Kind::PushNonVol, 1, 5, 0});  // RBP
    unwind.codes.push_back({Win64UnwindCode::Kind::AllocStack, 8, 0, 32}); // sub rsp, 32
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
    CHECK(readLE32(pdataSec->data, 0) == 0);
    CHECK(readLE32(pdataSec->data, 4) == 9);
    CHECK(readLE32(pdataSec->data, 8) == 0);
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

    {
        CodeSection armText;
        CodeSection armRodata;
        armText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t calleeIdx = armText.findOrDeclareSymbol("callee");
        armText.addRelocation(RelocKind::A64Call26, calleeIdx, 0);
        armText.emit32LE(0x94000000); // bl placeholder
        armText.emit32LE(0xD65F03C0); // ret

        std::ostringstream armErr;
        CoffWriter armWriter(ObjArch::AArch64);
        const std::string armPath = "build/test-out/coff_arm64.obj";
        ASSERT(armWriter.write(armPath, armText, armRodata, armErr));

        ObjFile armObj;
        ASSERT(readObjFile(armPath, armObj, armErr));
        CHECK(armObj.machine == 0xAA64);

        const ObjSection *armTextSec = findSection(armObj, ".text");
        ASSERT(armTextSec != nullptr);
        CHECK(armTextSec->relocs.size() == 1);
        CHECK(armTextSec->relocs[0].type == 3);
        CHECK(armTextSec->relocs[0].addend == 0);
        CHECK(armObj.symbols[armTextSec->relocs[0].symIndex].name == "callee");
        CHECK(findSection(armObj, ".xdata") == nullptr);
        CHECK(findSection(armObj, ".pdata") == nullptr);
    }

    {
        CodeSection armText;
        CodeSection armRodata;
        const uint32_t funcIdx =
            armText.defineSymbol("win_arm64_func", SymbolBinding::Global, SymbolSection::Text);
        armText.emit32LE(0xA9BF7BFD); // stp x29, x30, [sp, #-16]!
        armText.emit32LE(0x910003FD); // mov x29, sp
        armText.emit32LE(0xD65F03C0); // ret

        WinArm64UnwindEntry armUnwind{};
        armUnwind.symbolIndex = funcIdx;
        armUnwind.functionLength = 12;
        armUnwind.prologueSize = 8;
        armUnwind.unwindCodes = {0xE3, 0x81, 0xE4};
        armUnwind.packedEpilogInHeader = true;
        armUnwind.epilogCodeIndex = 0;
        armText.addWinArm64UnwindEntry(std::move(armUnwind));

        std::ostringstream armErr;
        CoffWriter armWriter(ObjArch::AArch64);
        const std::string armPath = "build/test-out/coff_arm64_unwind.obj";
        ASSERT(armWriter.write(armPath, armText, armRodata, armErr));

        ObjFile armObj;
        ASSERT(readObjFile(armPath, armObj, armErr));
        const ObjSection *armXdataSec = findSection(armObj, ".xdata");
        const ObjSection *armPdataSec = findSection(armObj, ".pdata");
        ASSERT(armXdataSec != nullptr);
        ASSERT(armPdataSec != nullptr);

        CHECK(armXdataSec->data.size() == 8);
        const uint32_t header = readLE32(armXdataSec->data, 0);
        CHECK((header & 0x3FFFFu) == 3u);
        CHECK(((header >> 21) & 1u) == 1u);
        CHECK(((header >> 27) & 0x1Fu) == 1u);
        CHECK(armXdataSec->data[4] == 0xE3);
        CHECK(armXdataSec->data[5] == 0x81);
        CHECK(armXdataSec->data[6] == 0xE4);

        CHECK(armPdataSec->data.size() == 8);
        CHECK(armPdataSec->relocs.size() == 2);
        CHECK(armPdataSec->relocs[0].type == 2);
        CHECK(armPdataSec->relocs[1].type == 2);
        const uint32_t funcSym = findSymbolIndex(armObj, "win_arm64_func");
        const uint32_t xdataSym = findSymbolIndex(armObj, "$xdata$0");
        CHECK(funcSym != 0);
        CHECK(xdataSym != 0);
        CHECK(armPdataSec->relocs[0].symIndex == funcSym);
        CHECK(armPdataSec->relocs[1].symIndex == xdataSym);
    }

    {
        CodeSection relocText;
        CodeSection relocRodata;
        relocText.defineSymbol("target_func", SymbolBinding::Local, SymbolSection::Text);
        relocText.emit8(0xC3);

        const uint32_t symIdx = relocRodata.findOrDeclareSymbol("target_func");
        relocRodata.addRelocation(RelocKind::Abs64, symIdx, 0, SymbolSection::Text);
        relocRodata.emit64LE(0);

        std::ostringstream relocErr;
        CoffWriter relocWriter(ObjArch::X86_64);
        const std::string relocPath = "build/test-out/coff_rdata_reloc.obj";
        ASSERT(relocWriter.write(relocPath, relocText, relocRodata, relocErr));

        ObjFile relocObj;
        ASSERT(readObjFile(relocPath, relocObj, relocErr));
        const ObjSection *rdataSec = findSection(relocObj, ".rdata");
        ASSERT(rdataSec != nullptr);
        CHECK(rdataSec->relocs.size() == 1);
        if (!rdataSec->relocs.empty()) {
            CHECK(rdataSec->relocs[0].type == 1); // IMAGE_REL_AMD64_ADDR64
            CHECK(relocObj.symbols[rdataSec->relocs[0].symIndex].name == "target_func");
        }
    }

    {
        CodeSection coalesceText;
        CodeSection coalesceRodata;
        const uint32_t symIdx = coalesceRodata.findOrDeclareSymbol("later_global");
        coalesceRodata.emit64LE(0);
        coalesceRodata.addRelocationAt(0, RelocKind::Abs64, symIdx, 0);

        coalesceText.defineSymbol("later_global", SymbolBinding::Global, SymbolSection::Text);
        coalesceText.emit8(0xC3);

        std::ostringstream coalesceErr;
        CoffWriter coalesceWriter(ObjArch::X86_64);
        const std::string coalescePath = "build/test-out/coff_single_external_coalesce.obj";
        ASSERT(coalesceWriter.write(
            coalescePath, coalesceText, coalesceRodata, coalesceErr));

        ObjFile coalesceObj;
        ASSERT(readObjFile(coalescePath, coalesceObj, coalesceErr));
        const ObjSection *coalesceRdata = findSection(coalesceObj, ".rdata");
        ASSERT(coalesceRdata != nullptr);
        ASSERT(coalesceRdata->relocs.size() == 1);
        const auto &targetSym = coalesceObj.symbols[coalesceRdata->relocs[0].symIndex];
        CHECK(targetSym.name == "later_global");
        CHECK(targetSym.binding == ObjSymbol::Global);
        CHECK(targetSym.sectionIndex != 0);
    }

    {
        CodeSection biasedText;
        CodeSection biasedRodata;
        biasedText.setLogicalOffsetBias(128);
        biasedText.defineSymbol("biased_func", SymbolBinding::Global, SymbolSection::Text);
        biasedText.emit8(0xC3);

        std::ostringstream biasedErr;
        CoffWriter biasedWriter(ObjArch::X86_64);
        const std::string biasedPath = "build/test-out/coff_biased_symbol.obj";
        ASSERT(biasedWriter.write(biasedPath, biasedText, biasedRodata, biasedErr));

        ObjFile biasedObj;
        ASSERT(readObjFile(biasedPath, biasedObj, biasedErr));
        const uint32_t biasedIdx = findSymbolIndex(biasedObj, "biased_func");
        ASSERT(biasedIdx != 0);
        CHECK(biasedObj.symbols[biasedIdx].offset == 0);
    }

    {
        CodeSection ambigText;
        CodeSection ambigRodata;
        ambigText.defineSymbol("dup", SymbolBinding::Local, SymbolSection::Text);
        ambigText.emit8(0xC3);
        ambigText.defineSymbol("dup", SymbolBinding::Local, SymbolSection::Text);
        ambigText.emit8(0xC3);

        const uint32_t symIdx = ambigRodata.findOrDeclareSymbol("dup");
        ambigRodata.addRelocation(RelocKind::Abs64, symIdx, 0, SymbolSection::Text);
        ambigRodata.emit64LE(0);

        std::ostringstream ambigErr;
        CoffWriter ambigWriter(ObjArch::X86_64);
        CHECK(!ambigWriter.write("build/test-out/coff_ambiguous_cross.obj",
                                 ambigText,
                                 ambigRodata,
                                 ambigErr));
        CHECK(ambigErr.str().find("ambiguous cross-section target") != std::string::npos);
    }

    {
        CodeSection armAddendText;
        CodeSection armAddendRodata;
        armAddendText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t calleeIdx = armAddendText.findOrDeclareSymbol("callee");
        armAddendText.addRelocation(RelocKind::A64Call26, calleeIdx, 4);
        armAddendText.emit32LE(0x94000000);

        std::ostringstream armAddendErr;
        CoffWriter armAddendWriter(ObjArch::AArch64);
        const std::string armAddendPath = "build/test-out/coff_arm64_branch_addend.obj";
        ASSERT(armAddendWriter.write(
            armAddendPath, armAddendText, armAddendRodata, armAddendErr));

        ObjFile armAddendObj;
        ASSERT(readObjFile(armAddendPath, armAddendObj, armAddendErr));
        const ObjSection *armAddendSec = findSection(armAddendObj, ".text");
        ASSERT(armAddendSec != nullptr);
        ASSERT(armAddendSec->relocs.size() == 1);
        CHECK(armAddendSec->relocs[0].addend == 4);
    }

    {
        CodeSection adrpText;
        CodeSection adrpRodata;
        adrpText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t targetIdx = adrpText.findOrDeclareSymbol("target_page");
        adrpText.addRelocation(RelocKind::A64AdrpPage21, targetIdx, 0x2000);
        adrpText.emit32LE(0x90000000); // adrp x0, #0

        std::ostringstream adrpErr;
        CoffWriter adrpWriter(ObjArch::AArch64);
        const std::string adrpPath = "build/test-out/coff_arm64_adrp_addend.obj";
        ASSERT(adrpWriter.write(adrpPath, adrpText, adrpRodata, adrpErr));

        ObjFile adrpObj;
        ASSERT(readObjFile(adrpPath, adrpObj, adrpErr));
        const ObjSection *adrpSec = findSection(adrpObj, ".text");
        ASSERT(adrpSec != nullptr);
        ASSERT(adrpSec->relocs.size() == 1);
        CHECK(adrpSec->relocs[0].addend == 0x2000);
    }

    {
        CodeSection wrongArchText;
        CodeSection wrongArchRodata;
        wrongArchText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t calleeIdx = wrongArchText.findOrDeclareSymbol("callee");
        wrongArchText.addRelocation(RelocKind::A64Call26, calleeIdx, 0);
        wrongArchText.emit32LE(0x94000000);

        std::ostringstream wrongArchErr;
        CoffWriter wrongArchWriter(ObjArch::X86_64);
        CHECK(!wrongArchWriter.write("build/test-out/coff_wrong_arch_reloc.obj",
                                     wrongArchText,
                                     wrongArchRodata,
                                     wrongArchErr));
        CHECK(wrongArchErr.str().find("not valid for this object architecture") !=
              std::string::npos);
    }

    {
        CodeSection badOffsetText;
        CodeSection badOffsetRodata;
        badOffsetText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t target = badOffsetText.findOrDeclareSymbol("target");
        badOffsetText.emit8(0xE8);
        badOffsetText.addRelocationAt(1, RelocKind::Branch32, target, -4);

        std::ostringstream badOffsetErr;
        CoffWriter badOffsetWriter(ObjArch::X86_64);
        CHECK(!badOffsetWriter.write("build/test-out/coff_bad_reloc_offset.obj",
                                     badOffsetText,
                                     badOffsetRodata,
                                     badOffsetErr));
        CHECK(badOffsetErr.str().find("extends beyond .text contents") != std::string::npos);
    }

    {
        CodeSection collisionText;
        CodeSection collisionRodata;
        collisionRodata.defineSymbol("collision", SymbolBinding::Local, SymbolSection::Rodata);
        collisionRodata.emit64LE(0);

        collisionText.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t ext = collisionText.findOrDeclareSymbol("collision");
        collisionText.emit8(0xE8);
        const size_t dispOff = collisionText.currentOffset();
        collisionText.emit32LE(0);
        collisionText.addRelocationAt(dispOff, RelocKind::Branch32, ext, -4);

        std::ostringstream collisionErr;
        CoffWriter collisionWriter(ObjArch::X86_64);
        const std::string collisionPath = "build/test-out/coff_external_local_collision.obj";
        ASSERT(collisionWriter.write(
            collisionPath, collisionText, collisionRodata, collisionErr));

        ObjFile collisionObj;
        ASSERT(readObjFile(collisionPath, collisionObj, collisionErr));
        const ObjSection *collisionTextSec = findSection(collisionObj, ".text");
        ASSERT(collisionTextSec != nullptr);
        ASSERT(!collisionTextSec->relocs.empty());
        const auto &targetSym = collisionObj.symbols[collisionTextSec->relocs[0].symIndex];
        CHECK(targetSym.name == "collision");
        CHECK(targetSym.binding == ObjSymbol::Undefined);
    }

    {
        CodeSection badRel32Text;
        CodeSection badRel32Rodata;
        badRel32Text.defineSymbol("caller", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t target = badRel32Text.findOrDeclareSymbol("target");
        badRel32Text.emit8(0xE8);
        const size_t dispOff = badRel32Text.currentOffset();
        badRel32Text.emit32LE(0);
        badRel32Text.addRelocationAt(dispOff,
                                     RelocKind::Branch32,
                                     target,
                                     static_cast<int64_t>(
                                         std::numeric_limits<int32_t>::max()) +
                                         16);

        std::ostringstream badRel32Err;
        CoffWriter badRel32Writer(ObjArch::X86_64);
        CHECK(!badRel32Writer.write("build/test-out/coff_bad_rel32_addend.obj",
                                    badRel32Text,
                                    badRel32Rodata,
                                    badRel32Err));
        CHECK(badRel32Err.str().find("outside signed 32-bit range") != std::string::npos);
    }

    {
        CodeSection dupTextA;
        CodeSection dupTextB;
        CodeSection emptyRodata;
        dupTextA.defineSymbol("same_global", SymbolBinding::Global, SymbolSection::Text);
        dupTextA.emit8(0xC3);
        dupTextB.defineSymbol("same_global", SymbolBinding::Global, SymbolSection::Text);
        dupTextB.emit8(0xC3);

        std::ostringstream dupErr;
        CoffWriter dupWriter(ObjArch::X86_64);
        CHECK(!dupWriter.write("build/test-out/coff_dup_global.obj",
                               std::vector<CodeSection>{dupTextA, dupTextB},
                               emptyRodata,
                               dupErr));
        CHECK(dupErr.str().find("duplicate global symbol") != std::string::npos);
    }

    {
        CodeSection badUnwindText;
        CodeSection badUnwindRodata;
        const uint32_t fn = badUnwindText.defineSymbol(
            "bad_unwind", SymbolBinding::Global, SymbolSection::Text);
        badUnwindText.emit8(0xC3);

        Win64UnwindEntry badUnwind{};
        badUnwind.symbolIndex = fn;
        badUnwind.functionLength = 1;
        badUnwind.prologueSize = 1;
        badUnwind.codes.push_back({Win64UnwindCode::Kind::AllocStack, 1, 0, 4});
        badUnwindText.addWin64UnwindEntry(std::move(badUnwind));

        std::ostringstream unwindErr;
        CoffWriter unwindWriter(ObjArch::X86_64);
        CHECK(!unwindWriter.write("build/test-out/coff_bad_unwind.obj",
                                  badUnwindText,
                                  badUnwindRodata,
                                  unwindErr));
        CHECK(unwindErr.str().find("8-byte aligned") != std::string::npos);
    }

    {
        CodeSection badUnwindText;
        CodeSection badUnwindRodata;
        badUnwindText.defineSymbol("bad_unwind_index", SymbolBinding::Global, SymbolSection::Text);
        badUnwindText.emit8(0xC3);

        Win64UnwindEntry badUnwind{};
        badUnwind.symbolIndex = 99;
        badUnwind.functionLength = 1;
        badUnwind.prologueSize = 0;
        badUnwindText.addWin64UnwindEntry(std::move(badUnwind));

        std::ostringstream unwindErr;
        CoffWriter unwindWriter(ObjArch::X86_64);
        CHECK(!unwindWriter.write("build/test-out/coff_bad_unwind_index.obj",
                                  badUnwindText,
                                  badUnwindRodata,
                                  unwindErr));
        CHECK(unwindErr.str().find("unknown symbol index") != std::string::npos);
    }

    {
        CodeSection textA;
        CodeSection textB;
        CodeSection rodataMulti;

        textA.defineSymbol("func_a", SymbolBinding::Global, SymbolSection::Text);
        textA.emit32LE(0xD65F03C0); // ret

        textB.defineSymbol("func_b", SymbolBinding::Global, SymbolSection::Text);
        textB.emit32LE(0xD65F03C0); // ret

        std::ostringstream multiErr;
        CoffWriter multiWriter(ObjArch::AArch64);
        const std::string multiPath = "build/test-out/coff_arm64_multitext.obj";
        ASSERT(multiWriter.write(
            multiPath, std::vector<CodeSection>{textA, textB}, rodataMulti, multiErr));

        ObjFile multiObj;
        ASSERT(readObjFile(multiPath, multiObj, multiErr));
        CHECK(findSection(multiObj, ".text.func_a") != nullptr);
        CHECK(findSection(multiObj, ".text.func_b") != nullptr);
    }

    {
        CodeSection textA;
        CodeSection textB;
        CodeSection rodataMulti;

        textA.defineSymbol("func_a", SymbolBinding::Global, SymbolSection::Text);
        textA.emit8(0x90);
        textA.emit8(0xC3);
        textB.defineSymbol("func_b", SymbolBinding::Global, SymbolSection::Text);
        textB.emit8(0xC3);

        rodataMulti.addSectionOffsetRelocation(RelocKind::Abs64, textA, SymbolSection::Text, 1);
        rodataMulti.emit64LE(0);

        std::ostringstream sectionOffErr;
        CoffWriter sectionOffWriter(ObjArch::X86_64);
        const std::string sectionOffPath = "build/test-out/coff_multitext_section_offset.obj";
        ASSERT(sectionOffWriter.write(
            sectionOffPath, std::vector<CodeSection>{textA, textB}, rodataMulti, sectionOffErr));

        ObjFile sectionOffObj;
        ASSERT(readObjFile(sectionOffPath, sectionOffObj, sectionOffErr));
        const ObjSection *sectionOffRdata = findSection(sectionOffObj, ".rdata");
        ASSERT(sectionOffRdata != nullptr);
        ASSERT(sectionOffRdata->relocs.size() == 1);
        CHECK(sectionOffRdata->relocs[0].addend == 1);
        CHECK(sectionOffObj.symbols[sectionOffRdata->relocs[0].symIndex].sectionIndex != 0);
    }

    {
        CodeSection textA;
        CodeSection textB;
        CodeSection rodataMulti;

        textA.defineSymbol("func_a", SymbolBinding::Global, SymbolSection::Text);
        textA.emit8(0x90);
        textA.emit8(0xC3);
        textB.defineSymbol("func_b", SymbolBinding::Global, SymbolSection::Text);
        textB.emit8(0xCC);
        textB.emit8(0xC3);

        rodataMulti.addSectionOffsetRelocation(RelocKind::Abs64, SymbolSection::Text, 1);
        rodataMulti.emit64LE(0);

        std::ostringstream sectionOffErr;
        CoffWriter sectionOffWriter(ObjArch::X86_64);
        const std::string sectionOffPath = "build/test-out/coff_multitext_ambiguous_offset.obj";
        CHECK(!sectionOffWriter.write(
            sectionOffPath, std::vector<CodeSection>{textA, textB}, rodataMulti, sectionOffErr));
        CHECK(sectionOffErr.str().find("ambiguous .text offset") != std::string::npos);
    }

    {
        std::vector<uint8_t> commonObj(20 + 18 + 4, 0);
        writeLE16(commonObj, 0, 0x8664);      // IMAGE_FILE_MACHINE_AMD64
        writeLE32(commonObj, 8, 20);          // symbol table immediately after header
        writeLE32(commonObj, 12, 1);          // one symbol
        const size_t symOff = 20;
        const char name[] = "common";
        for (size_t i = 0; i < sizeof(name) - 1; ++i)
            commonObj[symOff + i] = static_cast<uint8_t>(name[i]);
        writeLE32(commonObj, symOff + 8, 16); // common symbol size
        writeLE16(commonObj, symOff + 12, 0); // IMAGE_SYM_UNDEFINED
        commonObj[symOff + 16] = 2;           // IMAGE_SYM_CLASS_EXTERNAL
        writeLE32(commonObj, symOff + 18, 4); // empty string table

        ObjFile commonParsed;
        std::ostringstream commonErr;
        ASSERT(readObjFile(
            commonObj.data(), commonObj.size(), "coff_common.obj", commonParsed, commonErr));
        const uint32_t commonIdx = findSymbolIndex(commonParsed, "common");
        ASSERT(commonIdx != 0);
        CHECK(commonParsed.symbols[commonIdx].binding == ObjSymbol::Global);
        CHECK(commonParsed.symbols[commonIdx].common);
        CHECK(commonParsed.symbols[commonIdx].size == 16);
        CHECK(commonParsed.symbols[commonIdx].commonAlignment == 16);
        CHECK(commonParsed.symbols[commonIdx].sectionIndex == 0);
    }

    {
        std::vector<uint8_t> bigObjHeader(20, 0);
        bigObjHeader[2] = 0xFF;
        bigObjHeader[3] = 0xFF;

        ObjFile bigObj;
        std::ostringstream bigErr;
        CHECK(!readObjFile(bigObjHeader.data(), bigObjHeader.size(), "bigobj.obj", bigObj, bigErr));
        CHECK(bigErr.str().find("BigObj") != std::string::npos);
    }

    {
        std::vector<uint8_t> badLongName(20 + 40, 0);
        writeLE16(badLongName, 0, 0x8664); // IMAGE_FILE_MACHINE_AMD64
        writeLE16(badLongName, 2, 1);      // one section
        const size_t secOff = 20;
        badLongName[secOff + 0] = '/';
        badLongName[secOff + 1] = '4'; // References a missing COFF string table.
        writeLE32(badLongName, secOff + 36, 0x40000040); // initialized readable data

        ObjFile badLongObj;
        std::ostringstream badLongErr;
        CHECK(!readObjFile(
            badLongName.data(), badLongName.size(), "bad_long_name.obj", badLongObj, badLongErr));
        CHECK(badLongErr.str().find("invalid string table offset") != std::string::npos ||
              badLongErr.str().find("string table") != std::string::npos);
    }

    {
        CodeSection manyRelocs;
        CodeSection emptyRodata;
        manyRelocs.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
        const uint32_t ext = manyRelocs.findOrDeclareSymbol("target");
        constexpr size_t kRelocCount = 65536;
        for (size_t i = 0; i < kRelocCount; ++i) {
            const size_t off = manyRelocs.currentOffset();
            manyRelocs.emit32LE(0);
            manyRelocs.addRelocationAt(off, RelocKind::PCRel32, ext, -4);
        }

        std::ostringstream overflowErr;
        CoffWriter overflowWriter(ObjArch::X86_64);
        const std::string overflowPath = "build/test-out/coff_reloc_overflow.obj";
        ASSERT(overflowWriter.write(overflowPath, manyRelocs, emptyRodata, overflowErr));

        const auto overflowBytes = readFile(overflowPath);
        ASSERT(overflowBytes.size() >= 20 + 40);
        const size_t textHeader = 20;
        CHECK(readLE32(overflowBytes, textHeader + 36) & 0x01000000U);
        const uint32_t relocOff = readLE32(overflowBytes, textHeader + 24);
        ASSERT(relocOff + 10 <= overflowBytes.size());
        CHECK(readLE32(overflowBytes, relocOff) == kRelocCount + 1);

        ObjFile overflowObj;
        ASSERT(readObjFile(overflowPath, overflowObj, overflowErr));
        const ObjSection *overflowText = findSection(overflowObj, ".text");
        ASSERT(overflowText != nullptr);
        CHECK(overflowText->relocs.size() == kRelocCount);
    }

    if (gFail == 0) {
        std::cout << "All CoffWriter tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " CoffWriter test(s) FAILED.\n";
    return EXIT_FAILURE;
}
