// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Unit tests for DebugCtrl path normalization.
// Key invariants: Paths are normalized lexically and basenames extracted correctly.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/dev/vm.md

#include "VM/Debug.h"
#include <cassert>
#include <string>

int main()
{
    std::string norm = il::vm::DebugCtrl::normalizePath("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    auto pos = norm.find_last_of('/');
    std::string base = pos == std::string::npos ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
}
