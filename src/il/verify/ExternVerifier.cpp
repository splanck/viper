//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/ExternVerifier.cpp
// Purpose: Define the logic used to validate extern declarations found in IL
//          modules by cross-referencing module state with the runtime signature
//          registry and enforcing uniqueness rules.
// Key invariants: Extern declarations are uniquely keyed by name within a
//                 module and their type signatures must agree with the runtime
//                 database when an entry exists.
// Ownership/Lifetime: The verifier stores non-owning pointers to declarations
//                     that remain valid for the duration of the verification
//                     pass.  Diagnostics are produced via the provided sinks but
//                     no ownership is transferred.
// Links: docs/il-guide.md#externs
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements verification of module extern declarations.
/// @details Builds lookup tables for extern signatures, checks duplicate
///          declarations, and validates consistency with the runtime signature
///          database.

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
/// @details Each extern stores a return type and an ordered parameter list.  The
///          helper performs a structural equality check across both properties,
///          enabling the verifier to identify redeclarations that change the
///          callable surface area.
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
/// @details The runtime signature table mirrors the ABI contract enforced by the
///          runtime.  This overload aligns the IL declaration with the runtime
///          descriptor to ensure both agree on return and parameter kinds before
///          a call is permitted.
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
/// @details Verification populates @ref externs_ with pointers to every extern in
///          the currently processed module.  Exposing the map enables follow-up
///          passes to query the verified declarations without re-walking the
///          module.
///
/// @return Reference to the map keyed by extern name.
[[nodiscard]] const ExternVerifier::ExternMap &ExternVerifier::externs() const
{
    return externs_;
}

/// @brief Populate the extern map and validate declarations for a module.
///
/// @details The verifier clears any previously cached state, then iterates the
///          module's extern list.  For each declaration it ensures the name is
///          unique within the module, compares re-declarations for exact
///          signature matches, and, when the runtime publishes a signature,
///          cross-checks the IL declaration against that canonical descriptor.
///          Upon failure the function returns a diagnostic explaining the
///          mismatch; otherwise an empty success signals the module is valid.
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
