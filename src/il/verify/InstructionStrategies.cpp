//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the strategy objects used by the IL verifier to validate
// instructions.  Each strategy determines whether it should handle a given
// instruction and performs the appropriate verification, allowing the verifier
// pipeline to dispatch by behaviour rather than opcode tables scattered across
// the code base.
//
//===----------------------------------------------------------------------===//
//
// @file
// @brief Construction of default instruction verification strategies.
// @details Provides both a control-flow specific strategy and a catch-all
//          default that delegates to the general instruction checker.  The
//          strategies are returned as heap-allocated polymorphic objects so the
//          verifier can combine them in priority order.

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

/// @brief Verify a non-control-flow instruction using the default checker.
///
/// @param ctx Verification context describing the current instruction.
/// @return Success on validity; otherwise a diagnostic error.
Expected<void> verifyInstruction_E(const VerifyCtx &ctx);

namespace
{

/// @brief Strategy that handles control-flow instructions with dedicated checks.
class ControlFlowStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    /// @brief Identify whether the strategy should verify the given instruction.
    /// @param instr Instruction under inspection.
    /// @return @c true when @p instr is a control-flow opcode.
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::SwitchI32 ||
               instr.op == Opcode::Ret;
    }

    /// @brief Run control-flow specific verification logic.
    /// @details Dispatches to the appropriate helper based on the opcode while
    ///          ignoring maps that are irrelevant for the handled instructions.
    /// @param fn Function owning the instruction.
    /// @param bb Basic block containing the instruction.
    /// @param instr Instruction to verify.
    /// @param blockMap Mapping from labels to block definitions.
    /// @param externs Map of extern declarations (unused).
    /// @param funcs Map of function declarations (unused).
    /// @param types Type inference context for the current function.
    /// @param sink Diagnostic sink used for reporting (unused here).
    /// @return Success when the instruction satisfies control-flow invariants.
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

/// @brief Strategy that delegates generic instruction checking to the common verifier.
class DefaultInstructionStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    /// @brief Always claim responsibility for verification when no other strategy applies.
    /// @return Always returns @c true.
    bool matches(const Instr &) const override
    {
        return true;
    }

    /// @brief Verify an instruction using the default checker pipeline.
    /// @details Binds the instruction into a @c VerifyCtx and invokes the shared
    ///          instruction checker that handles type and operand validation.
    /// @param fn Function owning the instruction.
    /// @param bb Basic block containing the instruction.
    /// @param instr Instruction to verify.
    /// @param blockMap Mapping from labels to block definitions (unused).
    /// @param externs Map of extern declarations.
    /// @param funcs Map of function declarations.
    /// @param types Type inference context for the current function.
    /// @param sink Diagnostic sink used for reporting failures.
    /// @return Success when the instruction is valid; otherwise a diagnostic error.
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

/// @brief Construct the default set of instruction verification strategies.
///
/// @return Vector containing control-flow and generic verification strategies.
std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>>
makeDefaultInstructionStrategies()
{
    std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>> strategies;
    strategies.push_back(std::make_unique<ControlFlowStrategy>());
    strategies.push_back(std::make_unique<DefaultInstructionStrategy>());
    return strategies;
}

} // namespace il::verify
