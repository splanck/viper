// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify path normalization for debug source breakpoints.
// Key invariants: Normalization is purely lexical and extracts basename.
// Ownership/Lifetime: Test owns no resources.
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string in = "a/b/../c\\file.bas";
    std::string norm = il::vm::DebugCtrl::normalizePath(in);
    assert(norm == "a/c/file.bas");
    std::string base = norm.substr(norm.find_last_of('/') + 1);
    assert(base == "file.bas");
    return 0;
}
