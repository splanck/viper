//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/ExternVerifier.cpp
// Purpose: Validate extern declarations against duplicate definitions and the
//          runtime signature database.
// Key invariants: Extern names remain unique per module and signatures must
//                 agree with runtime metadata when available.
// Ownership/Lifetime: The verifier references module-owned declarations and
//                     caches pointers without extending their lifetime.
// Links: docs/il-guide.md#extern-declarations, docs/architecture.md#il-verify
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements verification of module extern declarations.
/// @details Builds lookup tables for extern signatures, checks duplicate
/// declarations, and validates consistency with the runtime signature database.

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
/// @details Checks both return kind and parameter sequence to ensure modules do
///          not supply incompatible duplicate declarations. Parameter counts and
///          element kinds must match exactly for the declarations to be treated
///          as identical.
///
/// @param lhs First extern declaration.
/// @param rhs Second extern declaration.
/// @return @c true when return and parameter types are identical.
bool signaturesMatch(const Extern &lhs, const Extern &rhs)
{
    if (lhs.retType.kind != rhs.retType.kind || lhs.params.size() != rhs.params.size())
        return false;
    for (size_t i = 0; i < lhs.params.size(); ++i)
        if (lhs.params[i].kind != rhs.params[i].kind)
            return false;
    return true;
}

/// @brief Compare an extern declaration against a runtime signature descriptor.
///
/// @details Runtime metadata defines the ABI contract for built-in externs.
///          This helper validates that the IL declaration mirrors the runtime's
///          return type and parameter sequence exactly to prevent call
///          mismatches at execution time.
///
/// @param decl Extern declaration authored in IL.
/// @param runtime Canonical runtime signature retrieved from the runtime table.
/// @return @c true when both signatures agree on return and parameter types.
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

/// @brief Access the interned extern declaration map.
///
/// @details Exposes the cached mapping from extern names to declarations so
///          downstream verifier components can perform lookups without
///          rebuilding the table.
///
/// @return Reference to the map keyed by extern name.
[[nodiscard]] const ExternVerifier::ExternMap &ExternVerifier::externs() const
{
    return externs_;
}

/// @brief Populate the extern map and validate declarations for a module.
///
/// @details Clears any previous state, then walks all externs to enforce
///          uniqueness and signature correctness. Duplicate names trigger a
///          diagnostic, with additional context when the conflicting signatures
///          differ. When runtime metadata exists the declaration must match the
///          canonical signature exactly, ensuring VM and runtime remain in
///          agreement.
///
/// @param module Module supplying extern declarations.
/// @param sink Diagnostic sink used for structured reporting (unused currently).
/// @return Empty success on validity; otherwise a formatted diagnostic error.
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
