// File: lib/Passes/Mem2Reg.h
// Purpose: Promote eligible stack slots to SSA registers.
// Key invariants: Only handles allocas with a single dominating store and no address escaping.
// Ownership/Lifetime: Mutates the module in place.
// Links: docs/passes/mem2reg.md
#pragma once

#include "il/core/Module.hpp"

namespace viper::passes
{

/// \brief Promote simple allocas to SSA form.
/// \param M Module to transform in place.
void mem2reg(il::core::Module &M);

} // namespace viper::passes
