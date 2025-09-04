// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Validate path normalization helper.
// Key invariants: Normalizes separators and resolves '.' and '..' segments lexically.
// Ownership/Lifetime: Test owns no resources.
// Links: docs/tools/ilc.md
#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string path = "a/b/../c\\file.bas";
    std::string norm = il::vm::DebugCtrl::normalizePath(path);
    assert(norm == "a/c/file.bas");
    size_t pos = norm.rfind('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
