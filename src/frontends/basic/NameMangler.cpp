//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/NameMangler.cpp
// Purpose: Implement the BASIC name mangler responsible for synthesizing stable
//          identifiers for temporaries and lowered control-flow labels.
// Key invariants: Generated names are deterministic and collision-free within a
//                 compilation unit.
// Links: docs/basic-language.md, docs/codemap/basic.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements helpers that produce compiler-reserved BASIC identifiers.
/// @details The mangler is isolated so semantic analysis and lowering can share
///          a single policy for naming blocks and temporaries.  Keeping the
///          implementation out-of-line documents the sequencing requirements for
///          counters and hints without polluting the public header.

#include "frontends/basic/NameMangler.hpp"

namespace il::frontends::basic
{
/// @brief Produce the next compiler-generated temporary identifier.
///
/// The mangler reserves the "%t" prefix for temporaries.  Every invocation
/// increments `tempCounter` and appends the previous value to the prefix,
/// yielding monotonically increasing, collision-free identifiers that remain
/// deterministic across compiler runs.
std::string NameMangler::nextTemp()
{
    return "%t" + std::to_string(tempCounter++);
}

/// @brief Derive a unique block label from a human-friendly hint.
///
/// The mangler remembers how often each hint has been used.  The first request
/// returns the hint verbatim to preserve readability in the printed IR.  Each
/// subsequent request appends the current counter value, then increments the
/// counter, ensuring unique yet recognizable labels across control-flow
/// lowering passes.
std::string NameMangler::block(const std::string &hint)
{
    auto &count = blockCounters[hint];
    std::string name = hint;
    if (count > 0)
        name += std::to_string(count);
    ++count;
    return name;
}

} // namespace il::frontends::basic
