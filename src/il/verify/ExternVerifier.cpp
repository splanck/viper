// File: src/il/verify/ExternVerifier.cpp
// Purpose: Implements verification of module extern declarations and builds lookup tables.
// Key invariants: Extern signatures remain stable during verification; duplicate names are rejected.
// Ownership/Lifetime: Stores pointers to module-owned extern declarations.
// Links: docs/il-guide.md#reference

#include "il/verify/ExternVerifier.hpp"

#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <sstream>

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Expected;
using il::support::makeError;

bool signaturesMatch(const Extern &lhs, const Extern &rhs)
{
    if (lhs.retType.kind != rhs.retType.kind || lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i)
        if (lhs.params[i].kind != rhs.params[i].kind)
            return false;
    return true;
}

bool signaturesMatch(const Extern &decl, const il::runtime::RuntimeSignature &runtime)
{
    if (decl.retType.kind != runtime.retType.kind || decl.params.size() != runtime.paramTypes.size())
        return false;
    for (size_t i = 0; i < runtime.paramTypes.size(); ++i)
        if (decl.params[i].kind != runtime.paramTypes[i].kind)
            return false;
    return true;
}

} // namespace

Expected<void> ExternVerifier::run(const Module &module, DiagSink &)
{
    externs_.clear();

    for (const auto &ext : module.externs)
    {
        auto [it, inserted] = externs_.emplace(ext.name, &ext);
        if (!inserted)
        {
            const Extern *prev = it->second;
            std::ostringstream msg;
            msg << "duplicate extern @" << ext.name;
            if (!signaturesMatch(*prev, ext))
                msg << " with mismatched signature";
            return Expected<void>{makeError({}, msg.str())};
        }

        if (const auto *runtimeSig = il::runtime::findRuntimeSignature(ext.name))
        {
            if (!signaturesMatch(ext, *runtimeSig))
                return Expected<void>{makeError({}, "extern @" + ext.name + " signature mismatch")};
        }
    }

    return {};
}

} // namespace il::verify
