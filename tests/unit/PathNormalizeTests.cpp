// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify DebugCtrl path normalization and basename extraction.
// Key invariants: Normalization replaces backslashes and collapses relative components.
// Ownership/Lifetime: N/A (test only).
// Links: docs/testing.md
#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string norm = il::vm::DebugCtrl::normalizePath("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = pos == std::string::npos ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
