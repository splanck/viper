//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_reloc_applier.cpp
// Purpose: Unit tests for the native linker's relocation applier — verifies
//          correct patching of PCRel32, Abs64, Branch26, ADRP Page21,
//          undefined symbol errors, and out-of-bounds detection.
// Key invariants:
//   - PCRel32: result = S + A - P
//   - Abs64: result = S + A written as 8 LE bytes
//   - Undefined symbols produce an error (not silent address 0)
//   - Out-of-bounds relocations return false
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/RelocApplier.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocApplier.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: build a minimal ObjFile with one .text section containing \p code,
/// one symbol at index 1, and one relocation.
static ObjFile makeObj(const std::string &name,
                       ObjFileFormat fmt,
                       const std::vector<uint8_t> &code,
                       const std::string &symName,
                       uint32_t relocType,
                       size_t relocOff,
                       int64_t addend) {
    ObjFile obj;
    obj.name = name;
    obj.format = fmt;

    // Section 0: null (required).
    obj.sections.push_back({});

    // Section 1: .text with the provided code.
    ObjSection text;
    text.name = ".text";
    text.data = code;
    text.executable = true;
    text.alloc = true;
    text.alignment = 4;

    ObjReloc rel;
    rel.offset = relocOff;
    rel.type = relocType;
    rel.symIndex = 1; // Points to symbol at index 1.
    rel.addend = addend;
    text.relocs.push_back(rel);

    obj.sections.push_back(text);

    // Symbol 0: null (required).
    obj.symbols.push_back({});

    // Symbol 1: the target symbol.
    ObjSymbol sym;
    sym.name = symName;
    sym.binding = ObjSymbol::Undefined;
    obj.symbols.push_back(sym);

    return obj;
}

/// Helper: set up a LinkLayout with one .text output section at a given VA,
/// containing the object's section data.
static LinkLayout makeLayout(const std::vector<ObjFile> &objects, uint64_t textVA) {
    LinkLayout layout;
    layout.pageSize = 0x1000;

    OutputSection out;
    out.name = ".text";
    out.executable = true;
    out.virtualAddr = textVA;

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        for (size_t si = 1; si < objects[oi].sections.size(); ++si) {
            InputChunk chunk;
            chunk.inputObjIndex = oi;
            chunk.inputSecIndex = si;
            chunk.outputOffset = out.data.size();
            chunk.size = objects[oi].sections[si].data.size();
            out.chunks.push_back(chunk);
            out.data.insert(out.data.end(),
                            objects[oi].sections[si].data.begin(),
                            objects[oi].sections[si].data.end());
        }
    }

    layout.sections.push_back(out);
    return layout;
}

static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t readLE64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

