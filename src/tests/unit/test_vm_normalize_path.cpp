//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_normalize_path.cpp
// Purpose: Verify debug path normalization collapses separators and dot segments. 
// Key invariants: Backslashes become slashes; './' removed; 'dir/../' collapsed.
// Ownership/Lifetime: Standalone executable.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/vm/debug/Debug.hpp"
#include <cassert>
#include <string>

int main()
{
    using il::vm::DebugCtrl;
    assert(DebugCtrl::normalizePath(R"(a\b\c)") == "a/b/c");
    assert(DebugCtrl::normalizePath(R"(C:\project\src\..\main.bas)") == "C:/project/main.bas");
    assert(DebugCtrl::normalizePath("./a/./b") == "a/b");
    assert(DebugCtrl::normalizePath("../foo/../bar") == "../bar");
    assert(DebugCtrl::normalizePath("dir/../file") == "file");
    assert(DebugCtrl::normalizePath("/foo/../") == "/");
    assert(DebugCtrl::normalizePath("") == ".");
    return 0;
}
