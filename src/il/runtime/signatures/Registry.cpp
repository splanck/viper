//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Registry.cpp
// Purpose: Implement the lightweight signature registry used for debug
//          validation of runtime helper metadata.
// Key invariants: Registration preserves insertion order, exposes stable
//                 references for the lifetime of the process, and tolerates
//                 duplicate entries so higher layers can re-register helpers
//                 without mutating the data that prior consumers observe.
// Ownership/Lifetime: Stored signatures have static storage managed by this
//                     translation unit, ensuring the registry survives for the
//                     duration of the process and remains accessible from
//                     multiple translation units.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"
#include "il/runtime/HelperEffects.hpp"

/// @file
/// @brief Defines the backing container for runtime signature metadata.
/// @details Runtime verification utilities rely on a shared table of
///          @ref Signature objects to cross-check compiler emitted calls
///          against the C runtime ABI.  This translation unit provides the
///          canonical storage and mutation helpers for that table, keeping the
///          behaviour uniform across the various registration modules.

namespace il::runtime::signatures
{
namespace
{
/// @brief Access the process-wide container that owns runtime signatures.
/// @details The helper wraps a function-local static so the vector is lazily
///          constructed upon first use yet guaranteed to survive until program
///          shutdown.  Because the registry returns a reference, callers avoid
///          global variable exposure while still mutating the shared storage.
///          The vector intentionally never shrinks; registration is append-only
///          so previously returned references remain valid for diagnostic tools
///          that snapshot the registry contents.
/// @return Mutable vector storing registered signatures in insertion order.
std::vector<Signature> &registry()
{
    static std::vector<Signature> g_signatures;
    return g_signatures;
}
} // namespace

/// @brief Append a runtime signature to the diagnostic registry.
/// @details Each call records a @ref Signature entry describing a runtime
///          helper.  The append-only model deliberately allows duplicate names
///          so independent subsystems can register overlapping helpers without
///          coordination.  Consumers that require uniqueness can deduplicate the
///          returned array themselves without mutating the canonical storage.
/// @param signature Signature metadata describing a runtime helper.
Signature apply_effect_overrides(Signature signature)
{
    const auto effects = il::runtime::classifyHelperEffects(signature.name);
    signature.nothrow = signature.nothrow || effects.nothrow;
    signature.readonly = signature.readonly || effects.readonly;
    signature.pure = signature.pure || effects.pure;
    return signature;
}

void register_signature(const Signature &signature)
{
    registry().push_back(apply_effect_overrides(signature));
}

/// @brief Retrieve a stable view of all registered runtime signatures.
/// @details Returns a reference to the underlying container so callers can
///          iterate without copying.  The reference remains valid because the
///          registry owns static storage for the lifetime of the process.
///          Subsequent registrations may invalidate iterators but never the
///          reference itself, matching standard library container semantics.
/// @return Read-only view of the registered signatures in insertion order.
const std::vector<Signature> &all_signatures()
{
    return registry();
}

} // namespace il::runtime::signatures
