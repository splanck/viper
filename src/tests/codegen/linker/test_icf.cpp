//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_icf.cpp
// Purpose: Unit tests for Identical Code Folding (ICF).
// Key invariants:
//   - Identical .text sections (bytes + reloc sigs) are folded
//   - Different bytes or different reloc targets prevent folding
//   - Address-taken functions are not folded
//   - Non-canonical sections have both data AND relocs cleared
//   - globalSyms entries are redirected to canonical
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/ICF.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ICF.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <cstdlib>
#include <iostream>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: create an ObjFile with one per-function .text section.
/// @param name  Object file name.
/// @param func  Function symbol name.
/// @param code  Machine code bytes.
/// @param relocs Relocations (symIndex references symbols in this ObjFile).
static ObjFile makeTextObj(const std::string &name, const std::string &func,
                           const std::vector<uint8_t> &code,
                           const std::vector<ObjReloc> &relocs = {},
                           const std::vector<ObjSymbol> &extraSyms = {})
{
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;

    // Section 0: null.
    obj.sections.push_back({});

    // Section 1: .text.func
    ObjSection sec;
    sec.name = ".text." + func;
    sec.data = code;
    sec.relocs = relocs;
    sec.executable = true;
    sec.writable = false;
    sec.alloc = true;
    sec.alignment = 4;
    obj.sections.push_back(sec);

    // Symbol 0: null.
    obj.symbols.push_back({});

    // Symbol 1: Global function symbol at offset 0.
    ObjSymbol sym;
    sym.name = func;
    sym.sectionIndex = 1;
    sym.offset = 0;
    sym.binding = ObjSymbol::Global;
    obj.symbols.push_back(sym);

    // Additional symbols (for reloc targets, etc.).
    for (const auto &es : extraSyms)
        obj.symbols.push_back(es);

    return obj;
}

/// Helper: register a function symbol in globalSyms.
static void registerGlobal(std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                           const std::string &name, size_t objIdx, uint32_t secIdx)
{
    GlobalSymEntry e;
    e.name = name;
    e.binding = GlobalSymEntry::Global;
    e.objIndex = objIdx;
    e.secIndex = secIdx;
    e.offset = 0;
    globalSyms[name] = e;
}

