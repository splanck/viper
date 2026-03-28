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
//          and dynamic symbol classification.
// Key invariants:
//   - Strong > Weak > Undefined precedence
//   - Multiple strong definitions of the same symbol = linker error
//   - Undefined symbols become dynamic after resolution
//   - Known dynamic patterns accepted silently; unknowns warned
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/SymbolResolver.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

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

int main() {
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

        bool ok = resolveSymbols(initObjs, archives, globalSyms, allObjects, dynamicSyms, err);
        CHECK(ok);
        CHECK(dynamicSyms.count("CFStringCreateWithCString") == 1);
        // CF* prefix is known — no warning expected.
        CHECK(err.str().empty());
    }

    // --- Unknown undefined symbol produces warning ---
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
        CHECK(ok);
        // Still treated as dynamic, but with a warning.
        CHECK(dynamicSyms.count("totally_unknown_func") == 1);
        CHECK(err.str().find("treating undefined symbol") != std::string::npos);
        CHECK(err.str().find("totally_unknown_func") != std::string::npos);
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

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All SymbolResolver tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " SymbolResolver test(s) FAILED.\n";
    return EXIT_FAILURE;
}
