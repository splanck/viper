// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify normalized paths yield correct basename.
// Key invariants: Backslashes become slashes; '..' segments collapsed;
// basename extracted from normalized path.
// Ownership: Standalone executable.
// Links: docs/class-catalog.md
#include "VM/Debug.h"
#include <cassert>

int main()
{
    using il::vm::DebugCtrl;
    std::string norm = DebugCtrl::normalizePath("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
