// File: tests/tools/BreakParsingTests.cpp
// Purpose: Verify heuristic classification of --break tokens.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/testing.md
#include "tools/ilc/break_parse.hpp"
#include <iostream>

int main()
{
    using ilc::isBreakSrcSpec;
    if (isBreakSrcSpec("L1"))
    {
        std::cerr << "L1 misclassified as src line\n";
        return 1;
    }
    if (!isBreakSrcSpec("tests/e2e/BreakSrcExact.bas:5"))
    {
        std::cerr << "path with slash not detected\n";
        return 1;
    }
    if (!isBreakSrcSpec("file.with.dots.bas:7"))
    {
        std::cerr << "path with dots not detected\n";
        return 1;
    }
    if (isBreakSrcSpec("L1:2"))
    {
        std::cerr << "label-like token misclassified\n";
        return 1;
    }
    return 0;
}
