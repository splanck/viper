// File: src/il/core/Function.hpp
// Purpose: Defines IL function structure.
// Key invariants: Parameters match function type.
// Ownership/Lifetime: Module owns functions and their blocks.
// Links: docs/il-spec.md
#pragma once
#include "il/core/BasicBlock.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include <string>
#include <vector>

namespace il::core
{

/// @brief Function definition consisting of parameters and basic blocks.
struct Function
{
    std::string name;
    Type retType;
    std::vector<Param> params;
    std::vector<BasicBlock> blocks;
};

} // namespace il::core
