//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "vm/DiagFormat.hpp"
#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>

namespace il::vm::ops::common
{
namespace
{
/// @brief Abort execution when a branch provides an incorrect number of arguments.
/// @details Formats a descriptive trap message that includes the source and
///          destination labels along with the expected/provided counts, then
///          routes the message through the runtime bridge before exiting.  The
///          function never returns because the VM treats argument mismatches as
///          fatal verifier errors.
/// @param target Destination block referenced by the branch.
/// @param source Source block issuing the branch (may be null for entry).
/// @param expected Number of parameters declared by the destination block.
/// @param provided Number of arguments supplied by the branch.
/// @param instr Branch instruction that triggered the mismatch.
/// @param frame Current frame supplying context for diagnostics.
[[noreturn]] void reportBranchArgMismatch(const il::core::BasicBlock &target,
                                          const il::core::BasicBlock *source,
                                          size_t expected,
                                          size_t provided,
                                          const il::core::Instr &instr,
                                          const Frame &frame)
{
    const std::string sourceLabel = source ? source->label : std::string{};
    const std::string functionName = frame.func ? frame.func->name : std::string{};

    RuntimeBridge::trap(
        TrapKind::InvalidOperation,
        diag::formatBranchArgMismatch(target.label, sourceLabel, expected, provided),
        instr.loc,
        functionName,
        sourceLabel);
    std::_Exit(1);
}
} // namespace

/// @brief Resolve the target for a SELECT CASE-style dispatch.
/// @details Iterates the ordered case table, checking single-value entries first
///          followed by range entries.  The first match determines the target
///          block.  When no case matches the function returns @p default_tgt,
///          allowing opcode handlers to fall back to the default branch.
/// @param scrutinee Value being matched.
/// @param table Ordered list of case entries (singletons and ranges).
/// @param default_tgt Target describing the default branch.
/// @return Target describing the block that should be executed next.
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

/// @brief Transfer control to the block described by @p target.
/// @details Validates the branch argument arity, evaluates operands using the
///          VM access helper, and moves the resulting slots into the destination
///          block's parameter array.  String parameters receive retain/release
///          bookkeeping to align with runtime ownership expectations.  Finally,
///          the function updates the caller's current block and instruction
///          pointer so the dispatch loop resumes at the new location.
/// @param frame Active frame that owns the parameter storage.
/// @param target Describes the branch instruction, destination map, and context.
void jump(Frame &frame, Target target)
{
    try
    {
        assert(target.valid() && "attempted to jump to an invalid target");

        const il::core::BasicBlock *dest = nullptr;
        if (auto *st = il::vm::detail::VMAccess::currentExecState(*target.vm))
        {
            auto &cache = st->branchTargetCache;
            auto &resolved = cache[target.instr];
            if (resolved.size() != target.instr->labels.size())
            {
                resolved.resize(target.instr->labels.size());
                for (size_t i = 0; i < target.instr->labels.size(); ++i)
                {
                    auto it = target.blocks->find(target.instr->labels[i]);
                    assert(it != target.blocks->end() &&
                           "branch target must resolve to a basic block");
                    resolved[i] = it->second;
                }
            }
            dest = resolved[target.labelIndex];
        }
        else
        {
            auto it = target.blocks->find(target.instr->labels[target.labelIndex]);
            assert(it != target.blocks->end() && "branch target must resolve to a basic block");
            dest = it->second;
        }
        const il::core::BasicBlock *sourceBlock = *target.currentBlock;

        const size_t expected = dest->params.size();
        const size_t provided = target.labelIndex < target.instr->brArgs.size()
                                    ? target.instr->brArgs[target.labelIndex].size()
                                    : 0;
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
    catch (const std::exception &ex)
    {
        const il::core::Instr *instr = target.instr;
        const std::string fnName = frame.func ? frame.func->name : std::string();
        const std::string blk = (target.currentBlock && *target.currentBlock)
                                    ? (*target.currentBlock)->label
                                    : std::string();
        std::string msg = std::string("branch jump exception: ") + ex.what();
        il::vm::RuntimeBridge::trap(TrapKind::InvalidOperation,
                                    msg,
                                    instr ? instr->loc : il::support::SourceLoc{},
                                    fnName,
                                    blk);
    }
}

/// @brief Evaluate the scrutinee operand for switch-like opcodes.
/// @details Looks up the active VM instance, evaluates the operand using the
///          generic VM access helper, and coerces the result to a 32-bit scalar
///          suitable for table lookups.  The helper asserts the presence of a
///          running VM because switch opcode handlers are only valid during
///          execution.
/// @param frame Active frame providing operand slots.
/// @param instr Instruction containing the scrutinee operand metadata.
/// @return Scalar representation of the scrutinee value.
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
