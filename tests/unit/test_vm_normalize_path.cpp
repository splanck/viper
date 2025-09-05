// File: tests/unit/test_vm_normalize_path.cpp
// Purpose: Verify debug path normalization resolves symlinks and expresses
// paths relative to the current working directory while collapsing separators
// and dot segments.
// Key invariants: Backslashes become slashes; './' removed; 'dir/../' collapsed;
// symlink targets returned relative to CWD.
// Ownership: Standalone executable.
// Links: docs/class-catalog.md
#include "VM/Debug.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    using il::vm::DebugCtrl;
    namespace fs = std::filesystem;
    assert(DebugCtrl::normalizePath(R"(a\b\c)") == "a/b/c");
    assert(DebugCtrl::normalizePath("./a/./b") == "a/b");
    assert(DebugCtrl::normalizePath("dir/../file") == "file");
    fs::path rootRel = fs::path("/").lexically_relative(fs::current_path());
    assert(DebugCtrl::normalizePath("/foo/../") == rootRel.generic_string());

    fs::path tmp = fs::current_path() / "np_tmp";
    fs::create_directories(tmp);
    fs::path target = tmp / "file.txt";
    {
        std::ofstream(target.string()) << "x";
    }
    fs::path link = fs::current_path() / "link.txt";
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    assert(DebugCtrl::normalizePath("link.txt") == "np_tmp/file.txt");
    assert(DebugCtrl::normalizePath(link.string()) == "np_tmp/file.txt");
    fs::remove(link);
    fs::remove(target);
    fs::remove(tmp);
    return 0;
}
