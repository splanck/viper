// File: lib/Passes/Mem2Reg.h
// Purpose: Promote eligible stack slots to SSA registers.
// Key invariants: Promotes i64/f64/i1 allocas with no escaped addresses using
// sealed SSA construction with block parameters, handling loops. Ownership/
// Lifetime: Mutates the module in place.
// Links: docs/passes/mem2reg.md
#pragma once

#include "il/core/Module.hpp"

namespace viper::passes
{

struct Mem2RegStats
{
    unsigned promotedVars{0};
    unsigned removedLoads{0};
    unsigned removedStores{0};
};

/// \brief Promote simple allocas to SSA form.
/// \param M Module to transform in place.
/// \param stats Optional statistics output.
void mem2reg(il::core::Module &M, Mem2RegStats *stats = nullptr);

} // namespace viper::passes
