//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/tco.cpp
// Purpose: Tail-call optimisation (TCO) for reusing the current execution frame.
//
// Key invariants:
//   - VIPER_VM_TAILCALL compile flag controls TCO availability.
//   - Callee must have a valid entry block; null callee or empty blocks fail.
//   - Argument arity must match entry block parameter count.
//   - String ownership follows retain/release semantics.
//   - EH stack and resume state are preserved across tail calls.
//
// Ownership/Lifetime:
//   - Frame is reused in place; no new allocations except vector resizing.
//   - Register count cache avoids repeated function scans.
//
// Links: docs/architecture.md, docs/vm.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Tail-call optimization support for VM execution frames.
/// @details Reuses the current frame for eligible tail calls to reduce stack
///          growth. The transformation is gated by compile-time flags and
///          preserves exception-handling state across the call.

#include "vm/tco.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Type.hpp"
#include "rt.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <optional>
#include <string>

namespace il::vm
{

/// @brief Attempt to perform a tail call by reusing the current frame.
/// @details When enabled, this function rewrites the active execution state to
///          execute @p callee in place of the current function. It rebuilds the
///          block map, resizes the register file, reinitializes parameters, and
///          preserves EH/resume state. The call is rejected when the callee is
///          null, has no entry block, or the argument count does not match.
/// @param vm Active VM instance.
/// @param callee Function to tail-call (must be non-NULL).
/// @param args Arguments to seed into the callee's entry parameters.
/// @return True if the tail call was performed; false if not eligible.
bool tryTailCall(VM &vm, const il::core::Function *callee, std::span<const Slot> args)
{
#if !defined(VIPER_VM_TAILCALL) || !VIPER_VM_TAILCALL
    (void)vm;
    (void)callee;
    (void)args;
    return false;
#else
    if (callee == nullptr)
        return false;

    auto *stPtr = detail::VMAccess::currentExecState(vm);
    if (!stPtr)
        return false;
    auto &st = *stPtr;

    // Prepare new block map and entry block
    st.blocks.clear();
    for (const auto &b : callee->blocks)
        st.blocks[b.label] = &b;
    const il::core::BasicBlock *entry = callee->blocks.empty() ? nullptr : &callee->blocks.front();
    if (entry == nullptr)
        return false;

    // Reinitialise frame, preserving EH and resume state
    Frame &fr = st.fr;
    const il::core::Function *fromFn = fr.func;
    // Preserve EH stack and resume state
    auto preservedEh = fr.ehStack;
    auto preservedResume = fr.resumeState;

    fr.func = callee;
    fr.regs.clear();

    // Use shared helper to compute/cache register file size
    const size_t maxSsaId = detail::VMAccess::computeMaxSsaId(vm, *callee);
    const size_t regCount = maxSsaId + 1;
    fr.regs.resize(regCount);
    fr.sp = 0;
    // Reset params to new size
    fr.params.assign(fr.regs.size(), std::nullopt);
    // Restore preserved EH state
    fr.ehStack = std::move(preservedEh);
    fr.resumeState = preservedResume;
    fr.activeError = {};

    // Seed entry parameters from args, retaining strings
    // Use entry block parameters for arity and seeding
    const auto &params = entry->params;
    if (args.size() != params.size())
    {
        // Arity mismatch: skip TCO
        return false;
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto id = params[i].id;
        // Parameter ID must fit in the pre-sized register file
        assert(id < fr.params.size() && "TCO: parameter ID exceeds params vector size");
        if (id >= fr.params.size())
            return false;
        const bool isStr = params[i].type.kind == il::core::Type::Kind::Str;
        if (isStr)
        {
            auto &dest = fr.params[id];
            // Retain new value before releasing old to prevent dangling pointer
            // when args[i] aliases fr.params[id] (self-assignment during tail call).
            Slot retained = args[i];
            rt_str_retain_maybe(retained.str);
            if (dest)
                rt_str_release_maybe(dest->str);
            dest = retained;
        }
        else
        {
            fr.params[id] = args[i];
        }
    }

    // Point execution to callee entry
    st.bb = entry;
    st.ip = 0;
    st.skipBreakOnce = false;
    st.switchCache.clear();
    // Transfer block parameters to registers immediately.
    // This is critical because after TCO, the dispatch loop may use the fast path
    // which skips the pre-execution debug hooks that normally call transferBlockParams.
    detail::VMAccess::transferBlockParams(vm, fr, *entry);
    // Emit debug/trace tailcall event
    vm.onTailCall(fromFn, callee);
    return true;
#endif
}

} // namespace il::vm
