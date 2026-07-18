//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/io/ParserLimits.hpp
// Purpose: Define configurable resource budgets for textual IL parsing.
// Key invariants: Defaults are large enough for generated compiler output while
//                 bounding allocation from untrusted source text.
// Ownership/Lifetime: Plain value configuration copied into parser state.
// Links: docs/adr/0111-il-text-resource-limits.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

namespace il::io {

struct ParserLimits {
    std::size_t maxLineBytes{1U << 20};
    std::size_t maxLines{1'000'000};
    std::size_t maxFunctions{100'000};
    std::size_t maxExterns{100'000};
    std::size_t maxGlobals{100'000};
    std::size_t maxBlocks{1'000'000};
    std::size_t maxInstructions{10'000'000};
    std::size_t maxValuesPerInstruction{65'535};
    std::size_t maxTempsPerFunction{10'000'000};
};

} // namespace il::io
