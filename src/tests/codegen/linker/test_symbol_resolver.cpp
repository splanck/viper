//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_symbol_resolver.cpp
// Purpose: Unit tests for the native linker's symbol resolution — verifies
//          strong/weak/undefined precedence, multiply-defined detection,
//          archive extraction, and dynamic symbol classification.
// Key invariants:
//   - Strong > Weak > Undefined precedence
//   - Multiple strong definitions of the same symbol = linker error
//   - Archives still satisfy symbols that also exist in shared libraries
//   - Only allowlisted shared-library symbols remain dynamic after resolution
//   - Unknown unresolved symbols are hard errors
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/SymbolResolver.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/common/objfile/ElfWriter.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        sec.writable = (sn.find("data") != std::string::npos);
        sec.alloc = true;
        sec.alignment = 4;
        obj.sections.push_back(sec);
    }

    // Symbol 0: null.
    obj.symbols.push_back({});

    return obj;
}

/// Helper: add a symbol to an ObjFile.
static void addSymbol(ObjFile &obj,
                      const std::string &name,
                      uint32_t secIdx,
                      ObjSymbol::Binding binding,
                      size_t offset = 0) {
    ObjSymbol sym;
    sym.name = name;
    sym.sectionIndex = secIdx;
    sym.binding = binding;
    sym.offset = offset;
    obj.symbols.push_back(sym);
}

static void addCommonSymbol(ObjFile &obj,
                            const std::string &name,
                            size_t size,
                            size_t alignment = 8,
                            ObjSymbol::Binding binding = ObjSymbol::Global) {
    ObjSymbol sym;
    sym.name = name;
    sym.binding = binding;
    sym.common = true;
    sym.size = size;
    sym.commonAlignment = alignment;
    obj.symbols.push_back(sym);
}

static ObjFile makeComdatObj(const std::string &objName,
                             const std::string &symName,
                             ComdatSelection selection,
                             const std::vector<uint8_t> &bytes,
                             const std::string &key = "comdat-key") {
    auto obj = makeObj(objName, {".text$" + symName}, 0);
    obj.sections[1].data = bytes;
    obj.sections[1].comdatSelection = selection;
    obj.sections[1].comdatKey = key;
    addSymbol(obj, symName, 1, ObjSymbol::Global);
    return obj;
}

static std::vector<uint8_t> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return {};
    const auto size = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}

static std::vector<uint8_t> writeElfObjectWithGlobals(const std::string &path,
                                                      const std::vector<std::string> &symbols) {
    CodeSection text;
    CodeSection rodata;
    for (const auto &name : symbols) {
        text.defineSymbol(name, SymbolBinding::Global, SymbolSection::Text);
        text.emit8(0xC3);
    }

    std::ostringstream err;
    ElfWriter writer(ObjArch::X86_64);
    CHECK(writer.write(path, text, rodata, err));
    CHECK(err.str().empty());
    return readBinaryFile(path);
}

