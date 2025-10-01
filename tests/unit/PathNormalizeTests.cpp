// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify normalized paths yield correct basename.
// Key invariants: Backslashes become slashes; '..' segments collapsed;
// basename extracted from normalized path.
// Ownership: Standalone executable.
// Links: docs/codemap.md
#include "support/path_utils.hpp"
#include <cassert>

int main()
{
    il::support::PathCache cache;
    std::string norm = cache.normalize("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    std::string base = il::support::basename(norm);
    assert(base == "file.bas");
    return 0;
}
