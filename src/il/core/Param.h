// File: src/il/core/Param.h
// Purpose: Defines function parameter representation.
// Key invariants: Type matches function signature.
// Ownership/Lifetime: Parameters stored by value.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Type.h"
#include <string>

namespace il::core {

/// @brief Function parameter.
struct Param {
  std::string name;
  Type type;
};

} // namespace il::core
