// File: src/il/verify/InstructionStrategies.cpp
// Purpose: Provide the default instruction verification strategies for FunctionVerifier.
// Key invariants: Control-flow opcodes are handled separately from generic instruction checking.
// Ownership/Lifetime: Strategies are allocated per-verifier invocation and owned by the caller.
// Links: docs/il-guide.md#reference

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
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::SwitchI32 ||
               instr.op == Opcode::Ret;
    }

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
    bool matches(const Instr &) const override
    {
        return true;
    }

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

std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>>
makeDefaultInstructionStrategies()
{
    std::vector<std::unique_ptr<FunctionVerifier::InstructionStrategy>> strategies;
    strategies.push_back(std::make_unique<ControlFlowStrategy>());
    strategies.push_back(std::make_unique<DefaultInstructionStrategy>());
    return strategies;
}

} // namespace il::verify
