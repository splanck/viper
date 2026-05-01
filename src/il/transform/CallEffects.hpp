//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/CallEffects.hpp
// Purpose: Provide a unified API for querying call instruction side effects
//          to support optimization passes that need to determine whether calls
//          can be safely eliminated, reordered, or hoisted.
// Key invariants: Effect classification is conservative—when in doubt, assume
//                 the call may have side effects. The API integrates runtime
//                 signature metadata and deliberately does not trust raw
//                 instruction attributes without verified callee metadata.
// Ownership/Lifetime: Header-only utilities; no global state.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/runtime/HelperEffects.hpp"
#include "il/runtime/RuntimeOwnership.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <cstdint>
#include <string_view>

namespace il::transform {

/// @brief Describe the side-effect classification of a call instruction.
/// @details Used by optimization passes to determine what transformations
///          are safe. Known callee metadata is authoritative.
struct CallEffects {
    bool pure = false;     ///< Call has no observable side effects (can eliminate if unused).
    bool readonly = false; ///< Call may read memory but performs no writes (can reorder).
    bool nothrow = false;  ///< Call cannot throw or trap (can hoist across exception boundaries).
    std::uint64_t consumedArgMask = 0; ///< IL-visible args whose ownership is consumed.
    std::uint64_t retainedArgMask = 0; ///< IL-visible args whose reference count is retained.
    bool returnsOwned = false;         ///< Call returns an owned reference/string handle.
    bool mayAllocate = false;          ///< Call may allocate runtime-managed storage.

    /// @brief Returns true if the call can be safely eliminated when its result is unused.
    [[nodiscard]] constexpr bool canEliminateIfUnused() const noexcept {
        return pure && nothrow;
    }

    /// @brief Returns true if the call can be safely reordered with memory operations.
    [[nodiscard]] constexpr bool canReorderWithMemory() const noexcept {
        return pure || readonly;
    }

    /// @brief True when argument @p index has ownership consumed by this call.
    [[nodiscard]] constexpr bool consumesArg(unsigned index) const noexcept {
        return index < 64 && (consumedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief True when argument @p index has its reference count retained by this call.
    [[nodiscard]] constexpr bool retainsArg(unsigned index) const noexcept {
        return index < 64 && (retainedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief True when any known ownership effect is attached to this call.
    [[nodiscard]] constexpr bool hasOwnershipEffects() const noexcept {
        return consumedArgMask != 0 || retainedArgMask != 0 || returnsOwned || mayAllocate;
    }
};

inline void applyRuntimeOwnership(CallEffects &effects,
                                  il::runtime::RuntimeOwnershipEffects ownership) {
    effects.consumedArgMask |= ownership.consumedArgMask;
    effects.retainedArgMask |= ownership.retainedArgMask;
    effects.returnsOwned = effects.returnsOwned || ownership.returnsOwned;
    effects.mayAllocate = effects.mayAllocate || ownership.mayAllocate;
}

/// @brief Query side-effect metadata for a call instruction.
/// @details Runtime metadata is authoritative when available. Unknown callees
///          are classified conservatively; the verifier rejects call-site
///          attributes unless the callee has effect metadata.
///
/// @param instr The call instruction to classify.
/// @return Effect classification for the call.
inline CallEffects classifyCallEffects(const il::core::Instr &instr) {
    CallEffects effects;

    if (instr.op != il::core::Opcode::Call)
        return effects; // Conservative: unknown effect for non-call instructions

    if (const auto *sig = il::runtime::findRuntimeSignature(instr.callee)) {
        effects.pure = sig->pure;
        effects.readonly = sig->readonly;
        effects.nothrow = sig->nothrow;
        effects.consumedArgMask = sig->consumedArgMask;
        effects.retainedArgMask = sig->retainedArgMask;
        effects.returnsOwned = sig->returnsOwned;
        effects.mayAllocate = sig->mayAllocate;
        applyRuntimeOwnership(effects, il::runtime::classifyRuntimeOwnership(instr.callee));
        return effects;
    } else {
        const auto helperEffects = il::runtime::classifyHelperEffects(instr.callee);
        if (helperEffects.known) {
            effects.pure = helperEffects.pure;
            effects.readonly = helperEffects.readonly;
            effects.nothrow = helperEffects.nothrow;
        }
    }
    applyRuntimeOwnership(effects, il::runtime::classifyRuntimeOwnership(instr.callee));

    return effects;
}

/// @brief Query side-effect metadata for a callee by name.
/// @details Useful when the full instruction is not available.
/// @param callee The callee name to classify.
/// @return Effect classification for the callee.
inline CallEffects classifyCalleeEffects(std::string_view callee) {
    CallEffects effects;

    if (const auto *sig = il::runtime::findRuntimeSignature(callee)) {
        effects.pure = sig->pure;
        effects.readonly = sig->readonly;
        effects.nothrow = sig->nothrow;
        effects.consumedArgMask = sig->consumedArgMask;
        effects.retainedArgMask = sig->retainedArgMask;
        effects.returnsOwned = sig->returnsOwned;
        effects.mayAllocate = sig->mayAllocate;
        applyRuntimeOwnership(effects, il::runtime::classifyRuntimeOwnership(callee));
        return effects;
    }

    const auto helperEffects = il::runtime::classifyHelperEffects(callee);
    effects.pure = helperEffects.pure;
    effects.readonly = helperEffects.readonly;
    effects.nothrow = helperEffects.nothrow;
    applyRuntimeOwnership(effects, il::runtime::classifyRuntimeOwnership(callee));

    return effects;
}

} // namespace il::transform
