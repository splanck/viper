//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements verification for module-level extern declarations.  The verifier
// ensures that extern names are unique, signatures match any known runtime
// definitions, and provides a lookup table used by later verification passes.
// The logic operates directly on the caller-owned Module without taking
// ownership of the declarations themselves.
//
//===----------------------------------------------------------------------===//

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

/// @brief Compare two extern declarations for signature equivalence.
///
/// Checks both the return type and the parameter kinds; parameter names are
/// ignored because they have no semantic meaning for extern matching.
///
/// @param lhs First extern declaration.
/// @param rhs Second extern declaration.
/// @return `true` when the signatures are identical.
bool signaturesMatch(const Extern &lhs, const Extern &rhs)
{
    if (lhs.retType.kind != rhs.retType.kind || lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i)
        if (lhs.params[i].kind != rhs.params[i].kind)
            return false;
    return true;
}

/// @brief Check a module extern declaration against the runtime ABI description.
///
/// Ensures that the module definition agrees with the runtime-supplied
/// signature so that calls dispatched through the runtime trampoline remain
/// well-typed.
///
/// @param decl Module extern declaration.
/// @param runtime Runtime ABI signature for the same symbol.
/// @return `true` when both signatures agree.
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

/// @brief Access the verified extern lookup table.
///
/// The map associates extern names with the declarations owned by the module
/// passed to `run()`.
///
/// @return Reference to the internal extern map.
[[nodiscard]] const ExternVerifier::ExternMap &ExternVerifier::externs() const
{
    return externs_;
}

/// @brief Validate extern declarations and populate the lookup map.
///
/// Walks all extern declarations, reporting duplicates and mismatched runtime
/// signatures.  When verification succeeds the map contains every extern keyed
/// by name for use by downstream passes.
///
/// @param module Module providing the extern declarations.
/// @param sink Diagnostic sink (unused because all diagnostics are returned as errors).
/// @return Success when all externs are valid, otherwise a diagnostic.
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
