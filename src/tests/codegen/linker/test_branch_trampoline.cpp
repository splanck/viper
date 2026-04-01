//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_branch_trampoline.cpp
// Purpose: Unit tests for AArch64 branch trampoline insertion.
// Key invariants:
//   - Out-of-range Branch26 gets a trampoline inserted at a reachable text boundary
//   - In-range branches are untouched
//   - x86_64 is a no-op
//   - Multiple branches to same target share one trampoline
//   - VAs are re-assigned after trampoline insertion
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/BranchTrampoline.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/BranchTrampoline.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/RelocConstants.hpp"
#include "codegen/common/linker/SectionMerger.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_set>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static size_t countInsn(const std::vector<uint8_t> &data, uint32_t word) {
    size_t count = 0;
    for (size_t off = 0; off + 4 <= data.size(); off += 4) {
        if (readLE32(data.data() + off) == word)
            ++count;
    }
    return count;
}

/// Helper: create a minimal ObjFile with a .text section containing code.
static ObjFile makeCodeObj(const std::string &name,
                           const std::string &func,
                           const std::vector<uint8_t> &code,
                           const std::vector<ObjReloc> &relocs = {},
                           const std::vector<ObjSymbol> &extraSyms = {}) {
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;

    obj.sections.push_back({}); // null

    ObjSection sec;
    sec.name = ".text." + func;
    sec.data = code;
    sec.relocs = relocs;
    sec.executable = true;
    sec.writable = false;
    sec.alloc = true;
    sec.alignment = 4;
    obj.sections.push_back(sec);

    obj.symbols.push_back({}); // null

    ObjSymbol sym;
    sym.name = func;
    sym.sectionIndex = 1;
    sym.offset = 0;
    sym.binding = ObjSymbol::Global;
    obj.symbols.push_back(sym);

    for (const auto &es : extraSyms)
        obj.symbols.push_back(es);

    return obj;
}

/// Run the merge+trampoline pipeline and return the layout.
static bool runPipeline(std::vector<ObjFile> &objs,
                        std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                        LinkLayout &layout,
                        LinkArch arch,
                        LinkPlatform platform,
                        std::ostream &err) {
    layout.globalSyms = std::move(globalSyms);
    if (!mergeSections(objs, platform, arch, layout, err))
        return false;
    if (!insertBranchTrampolines(objs, layout, arch, platform, err))
        return false;
    return true;
}

