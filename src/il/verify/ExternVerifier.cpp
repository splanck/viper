//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// @file
// @brief Implement verification of module extern declarations.
// @details Builds lookup tables for extern signatures, checks duplicate
//          declarations, and validates consistency with the runtime signature
//          database.

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
/// @details Used when deduplicating module-provided externs so the verifier can
///          distinguish benign duplicates from conflicting declarations.
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
/// @details Ensures module-declared externs mirror the canonical runtime
///          signature database so runtime dispatch remains sound.
///
/// @param decl Extern declaration authored in IL.
/// @param runtime Canonical runtime signature retrieved from the runtime table.
/// @return @c true when both signatures agree on return and parameter types.
bool signaturesMatch(const Extern &decl, const il::runtime::RuntimeSignature &runtime)
{
    if (decl.retType.kind != runtime.retType.kind ||
        decl.params.size() != runtime.paramTypes.size())
        return false;
    for (size_t i = 0; i < runtime.paramTypes.size(); ++i)
        if (decl.params[i].kind != runtime.paramTypes[i].kind)
            return false;
    return true;
}

} // namespace

/// @brief Access the interned extern declaration map.
/// @return Reference to the map keyed by extern name.
[[nodiscard]] const ExternVerifier::ExternMap &ExternVerifier::externs() const
{
    return externs_;
}

/// @brief Populate the extern map and validate declarations for a module.
///
/// @details Rejects duplicates, reports signature mismatches, and cross-checks
///          definitions against known runtime signatures.
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
