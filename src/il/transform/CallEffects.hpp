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

namespace il::transform
{

/// @brief Describe the side-effect classification of a call instruction.
/// @details Used by optimization passes to determine what transformations
///          are safe. The flags are conservative: if any source indicates
///          an effect is absent, that information is used.
struct CallEffects
{
    bool pure = false;     ///< Call has no observable side effects (can eliminate if unused).
    bool readonly = false; ///< Call may read memory but performs no writes (can reorder).
    bool nothrow = false;  ///< Call cannot throw or trap (can hoist across exception boundaries).

    /// @brief Returns true if the call can be safely eliminated when its result is unused.
    [[nodiscard]] constexpr bool canEliminateIfUnused() const noexcept
    {
        return pure;
    }

    /// @brief Returns true if the call can be safely reordered with memory operations.
    [[nodiscard]] constexpr bool canReorderWithMemory() const noexcept
    {
        return pure || readonly;
    }
};

/// @brief Query side-effect metadata for a call instruction.
/// @details Combines information from:
///   1. Instruction-level CallAttr flags (pure, readonly)
///   2. Runtime signature registry (if callee is a known runtime helper)
///   3. HelperEffects constexpr table (for fast lookup of common helpers)
///
/// The function returns a conservative classification: a call is only marked
/// pure/readonly/nothrow if at least one source indicates so.
///
/// @param instr The call instruction to classify.
/// @return Effect classification for the call.
inline CallEffects classifyCallEffects(const il::core::Instr &instr)
{
    CallEffects effects;

    if (instr.op != il::core::Opcode::Call)
        return effects; // Conservative: unknown effect for non-call instructions

    // 1. Check instruction-level attributes first (fastest)
    effects.pure = instr.CallAttr.pure;
    effects.readonly = instr.CallAttr.readonly;

    // 2. Check HelperEffects constexpr table (fast, covers common helpers)
    const auto helperEffects = il::runtime::classifyHelperEffects(instr.callee);
    effects.pure = effects.pure || helperEffects.pure;
    effects.readonly = effects.readonly || helperEffects.readonly;
    effects.nothrow = effects.nothrow || helperEffects.nothrow;

    // 3. Check runtime signature registry (comprehensive, slightly slower)
    for (const auto &sig : il::runtime::signatures::all_signatures())
    {
        if (sig.name == instr.callee)
        {
            effects.pure = effects.pure || sig.pure;
            effects.readonly = effects.readonly || sig.readonly;
            effects.nothrow = effects.nothrow || sig.nothrow;
            break;
        }
    }

    return effects;
}

/// @brief Query side-effect metadata for a callee by name.
/// @details Useful when the full instruction is not available.
/// @param callee The callee name to classify.
/// @return Effect classification for the callee.
inline CallEffects classifyCalleeEffects(std::string_view callee)
{
    CallEffects effects;

    // 1. Check HelperEffects constexpr table (fast, covers common helpers)
    const auto helperEffects = il::runtime::classifyHelperEffects(callee);
    effects.pure = helperEffects.pure;
    effects.readonly = helperEffects.readonly;
    effects.nothrow = helperEffects.nothrow;

    // 2. Check runtime signature registry (comprehensive)
    for (const auto &sig : il::runtime::signatures::all_signatures())
    {
        if (sig.name == callee)
        {
            effects.pure = effects.pure || sig.pure;
            effects.readonly = effects.readonly || sig.readonly;
            effects.nothrow = effects.nothrow || sig.nothrow;
            break;
        }
    }

    return effects;
}

} // namespace il::transform
