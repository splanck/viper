// File: src/codegen/x86_64/passes/LegalizePass.cpp
// Purpose: Implement the legalisation pass guard in the x86-64 codegen pipeline.
// Key invariants: Legalisation is only considered successful when lowering has produced an
//                 adapter module; future extensions can add real transformations here.
// Ownership/Lifetime: Stateless pass mutating Module flags in place.
// Links: docs/codemap.md

#include "codegen/x86_64/passes/LegalizePass.hpp"

namespace viper::codegen::x64::passes
{

bool LegalizePass::run(Module &module, Diagnostics &diags)
{
    if (!module.lowered)
    {
        diags.error("legalize: lowering has not produced an adapter module");
        return false;
    }
    module.legalised = true;
    return true;
}

} // namespace viper::codegen::x64::passes
