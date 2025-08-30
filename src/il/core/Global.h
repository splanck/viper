// File: src/il/core/Global.h
// Purpose: Represents global variables in IL modules.
// Key invariants: Initialized values match declared type.
// Ownership/Lifetime: Module owns global variables.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Type.h"
#include <string>

namespace il::core {

/// @brief Global constant or variable.
struct Global {
  std::string name;
  Type type;
  std::string init; // used for const str
};

} // namespace il::core
