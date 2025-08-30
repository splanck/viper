// File: src/il/core/Function.h
// Purpose: Defines IL function structure.
// Key invariants: Parameters match function type.
// Ownership/Lifetime: Module owns functions and their blocks.
// Links: docs/il-spec.md
#pragma once
#include "il/core/BasicBlock.h"
#include "il/core/Param.h"
#include "il/core/Type.h"
#include <string>
#include <vector>

namespace il::core {

/// @brief Function definition consisting of parameters and basic blocks.
struct Function {
  std::string name;
  Type retType;
  std::vector<Param> params;
  std::vector<BasicBlock> blocks;
};

} // namespace il::core
