// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify path normalization helper handles separators and parent segments.
// Key invariants: Normalization replaces backslashes, removes './', and collapses 'dir/../'.
// Ownership: Test directly exercises il::vm::DebugCtrl utility.
// Links: docs/class-catalog.md

#include "VM/Debug.h"
#include <cassert>

int main()
{
    std::string p = "a/b/../c\\file.bas";
    std::string norm = il::vm::DebugCtrl::normalizePath(p);
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = pos == std::string::npos ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
