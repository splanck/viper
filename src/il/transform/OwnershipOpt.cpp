//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/OwnershipOpt.cpp
// Purpose: Implement local ownership traffic cleanup for runtime references.
//
//===----------------------------------------------------------------------===//

#include "il/transform/OwnershipOpt.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/transform/CallEffects.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

using namespace il::core;

namespace il::transform {
namespace {

enum class OwnershipCallKind { None, Retain, Release };

[[nodiscard]] OwnershipCallKind classifyOwnershipCall(const Instr &instr) {
    if (instr.op != Opcode::Call)
        return OwnershipCallKind::None;

    const std::string_view name = instr.callee;
    if (name == "rt_str_retain" || name == "rt_str_retain_maybe" || name == "rt_arr_i32_retain" ||
        name == "rt_arr_i64_retain" || name == "rt_arr_f64_retain") {
        return OwnershipCallKind::Retain;
    }

    if (name == "rt_str_release" || name == "rt_str_release_maybe" ||
        name == "rt_arr_i32_release" || name == "rt_arr_i64_release" ||
        name == "rt_arr_f64_release") {
        return OwnershipCallKind::Release;
    }

    return OwnershipCallKind::None;
}

[[nodiscard]] bool sameFirstArgument(const Instr &lhs, const Instr &rhs) {
    return !lhs.operands.empty() && !rhs.operands.empty() &&
           valueEquals(lhs.operands.front(), rhs.operands.front());
}

[[nodiscard]] bool instrUsesValue(const Instr &instr, const Value &value) {
    for (const auto &operand : instr.operands)
        if (valueEquals(operand, value))
            return true;
    for (const auto &argList : instr.brArgs)
        for (const auto &arg : argList)
            if (valueEquals(arg, value))
                return true;
    return false;
}

[[nodiscard]] bool mayBlockRetainReleasePair(const Instr &instr, const Value &retained) {
    // Known-neutral runtime helpers borrow every argument: they read handles
    // without touching any reference count and cannot re-enter user code, so
    // they neither need the pair's +1 nor can they disturb the value's other
    // owners — even when the retained value is one of their arguments.
    if (instr.op == Opcode::Call) {
        const CallEffects effects = classifyCallEffects(instr);
        if (effects.knownNeutral)
            return false;
    }

    if (instrUsesValue(instr, retained))
        return true;

    switch (instr.op) {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
        case Opcode::FCmpOrd:
        case Opcode::FCmpUno:
        case Opcode::Sitofp:
        case Opcode::Fptosi:
        case Opcode::CastSiToFp:
        case Opcode::CastUiToFp:
        case Opcode::Zext1:
        case Opcode::Trunc1:
        case Opcode::ConstF64:
        case Opcode::ConstNull:
        case Opcode::GAddr:
        case Opcode::Select:
            return false;
        case Opcode::Call: {
            const CallEffects effects = classifyCallEffects(instr);
            return !effects.canEliminateIfUnused() || effects.hasOwnershipEffects();
        }
        default:
            return true;
    }
}

[[nodiscard]] std::optional<std::size_t> findMatchingRelease(const BasicBlock &block,
                                                             std::size_t retainIndex) {
    const Instr &retain = block.instructions[retainIndex];
    if (retain.operands.empty())
        return std::nullopt;

    const Value retained = retain.operands.front();
    for (std::size_t index = retainIndex + 1; index < block.instructions.size(); ++index) {
        const Instr &candidate = block.instructions[index];
        const OwnershipCallKind kind = classifyOwnershipCall(candidate);
        if (kind == OwnershipCallKind::Release && sameFirstArgument(retain, candidate) &&
            !candidate.result) {
            return index;
        }
        if (mayBlockRetainReleasePair(candidate, retained))
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::string_view OwnershipOpt::id() const {
    return "ownership-opt";
}

PreservedAnalyses OwnershipOpt::run(Function &function, AnalysisManager & /*analysis*/) {
    bool changed = false;

    for (auto &block : function.blocks) {
        for (std::size_t index = 0; index < block.instructions.size();) {
            Instr &instr = block.instructions[index];
            if (classifyOwnershipCall(instr) != OwnershipCallKind::Retain || instr.result) {
                ++index;
                continue;
            }

            const auto releaseIndex = findMatchingRelease(block, index);
            if (!releaseIndex) {
                ++index;
                continue;
            }

            block.instructions.erase(block.instructions.begin() +
                                     static_cast<std::ptrdiff_t>(*releaseIndex));
            block.instructions.erase(block.instructions.begin() +
                                     static_cast<std::ptrdiff_t>(index));
            changed = true;
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    preserved.preserveCFG();
    preserved.preserveDominators();
    preserved.preserveLoopInfo();
    return preserved;
}

void registerOwnershipOptPass(PassRegistry &registry) {
    registry.registerFunctionPass(
        "ownership-opt", []() { return std::make_unique<OwnershipOpt>(); }, true);
}

} // namespace il::transform
