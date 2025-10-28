//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/ops/common/Branching.cpp
// Purpose: Implement shared branching helpers used by VM opcode handlers.
// Key invariants: Helpers honour IL semantics by validating branch argument
//                 counts and propagating values before transferring control.
// Ownership/Lifetime: Operates on VM-owned state; no allocations escape the
//                     helper scope.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "vm/ops/common/Branching.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "rt.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

namespace il::vm::ops::common
{
namespace
{
[[noreturn]] void reportBranchArgMismatch(const il::core::BasicBlock &target,
                                          const il::core::BasicBlock *source,
                                          size_t expected,
                                          size_t provided,
                                          const il::core::Instr &instr,
                                          const Frame &frame)
{
    const std::string sourceLabel = source ? source->label : std::string{};
    const std::string functionName = frame.func ? frame.func->name : std::string{};

    std::ostringstream os;
    os << "branch argument count mismatch targeting '" << target.label << '\'';
    if (!sourceLabel.empty())
        os << " from '" << sourceLabel << '\'';
    os << ": expected " << expected << ", got " << provided;
    RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), instr.loc, functionName, sourceLabel);
    std::_Exit(1);
}
} // namespace

Target select_case(Scalar scrutinee, std::span<const Case> table, Target default_tgt)
{
    for (const Case &entry : table)
    {
        if (!entry.isRange)
        {
            if (scrutinee.value == entry.lower.value)
                return entry.target;
            continue;
        }

        if (scrutinee.value >= entry.lower.value && scrutinee.value <= entry.upper.value)
            return entry.target;
    }

    return default_tgt;
}

void jump(Frame &frame, Target target)
{
    assert(target.valid() && "attempted to jump to an invalid target");

    auto it = target.blocks->find(target.instr->labels[target.labelIndex]);
    assert(it != target.blocks->end() && "branch target must resolve to a basic block");
    const il::core::BasicBlock *dest = it->second;
    const il::core::BasicBlock *sourceBlock = *target.currentBlock;

    const size_t expected = dest->params.size();
    const size_t provided =
        target.labelIndex < target.instr->brArgs.size() ? target.instr->brArgs[target.labelIndex].size() : 0;
    if (provided != expected)
        reportBranchArgMismatch(*dest, sourceBlock, expected, provided, *target.instr, frame);

    if (provided > 0)
    {
        const auto &args = target.instr->brArgs[target.labelIndex];
        for (size_t i = 0; i < provided; ++i)
        {
            const auto &param = dest->params[i];
            const auto id = param.id;
            assert(id < frame.params.size());

            Slot incoming = detail::VMAccess::eval(*target.vm, frame, args[i]);
            auto &destSlot = frame.params[id];

            if (param.type.kind == il::core::Type::Kind::Str)
            {
                if (destSlot)
                    rt_str_release_maybe(destSlot->str);

                rt_str_retain_maybe(incoming.str);
                destSlot = incoming;
                continue;
            }

            destSlot = incoming;
        }
    }

    *target.currentBlock = dest;
    *target.ip = 0;
}

Scalar eval_scrutinee(Frame &frame, const il::core::Instr &instr)
{
    VM *vm = activeVMInstance();
    assert(vm != nullptr && "active VM instance required to evaluate scrutinee");
    Slot slot = detail::VMAccess::eval(*vm, frame, switchScrutinee(instr));
    Scalar scalar{};
    scalar.value = static_cast<int32_t>(slot.i64);
    return scalar;
}

} // namespace il::vm::ops::common

