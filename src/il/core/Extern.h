// File: src/il/core/Extern.h
// Purpose: Represents external function declarations in IL modules.
// Key invariants: Parameter count matches signature.
// Ownership/Lifetime: Module owns extern declarations.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Type.h"
#include <string>
#include <vector>

namespace il::core {

/// @brief External function declaration.
struct Extern {
  std::string name;
  Type retType;
  std::vector<Type> params;
};

} // namespace il::core
