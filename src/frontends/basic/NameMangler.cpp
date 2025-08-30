// File: src/frontends/basic/NameMangler.cpp
// Purpose: Implements symbol mangling for BASIC frontend.
// Key invariants: None.
// Ownership/Lifetime: Uses strings managed externally.
// Links: docs/class-catalog.md
#include "frontends/basic/NameMangler.h"

namespace il::frontends::basic {

std::string NameMangler::nextTemp() { return "%t" + std::to_string(tempCounter++); }

std::string NameMangler::block(const std::string &hint) {
  auto &count = blockCounters[hint];
  std::string name = hint;
  if (count > 0)
    name += std::to_string(count);
  ++count;
  return name;
}

} // namespace il::frontends::basic