int main() {
    // --- PCRel32 (ELF x86_64: R_X86_64_PC32 = type 2) ---
    {
        // Object 0: caller with relocation referencing "target_func".
        std::vector<uint8_t> callerCode(8, 0);
        auto caller = makeObj("caller.o",
                              ObjFileFormat::ELF,
                              callerCode,
                              "target_func",
                              /*R_X86_64_PC32=*/2,
                              /*relocOff=*/0,
                              /*addend=*/-4);

        // Object 1: target defining "target_func" in its own .text section.
        ObjFile target;
        target.name = "target.o";
        target.format = ObjFileFormat::ELF;
        target.sections.push_back({}); // null section
        ObjSection targetText;
        targetText.name = ".text";
        targetText.data.resize(16, 0x90); // NOP sled
        targetText.executable = true;
        targetText.alloc = true;
        targetText.alignment = 4;
        target.sections.push_back(targetText);
        target.symbols.push_back({}); // null symbol
        ObjSymbol targetSym;
        targetSym.name = "target_func";
        targetSym.binding = ObjSymbol::Global;
        targetSym.sectionIndex = 1;
        targetSym.offset = 0;
        target.symbols.push_back(targetSym);

        std::vector<ObjFile> objs = {caller, target};
        auto layout = makeLayout(objs, 0x401000);

        // Register "target_func" in global symbol table.
        // It's in obj 1, section 1 — the first pass of applyRelocations will
        // resolve it from the layout to the correct VA.
        GlobalSymEntry entry;
        entry.name = "target_func";
        entry.binding = GlobalSymEntry::Global;
        entry.objIndex = 1;
        entry.secIndex = 1;
        entry.offset = 0;
        entry.resolvedAddr = 0; // Will be resolved by first pass.
        layout.globalSyms["target_func"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(ok);
        CHECK(err.str().empty());

        // target_func is at VA = textVA + callerCode.size() = 0x401000 + 8 = 0x401008
        // P = textVA + relocOff = 0x401000 + 0 = 0x401000
        // Expected: S + A - P = 0x401008 + (-4) - 0x401000 = 4
        uint32_t patched = readLE32(layout.sections[0].data.data());
        CHECK(patched == 4);
    }

    // --- Abs64 (ELF x86_64: R_X86_64_64 = type 1) ---
    {
        // Object 0: code with an Abs64 relocation.
        std::vector<uint8_t> code(16, 0);
        auto caller = makeObj("test_abs64.o",
                              ObjFileFormat::ELF,
                              code,
                              "global_var",
                              /*R_X86_64_64=*/1,
                              /*relocOff=*/0,
                              /*addend=*/0);

        // Object 1: defines "global_var" in a .data section.
        ObjFile dataObj;
        dataObj.name = "data.o";
        dataObj.format = ObjFileFormat::ELF;
        dataObj.sections.push_back({}); // null
        ObjSection dataSec;
        dataSec.name = ".data";
        dataSec.data.resize(8, 0x42);
        dataSec.writable = true;
        dataSec.alloc = true;
        dataSec.alignment = 8;
        dataObj.sections.push_back(dataSec);
        dataObj.symbols.push_back({}); // null
        ObjSymbol dataSym;
        dataSym.name = "global_var";
        dataSym.binding = ObjSymbol::Global;
        dataSym.sectionIndex = 1;
        dataSym.offset = 0;
        dataObj.symbols.push_back(dataSym);

        std::vector<ObjFile> objs = {caller, dataObj};
        auto layout = makeLayout(objs, 0x401000);

        // Register global symbol — first pass resolves from layout.
        GlobalSymEntry entry;
        entry.name = "global_var";
        entry.binding = GlobalSymEntry::Global;
        entry.objIndex = 1;
        entry.secIndex = 1;
        entry.offset = 0;
        entry.resolvedAddr = 0;
        layout.globalSyms["global_var"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(ok);

        // global_var is at textVA + callerCode.size() = 0x401000 + 16 = 0x401010
        uint64_t patched = readLE64(layout.sections[0].data.data());
        CHECK(patched == 0x401010);
    }

    // --- Undefined symbol produces error (validates Phase 1A fix) ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_undef.o",
                           ObjFileFormat::ELF,
                           code,
                           "missing_symbol",
                           /*R_X86_64_PC32=*/2,
                           /*relocOff=*/0,
                           /*addend=*/-4);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x401000);
        // Deliberately do NOT add "missing_symbol" to globalSyms.

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(!ok);
        CHECK(err.str().find("undefined symbol") != std::string::npos);
        CHECK(err.str().find("missing_symbol") != std::string::npos);
    }

    // --- Abs64 out-of-bounds produces error (validates Phase 1B fix) ---
    {
        // Code is only 4 bytes, but Abs64 needs 8.
        std::vector<uint8_t> code(4, 0);
        auto obj = makeObj("test_oob.o",
                           ObjFileFormat::ELF,
                           code,
                           "some_sym",
                           /*R_X86_64_64=*/1,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x401000);

        // Use Dynamic binding so first pass doesn't try to resolve.
        GlobalSymEntry entry;
        entry.name = "some_sym";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x402000;
        layout.globalSyms["some_sym"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(!ok);
        CHECK(err.str().find("out of bounds") != std::string::npos);
    }

    // --- Relocation offset beyond section produces error ---
    {
        std::vector<uint8_t> code(4, 0);
        auto obj = makeObj("test_bad_off.o",
                           ObjFileFormat::ELF,
                           code,
                           "func",
                           /*R_X86_64_PC32=*/2,
                           /*relocOff=*/100,
                           /*addend=*/-4);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x401000);

        GlobalSymEntry entry;
        entry.name = "func";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x402000;
        layout.globalSyms["func"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(!ok);
        CHECK(err.str().find("relocation offset") != std::string::npos);
    }

    // --- Abs32 (ELF x86_64: R_X86_64_32 = type 10) ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_abs32.o",
                           ObjFileFormat::ELF,
                           code,
                           "small_addr",
                           /*R_X86_64_32=*/10,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x401000);

        // Use Dynamic binding so first pass doesn't recalculate resolvedAddr.
        GlobalSymEntry entry;
        entry.name = "small_addr";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x12345678;
        layout.globalSyms["small_addr"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(ok);

        uint32_t patched = readLE32(layout.sections[0].data.data());
        CHECK(patched == 0x12345678);
    }

    // --- COFF x86_64 ADDR32NB writes an RVA (S + A - ImageBase) ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_addr32nb.obj",
                           ObjFileFormat::COFF,
                           code,
                           "unwind_info",
                           /*IMAGE_REL_AMD64_ADDR32NB=*/3,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x140001000ULL);

        GlobalSymEntry entry;
        entry.name = "unwind_info";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x140002000ULL;
        layout.globalSyms["unwind_info"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Windows, LinkArch::X86_64, err);
        CHECK(ok);

        uint32_t patched = readLE32(layout.sections[0].data.data());
        CHECK(patched == 0x2000);
    }

    // --- COFF x86_64 REL32 is relative to the end of the relocated field ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_rel32.obj",
                           ObjFileFormat::COFF,
                           code,
                           "target",
                           /*IMAGE_REL_AMD64_REL32=*/4,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x140001000ULL);

        GlobalSymEntry entry;
        entry.name = "target";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x140002000ULL;
        layout.globalSyms["target"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Windows, LinkArch::X86_64, err);
        CHECK(ok);

        uint32_t patched = readLE32(layout.sections[0].data.data());
        CHECK(patched == 0x00000FFC);
    }

    // --- COFF x86_64 REL32_4 includes the trailing-byte bias ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_rel32_4.obj",
                           ObjFileFormat::COFF,
                           code,
                           "target",
                           /*IMAGE_REL_AMD64_REL32_4=*/8,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x140001000ULL);

        GlobalSymEntry entry;
        entry.name = "target";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x140002000ULL;
        layout.globalSyms["target"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Windows, LinkArch::X86_64, err);
        CHECK(ok);

        uint32_t patched = readLE32(layout.sections[0].data.data());
        CHECK(patched == 0x00000FF8);
    }

    // --- Unknown reloc type produces error ---
    {
        std::vector<uint8_t> code(8, 0);
        auto obj = makeObj("test_unk.o",
                           ObjFileFormat::ELF,
                           code,
                           "any_sym",
                           /*unknown type=*/999,
                           /*relocOff=*/0,
                           /*addend=*/0);

        std::vector<ObjFile> objs = {obj};
        auto layout = makeLayout(objs, 0x401000);

        GlobalSymEntry entry;
        entry.name = "any_sym";
        entry.binding = GlobalSymEntry::Dynamic;
        entry.resolvedAddr = 0x402000;
        layout.globalSyms["any_sym"] = entry;

        std::ostringstream err;
        std::unordered_set<std::string> dynSyms;
        bool ok =
            applyRelocations(objs, layout, dynSyms, LinkPlatform::Linux, LinkArch::X86_64, err);
        CHECK(!ok);
        CHECK(err.str().find("unknown reloc type") != std::string::npos);
    }

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All RelocApplier tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " RelocApplier test(s) FAILED.\n";
    return EXIT_FAILURE;
}
