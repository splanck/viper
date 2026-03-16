//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_string_table.cpp
// Purpose: Unit tests for StringTable — verifies string interning, dedup,
//          offset computation, and NUL termination behavior.
// Key invariants:
//   - Offset 0 is always the empty string
//   - Duplicate adds return the same offset
//   - All strings are NUL-terminated in the raw data
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/StringTable.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/StringTable.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

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
    // --- Initial state ---
    {
        StringTable st;
        CHECK(st.size() == 1); // single NUL byte
        CHECK(st.data().size() == 1);
        CHECK(st.data()[0] == '\0');
        CHECK(st.find("") == 0);
    }

    // --- Add single string ---
    {
        StringTable st;
        uint32_t off = st.add("hello");
        CHECK(off == 1);       // after the initial NUL
        CHECK(st.size() == 7); // NUL + "hello" + NUL = 1 + 5 + 1 = 7
        CHECK(st.find("hello") == 1);

        // Verify NUL termination
        const auto &d = st.data();
        CHECK(d[0] == '\0');
        CHECK(d[1] == 'h');
        CHECK(d[5] == 'o');
        CHECK(d[6] == '\0');
    }

    // --- Deduplication ---
    {
        StringTable st;
        uint32_t off1 = st.add("foo");
        uint32_t off2 = st.add("foo");
        CHECK(off1 == off2);   // same offset
        CHECK(st.size() == 5); // NUL + "foo" + NUL = 1 + 3 + 1 = 5
    }

    // --- Multiple strings ---
    {
        StringTable st;
        uint32_t a = st.add(".text");
        uint32_t b = st.add(".rodata");
        uint32_t c = st.add(".symtab");

        CHECK(a == 1);                     // NUL + ".text"
        CHECK(b == a + 5 + 1);             // after ".text\0"
        CHECK(c == b + 7 + 1);             // after ".rodata\0"
        CHECK(st.size() == 1 + 6 + 8 + 8); // NUL + ".text\0" + ".rodata\0" + ".symtab\0"

        // Find returns correct offsets
        CHECK(st.find(".text") == a);
        CHECK(st.find(".rodata") == b);
        CHECK(st.find(".symtab") == c);
    }

    // --- Find nonexistent ---
    {
        StringTable st;
        st.add("exists");
        CHECK(st.find("nope") == UINT32_MAX);
    }

    // --- Empty string add ---
    {
        StringTable st;
        uint32_t off = st.add("");
        CHECK(off == 0);       // dedup with the initial empty string
        CHECK(st.size() == 1); // no growth
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All StringTable tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " StringTable test(s) FAILED.\n";
    return EXIT_FAILURE;
}
