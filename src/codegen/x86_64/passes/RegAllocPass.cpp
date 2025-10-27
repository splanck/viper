// File: src/codegen/x86_64/passes/RegAllocPass.cpp
// Purpose: Implement the register allocation gating pass for the x86-64 pipeline.
// Key invariants: Register allocation is only considered complete when legalisation has run.
// Ownership/Lifetime: Stateless transformation toggling Module flags.
// Links: docs/codemap.md

#include "codegen/x86_64/passes/RegAllocPass.hpp"

namespace viper::codegen::x64::passes
{

bool RegAllocPass::run(Module &module, Diagnostics &diags)
{
    if (!module.legalised)
    {
        diags.error("regalloc: legalisation must run before register allocation");
        return false;
    }
    module.registersAllocated = true;
    return true;
}

} // namespace viper::codegen::x64::passes
