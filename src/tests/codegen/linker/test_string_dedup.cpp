//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_string_dedup.cpp
// Purpose: Unit tests for cross-module string deduplication.
// Key invariants:
//   - Identical NUL-terminated strings across objects are merged
//   - Strings with identical prefixes but different lengths are NOT merged
//   - Non-string rodata (no NUL terminator) is not affected
//   - Single-occurrence strings are not modified
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/StringDedup.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/StringDedup.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

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

/// Helper: create a minimal ObjFile with one rodata section containing a string.
static ObjFile makeRodataObj(const std::string &name, const std::string &str)
{
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;

    // Section 0: null.
    obj.sections.push_back({});

    // Section 1: rodata with the string (NUL-terminated).
    ObjSection sec;
    sec.name = ".rodata";
    sec.data.assign(str.begin(), str.end());
    sec.data.push_back(0); // NUL terminator.
    sec.executable = false;
    sec.writable = false;
    sec.alloc = true;
    sec.alignment = 1;
    obj.sections.push_back(sec);

    // Symbol 0: null.
    obj.symbols.push_back({});

    // Symbol 1: local symbol referencing the string at offset 0.
    ObjSymbol sym;
    sym.name = "L.str." + name;
    sym.sectionIndex = 1;
    sym.offset = 0;
    sym.binding = ObjSymbol::Local;
    obj.symbols.push_back(sym);

    return obj;
}

int main()
{
    // --- Identical strings across 3 objects are deduplicated ---
    {
        auto obj1 = makeRodataObj("a.o", "hello");
        auto obj2 = makeRodataObj("b.o", "hello");
        auto obj3 = makeRodataObj("c.o", "hello");

        std::vector<ObjFile> objs = {obj1, obj2, obj3};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);

        // 3 occurrences → 2 eliminated (1 canonical kept).
        CHECK(eliminated == 2);

        // All three symbols should share the same name.
        CHECK(objs[0].symbols[1].name == objs[1].symbols[1].name);
        CHECK(objs[1].symbols[1].name == objs[2].symbols[1].name);

        // All should be promoted to Global binding.
        CHECK(objs[0].symbols[1].binding == ObjSymbol::Global);
        CHECK(objs[1].symbols[1].binding == ObjSymbol::Global);
        CHECK(objs[2].symbols[1].binding == ObjSymbol::Global);

        // One globalSyms entry should exist with the synthetic name.
        CHECK(globalSyms.count(objs[0].symbols[1].name) == 1);
    }

    // --- Strings with same prefix but different length are NOT merged ---
    {
        auto obj1 = makeRodataObj("a.o", "he");
        auto obj2 = makeRodataObj("b.o", "hello");

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);

        // Different lengths → no dedup.
        CHECK(eliminated == 0);

        // Symbols should retain their original distinct names.
        CHECK(objs[0].symbols[1].name != objs[1].symbols[1].name);
    }

    // --- Single-occurrence string is not modified ---
    {
        auto obj1 = makeRodataObj("a.o", "unique");

        std::vector<ObjFile> objs = {obj1};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);

        CHECK(eliminated == 0);

        // Symbol should keep its original name and binding.
        CHECK(objs[0].symbols[1].name == "L.str.a.o");
        CHECK(objs[0].symbols[1].binding == ObjSymbol::Local);
    }

    // --- Non-string rodata (no NUL) is not affected ---
    {
        ObjFile obj;
        obj.name = "floats.o";
        obj.format = ObjFileFormat::ELF;

        obj.sections.push_back({}); // null

        // Section 1: rodata with 8 bytes of float data (no NUL).
        ObjSection sec;
        sec.name = ".rodata";
        sec.data = {0x40, 0x49, 0x0F, 0xDB, 0x40, 0x49, 0x0F, 0xDB}; // pi, pi
        sec.executable = false;
        sec.writable = false;
        sec.alloc = true;
        obj.sections.push_back(sec);

        obj.symbols.push_back({}); // null

        ObjSymbol sym;
        sym.name = "L.float.0";
        sym.sectionIndex = 1;
        sym.offset = 0;
        sym.binding = ObjSymbol::Local;
        obj.symbols.push_back(sym);

        // Duplicate object.
        ObjFile obj2 = obj;
        obj2.name = "floats2.o";
        obj2.symbols[1].name = "L.float.1";

        std::vector<ObjFile> objs = {obj, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        // Note: these bytes DO contain 0x00 if the float data happened to
        // include NUL bytes. In this case the data 0x40,0x49,0x0F,0xDB...
        // has no NUL so memchr returns nullptr → skipped.
        // But if data DOES contain NUL, it would be "deduplicated" as a
        // NUL-terminated string — which is fine since identical byte
        // sequences are interchangeable regardless of type.
        size_t eliminated = deduplicateStrings(objs, globalSyms);
        CHECK(eliminated == 0);
    }

    // --- Global symbols are not touched ---
    {
        auto obj1 = makeRodataObj("a.o", "hello");
        obj1.symbols[1].binding = ObjSymbol::Global; // Make it global.

        auto obj2 = makeRodataObj("b.o", "hello");

        std::vector<ObjFile> objs = {obj1, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);

        // obj1's symbol is Global → skipped. Only obj2 is LOCAL.
        // Single LOCAL occurrence → not deduplicated.
        CHECK(eliminated == 0);
    }

    // --- Executable section symbols are not touched ---
    {
        ObjFile obj;
        obj.name = "code.o";
        obj.format = ObjFileFormat::ELF;
        obj.sections.push_back({}); // null

        ObjSection sec;
        sec.name = ".text";
        sec.data = {0xC3, 0x00}; // ret + NUL byte
        sec.executable = true;
        sec.writable = false;
        sec.alloc = true;
        obj.sections.push_back(sec);

        obj.symbols.push_back({}); // null
        ObjSymbol sym;
        sym.name = "L.code";
        sym.sectionIndex = 1;
        sym.offset = 0;
        sym.binding = ObjSymbol::Local;
        obj.symbols.push_back(sym);

        ObjFile obj2 = obj;
        obj2.name = "code2.o";

        std::vector<ObjFile> objs = {obj, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);
        CHECK(eliminated == 0); // Executable sections are excluded.
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All StringDedup tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " StringDedup test(s) FAILED.\n";
    return EXIT_FAILURE;
}
