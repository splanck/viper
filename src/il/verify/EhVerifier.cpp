//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/EhVerifier.cpp
//
// Summary:
//   Dispatches exception-handling verification using the shared rule registry.
//   The verifier identifies functions containing EH constructs and applies the
//   EH-specific predicates, surfacing the first rule violation as a structured
//   diagnostic.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief EH verifier entry point backed by the shared rule registry.
/// @details The implementation scans functions for EH instructions and, when
///          found, evaluates the registry rules prefixed with "eh.".  Rule
///          failures are converted into uniform diagnostics via the
///          @ref diag_rule_msg helper so downstream tools receive consistent
///          messaging.

#include "il/verify/EhVerifier.hpp"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Diagnostics.hpp"
#include "il/verify/Rules.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace il::verify
{
namespace
{

std::optional<const il::core::Instr *> firstInstruction(const il::core::Function &fn)
{
    for (const auto &bb : fn.blocks)
    {
        if (!bb.instructions.empty())
            return &bb.instructions.front();
    }
    return std::nullopt;
}

bool decodeRulePayload(const std::string &encoded,
                       std::string &message,
                       int &blockIndex,
                       int &instrIndex)
{
    const auto firstSep = encoded.find(kRuleMessageSep);
    if (firstSep == std::string::npos)
        return false;
    const auto secondSep = encoded.find(kRuleMessageSep, firstSep + 1);
    if (secondSep == std::string::npos)
        return false;

    const std::string blockStr = encoded.substr(0, firstSep);
    const std::string instrStr = encoded.substr(firstSep + 1, secondSep - firstSep - 1);
    message = encoded.substr(secondSep + 1);

    try
    {
        blockIndex = std::stoi(blockStr);
        instrIndex = std::stoi(instrStr);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool hasEhInstructions(const il::core::Function &fn)
{
    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            switch (instr.op)
            {
                case il::core::Opcode::EhPush:
                case il::core::Opcode::EhPop:
                case il::core::Opcode::Trap:
                case il::core::Opcode::TrapFromErr:
                case il::core::Opcode::ResumeSame:
                case il::core::Opcode::ResumeNext:
                case il::core::Opcode::ResumeLabel:
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

VerifyDiagCode codeForRule(std::string_view name)
{
    if (name == "eh.stack.underflow")
        return VerifyDiagCode::EhStackUnderflow;
    if (name == "eh.stack.leak")
        return VerifyDiagCode::EhStackLeak;
    if (name == "eh.resume.token")
        return VerifyDiagCode::EhResumeTokenMissing;
    if (name == "eh.resume.label.dominates")
        return VerifyDiagCode::EhResumeLabelInvalidTarget;
    return VerifyDiagCode::Unknown;
}

const il::core::Instr *lookupInstr(const il::core::Function &fn, int blockIndex, int instrIndex)
{
    if (blockIndex < 0 || instrIndex < 0)
        return nullptr;
    if (static_cast<size_t>(blockIndex) >= fn.blocks.size())
        return nullptr;
    const auto &bb = fn.blocks[blockIndex];
    if (static_cast<size_t>(instrIndex) >= bb.instructions.size())
        return nullptr;
    return &bb.instructions[instrIndex];
}

} // namespace

il::support::Expected<void> EhVerifier::run(const il::core::Module &module, DiagSink &sink) const
{
    (void)sink;
    const auto &rules = viper_verifier_rules();

    for (const auto &fn : module.functions)
    {
        if (!hasEhInstructions(fn))
            continue;

        const auto firstInstr = firstInstruction(fn);
        if (!firstInstr)
            continue;

        for (const Rule &rule : rules)
        {
            if (!std::string_view(rule.name).starts_with("eh."))
                continue;

            std::string encoded;
            if (rule.check(fn, **firstInstr, encoded))
                continue;

            std::string message;
            int blockIndex = -1;
            int instrIndex = -1;
            if (!decodeRulePayload(encoded, message, blockIndex, instrIndex))
            {
                const std::string diag = diag_rule_msg(rule.name,
                                                       "internal decoding failure",
                                                       fn.name,
                                                       0,
                                                       0);
                return il::support::Expected<void>(
                    makeVerifierError(VerifyDiagCode::Unknown, (*firstInstr)->loc, diag));
            }

            const il::core::Instr *failedInstr = lookupInstr(fn, blockIndex, instrIndex);
            auto code = codeForRule(rule.name);
            const il::support::SourceLoc loc = failedInstr ? failedInstr->loc : (*firstInstr)->loc;
            const std::string diag = diag_rule_msg(rule.name, message, fn.name, blockIndex, instrIndex);
            return il::support::Expected<void>(makeVerifierError(code, loc, diag));
        }
    }

    return {};
}

} // namespace il::verify