int main() {
    std::filesystem::create_directories("build/test-out");

    // --- Single object: global symbol resolves ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms.count("main") == 1);
        CHECK(globalSyms["main"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["main"].objIndex == 0);
        CHECK(globalSyms["main"].secIndex == 1);
        CHECK(allObjects.size() == 1);
        CHECK(dynamicSyms.empty());
    }

    // --- Strong overrides weak ---
    {
        auto weakObj = makeObj("weak.o", {".text"});
        addSymbol(weakObj, "func", 1, ObjSymbol::Weak);

        auto strongObj = makeObj("strong.o", {".text"});
        addSymbol(strongObj, "func", 1, ObjSymbol::Global, 8);

        std::vector<ObjFile> initObjs = {weakObj, strongObj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms.count("func") == 1);
        CHECK(globalSyms["func"].binding == GlobalSymEntry::Global);
        // Strong definition is in obj 1 (strongObj).
        CHECK(globalSyms["func"].objIndex == 1);
        CHECK(globalSyms["func"].offset == 8);
    }

    // --- Weak does not override strong ---
    {
        auto strongObj = makeObj("strong.o", {".text"});
        addSymbol(strongObj, "func", 1, ObjSymbol::Global, 0);

        auto weakObj = makeObj("weak.o", {".text"});
        addSymbol(weakObj, "func", 1, ObjSymbol::Weak, 16);

        std::vector<ObjFile> initObjs = {strongObj, weakObj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(globalSyms["func"].binding == GlobalSymEntry::Global);
        // Strong from obj 0 is preserved.
        CHECK(globalSyms["func"].objIndex == 0);
        CHECK(globalSyms["func"].offset == 0);
    }

    // --- Multiple strong definitions = error ---
    {
        auto obj1 = makeObj("a.o", {".text"});
        addSymbol(obj1, "collide", 1, ObjSymbol::Global);

        auto obj2 = makeObj("b.o", {".text"});
        addSymbol(obj2, "collide", 1, ObjSymbol::Global);

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(err.str().find("multiply defined") != std::string::npos);
        CHECK(err.str().find("collide") != std::string::npos);
    }

    // --- COMDAT ANY duplicate strong definitions pick the first copy ---
    {
        auto obj1 = makeComdatObj("a.o", "inline_func", ComdatSelection::Any, {0xC3});
        auto obj2 = makeComdatObj("b.o", "inline_func", ComdatSelection::Any, {0x90, 0xC3});

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms["inline_func"].objIndex == 0);
    }

    // --- COMDAT SAME_SIZE diagnoses mismatched section sizes ---
    {
        auto obj1 = makeComdatObj("a.o", "same_size_func", ComdatSelection::SameSize, {0xC3});
        auto obj2 =
            makeComdatObj("b.o", "same_size_func", ComdatSelection::SameSize, {0x90, 0xC3});

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(err.str().find("SAME_SIZE") != std::string::npos);
    }

    // --- COMDAT EXACT_MATCH diagnoses same-size content mismatches ---
    {
        auto obj1 =
            makeComdatObj("a.o", "exact_func", ComdatSelection::ExactMatch, {0x90, 0xC3});
        auto obj2 =
            makeComdatObj("b.o", "exact_func", ComdatSelection::ExactMatch, {0xCC, 0xC3});

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(err.str().find("EXACT_MATCH") != std::string::npos);
    }

    // --- COMDAT EXACT_MATCH includes relocation identity, not just raw bytes ---
    {
        auto obj1 =
            makeComdatObj("a.o", "exact_reloc", ComdatSelection::ExactMatch, {0xE8, 0, 0, 0, 0});
        addSymbol(obj1, "target_a", 0, ObjSymbol::Undefined);
        ObjReloc rel1;
        rel1.offset = 1;
        rel1.type = 4;
        rel1.symIndex = static_cast<uint32_t>(obj1.symbols.size() - 1);
        obj1.sections[1].relocs.push_back(rel1);

        auto obj2 =
            makeComdatObj("b.o", "exact_reloc", ComdatSelection::ExactMatch, {0xE8, 0, 0, 0, 0});
        addSymbol(obj2, "target_b", 0, ObjSymbol::Undefined);
        ObjReloc rel2 = rel1;
        rel2.symIndex = static_cast<uint32_t>(obj2.symbols.size() - 1);
        obj2.sections[1].relocs.push_back(rel2);

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(err.str().find("EXACT_MATCH") != std::string::npos);
    }

    // --- COMDAT LARGEST selects the largest section contribution ---
    {
        auto obj1 = makeComdatObj("a.o", "largest_func", ComdatSelection::Largest, {0xC3});
        auto obj2 =
            makeComdatObj("b.o", "largest_func", ComdatSelection::Largest, {0x90, 0x90, 0xC3});

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms["largest_func"].objIndex == 1);
    }

    // --- COMDAT NODUPLICATES remains a hard multiple-definition error ---
    {
        auto obj1 = makeComdatObj("a.o", "unique_func", ComdatSelection::NoDuplicates, {0xC3});
        auto obj2 = makeComdatObj("b.o", "unique_func", ComdatSelection::NoDuplicates, {0xC3});

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(err.str().find("NODUPLICATES") != std::string::npos);
    }

    // --- Windows CRT inline stdio option storage is pick-any ---
    {
        const std::string printfOptions =
            "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9";
        const std::string scanfOptions =
            "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9";

        auto obj1 = makeObj("a.obj", {".data"});
        addSymbol(obj1, printfOptions, 1, ObjSymbol::Global);
        addSymbol(obj1, scanfOptions, 1, ObjSymbol::Global, 8);

        auto obj2 = makeObj("b.obj", {".data"});
        addSymbol(obj2, printfOptions, 1, ObjSymbol::Global);
        addSymbol(obj2, scanfOptions, 1, ObjSymbol::Global, 8);

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Windows);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms[printfOptions].binding == GlobalSymEntry::Global);
        CHECK(globalSyms[printfOptions].objIndex == 0);
        CHECK(globalSyms[scanfOptions].binding == GlobalSymEntry::Global);
        CHECK(globalSyms[scanfOptions].objIndex == 0);
    }

    // --- Undefined symbol resolved by second object ---
    {
        auto caller = makeObj("caller.o", {".text"});
        addSymbol(caller, "helper", 0, ObjSymbol::Undefined);

        auto provider = makeObj("provider.o", {".text"});
        addSymbol(provider, "helper", 1, ObjSymbol::Global, 4);

        std::vector<ObjFile> initObjs = {caller, provider};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(globalSyms.count("helper") == 1);
        CHECK(globalSyms["helper"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["helper"].objIndex == 1);
        CHECK(globalSyms["helper"].offset == 4);
        CHECK(dynamicSyms.empty());
    }

    // --- Undefined symbol becomes dynamic (known C library function) ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "printf", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        // printf should be in dynamicSyms (known C library function — no warning).
        CHECK(dynamicSyms.count("printf") == 1);
        CHECK(err.str().empty());
    }

    // --- Known libc/ctype helpers remain silent when treated as dynamic ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "localeconv", 0, ObjSymbol::Undefined);
        addSymbol(obj, "memchr", 0, ObjSymbol::Undefined);
        addSymbol(obj, "isalnum", 0, ObjSymbol::Undefined);
        addSymbol(obj, "isupper", 0, ObjSymbol::Undefined);
        addSymbol(obj, "islower", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(dynamicSyms.count("localeconv") == 1);
        CHECK(dynamicSyms.count("memchr") == 1);
        CHECK(dynamicSyms.count("isalnum") == 1);
        CHECK(dynamicSyms.count("isupper") == 1);
        CHECK(dynamicSyms.count("islower") == 1);
        CHECK(err.str().empty());
    }

    // --- Undefined symbol becomes dynamic (known prefix-matched symbol) ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "CFStringCreateWithCString", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::macOS);
        CHECK(ok);
        CHECK(dynamicSyms.count("CFStringCreateWithCString") == 1);
        // CF* prefix is known — no warning expected.
        CHECK(err.str().empty());
    }

    // --- Allowlisted dynamic symbol still resolves from a real provider ---
    {
        auto caller = makeObj("caller.o", {".text"});
        addSymbol(caller, "printf", 0, ObjSymbol::Undefined);

        auto provider = makeObj("provider.o", {".text"});
        addSymbol(provider, "printf", 1, ObjSymbol::Global, 12);

        std::vector<ObjFile> initObjs = {caller, provider};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Linux);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(dynamicSyms.empty());
        CHECK(globalSyms.count("printf") == 1);
        CHECK(globalSyms["printf"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["printf"].objIndex == 1);
        CHECK(globalSyms["printf"].offset == 12);
    }

    // --- Double-underscore names are not treated as universally dynamic ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "__user_defined_helper", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Linux);
        CHECK(!ok);
        CHECK(dynamicSyms.empty());
        CHECK(err.str().find("undefined symbol '__user_defined_helper'") != std::string::npos);
    }

    // --- Unknown undefined symbol is a hard error ---
    {
        auto obj = makeObj("main.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "totally_unknown_func", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(!ok);
        CHECK(dynamicSyms.empty());
        CHECK(err.str().find("undefined symbol 'totally_unknown_func'") != std::string::npos);
    }

    // --- Runtime shim symbols that must come from archives are not downgraded ---
    {
        auto obj = makeObj("main.obj", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "fprintf", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Windows);
        CHECK(!ok);
        CHECK(dynamicSyms.empty());
        CHECK(err.str().find("undefined symbol 'fprintf'") != std::string::npos);
        CHECK(err.str().find("expected a static/archive definition") != std::string::npos);
    }

    // --- Local symbols don't participate in resolution ---
    {
        auto obj1 = makeObj("a.o", {".text"});
        addSymbol(obj1, "local_helper", 1, ObjSymbol::Local);

        auto obj2 = makeObj("b.o", {".text"});
        addSymbol(obj2, "local_helper", 1, ObjSymbol::Local);

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        // Local symbols should NOT appear in global table.
        CHECK(globalSyms.count("local_helper") == 0);
        CHECK(dynamicSyms.empty());
    }

    // --- Multiple undefined become dynamic ---
    {
        auto obj = makeObj("app.o", {".text"});
        addSymbol(obj, "main", 1, ObjSymbol::Global);
        addSymbol(obj, "malloc", 0, ObjSymbol::Undefined);
        addSymbol(obj, "free", 0, ObjSymbol::Undefined);
        addSymbol(obj, "pthread_create", 0, ObjSymbol::Undefined);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(dynamicSyms.count("malloc") == 1);
        CHECK(dynamicSyms.count("free") == 1);
        CHECK(dynamicSyms.count("pthread_create") == 1);
        // All are known C library functions — no warnings.
        CHECK(err.str().empty());
    }

    // --- Weak + Weak: first wins (no error) ---
    {
        auto obj1 = makeObj("a.o", {".text"});
        addSymbol(obj1, "weak_fn", 1, ObjSymbol::Weak, 0);

        auto obj2 = makeObj("b.o", {".text"});
        addSymbol(obj2, "weak_fn", 1, ObjSymbol::Weak, 8);

        std::vector<ObjFile> initObjs = {obj1, obj2};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms["weak_fn"].binding == GlobalSymEntry::Weak);
        // First weak definition wins.
        CHECK(globalSyms["weak_fn"].objIndex == 0);
        CHECK(globalSyms["weak_fn"].offset == 0);
    }

    // --- Empty symbol names are skipped ---
    {
        auto obj = makeObj("test.o", {".text"});
        addSymbol(obj, "", 1, ObjSymbol::Global);
        addSymbol(obj, "real_sym", 1, ObjSymbol::Global);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(globalSyms.count("") == 0);
        CHECK(globalSyms.count("real_sym") == 1);
    }

    // --- COFF weak external fallback triggers archive extraction ---
    {
        auto user = makeObj("weak_user.obj", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        ObjSymbol weak;
        weak.name = "maybe_func";
        weak.binding = ObjSymbol::Undefined;
        weak.weakExternal = true;
        weak.weakDefaultName = "fallback_func";
        user.symbols.push_back(std::move(weak));

        CodeSection providerText;
        CodeSection providerRodata;
        providerText.defineSymbol("fallback_func", SymbolBinding::Global, SymbolSection::Text);
        providerText.emit8(0xC3);

        std::ostringstream writeErr;
        const std::string providerPath = "build/test-out/weak_fallback_provider.o";
        ElfWriter writer(ObjArch::X86_64);
        CHECK(writer.write(providerPath, providerText, providerRodata, writeErr));

        Archive archive;
        archive.path = "synthetic_weak_fallback.a";
        archive.data = readBinaryFile(providerPath);
        archive.members.push_back({"fallback.o", 0, archive.data.size()});
        archive.symbolIndex["fallback_func"] = 0;

        std::vector<ObjFile> initObjs = {user};
        std::vector<Archive> archives = {archive};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(allObjects.size() == 2);
        CHECK(globalSyms.count("fallback_func") == 1);
        CHECK(globalSyms["fallback_func"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["fallback_func"].objIndex == 1);
    }

    // --- COFF weak external NOLIBRARY fallback does not extract archives ---
    {
        auto user = makeObj("weak_nolib_user.obj", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        ObjSymbol weak;
        weak.name = "maybe_func";
        weak.binding = ObjSymbol::Undefined;
        weak.weakExternal = true;
        weak.weakDefaultName = "fallback_func";
        weak.weakExternalCharacteristics = 1; // IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY.
        user.symbols.push_back(std::move(weak));

        CodeSection providerText;
        CodeSection providerRodata;
        providerText.defineSymbol("fallback_func", SymbolBinding::Global, SymbolSection::Text);
        providerText.emit8(0xC3);

        std::ostringstream writeErr;
        const std::string providerPath = "build/test-out/weak_nolib_provider.o";
        ElfWriter writer(ObjArch::X86_64);
        CHECK(writer.write(providerPath, providerText, providerRodata, writeErr));

        Archive archive;
        archive.path = "synthetic_weak_nolib.a";
        archive.data = readBinaryFile(providerPath);
        archive.members.push_back({"fallback.o", 0, archive.data.size()});
        archive.symbolIndex["fallback_func"] = 0;

        std::vector<ObjFile> initObjs = {user};
        std::vector<Archive> archives = {archive};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(allObjects.size() == 1);
        CHECK(globalSyms.count("fallback_func") == 0);
    }

    // --- Archive duplicate symbol candidates are retried after stale entries ---
    {
        auto user = makeObj("dup_candidate_user.o", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "target_func", 0, ObjSymbol::Undefined);

        auto staleBytes = writeElfObjectWithGlobals("build/test-out/stale_candidate.o", {"other_func"});
        auto targetBytes = writeElfObjectWithGlobals("build/test-out/target_candidate.o", {"target_func"});

        Archive archive;
        archive.path = "synthetic_duplicate_candidates.a";
        archive.data = staleBytes;
        archive.data.insert(archive.data.end(), targetBytes.begin(), targetBytes.end());
        archive.members.push_back({"stale.o", 0, staleBytes.size()});
        archive.members.push_back({"target.o", staleBytes.size(), targetBytes.size()});
        archive.symbolIndex["target_func"] = 0;
        archive.symbolCandidates["target_func"] = {0, 1};

        std::vector<ObjFile> initObjs = {user};
        std::vector<Archive> archives = {archive};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(allObjects.size() == 3);
        CHECK(globalSyms["target_func"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["target_func"].objIndex == 2);
    }

    // --- Windows runtime duplicate exceptions do not mask arbitrary archive conflicts ---
    {
        auto user = makeObj("user.obj", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "fprintf", 1, ObjSymbol::Global);
        addSymbol(user, "need_archive", 0, ObjSymbol::Undefined);

        auto memberBytes = writeElfObjectWithGlobals("build/test-out/third_party_conflict.o",
                                                     {"need_archive", "fprintf"});

        Archive archive;
        archive.path = "third_party.lib";
        archive.data = memberBytes;
        archive.members.push_back({"conflict.o", 0, memberBytes.size()});
        archive.symbolIndex["need_archive"] = 0;
        archive.symbolCandidates["need_archive"] = {0};

        std::vector<ObjFile> initObjs = {user};
        std::vector<Archive> archives = {archive};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Windows);
        CHECK(!ok);
        CHECK(err.str().find("multiply defined symbol 'fprintf'") != std::string::npos);
    }

    // --- Viper runtime archives may provide Windows shim duplicates ---
    {
        auto user = makeObj("user.obj", {".text"});
        addSymbol(user, "main", 1, ObjSymbol::Global);
        addSymbol(user, "fprintf", 1, ObjSymbol::Global);
        addSymbol(user, "need_archive", 0, ObjSymbol::Undefined);

        auto memberBytes = writeElfObjectWithGlobals("build/test-out/viper_runtime_shim.o",
                                                     {"need_archive", "fprintf"});

        Archive archive;
        archive.path = "viper_rt_base.lib";
        archive.data = memberBytes;
        archive.members.push_back({"shim.o", 0, memberBytes.size()});
        archive.symbolIndex["need_archive"] = 0;
        archive.symbolCandidates["need_archive"] = {0};

        std::vector<ObjFile> initObjs = {user};
        std::vector<Archive> archives = {archive};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Windows);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms["fprintf"].objIndex == 0);
        CHECK(globalSyms["need_archive"].objIndex == 1);
    }

    // --- Undefined then defined in same object ---
    {
        // An object can reference a symbol as undefined but also define it
        // (e.g. forward references within a single TU). The defined symbol
        // should win.
        auto obj = makeObj("self.o", {".text", ".data"});
        addSymbol(obj, "self_func", 0, ObjSymbol::Undefined);
        addSymbol(obj, "self_func", 1, ObjSymbol::Global, 4);

        std::vector<ObjFile> initObjs = {obj};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(globalSyms["self_func"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["self_func"].secIndex == 1);
        CHECK(dynamicSyms.empty());
    }

    // --- Mixed: undefined + weak + strong across three objects ---
    {
        auto undef = makeObj("undef.o", {".text"});
        addSymbol(undef, "mixed_sym", 0, ObjSymbol::Undefined);

        auto weak = makeObj("weak.o", {".text"});
        addSymbol(weak, "mixed_sym", 1, ObjSymbol::Weak, 0);

        auto strong = makeObj("strong.o", {".data"});
        addSymbol(strong, "mixed_sym", 1, ObjSymbol::Global, 16);

        std::vector<ObjFile> initObjs = {undef, weak, strong};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(globalSyms["mixed_sym"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["mixed_sym"].objIndex == 2);
        CHECK(globalSyms["mixed_sym"].offset == 16);
        CHECK(dynamicSyms.empty());
    }

    // --- ELF/COFF common symbols coalesce into one linker-owned allocation ---
    {
        auto a = makeObj("a.o", {});
        addCommonSymbol(a, "tentative", 4, 4);

        auto b = makeObj("b.o", {});
        addCommonSymbol(b, "tentative", 12, 8);

        std::vector<ObjFile> initObjs = {a, b};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(allObjects.size() == 3);
        CHECK(allObjects.back().sections.size() == 2);
        CHECK(allObjects.back().sections[1].name == ".common");
        CHECK(allObjects.back().sections[1].zeroFill);
        CHECK(allObjects.back().sections[1].data.empty());
        CHECK(objSectionMemSize(allObjects.back().sections[1]) == 12);
        CHECK(allObjects.back().sections[1].alignment == 8);
        CHECK(globalSyms["tentative"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["tentative"].objIndex == 2);
        CHECK(globalSyms["tentative"].secIndex == 1);
        CHECK(!globalSyms["tentative"].common);
    }

    // --- Strong definitions override tentative/common definitions ---
    {
        auto common = makeObj("common.o", {});
        addCommonSymbol(common, "storage", 32, 16);

        auto strong = makeObj("strong.o", {".data"});
        addSymbol(strong, "storage", 1, ObjSymbol::Global, 4);

        std::vector<ObjFile> initObjs = {common, strong};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(allObjects.size() == 2);
        CHECK(globalSyms["storage"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["storage"].objIndex == 1);
        CHECK(globalSyms["storage"].secIndex == 1);
        CHECK(globalSyms["storage"].offset == 4);
        CHECK(!globalSyms["storage"].common);
    }

    // --- ELF/COFF do not apply Mach-O underscore fallback ---
    {
        auto caller = makeObj("caller.o", {".text"});
        addSymbol(caller, "foo", 0, ObjSymbol::Undefined);
        auto provider = makeObj("provider.o", {".text"});
        addSymbol(provider, "_foo", 1, ObjSymbol::Global);

        std::vector<ObjFile> initObjs = {caller, provider};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::Linux);
        CHECK(!ok);
        CHECK(err.str().find("undefined symbol 'foo'") != std::string::npos);
    }

    // --- Mach-O keeps the underscore compatibility fallback ---
    {
        auto caller = makeObj("caller.o", {".text"});
        caller.format = ObjFileFormat::MachO;
        addSymbol(caller, "foo", 0, ObjSymbol::Undefined);
        auto provider = makeObj("provider.o", {".text"});
        provider.format = ObjFileFormat::MachO;
        addSymbol(provider, "_foo", 1, ObjSymbol::Global, 8);

        std::vector<ObjFile> initObjs = {caller, provider};
        std::vector<Archive> archives;
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;
        std::vector<ObjFile> allObjects;
        std::unordered_set<std::string> dynamicSyms;
        std::ostringstream err;

        bool ok = resolveSymbols(
            initObjs, archives, globalSyms, allObjects, dynamicSyms, err, LinkPlatform::macOS);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(globalSyms.count("foo") == 1);
        CHECK(globalSyms["foo"].binding == GlobalSymEntry::Global);
        CHECK(globalSyms["foo"].objIndex == 1);
        CHECK(globalSyms["foo"].offset == 8);
    }

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All SymbolResolver tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " SymbolResolver test(s) FAILED.\n";
    return EXIT_FAILURE;
}
