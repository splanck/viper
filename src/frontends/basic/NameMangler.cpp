// File: src/frontends/basic/NameMangler.cpp
// Purpose: Implements symbol mangling for BASIC frontend.
// Key invariants: None.
// Ownership/Lifetime: Uses strings managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/NameMangler.hpp"

namespace il::frontends::basic
{
// Generate a unique temporary name using the "%t" prefix. Each invocation
// increments `tempCounter`, ensuring sequential numbering for temporaries.
std::string NameMangler::nextTemp()
{
    return "%t" + std::to_string(tempCounter++);
}

// Produce a block label from the given `hint`. The `blockCounters` map tracks
// how many times each hint has been requested: the first use returns the hint
// unchanged, while subsequent uses append an incrementing numeric suffix.
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
