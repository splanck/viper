//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the default instruction verification strategies used by
// il::verify::FunctionVerifier.  Control-flow operations are handled by a
// dedicated strategy that validates terminators against the CFG, while a second
// strategy delegates to the opcode-agnostic instruction checker.  Grouping the
// logic here keeps verifier configuration in one place and documents the
// layering between branch verification, type inference, and the generic
// instruction rules derived from the IL specification.
//
//===----------------------------------------------------------------------===//

#include "il/verify/InstructionStrategies.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/verify/BranchVerifier.hpp"
#include "il/verify/FunctionVerifier.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/VerifyCtx.hpp"

#include <memory>
#include <unordered_map>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;

Expected<void> verifyInstruction_E(const VerifyCtx &ctx);

namespace
{

class ControlFlowStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    /// @brief Identify whether an instruction is a control-flow terminator.
    ///
    /// Matches the subset of opcodes that alter control flow and therefore
    /// require specialised verification to ensure branch targets and argument
    /// counts line up with the CFG.
    ///
    /// @param instr Instruction under consideration.
    /// @return `true` when @p instr is handled by the control-flow verifier.
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::SwitchI32 ||
               instr.op == Opcode::Ret;
    }

    /// @brief Verify a control-flow instruction against CFG expectations.
    ///
    /// Dispatches to opcode-specific helpers that enforce operand arity,
    /// argument typing, and branch target validity.  Extern and function tables
    /// are ignored because terminators do not reference them directly.
    ///
    /// @param fn Function currently being verified.
    /// @param bb Basic block containing the instruction.
    /// @param instr Instruction to validate.
    /// @param blockMap Mapping from block labels to block pointers.
    /// @param externs Table of extern declarations (unused).
    /// @param funcs Table of function declarations (unused).
    /// @param types Type inference engine seeded for the function.
    /// @param sink Diagnostic sink for reporting issues (unused when checks succeed).
    /// @return Success or a diagnostic when verification fails.
    Expected<void> verify(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          const std::unordered_map<std::string, const Extern *> &externs,
                          const std::unordered_map<std::string, const Function *> &funcs,
                          TypeInference &types,
                          DiagSink &sink) const override
    {
        (void)externs;
        (void)funcs;
        (void)sink;
        switch (instr.op)
        {
        case Opcode::Br:
            return verifyBr_E(fn, bb, instr, blockMap, types);
        case Opcode::CBr:
            return verifyCBr_E(fn, bb, instr, blockMap, types);
        case Opcode::SwitchI32:
            return verifySwitchI32_E(fn, bb, instr, blockMap, types);
        case Opcode::Ret:
            return verifyRet_E(fn, bb, instr, types);
        default:
            break;
        }
        return {};
    }
};

class DefaultInstructionStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    /// @brief Match any instruction not claimed by more specific strategies.
    ///
    /// Returning `true` ensures that generic opcode verification runs for every
    /// instruction once specialised handlers have had a chance to opt in.
    bool matches(const Instr &) const override { return true; }

    /// @brief Delegate verification to the opcode-agnostic checker.
    ///
    /// Builds a VerifyCtx describing the current instruction and forwards it to
    /// `verifyInstruction_E`, which enforces operand arity, type rules, and
    /// side-effect annotations defined in the IL specification.
    ///
    /// @param fn Function currently being verified.
    /// @param bb Basic block containing the instruction.
    /// @param instr Instruction to validate.
    /// @param blockMap Mapping from block labels to block pointers (unused).
    /// @param externs Table of extern declarations available to the module.
    /// @param funcs Map of function definitions available to the module.
    /// @param types Type inference engine seeded for the function.
    /// @param sink Diagnostic sink for reporting issues.
    /// @return Success or a diagnostic when verification fails.
    Expected<void> verify(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          const std::unordered_map<std::string, const Extern *> &externs,
                          const std::unordered_map<std::string, const Function *> &funcs,
                          TypeInference &types,
                          DiagSink &sink) const override
    {
        (void)blockMap;
        VerifyCtx ctx{sink, types, externs, funcs, fn, bb, instr};
        return verifyInstruction_E(ctx);
    }
};

} // namespace

/// @brief Build the default instruction strategy pipeline for FunctionVerifier.
///
/// The returned vector ensures that control-flow instructions are examined
/// before falling back to the general-purpose checker, mirroring the order used
/// by the verifier when iterating the list.
///
/// @return Strategies owned by the caller, ready to be installed on a verifier.
std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>> makeDefaultInstructionStrategies()
{
    std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>> strategies;
    strategies.push_back(std::make_unique<ControlFlowStrategy>());
    strategies.push_back(std::make_unique<DefaultInstructionStrategy>());
    return strategies;
}

} // namespace il::verify
