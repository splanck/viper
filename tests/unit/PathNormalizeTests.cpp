// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify DebugCtrl path normalization and basename extraction.
// Key invariants: normalization collapses dot segments and backslashes.
// Ownership/Lifetime: N/A (standalone unit test).
// Links: docs/dev/vm.md
#include "VM/Debug.h"
#include <cassert>
#include <string>

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
