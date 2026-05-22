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
//   - Sections are kept only if rooted or transitively reachable
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
    // --- User objects are rooted by entry, not by blanket reachability ---
    {
        auto user = makeObj("user.o", {".text", ".data"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        auto archive = makeObj("archive.o", {".text"});

        std::vector<ObjFile> objs = {user, archive};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        // Entry section stays live, but unrelated user sections are eligible for GC.
        CHECK(!objs[0].sections[1].data.empty()); // .text
        CHECK(objs[0].sections[2].data.empty());  // .data

        // Archive section not referenced → stripped.
        CHECK(objs[1].sections[1].data.empty());
    }

    // --- Transitive liveness through relocations ---
    {
        auto user = makeObj("user.o", {".text"});
        auto lib1 = makeObj("lib1.o", {".text"});
        auto lib2 = makeObj("lib2.o", {".text"});

        addSymbol(user, "main", 1, ObjSymbol::Global);
        // user.text references "func1" (in lib1).
        addSymbol(user, "func1", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 2); // sec 1 references sym 2 ("func1")

        // lib1 defines "func1" in section 1 and references "func2" (in lib2).
        addSymbol(lib1, "func1", 1, ObjSymbol::Global);
        addSymbol(lib1, "func2", 0, ObjSymbol::Undefined);
        addReloc(lib1, 1, 2); // sec 1 references sym 2 ("func2")

        // lib2 defines "func2" in section 1.
        addSymbol(lib2, "func2", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {user, lib1, lib2};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
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

    // --- COFF weak external fallback sections are marked live ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        ObjSymbol weak;
        weak.name = "maybe_func";
        weak.binding = ObjSymbol::Undefined;
        weak.weakExternal = true;
        weak.weakDefaultName = "fallback_func";
        user.symbols.push_back(weak);
        addReloc(user, 1, 2);

        auto fallbackObj = makeObj("fallback.obj", {".text"});
        fallbackObj.format = ObjFileFormat::COFF;
        addSymbol(fallbackObj, "fallback_func", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {user, fallbackObj};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        globalSyms["fallback_func"] = {"fallback_func", GlobalSymEntry::Global, 1, 1, 0, 0};

        std::ostringstream err;
        deadStrip(objs, 1, globalSyms, "main", err);
        CHECK(!objs[1].sections[1].data.empty());
    }

    // --- Unreferenced archive sections are stripped ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        auto unused = makeObj("unused.o", {".text", ".data"});

        std::vector<ObjFile> objs = {user, unused};
        size_t userCount = 1;

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
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

    // --- Always-live ELF init/fini sections include priority suffixes ---
    {
        auto user = makeObj("user.o", {".text"});
        auto initObj = makeObj("init.o", {".preinit_array", ".init_array.101", ".fini_array.999"});

        std::vector<ObjFile> objs = {user, initObj};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, 1, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
    }

    // --- Always-live legacy ELF constructor/destructor sections include suffixes ---
    {
        auto user = makeObj("user.o", {".text"});
        auto ctorObj = makeObj("ctors.o", {".ctors", ".ctors.101", ".dtors", ".dtors.999"});

        std::vector<ObjFile> objs = {user, ctorObj};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::ostringstream err;

        deadStrip(objs, 1, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
        CHECK(!objs[1].sections[4].data.empty());
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

    // --- Synthetic objects are always live ---
    {
        ObjFile synth;
        synth.name = "<dyld-stubs>";
        synth.synthetic = true;
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

    // --- User/archive object names that look synthetic are not trusted ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);

        ObjFile archiveLike;
        archiveLike.name = "<not-synthetic>.o";
        archiveLike.format = ObjFileFormat::ELF;
        archiveLike.sections.push_back({});
        ObjSection bss;
        bss.name = ".bss";
        bss.alloc = true;
        bss.writable = true;
        bss.zeroFill = true;
        bss.memSize = 32;
        archiveLike.sections.push_back(bss);
        archiveLike.symbols.push_back({});

        std::vector<ObjFile> objs = {user, archiveLike};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, 1, globalSyms, "main", err);
        CHECK(objs[1].sections[1].stripped);
        CHECK(!objs[1].sections[1].zeroFill);
        CHECK(objs[1].sections[1].memSize == 0);
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
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "func", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 2);

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
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        globalSyms["func"] = {"func", GlobalSymEntry::Global, 1, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, userCount, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
    }

    // --- Windows unwind reachability also handles external symbol references ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "target", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 2);

        ObjFile unwind;
        unwind.name = "unwind-ext.obj";
        unwind.format = ObjFileFormat::COFF;
        unwind.sections.push_back({});

        ObjSection text;
        text.name = ".text$target";
        text.data.resize(16, 0x90);
        text.executable = true;
        text.alloc = true;
        unwind.sections.push_back(text);

        ObjSection pdata;
        pdata.name = ".pdata$target";
        pdata.data.resize(12, 0);
        pdata.alloc = true;
        unwind.sections.push_back(pdata);

        unwind.symbols.push_back({});
        addSymbol(unwind, "target", 1, ObjSymbol::Global);
        ObjSymbol externalTarget;
        externalTarget.name = "target";
        externalTarget.binding = ObjSymbol::Undefined;
        unwind.symbols.push_back(externalTarget);
        addReloc(unwind, 2, 2); // .pdata -> external symbol resolved to .text$target

        std::vector<ObjFile> objs = {user, unwind};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        globalSyms["target"] = {"target", GlobalSymEntry::Global, 1, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, 1, globalSyms, "main", LinkPlatform::Windows, err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
    }

    // --- COFF unwind fanout remains linear for many live functions ---
    {
        constexpr size_t kFunctionCount = 512;

        auto user = makeObj("user.o", {".text"}, 4);
        addSymbol(user, "main", 1, ObjSymbol::Global);

        ObjFile coff;
        coff.name = "many-unwind.obj";
        coff.format = ObjFileFormat::COFF;
        coff.sections.push_back({});
        coff.symbols.push_back({});

        std::vector<size_t> textSections;
        std::vector<size_t> pdataSections;
        std::vector<size_t> xdataSections;
        textSections.reserve(kFunctionCount);
        pdataSections.reserve(kFunctionCount);
        xdataSections.reserve(kFunctionCount);

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};

        for (size_t i = 0; i < kFunctionCount; ++i) {
            const std::string name = "func_" + std::to_string(i);
            const uint32_t userSymIdx = static_cast<uint32_t>(user.symbols.size());
            addSymbol(user, name, 0, ObjSymbol::Undefined);
            addReloc(user, 1, userSymIdx);

            ObjSection text;
            text.name = ".text$" + name;
            text.data.resize(8, 0x90);
            text.executable = true;
            text.alloc = true;
            const size_t textIdx = coff.sections.size();
            coff.sections.push_back(text);
            const uint32_t textSymIdx = static_cast<uint32_t>(coff.symbols.size());
            addSymbol(coff, name, static_cast<uint32_t>(textIdx), ObjSymbol::Global);

            ObjSection pdata;
            pdata.name = ".pdata$" + name;
            pdata.data.resize(12, 0);
            pdata.alloc = true;
            const size_t pdataIdx = coff.sections.size();
            coff.sections.push_back(pdata);

            ObjSection xdata;
            xdata.name = ".xdata$" + name;
            xdata.data.resize(8, 0);
            xdata.alloc = true;
            const size_t xdataIdx = coff.sections.size();
            coff.sections.push_back(xdata);
            const uint32_t xdataSymIdx = static_cast<uint32_t>(coff.symbols.size());
            addSymbol(coff,
                      "$unwind_" + std::to_string(i),
                      static_cast<uint32_t>(xdataIdx),
                      ObjSymbol::Local);

            addReloc(coff, pdataIdx, textSymIdx);
            addReloc(coff, pdataIdx, xdataSymIdx);

            globalSyms[name] = {name,
                                GlobalSymEntry::Global,
                                1,
                                static_cast<uint32_t>(textIdx),
                                0,
                                0};
            textSections.push_back(textIdx);
            pdataSections.push_back(pdataIdx);
            xdataSections.push_back(xdataIdx);
        }

        ObjSection deadText;
        deadText.name = ".text$dead";
        deadText.data.resize(8, 0x90);
        deadText.executable = true;
        deadText.alloc = true;
        const size_t deadTextIdx = coff.sections.size();
        coff.sections.push_back(deadText);
        const uint32_t deadTextSymIdx = static_cast<uint32_t>(coff.symbols.size());
        addSymbol(coff, "dead_func", static_cast<uint32_t>(deadTextIdx), ObjSymbol::Global);

        ObjSection deadPdata;
        deadPdata.name = ".pdata$dead";
        deadPdata.data.resize(12, 0);
        deadPdata.alloc = true;
        const size_t deadPdataIdx = coff.sections.size();
        coff.sections.push_back(deadPdata);

        ObjSection deadXdata;
        deadXdata.name = ".xdata$dead";
        deadXdata.data.resize(8, 0);
        deadXdata.alloc = true;
        const size_t deadXdataIdx = coff.sections.size();
        coff.sections.push_back(deadXdata);
        const uint32_t deadXdataSymIdx = static_cast<uint32_t>(coff.symbols.size());
        addSymbol(coff,
                  "$unwind_dead",
                  static_cast<uint32_t>(deadXdataIdx),
                  ObjSymbol::Local);
        addReloc(coff, deadPdataIdx, deadTextSymIdx);
        addReloc(coff, deadPdataIdx, deadXdataSymIdx);
        globalSyms["dead_func"] = {"dead_func",
                                   GlobalSymEntry::Global,
                                   1,
                                   static_cast<uint32_t>(deadTextIdx),
                                   0,
                                   0};

        std::vector<ObjFile> objs = {user, coff};
        std::ostringstream err;
        deadStrip(objs, 1, globalSyms, "main", LinkPlatform::Windows, err);

        for (size_t i = 0; i < kFunctionCount; ++i) {
            CHECK(!objs[1].sections[textSections[i]].data.empty());
            CHECK(!objs[1].sections[pdataSections[i]].data.empty());
            CHECK(!objs[1].sections[xdataSections[i]].data.empty());
        }
        CHECK(objs[1].sections[deadTextIdx].data.empty());
        CHECK(objs[1].sections[deadPdataIdx].data.empty());
        CHECK(objs[1].sections[deadXdataIdx].data.empty());
    }

    // --- EH/unwind metadata sections are always live ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);

        auto eh = makeObj("eh.o", {".eh_frame", ".gcc_except_table", "__LD,__compact_unwind"});
        std::vector<ObjFile> objs = {user, eh};

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        std::ostringstream err;

        deadStrip(objs, 1, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[3].data.empty());
    }

    // --- Non-alloc debug sections are stripped by default and preserved on request ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);

        auto makeDebugObj = []() {
            ObjFile debugObj;
            debugObj.name = "debug.o";
            debugObj.format = ObjFileFormat::ELF;
            debugObj.sections.push_back({});
            ObjSection debugLine;
            debugLine.name = ".debug_line";
            debugLine.data.resize(16, 0x42);
            debugLine.alloc = false;
            debugLine.alignment = 1;
            debugObj.sections.push_back(debugLine);
            debugObj.symbols.push_back({});
            return debugObj;
        };

        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};

        {
            std::vector<ObjFile> objs = {user, makeDebugObj()};
            std::ostringstream err;
            deadStrip(objs, 1, globalSyms, "main", err);
            CHECK(objs[1].sections[1].data.empty());
            CHECK(objs[1].sections[1].stripped);
        }

        {
            std::vector<ObjFile> objs = {user, makeDebugObj()};
            std::ostringstream err;
            deadStrip(objs, 1, globalSyms, "main", LinkPlatform::Linux, true, err);
            CHECK(!objs[1].sections[1].data.empty());
            CHECK(!objs[1].sections[1].stripped);
        }
    }

    // --- COFF associative COMDAT sections follow their parent section ---
    {
        auto user = makeObj("user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "func", 0, ObjSymbol::Undefined);
        addReloc(user, 1, 2);

        ObjFile comdat;
        comdat.name = "comdat.obj";
        comdat.format = ObjFileFormat::COFF;
        comdat.sections.push_back({});

        ObjSection text;
        text.name = ".text$func";
        text.data.resize(8, 0x90);
        text.executable = true;
        text.alloc = true;
        comdat.sections.push_back(text);

        ObjSection assoc;
        assoc.name = ".xdata$func";
        assoc.data.resize(8, 0xAA);
        assoc.alloc = true;
        assoc.associativeSection = 1;
        comdat.sections.push_back(assoc);

        comdat.symbols.push_back({});
        addSymbol(comdat, "func", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {user, comdat};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        globalSyms["func"] = {"func", GlobalSymEntry::Global, 1, 1, 0, 0};

        std::ostringstream err;
        deadStrip(objs, 1, globalSyms, "main", err);

        CHECK(!objs[1].sections[1].data.empty());
        CHECK(!objs[1].sections[2].data.empty());
        CHECK(!objs[1].sections[2].stripped);
    }

    // --- Weak same-object relocation follows the strong global winner ---
    {
        auto user = makeObj("user.o", {".text.main", ".text.weak"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "hook", 2, ObjSymbol::Weak);
        addReloc(user, 1, 2);

        auto provider = makeObj("provider.o", {".text.hook"});
        addSymbol(provider, "hook", 1, ObjSymbol::Global);

        std::vector<ObjFile> objs = {user, provider};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        globalSyms["main"] = {"main", GlobalSymEntry::Global, 0, 1, 0, 0};
        globalSyms["hook"] = {"hook", GlobalSymEntry::Global, 1, 1, 0, 0};

        std::ostringstream err;
        deadStrip(objs, 1, globalSyms, "main", LinkPlatform::Linux, err);

        CHECK(!objs[0].sections[1].data.empty()); // main
        CHECK(objs[0].sections[2].data.empty());  // weak local loser
        CHECK(!objs[1].sections[1].data.empty()); // strong global winner
    }

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All DeadStripPass tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " DeadStripPass test(s) FAILED.\n";
    return EXIT_FAILURE;
}
