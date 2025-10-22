//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/InstructionStrategies.cpp
// Purpose: Assemble the default verification strategies used by the IL verifier
//          when walking instructions in a function.  The strategies separate
//          control-flow opcodes from the generic instruction pipeline so each
//          category can enforce its own invariants.
// Key invariants: Every instruction must be handled by exactly one strategy and
//                 the fallback strategy must accept any opcode not matched by a
//                 specialised handler.
// Ownership/Lifetime: Strategies are allocated with `std::unique_ptr` and
//                     returned to the caller, which assumes ownership of the
//                     resulting collection.  No additional global state is
//                     touched inside this file.
// Links: docs/il-guide.md#verifier
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides the default instruction verification strategies for the IL verifier.
/// @details Supplies specialised handlers for control-flow instructions alongside a
///          catch-all strategy that delegates to the general instruction checker.

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
/// @details Defined in @c InstructionChecker.cpp, this helper examines operand
///          types and side effects for generic opcodes.  Declaring it here keeps
///          the fallback strategy implementation self-contained while avoiding
///          header exposure.
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
    ///
    /// @details The matcher filters for branch, conditional branch, switch, and
    ///          return opcodes since those require bespoke control-flow analysis.
    ///
    /// @param instr Instruction under inspection.
    /// @return @c true when @p instr is a control-flow opcode.
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::SwitchI32 ||
               instr.op == Opcode::Ret;
    }

    /// @brief Run control-flow specific verification logic.
    ///
    /// @details Each recognised opcode forwards to the dedicated branch
    ///          verification helper that checks terminator structure, operand
    ///          types, and successor consistency.  The strategy ignores extern
    ///          and function maps because control-flow instructions never resolve
    ///          those tables.  When an opcode slips past the switch the function
    ///          returns success, allowing other strategies to claim ownership.
    ///
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
    ///
    /// @return Always returns @c true.
    bool matches(const Instr &) const override
    {
        return true;
    }

    /// @brief Verify an instruction using the default checker pipeline.
    ///
    /// @details The fallback strategy creates a @ref VerifyCtx that bundles all
    ///          verifier bookkeeping and forwards it to the generic checker.
    ///          Control-flow instructions are never dispatched here because the
    ///          control-flow strategy claims them first.
    ///
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
/// @details The returned collection orders the strategies so control-flow checks
///          run before the generic verifier.  Callers take ownership of the
///          vector and install it on a @ref FunctionVerifier instance.
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
