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

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: create a minimal ObjFile with one rodata section containing a string.
static ObjFile makeRodataObj(const std::string &name, const std::string &str) {
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;

    // Section 0: null.
    obj.sections.push_back({});

    // Section 1: cstring section with the string (NUL-terminated).
    ObjSection sec;
    sec.name = ".rodata.str1.1";
    sec.data.assign(str.begin(), str.end());
    sec.data.push_back(0); // NUL terminator.
    sec.executable = false;
    sec.writable = false;
    sec.alloc = true;
    sec.isCStringSection = true;
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

int main() {
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

        // Non-cstring sections are excluded from dedup entirely.
        size_t eliminated = deduplicateStrings(objs, globalSyms);
        CHECK(eliminated == 0);
    }

    // --- __const section data is NOT deduplicated (regression test) ---
    // Binary data like integer arrays {64, 128, ...} can start with bytes
    // that look like a short NUL-terminated string (e.g., 0x40 0x00 = "@\0").
    // The dedup pass must NOT treat __const sections as string sections.
    {
        ObjFile obj;
        obj.name = "pool.o";
        obj.format = ObjFileFormat::MachO;

        obj.sections.push_back({}); // null

        // Section 1: __const with integer array data (NOT a cstring section).
        ObjSection sec;
        sec.name = "__TEXT,__const";
        sec.data = {0x40,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00, // 64
                    0x80,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00}; // 128
        sec.executable = false;
        sec.writable = false;
        sec.alloc = true;
        sec.isCStringSection = false; // NOT a cstring section!
        obj.sections.push_back(sec);

        obj.symbols.push_back({}); // null

        ObjSymbol sym;
        sym.name = "kClassSizes";
        sym.sectionIndex = 1;
        sym.offset = 0;
        sym.binding = ObjSymbol::Local;
        obj.symbols.push_back(sym);

        // Another object with a __cstring section containing "@" (0x40, 0x00).
        ObjFile obj2;
        obj2.name = "game.o";
        obj2.format = ObjFileFormat::MachO;
        obj2.sections.push_back({}); // null

        ObjSection sec2;
        sec2.name = "__TEXT,__cstring";
        sec2.data = {0x40, 0x00}; // "@" string
        sec2.executable = false;
        sec2.writable = false;
        sec2.alloc = true;
        sec2.isCStringSection = true; // IS a cstring section
        obj2.sections.push_back(sec2);

        obj2.symbols.push_back({}); // null

        ObjSymbol sym2;
        sym2.name = "l_.str.at";
        sym2.sectionIndex = 1;
        sym2.offset = 0;
        sym2.binding = ObjSymbol::Local;
        obj2.symbols.push_back(sym2);

        std::vector<ObjFile> objs = {obj, obj2};
        std::unordered_map<std::string, GlobalSymEntry> globalSyms;

        size_t eliminated = deduplicateStrings(objs, globalSyms);

        // kClassSizes must NOT be merged with the "@" string — different
        // section types. Only one occurrence in a cstring section.
        CHECK(eliminated == 0);

        // kClassSizes must keep its original name and binding.
        CHECK(objs[0].symbols[1].name == "kClassSizes");
        CHECK(objs[0].symbols[1].binding == ObjSymbol::Local);
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
    if (gFail == 0) {
        std::cout << "All StringDedup tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " StringDedup test(s) FAILED.\n";
    return EXIT_FAILURE;
}
