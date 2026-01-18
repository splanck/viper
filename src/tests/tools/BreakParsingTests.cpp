//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/BreakParsingTests.cpp
// Purpose: Verify heuristic parsing of --break flag arguments.
// Key invariants: Tokens with path hints parse as source lines; empty prefixes are rejected.
// Ownership/Lifetime: N/A.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "tools/viper/break_spec.hpp"
#include <iostream>

int main()
{
    using ilc::isSrcBreakSpec;
    if (isSrcBreakSpec("L1"))
    {
        std::cerr << "L1 misclassified as src-line\n";
        return 1;
    }
    if (!isSrcBreakSpec("tests/e2e/BreakSrcExact.bas:5"))
    {
        std::cerr << "file path not classified as src-line\n";
        return 1;
    }
    if (!isSrcBreakSpec("file.with.dots.bas:7"))
    {
        std::cerr << "dotted file not classified as src-line\n";
        return 1;
    }
    if (!isSrcBreakSpec("foo:7"))
    {
        std::cerr << "plain token not classified as src-line\n";
        return 1;
    }
    if (!isSrcBreakSpec("foo:  7"))
    {
        std::cerr << "whitespace-padded line not classified as src-line\n";
        return 1;
    }
    if (!isSrcBreakSpec("L1:2"))
    {
        std::cerr << "label-like token with digits not classified as src-line\n";
        return 1;
    }
    if (isSrcBreakSpec(":5"))
    {
        std::cerr << "empty prefix misclassified as src-line\n";
        return 1;
    }
    return 0;
}
