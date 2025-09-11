// File: src/frontends/basic/NameMangler.cpp
// Purpose: Implements symbol mangling for BASIC frontend.
// Key invariants: None.
// Ownership/Lifetime: Uses strings managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/NameMangler.hpp"

namespace il::frontends::basic
{
/// Generates a temporary identifier using the "%t" prefix followed by an
/// incrementing numeric suffix. Each call advances `tempCounter`, ensuring
/// sequential and unique names for temporaries.
std::string NameMangler::nextTemp()
{
    return "%t" + std::to_string(tempCounter++);
}

/// Produces a block label from the provided `hint`. The first request for a
/// particular hint yields the hint unchanged; subsequent requests append an
/// incrementing number. This mutates `blockCounters` to track usage counts.
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
