//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the verifier responsible for checking exception-handling (EH)
// stack balance and handler coverage.  The pass walks the control-flow graph,
// ensuring pushes and pops match and that potentially faulting instructions are
// protected by appropriate handlers.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Validates EH stack discipline and handler coverage for IL functions.
/// @details The verifier explores each function's control flow using a worklist,
///          tracks active handlers, and reports diagnostics via @c Expected
///          results when it discovers mismatched pushes/pops or missing resume
///          tokens.

#include "il/verify/EhVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/Diagnostics.hpp"
#include "il/verify/Rules.hpp"

#include <string_view>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;

namespace
{

bool hasEhOpcode(const Function &fn)
{
    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            switch (instr.op)
            {
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::Trap:
                case Opcode::TrapFromErr:
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

bool isEhRule(const Rule &rule)
{
    return std::string_view(rule.name).rfind("eh.", 0) == 0;
}

VerifyDiagCode ruleDiagCode(std::string_view name)
{
    if (name == "eh.stack-underflow")
        return VerifyDiagCode::EhStackUnderflow;
    if (name == "eh.stack-leak")
        return VerifyDiagCode::EhStackLeak;
    if (name == "eh.resume-token")
        return VerifyDiagCode::EhResumeTokenMissing;
    if (name == "eh.resume-label-target")
        return VerifyDiagCode::EhResumeLabelInvalidTarget;
    return VerifyDiagCode::Unknown;
}

} // namespace

Expected<void> EhVerifier::run(const Module &module, DiagSink &sink) const
{
    (void)sink;
    const auto &rules = viper_verifier_rules();

    for (const auto &fn : module.functions)
    {
        if (!hasEhOpcode(fn))
            continue;

        for (size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex)
        {
            const auto &bb = fn.blocks[blockIndex];
            for (size_t instrIndex = 0; instrIndex < bb.instructions.size(); ++instrIndex)
            {
                const auto &instr = bb.instructions[instrIndex];

                for (const Rule &rule : rules)
                {
                    if (!isEhRule(rule))
                        continue;

                    std::string message;
                    if (rule.check(fn, instr, message))
                        continue;

                    const std::string diagMessage = diag_rule_msg(rule.name,
                                                                    message,
                                                                    fn.name,
                                                                    static_cast<int>(blockIndex),
                                                                    static_cast<int>(instrIndex));
                    const VerifyDiagCode code = ruleDiagCode(rule.name);
                    return Expected<void>{makeVerifierError(code, instr.loc, diagMessage)};
                }
            }
        }
    }

    return {};
}

} // namespace il::verify
