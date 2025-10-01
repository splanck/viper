// File: tests/unit/test_vm_normalize_path.cpp
// Purpose: Verify debug path normalization collapses separators and dot segments.
// Key invariants: Backslashes become slashes; './' removed; 'dir/../' collapsed.
// Ownership: Standalone executable.
// Links: docs/codemap.md
#include "support/path_utils.hpp"
#include <cassert>
#include <string>

int main()
{
    il::support::PathCache cache;
    assert(cache.normalize(R"(a\b\c)") == "a/b/c");
    assert(cache.normalize(R"(C:\project\src\..\main.bas)") == "C:/project/main.bas");
    assert(cache.normalize("./a/./b") == "a/b");
    assert(cache.normalize("../foo/../bar") == "../bar");
    assert(cache.normalize("dir/../file") == "file");
    assert(cache.normalize("/foo/../") == "/");
    assert(cache.normalize("") == ".");
    return 0;
}
