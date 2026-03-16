//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_symbol_table.cpp
// Purpose: Unit tests for SymbolTable — verifies null entry, add, findOrAdd,
//          index stability, and symbol properties.
// Key invariants:
//   - Index 0 is always the null symbol
//   - findOrAdd creates External symbols for unknown names
//   - Indices are stable after insertion
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/SymbolTable.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/SymbolTable.hpp"

#include <cstdlib>
#include <iostream>

using namespace viper::codegen::objfile;

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

int main()
{
    // --- Initial state: null entry at index 0 ---
    {
        SymbolTable st;
        CHECK(st.count() == 1);
        CHECK(st.at(0).name.empty());
        CHECK(st.at(0).binding == SymbolBinding::Local);
        CHECK(st.at(0).section == SymbolSection::Undefined);
    }

    // --- Add symbols ---
    {
        SymbolTable st;
        uint32_t idx1 = st.add(Symbol{"main", SymbolBinding::Global, SymbolSection::Text, 0, 100});
        uint32_t idx2 =
            st.add(Symbol{"rt_print_i64", SymbolBinding::External, SymbolSection::Undefined, 0, 0});

        CHECK(idx1 == 1);
        CHECK(idx2 == 2);
        CHECK(st.count() == 3);

        CHECK(st.at(1).name == "main");
        CHECK(st.at(1).binding == SymbolBinding::Global);
        CHECK(st.at(1).section == SymbolSection::Text);
        CHECK(st.at(1).offset == 0);
        CHECK(st.at(1).size == 100);

        CHECK(st.at(2).name == "rt_print_i64");
        CHECK(st.at(2).binding == SymbolBinding::External);
    }

    // --- findOrAdd: existing ---
    {
        SymbolTable st;
        st.add(Symbol{"foo", SymbolBinding::Global, SymbolSection::Text, 42, 0});
        uint32_t idx = st.findOrAdd("foo");
        CHECK(idx == 1);        // found existing
        CHECK(st.count() == 2); // no new entry
    }

    // --- findOrAdd: new (creates External) ---
    {
        SymbolTable st;
        uint32_t idx = st.findOrAdd("bar");
        CHECK(idx == 1);
        CHECK(st.count() == 2);
        CHECK(st.at(1).name == "bar");
        CHECK(st.at(1).binding == SymbolBinding::External);
        CHECK(st.at(1).section == SymbolSection::Undefined);
    }

    // --- Index stability ---
    {
        SymbolTable st;
        uint32_t a = st.add(Symbol{"a", SymbolBinding::Local, SymbolSection::Text, 0, 0});
        uint32_t b = st.add(Symbol{"b", SymbolBinding::Local, SymbolSection::Text, 10, 0});
        uint32_t c = st.findOrAdd("c");

        CHECK(a == 1);
        CHECK(b == 2);
        CHECK(c == 3);

        // Old indices still valid
        CHECK(st.at(a).name == "a");
        CHECK(st.at(b).name == "b");
        CHECK(st.at(b).offset == 10);
    }

    // --- Iteration ---
    {
        SymbolTable st;
        st.add(Symbol{"x", SymbolBinding::Global, SymbolSection::Text, 0, 0});
        st.add(Symbol{"y", SymbolBinding::External, SymbolSection::Undefined, 0, 0});

        int count = 0;
        for (auto it = st.begin(); it != st.end(); ++it)
            ++count;
        CHECK(count == 3); // null + x + y
    }

    // --- Mutable access ---
    {
        SymbolTable st;
        uint32_t idx =
            st.add(Symbol{"mutable_test", SymbolBinding::Local, SymbolSection::Text, 0, 0});
        st.at(idx).offset = 999;
        CHECK(st.at(idx).offset == 999);
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All SymbolTable tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " SymbolTable test(s) FAILED.\n";
    return EXIT_FAILURE;
}
