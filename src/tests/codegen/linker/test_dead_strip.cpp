//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_dead_strip.cpp
// Purpose: Unit tests for the native linker's dead strip pass — verifies
//          root identification, transitive liveness, and section clearing.
// Key invariants:
//   - User .o sections are always live
//   - Archive-extracted sections are live only if reachable from roots
//   - ObjC metadata and init/fini sections are always live
//   - Liveness propagates through relocations
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/DeadStripPass.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/DeadStripPass.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

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

/// Helper: create a minimal ObjFile with named sections.
static ObjFile makeObj(const std::string &name,
                       const std::vector<std::string> &secNames,
                       size_t dataSize = 16) {
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;

    // Section 0: null.
    obj.sections.push_back({});

    for (const auto &sn : secNames) {
        ObjSection sec;
        sec.name = sn;
        sec.data.resize(dataSize, 0xCC);
        sec.executable = (sn.find("text") != std::string::npos);
        sec.writable =
            (sn.find("data") != std::string::npos || sn.find("bss") != std::string::npos);
        sec.alloc = true;
        sec.alignment = 4;
        obj.sections.push_back(sec);
    }

    // Symbol 0: null.
    obj.symbols.push_back({});

    return obj;
}

/// Helper: add a symbol to an ObjFile that references a given section.
static void addSymbol(ObjFile &obj,
                      const std::string &name,
                      uint32_t secIdx,
                      ObjSymbol::Binding binding = ObjSymbol::Global) {
    ObjSymbol sym;
    sym.name = name;
    sym.sectionIndex = secIdx;
    sym.binding = binding;
    obj.symbols.push_back(sym);
}

/// Helper: add a relocation in a section that references a symbol.
static void addReloc(ObjFile &obj, size_t secIdx, uint32_t symIdx) {
    ObjReloc rel;
    rel.offset = 0;
    rel.type = 2; // R_X86_64_PC32 (doesn't matter for dead strip, only symIndex)
    rel.symIndex = symIdx;
    rel.addend = 0;
    obj.sections[secIdx].relocs.push_back(rel);
}

int main() {
    // --- User sections are always live ---
    {
        auto user = makeObj("user.o", {".text", ".data"});
        auto archive = makeObj("archive.o", {".text"});

        std::vector<ObjFile> objs = {user, archive};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // User sections should be untouched.
        CHECK(!objs[0].sections[1].data.empty()); // .text
        CHECK(!objs[0].sections[2].data.empty()); // .data

        // Archive section not referenced → stripped.
        CHECK(objs[1].sections[1].data.empty());
    }

    // --- Transitive liveness through relocations ---
    {
        auto user = makeObj("user.o", {".text"});
        auto lib1 = makeObj("lib1.o", {".text"});
        auto lib2 = makeObj("lib2.o", {".text"});

        // user.text references "func1" (in lib1).
        addSymbol(user, "func1", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 1); // sec 1 references sym 1 ("func1")

        // lib1 defines "func1" in section 1 and references "func2" (in lib2).
        addSymbol(lib1, "func1", 1, ObjSymbol::Global);
        addSymbol(lib1, "func2", 0, ObjSymbol::Undefined);
        addReloc(lib1, 1, 2); // sec 1 references sym 2 ("func2")

        // lib2 defines "func2" in section 1.
        addSymbol(lib2, "func2", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {user, lib1, lib2};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        // Register "func1" at obj 1, sec 1.
        globalSyms["func1"] = {"func1", GlobalSymEntry::Global, 1, 1, 0, 0};
        // Register "func2" at obj 2, sec 1.
        globalSyms["func2"] = {"func2", GlobalSymEntry::Global, 2, 1, 0, 0};

        std::ostringstream err;
        deadStrip(objs, userCount, globalSyms, "main", err);

        // lib1 and lib2 should be live (transitively reachable).
        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[2].sections[1].data.empty());
    }

    // --- Unreferenced archive sections are stripped ---
    {
        auto user = makeObj("user.o", {".text"});
        auto unused = makeObj("unused.o", {".text", ".data"});

        std::vector<ObjFile> objs = {user, unused};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // Unused archive sections should be stripped.
        CHECK(objs[1].sections[1].data.empty());
        CHECK(objs[1].sections[2].data.empty());
    }

    // --- Always-live sections: .init_array ---
    {
        auto user = makeObj("user.o", {".text"});
        auto initObj = makeObj("init.o", {".init_array"});

        std::vector<ObjFile> objs = {user, initObj};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // .init_array should be live despite no references.
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Always-live sections: ObjC metadata ---
    {
        auto user = makeObj("user.o", {".text"});
        auto objcObj = makeObj("objc.o", {"__objc_classlist"});

        std::vector<ObjFile> objs = {user, objcObj};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // ObjC metadata should be live.
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Entry point section is a root ---
    {
        auto startup = makeObj("crt0.o", {".text"});
        addSymbol(startup, "main", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {startup};
        size_t userCount = 0; // No user objects (all from archive).

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};

        std::ostringstream err;
        deadStrip(objs, userCount, globalSyms, "main", err);

        // Entry point section should be live.
        CHECK(!objs[0].sections[1].data.empty());
    }

    // --- Synthetic objects (name starts with '<') are always live ---
    {
        ObjFile synth;
        synth.name = "<dyld-stubs>";
        synth.format = ObjFileFormat::MachO;
        synth.sections.push_back({}); // null
        ObjSection stub;
        stub.name = "__stubs";
        stub.data.resize(8, 0x90);
        stub.executable = true;
        stub.alloc = true;
        synth.sections.push_back(stub);

        auto user = makeObj("user.o", {".text"});
        std::vector<ObjFile> objs = {user, synth};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // Synthetic object sections should be live.
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Windows COFF keeps every .CRT$ contribution alive ---
    {
        auto user = makeObj("user.o", {".text"});
        auto crtObj = makeObj("crt.obj", {".CRT$XIAA", ".CRT$XIAC", ".CRT$XCAA"});
        crtObj.format = ObjFileFormat::COFF;

        std::vector<ObjFile> objs = {user, crtObj};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "mainCRTStartup", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
    }

    // --- Windows unwind sections follow live code reachability ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "func", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 1);

        ObjFile unwind;
        unwind.name = "unwind.obj";
        unwind.format = ObjFileFormat::COFF;
        unwind.sections.push_back({});

        ObjSection text;
        text.name = ".text";
        text.data.resize(16, 0x90);
        text.executable = true;
        text.alloc = true;
        unwind.sections.push_back(text);

        ObjSection pdata;
        pdata.name = ".pdata";
        pdata.data.resize(12, 0);
        pdata.alloc = true;
        unwind.sections.push_back(pdata);

        ObjSection xdata;
        xdata.name = ".xdata";
        xdata.data.resize(8, 0);
        xdata.alloc = true;
        unwind.sections.push_back(xdata);

        unwind.symbols.push_back({});
        addSymbol(unwind, "func", 1, ObjSymbol::Global);
        addSymbol(unwind, "$pdata", 2, ObjSymbol::Local);
        addSymbol(unwind, "$unwind", 3, ObjSymbol::Local);
        addReloc(unwind, 2, 1); // .pdata -> .text
        addReloc(unwind, 2, 3); // .pdata -> .xdata

        std::vector<ObjFile> objs = {user, unwind};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["func"] = {"func", GlobalSymEntry::Global, 1, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
    }

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All DeadStripPass tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " DeadStripPass test(s) FAILED.\n";
    return EXIT_FAILURE;
}
