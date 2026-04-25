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
//                 signature metadata with instruction-level attributes.
// Ownership/Lifetime: Header-only utilities; no global state.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/runtime/HelperEffects.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include <string_view>

namespace il::transform {

/// @brief Describe the side-effect classification of a call instruction.
/// @details Used by optimization passes to determine what transformations
///          are safe. Known callee metadata is authoritative; instruction
///          attributes are used only when no registry metadata exists.
struct CallEffects {
    bool pure = false;     ///< Call has no observable side effects (can eliminate if unused).
    bool readonly = false; ///< Call may read memory but performs no writes (can reorder).
    bool nothrow = false;  ///< Call cannot throw or trap (can hoist across exception boundaries).

    /// @brief Returns true if the call can be safely eliminated when its result is unused.
    [[nodiscard]] constexpr bool canEliminateIfUnused() const noexcept {
        return pure && nothrow;
    }

    /// @brief Returns true if the call can be safely reordered with memory operations.
    [[nodiscard]] constexpr bool canReorderWithMemory() const noexcept {
        return pure || readonly;
    }
};

/// @brief Query side-effect metadata for a call instruction.
/// @details Runtime metadata is authoritative when available. Instruction
///          attributes are only trusted for callees that are not present in the
///          runtime helper tables; the verifier rejects contradictory attrs for
///          known callees.
///
/// @param instr The call instruction to classify.
/// @return Effect classification for the call.
inline CallEffects classifyCallEffects(const il::core::Instr &instr) {
    CallEffects effects;

    if (instr.op != il::core::Opcode::Call)
        return effects; // Conservative: unknown effect for non-call instructions

    const auto helperEffects = il::runtime::classifyHelperEffects(instr.callee);
    bool known = helperEffects.known;
    effects.pure = helperEffects.pure;
    effects.readonly = helperEffects.readonly;
    effects.nothrow = helperEffects.nothrow;

    for (const auto &sig : il::runtime::signatures::all_signatures()) {
        if (sig.name == instr.callee) {
            known = true;
            effects.pure = sig.pure;
            effects.readonly = sig.readonly;
            effects.nothrow = sig.nothrow;
            break;
        }
    }

    if (!known) {
        effects.pure = instr.CallAttr.pure;
        effects.readonly = instr.CallAttr.readonly;
        effects.nothrow = instr.CallAttr.nothrow;
    }

    return effects;
}

/// @brief Query side-effect metadata for a callee by name.
/// @details Useful when the full instruction is not available.
/// @param callee The callee name to classify.
/// @return Effect classification for the callee.
inline CallEffects classifyCalleeEffects(std::string_view callee) {
    CallEffects effects;

    // 1. Check HelperEffects constexpr table (fast, covers common helpers)
    const auto helperEffects = il::runtime::classifyHelperEffects(callee);
    effects.pure = helperEffects.pure;
    effects.readonly = helperEffects.readonly;
    effects.nothrow = helperEffects.nothrow;

    // Skip slower registry scan when already fully classified.
    if (effects.pure && effects.readonly && effects.nothrow)
        return effects;

    // 2. Check runtime signature registry (comprehensive)
    for (const auto &sig : il::runtime::signatures::all_signatures()) {
        if (sig.name == callee) {
            effects.pure = effects.pure || sig.pure;
            effects.readonly = effects.readonly || sig.readonly;
            effects.nothrow = effects.nothrow || sig.nothrow;
            break;
        }
    }

    return effects;
}

} // namespace il::transform
