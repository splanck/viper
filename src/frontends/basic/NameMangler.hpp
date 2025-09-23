// File: src/frontends/basic/NameMangler.hpp
// Purpose: Declares symbol mangling helpers for BASIC frontend.
// Key invariants: None.
// Ownership/Lifetime: Functions allocate strings for caller.
// Links: docs/codemap.md
#pragma once

#include <string>
#include <unordered_map>

namespace il::frontends::basic
{

/// @brief Generates deterministic names for temporaries and blocks.
/// @invariant Temp IDs increase sequentially; block names gain numeric suffixes on collision.
/// @ownership Pure utility; no external ownership.
class NameMangler
{
  public:
    /// @brief Return next temporary name ("%t0", "%t1", ...).
    std::string nextTemp();

    /// @brief Return a block label based on @p hint ("entry", "then", ...).
    /// If the hint was used before, a numeric suffix is appended.
    std::string block(const std::string &hint);

  private:
    /// @brief Monotonically increasing ID for temporary names.
    unsigned tempCounter{0};
    /// @brief Map of block name hints to the number of times they've been used.
    std::unordered_map<std::string, unsigned> blockCounters;
};

} // namespace il::frontends::basic
