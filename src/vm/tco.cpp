//===----------------------------------------------------------------------===//
// Tail-call optimisation helper
//===----------------------------------------------------------------------===//

#include "vm/tco.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Type.hpp"
#include "vm/VM.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "rt.hpp"

#include <optional>
#include <cstdio>
#include <string>

namespace il::vm
{

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
    const size_t regCount = callee->valueNames.size();
    if (callee->name == "fact" || callee->name == "f" || callee->name == "g")
    {
        std::fprintf(stderr,
                     "[TCO] callee=%s valueNames=%zu params=%zu blocks=%zu\n",
                     callee->name.c_str(),
                     regCount,
                     callee->params.size(),
                     callee->blocks.size());
        size_t maxParamId = 0;
        for (const auto &p : callee->params)
            maxParamId = std::max(maxParamId, static_cast<size_t>(p.id));
        std::fprintf(stderr, "[TCO] maxParamId=%zu\n", maxParamId);
        std::fflush(stderr);
    }
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
        if (id >= fr.params.size())
            return false;
        const bool isStr = params[i].type.kind == il::core::Type::Kind::Str;
        if (isStr)
        {
            auto &dest = fr.params[id];
            if (dest)
                rt_str_release_maybe(dest->str);
            Slot retained = args[i];
            rt_str_retain_maybe(retained.str);
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
    // Emit debug/trace tailcall event
    vm.onTailCall(fromFn, callee);
    return true;
#endif
}

} // namespace il::vm
