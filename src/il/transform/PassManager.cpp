// File: src/il/transform/PassManager.cpp
// Purpose: Implements a minimal pass manager for IL transformations.
// Key invariants: Verifier is invoked after each pass in debug builds.
// Ownership/Lifetime: Pass callbacks are stored by value.
// Links: docs/class-catalog.md
#include "il/transform/PassManager.hpp"
#include "il/verify/Verifier.hpp"
#include <cassert>
#include <sstream>

using namespace il::core;
using namespace il::verify;

namespace il::transform
{

void PassManager::addPass(const std::string &name, PassFn fn)
{
    passes_[name] = std::move(fn);
}

void PassManager::run(Module &m, const std::vector<std::string> &names) const
{
    for (const auto &n : names)
    {
        auto it = passes_.find(n);
        if (it == passes_.end())
            continue;
        it->second(m);
#ifndef NDEBUG
        std::ostringstream os;
        assert(Verifier::verify(m, os) && "IL verification failed after pass");
#endif
    }
}

} // namespace il::transform