int main() {
    // --- Test 1: In-range Branch26 — no trampolines generated ---
    {
        ObjSymbol targetSym;
        targetSym.name = "funcB";
        targetSym.binding = ObjSymbol::Undefined;

        ObjReloc rel;
        rel.offset = 0;
        rel.type = elf_a64::kCall26;
        rel.symIndex = 2; // extraSyms[0]
        rel.addend = 0;

        auto obj1 = makeCodeObj("a.o", "funcA", {0x00, 0x00, 0x00, 0x94}, {rel}, {targetSym});
        auto obj2 = makeCodeObj("b.o", "funcB", {0xC0, 0x03, 0x5F, 0xD6}); // RET

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        GlobalSymEntry e;
        e.name = "funcA";
        e.binding = GlobalSymEntry::Global;
        e.objIndex = 0;
        e.secIndex = 1;
        globalSyms["funcA"] = e;
        e.name = "funcB";
        e.objIndex = 1;
        globalSyms["funcB"] = e;

        LinkLayout layout;
        std::ostringstream err;
        CHECK(runPipeline(objs, globalSyms, layout, LinkArch::AArch64, LinkPlatform::Linux, err));

        // .text should contain exactly 8 bytes (4 + 4), no trampolines.
        CHECK(!layout.sections.empty());
        CHECK(layout.sections[0].executable);
        CHECK(layout.sections[0].data.size() == 8);
    }

    // --- Test 2: x86_64 no-op — pass does nothing ---
    {
        auto obj1 = makeCodeObj("a.o", "funcA", {0xE8, 0x00, 0x00, 0x00, 0x00}); // CALL +0

        std::vector<ObjFile> objs = {obj1};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        GlobalSymEntry e;
        e.name = "funcA";
        e.binding = GlobalSymEntry::Global;
        e.objIndex = 0;
        e.secIndex = 1;
        globalSyms["funcA"] = e;

        LinkLayout layout;
        std::ostringstream err;
        CHECK(runPipeline(objs, globalSyms, layout, LinkArch::X86_64, LinkPlatform::Linux, err));

        CHECK(!layout.sections.empty());
        CHECK(layout.sections[0].data.size() == 5);
    }

    // --- Test 3: Out-of-range Branch26 — trampoline inserted ---
    // Layout: funcB (4B) → padding (140MB) → funcA (4B + BL reloc)
    // funcA calls funcB. The distance >128MB triggers trampoline insertion.
    // Since funcA is near the end, the appended trampoline is reachable from funcA.
    {
        // obj1: funcB at start, then 140MB padding.
        ObjFile obj1;
        obj1.name = "a.o";
        obj1.format = ObjFileFormat::ELF;
        obj1.sections.push_back({}); // null

        ObjSection textB;
        textB.name = ".text.funcB";
        textB.data = {0xC0, 0x03, 0x5F, 0xD6}; // RET
        textB.executable = true;
        textB.alloc = true;
        textB.alignment = 4;
        obj1.sections.push_back(textB);

        ObjSection bigText;
        bigText.name = ".text.padding";
        bigText.data.resize(140 * 1024 * 1024, 0x00); // 140MB padding
        bigText.executable = true;
        bigText.alloc = true;
        bigText.alignment = 4;
        obj1.sections.push_back(bigText);

        obj1.symbols.push_back({}); // null
        ObjSymbol funcBSym;
        funcBSym.name = "funcB";
        funcBSym.sectionIndex = 1;
        funcBSym.offset = 0;
        funcBSym.binding = ObjSymbol::Global;
        obj1.symbols.push_back(funcBSym);
        ObjSymbol paddingSym;
        paddingSym.name = "__padding";
        paddingSym.sectionIndex = 2;
        paddingSym.offset = 0;
        paddingSym.binding = ObjSymbol::Global;
        obj1.symbols.push_back(paddingSym);

        // obj2: funcA with BL funcB, placed AFTER obj1 in merge → near end of .text.
        ObjFile obj2;
        obj2.name = "b.o";
        obj2.format = ObjFileFormat::ELF;
        obj2.sections.push_back({}); // null

        ObjSection textA;
        textA.name = ".text.funcA";
        textA.data = {0x00, 0x00, 0x00, 0x94}; // BL +0
        textA.executable = true;
        textA.alloc = true;
        textA.alignment = 4;

        ObjReloc rel;
        rel.offset = 0;
        rel.type = elf_a64::kCall26;
        rel.symIndex = 2; // "funcB" (undefined ref in obj2's symbol table)
        rel.addend = 0;
        textA.relocs.push_back(rel);
        obj2.sections.push_back(textA);

        obj2.symbols.push_back({}); // null
        ObjSymbol funcASym;
        funcASym.name = "funcA";
        funcASym.sectionIndex = 1;
        funcASym.offset = 0;
        funcASym.binding = ObjSymbol::Global;
        obj2.symbols.push_back(funcASym);
        ObjSymbol funcBUndef;
        funcBUndef.name = "funcB";
        funcBUndef.binding = ObjSymbol::Undefined;
        obj2.symbols.push_back(funcBUndef);

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        GlobalSymEntry e;
        e.binding = GlobalSymEntry::Global;
        e.name = "funcB";
        e.objIndex = 0;
        e.secIndex = 1;
        globalSyms["funcB"] = e;
        e.name = "__padding";
        e.objIndex = 0;
        e.secIndex = 2;
        globalSyms["__padding"] = e;
        e.name = "funcA";
        e.objIndex = 1;
        e.secIndex = 1;
        globalSyms["funcA"] = e;

        LinkLayout layout;
        std::ostringstream err;
        bool ok =
            runPipeline(objs, globalSyms, layout, LinkArch::AArch64, LinkPlatform::Linux, err);

        CHECK(ok);
        if (ok && !layout.sections.empty()) {
            // .text base: funcB(4) + padding(140MB) + funcA(4) = base.
            // Should grow by 12 bytes (one trampoline).
            const size_t baseSize = 4 + 140 * 1024 * 1024 + 4;
            CHECK(layout.sections[0].data.size() >= baseSize + 12);
            CHECK(countInsn(layout.sections[0].data, 0xD61F0200) == 1);

            std::unordered_set<std::string> dynamicSyms;
            std::ostringstream relocErr;
            CHECK(applyRelocations(
                objs, layout, dynamicSyms, LinkPlatform::Linux, LinkArch::AArch64, relocErr));
            CHECK(relocErr.str().empty());
        }
    }

    // --- Test 4: Trampoline dedup — same target, one trampoline ---
    // Layout: funcB (4B) → padding (140MB) → funcA (BL funcB) → funcC (BL funcB)
    // Both funcA and funcC call funcB out-of-range; should share one trampoline.
    {
        // obj1: funcB + padding.
        ObjFile obj1;
        obj1.name = "a.o";
        obj1.format = ObjFileFormat::ELF;
        obj1.sections.push_back({});

        ObjSection textB;
        textB.name = ".text.funcB";
        textB.data = {0xC0, 0x03, 0x5F, 0xD6};
        textB.executable = true;
        textB.alloc = true;
        textB.alignment = 4;
        obj1.sections.push_back(textB);

        ObjSection bigText;
        bigText.name = ".text.padding";
        bigText.data.resize(140 * 1024 * 1024, 0x00);
        bigText.executable = true;
        bigText.alloc = true;
        bigText.alignment = 4;
        obj1.sections.push_back(bigText);

        obj1.symbols.push_back({});
        ObjSymbol sB;
        sB.name = "funcB";
        sB.sectionIndex = 1;
        sB.offset = 0;
        sB.binding = ObjSymbol::Global;
        obj1.symbols.push_back(sB);
        ObjSymbol sPad;
        sPad.name = "__padding";
        sPad.sectionIndex = 2;
        sPad.offset = 0;
        sPad.binding = ObjSymbol::Global;
        obj1.symbols.push_back(sPad);

        // obj2: funcA and funcC, both calling funcB.
        ObjFile obj2;
        obj2.name = "b.o";
        obj2.format = ObjFileFormat::ELF;
        obj2.sections.push_back({});

        // funcA: BL funcB
        ObjSection textA;
        textA.name = ".text.funcA";
        textA.data = {0x00, 0x00, 0x00, 0x94};
        textA.executable = true;
        textA.alloc = true;
        textA.alignment = 4;
        ObjReloc rel1;
        rel1.offset = 0;
        rel1.type = elf_a64::kCall26;
        rel1.symIndex = 3; // funcB (undefined in obj2)
        rel1.addend = 0;
        textA.relocs.push_back(rel1);
        obj2.sections.push_back(textA);

        // funcC: BL funcB
        ObjSection textC;
        textC.name = ".text.funcC";
        textC.data = {0x00, 0x00, 0x00, 0x94};
        textC.executable = true;
        textC.alloc = true;
        textC.alignment = 4;
        ObjReloc rel2;
        rel2.offset = 0;
        rel2.type = elf_a64::kCall26;
        rel2.symIndex = 3; // funcB
        rel2.addend = 0;
        textC.relocs.push_back(rel2);
        obj2.sections.push_back(textC);

        obj2.symbols.push_back({});
        ObjSymbol sA;
        sA.name = "funcA";
        sA.sectionIndex = 1;
        sA.offset = 0;
        sA.binding = ObjSymbol::Global;
        obj2.symbols.push_back(sA);
        ObjSymbol sC;
        sC.name = "funcC";
        sC.sectionIndex = 2;
        sC.offset = 0;
        sC.binding = ObjSymbol::Global;
        obj2.symbols.push_back(sC);
        ObjSymbol sBUndef;
        sBUndef.name = "funcB";
        sBUndef.binding = ObjSymbol::Undefined;
        obj2.symbols.push_back(sBUndef);

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        GlobalSymEntry e;
        e.binding = GlobalSymEntry::Global;
        e.name = "funcB";
        e.objIndex = 0;
        e.secIndex = 1;
        globalSyms["funcB"] = e;
        e.name = "__padding";
        e.objIndex = 0;
        e.secIndex = 2;
        globalSyms["__padding"] = e;
        e.name = "funcA";
        e.objIndex = 1;
        e.secIndex = 1;
        globalSyms["funcA"] = e;
        e.name = "funcC";
        e.objIndex = 1;
        e.secIndex = 2;
        globalSyms["funcC"] = e;

        LinkLayout layout;
        std::ostringstream err;
        bool ok =
            runPipeline(objs, globalSyms, layout, LinkArch::AArch64, LinkPlatform::Linux, err);
        CHECK(ok);

        if (ok && !layout.sections.empty()) {
            // Both calls to funcB should share ONE 12-byte trampoline.
            const size_t baseSize =
                4 + 140 * 1024 * 1024 + 4 + 4; // funcB + padding + funcA + funcC
            size_t growth = layout.sections[0].data.size() - baseSize;
            CHECK(growth == 12);
            CHECK(countInsn(layout.sections[0].data, 0xD61F0200) == 1);
        }
    }

    // --- Test 5: VA re-assignment after trampoline insertion ---
    // Same layout as test 3, but with a .rodata section that should shift VA.
    {
        ObjFile obj1;
        obj1.name = "a.o";
        obj1.format = ObjFileFormat::ELF;
        obj1.sections.push_back({});

        ObjSection textB;
        textB.name = ".text.funcB";
        textB.data = {0xC0, 0x03, 0x5F, 0xD6};
        textB.executable = true;
        textB.alloc = true;
        textB.alignment = 4;
        obj1.sections.push_back(textB);

        ObjSection bigText;
        bigText.name = ".text.padding";
        bigText.data.resize(140 * 1024 * 1024, 0x00);
        bigText.executable = true;
        bigText.alloc = true;
        bigText.alignment = 4;
        obj1.sections.push_back(bigText);

        // Rodata section.
        ObjSection rodata;
        rodata.name = ".rodata";
        rodata.data = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x00}; // "Hello\0"
        rodata.executable = false;
        rodata.writable = false;
        rodata.alloc = true;
        rodata.alignment = 1;
        obj1.sections.push_back(rodata);

        obj1.symbols.push_back({});
        ObjSymbol sBDef;
        sBDef.name = "funcB";
        sBDef.sectionIndex = 1;
        sBDef.offset = 0;
        sBDef.binding = ObjSymbol::Global;
        obj1.symbols.push_back(sBDef);
        ObjSymbol sPad;
        sPad.name = "__padding";
        sPad.sectionIndex = 2;
        sPad.offset = 0;
        sPad.binding = ObjSymbol::Global;
        obj1.symbols.push_back(sPad);

        // obj2: funcA with BL funcB.
        ObjFile obj2;
        obj2.name = "b.o";
        obj2.format = ObjFileFormat::ELF;
        obj2.sections.push_back({});

        ObjSection textA;
        textA.name = ".text.funcA";
        textA.data = {0x00, 0x00, 0x00, 0x94};
        textA.executable = true;
        textA.alloc = true;
        textA.alignment = 4;
        ObjReloc rel;
        rel.offset = 0;
        rel.type = elf_a64::kCall26;
        rel.symIndex = 2;
        rel.addend = 0;
        textA.relocs.push_back(rel);
        obj2.sections.push_back(textA);

        obj2.symbols.push_back({});
        ObjSymbol sA;
        sA.name = "funcA";
        sA.sectionIndex = 1;
        sA.offset = 0;
        sA.binding = ObjSymbol::Global;
        obj2.symbols.push_back(sA);
        ObjSymbol sBUndef;
        sBUndef.name = "funcB";
        sBUndef.binding = ObjSymbol::Undefined;
        obj2.symbols.push_back(sBUndef);

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        GlobalSymEntry e;
        e.binding = GlobalSymEntry::Global;
        e.name = "funcB";
        e.objIndex = 0;
        e.secIndex = 1;
        globalSyms["funcB"] = e;
        e.name = "__padding";
        e.objIndex = 0;
        e.secIndex = 2;
        globalSyms["__padding"] = e;
        e.name = "funcA";
        e.objIndex = 1;
        e.secIndex = 1;
        globalSyms["funcA"] = e;

        // Run with trampolines.
        LinkLayout layout;
        std::ostringstream err;
        CHECK(runPipeline(objs, globalSyms, layout, LinkArch::AArch64, LinkPlatform::Linux, err));

        // Verify .text grew (trampoline inserted) and rodata VA doesn't overlap.
        uint64_t textVA = 0, textEnd = 0, rodataVA = 0;
        for (const auto &sec : layout.sections) {
            if (sec.executable) {
                textVA = sec.virtualAddr;
                textEnd = sec.virtualAddr + sec.data.size();
            }
            if (sec.name == ".rodata")
                rodataVA = sec.virtualAddr;
        }

        // .text should have grown by 12 bytes (one trampoline).
        const size_t baseSize = 4 + 140 * 1024 * 1024 + 4;
        CHECK(layout.sections[0].data.size() >= baseSize + 12);

        // Rodata VA must not overlap with .text.
        CHECK(rodataVA >= textEnd);
        // Rodata must exist and have a valid VA.
        CHECK(rodataVA > 0);
    }

    // --- Test 6: Trampoline reachability error for huge .text ---
    // (Skipped in CI — would require allocating >256MB. Tested conceptually.)

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All BranchTrampoline tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " BranchTrampoline test(s) FAILED.\n";
    return EXIT_FAILURE;
}
