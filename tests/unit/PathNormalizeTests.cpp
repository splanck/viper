// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify path normalization used for --break-src.
// Key invariants: normalizePath collapses segments and basename extraction is correct.
// Ownership: Standalone unit test with no external dependencies.
// Links: docs/dev/vm.md

#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string raw = "a/b/../c\\file.bas";
    std::string norm = il::vm::DebugCtrl::normalizePath(raw);
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = pos == std::string::npos ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
