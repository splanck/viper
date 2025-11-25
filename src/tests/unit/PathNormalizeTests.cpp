//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify normalized paths yield correct basename.
// Key invariants: Backslashes become slashes; '..' segments collapsed;
// Ownership/Lifetime: Standalone executable.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/vm/debug/Debug.hpp"
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
