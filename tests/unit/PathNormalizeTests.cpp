// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify path normalization helper collapses separators and dot segments.
// Key invariants: Normalization is lexical and basename extraction matches filename.
// Ownership: Standalone unit test.
// Links: docs/tools/ilc.md

#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string norm = il::vm::DebugCtrl::normalizePath("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    auto pos = norm.find_last_of('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
