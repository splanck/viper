// File: src/il/verify/GlobalVerifier.cpp
// Purpose: Implements global declaration verification ensuring uniqueness within a module.
// Key invariants: Global definitions may not share a name; lookup table mirrors module globals.
// Ownership/Lifetime: Stores pointers to module-owned globals for later lookups.
// Links: docs/il-reference.md

#include "il/verify/GlobalVerifier.hpp"

#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Expected;
using il::support::makeError;
}

Expected<void> GlobalVerifier::run(const Module &module, DiagSink &)
{
    globals_.clear();

    for (const auto &global : module.globals)
    {
        if (!globals_.emplace(global.name, &global).second)
            return Expected<void>{makeError({}, "duplicate global @" + global.name)};
    }

    return {};
}

} // namespace il::verify
