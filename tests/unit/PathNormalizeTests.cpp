// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify normalized paths yield correct basename and relative form.
// Key invariants: Backslashes become slashes; '..' segments collapsed;
// basename extracted from normalized path; absolute paths returned relative
// to the current working directory.
// Ownership: Standalone executable.
// Links: docs/class-catalog.md
#include "VM/Debug.h"
#include <cassert>
#include <filesystem>

int main()
{
    using il::vm::DebugCtrl;
    namespace fs = std::filesystem;
    fs::path abs = fs::current_path() / "a/b/../c\\file.bas";
    std::string norm = DebugCtrl::normalizePath(abs.string());
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    return 0;
}
