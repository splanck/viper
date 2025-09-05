// File: tests/unit/test_vm_normalize_path.cpp
// Purpose: Verify debug path normalization collapses separators and dot segments.
// Key invariants: Backslashes become slashes; './' removed; 'dir/../' collapsed;
// paths under the current directory lose the absolute prefix.
// Ownership: Standalone executable.
// Links: docs/class-catalog.md
#include "VM/Debug.h"
#include <cassert>
#include <filesystem>
#include <string>

int main()
{
    using il::vm::DebugCtrl;
    assert(DebugCtrl::normalizePath(R"(a\b\c)") == "a/b/c");
    assert(DebugCtrl::normalizePath("./a/./b") == "a/b");
    assert(DebugCtrl::normalizePath("dir/../file") == "file");
    assert(DebugCtrl::normalizePath("/foo/../") == "/");
    std::string cwd = std::filesystem::current_path().generic_string();
    std::string abs = cwd + "/dir/../file";
    assert(DebugCtrl::normalizePath(abs) == "file");
    return 0;
}