int main()
{
    // --- Test 1: Identical functions are folded ---
    {
        // Two functions with identical bytes and no relocations.
        auto obj1 = makeTextObj("a.o", "funcA", {0xC3, 0x90, 0x90, 0x90}); // ret; nop; nop; nop
        auto obj2 = makeTextObj("b.o", "funcB", {0xC3, 0x90, 0x90, 0x90});

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 1);

        // Canonical (funcA) should keep its data.
        CHECK(!objs[0].sections[1].data.empty());

        // Folded (funcB) should have both data and relocs cleared.
        CHECK(objs[1].sections[1].data.empty());
        CHECK(objs[1].sections[1].relocs.empty());

        // globalSyms["funcB"] should redirect to funcA's section.
        CHECK(globalSyms["funcB"].objIndex == 0);
        CHECK(globalSyms["funcB"].secIndex == 1);
    }

    // --- Test 2: Different bytes are NOT folded ---
    {
        auto obj1 = makeTextObj("a.o", "funcA", {0xC3, 0x90, 0x90, 0x90});
        auto obj2 = makeTextObj("b.o", "funcB", {0xC3, 0x90, 0x90, 0xCC}); // Different last byte.

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 0);

        // Both should keep their data.
        CHECK(!objs[0].sections[1].data.empty());
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Test 3: Same bytes but different reloc targets are NOT folded ---
    {
        // funcA calls "target1", funcB calls "target2" at same offset.
        ObjSymbol target1;
        target1.name = "target1";
        target1.binding = ObjSymbol::Undefined;

        ObjSymbol target2;
        target2.name = "target2";
        target2.binding = ObjSymbol::Undefined;

        // Both have a Branch26 reloc at offset 0 (symIndex=2 refers to extraSyms[0]).
        ObjReloc rel;
        rel.offset = 0;
        rel.type = 283; // R_AARCH64_CALL26
        rel.symIndex = 2; // extraSyms[0]
        rel.addend = 0;

        auto obj1 = makeTextObj("a.o", "funcA", {0x00, 0x00, 0x00, 0x94}, {rel}, {target1});
        auto obj2 = makeTextObj("b.o", "funcB", {0x00, 0x00, 0x00, 0x94}, {rel}, {target2});

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 0); // Different targets → not identical.
    }

    // --- Test 4: Same bytes AND same reloc targets ARE folded ---
    {
        ObjSymbol target;
        target.name = "sharedTarget";
        target.binding = ObjSymbol::Undefined;

        ObjReloc rel;
        rel.offset = 0;
        rel.type = 283;
        rel.symIndex = 2;
        rel.addend = 0;

        auto obj1 = makeTextObj("a.o", "funcA", {0x00, 0x00, 0x00, 0x94}, {rel}, {target});
        auto obj2 = makeTextObj("b.o", "funcB", {0x00, 0x00, 0x00, 0x94}, {rel}, {target});

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 1);
        CHECK(objs[1].sections[1].data.empty());
        CHECK(objs[1].sections[1].relocs.empty());
    }

    // --- Test 5: Address-taken function is NOT folded ---
    {
        // funcA and funcB are identical, but funcB is address-taken
        // (referenced by Abs64 from a data section).
        auto obj1 = makeTextObj("a.o", "funcA", {0xC3, 0x90, 0x90, 0x90});

        // obj2 has funcB in .text section AND a .data section referencing funcB.
        ObjFile obj2;
        obj2.name = "b.o";
        obj2.format = ObjFileFormat::ELF;
        obj2.sections.push_back({}); // null

        // Section 1: .text.funcB
        ObjSection textSec;
        textSec.name = ".text.funcB";
        textSec.data = {0xC3, 0x90, 0x90, 0x90};
        textSec.executable = true;
        textSec.writable = false;
        textSec.alloc = true;
        textSec.alignment = 4;
        obj2.sections.push_back(textSec);

        // Section 2: .data with a pointer to funcB.
        ObjSection dataSec;
        dataSec.name = ".data";
        dataSec.data = {0, 0, 0, 0, 0, 0, 0, 0}; // 8-byte pointer slot.
        dataSec.executable = false;
        dataSec.writable = true;
        dataSec.alloc = true;
        dataSec.alignment = 8;
        // Relocation referencing funcB (symIndex=1).
        ObjReloc dataRel;
        dataRel.offset = 0;
        dataRel.type = 1; // R_X86_64_64 (Abs64)
        dataRel.symIndex = 1;
        dataRel.addend = 0;
        dataSec.relocs.push_back(dataRel);
        obj2.sections.push_back(dataSec);

        obj2.symbols.push_back({}); // null
        ObjSymbol funcBSym;
        funcBSym.name = "funcB";
        funcBSym.sectionIndex = 1;
        funcBSym.offset = 0;
        funcBSym.binding = ObjSymbol::Global;
        obj2.symbols.push_back(funcBSym);

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 0); // funcB is address-taken → skip.

        // Both should keep their data.
        CHECK(!objs[0].sections[1].data.empty());
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Test 6: Generic .text section (not per-function) is skipped ---
    {
        ObjFile obj;
        obj.name = "mono.o";
        obj.format = ObjFileFormat::ELF;
        obj.sections.push_back({}); // null

        ObjSection sec;
        sec.name = ".text"; // Generic .text, not .text.funcname.
        sec.data = {0xC3, 0x90};
        sec.executable = true;
        sec.alloc = true;
        sec.alignment = 4;
        obj.sections.push_back(sec);

        obj.symbols.push_back({});
        ObjSymbol sym;
        sym.name = "funcX";
        sym.sectionIndex = 1;
        sym.offset = 0;
        sym.binding = ObjSymbol::Global;
        obj.symbols.push_back(sym);

        ObjFile obj2 = obj;
        obj2.name = "mono2.o";
        obj2.symbols[1].name = "funcY";

        std::vector<ObjFile> objs = {obj, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcX", 0, 1);
        registerGlobal(globalSyms, "funcY", 1, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 0); // Generic .text excluded.
    }

    // --- Test 7: Three identical functions → 2 folded ---
    {
        auto obj1 = makeTextObj("a.o", "funcA", {0xD6, 0x5F, 0x03, 0xC0}); // ret
        auto obj2 = makeTextObj("b.o", "funcB", {0xD6, 0x5F, 0x03, 0xC0});
        auto obj3 = makeTextObj("c.o", "funcC", {0xD6, 0x5F, 0x03, 0xC0});

        std::vector<ObjFile> objs = {obj1, obj2, obj3};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        registerGlobal(globalSyms, "funcA", 0, 1);
        registerGlobal(globalSyms, "funcB", 1, 1);
        registerGlobal(globalSyms, "funcC", 2, 1);

        size_t folded = foldIdenticalCode(objs, globalSyms);
        CHECK(folded == 2);

        // Canonical keeps its data.
        CHECK(!objs[0].sections[1].data.empty());

        // Both non-canonicals cleared.
        CHECK(objs[1].sections[1].data.empty());
        CHECK(objs[1].sections[1].relocs.empty());
        CHECK(objs[2].sections[1].data.empty());
        CHECK(objs[2].sections[1].relocs.empty());

        // All three globalSyms entries should point to obj 0.
        CHECK(globalSyms["funcA"].objIndex == 0);
        CHECK(globalSyms["funcB"].objIndex == 0);
        CHECK(globalSyms["funcC"].objIndex == 0);
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All ICF tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " ICF test(s) FAILED.\n";
    return EXIT_FAILURE;
}
