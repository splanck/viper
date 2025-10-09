//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Implements the BASIC front-end name mangler.  The mangler generates stable
// IR identifiers for compiler-generated temporaries and for BASIC block labels
// derived from user hints.  Consolidating the logic here keeps the policy that
// governs automatic naming away from semantic analysis so the strategy can
// evolve without touching the rest of the pipeline.
//===----------------------------------------------------------------------===//

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
