// File: src/il/verify/ControlFlowChecker.cpp
// Purpose: Implements control-flow specific IL verification helpers.
// Key invariants: Ensures terminators and branch arguments satisfy structural rules.
// Ownership/Lifetime: Operates with caller-provided verifier state.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/verify/ControlFlowChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include "il/verify/BranchVerifier.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/DiagSink.hpp"
#include "support/diag_expected.hpp"

#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

using namespace il::core;

namespace il::verify
{
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;
using il::support::Severity;

using VerifyInstrFnExpected = std::function<Expected<void>(
    const Function &fn,
    const BasicBlock &bb,
    const Instr &instr,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    const std::unordered_map<std::string, const Extern *> &externs,
    const std::unordered_map<std::string, const Function *> &funcs,
    TypeInference &types,
    DiagSink &sink)>;

namespace
{

/// @brief Validates block parameter declarations against IL structural rules.
/// @param fn Function owning @p bb; used for diagnostics.
/// @param bb Basic block whose parameter list is being validated.
/// @param types Type inference context seeded with parameter types.
/// @param paramIds Output container populated with the registered parameter IDs.
/// @return Empty on success; otherwise diagnostics describing duplicate names or
///         void parameters.
/// @details Ensures block parameters are unique and non-void so predecessors can
///          match arguments, per docs/il-guide.md#reference section "Basic Blocks".
Expected<void> validateBlockParams_impl(const Function &fn,
                                        const BasicBlock &bb,
                                        TypeInference &types,
                                        std::vector<unsigned> &paramIds)
{
    std::unordered_set<std::string> paramNames;
    for (const auto &param : bb.params)
    {
        if (!paramNames.insert(param.name).second)
            return Expected<void>{
                makeError({}, formatBlockDiag(fn, bb, "duplicate param %" + param.name))};

        if (param.type.kind == Type::Kind::Void)
            return Expected<void>{
                makeError({}, formatBlockDiag(fn, bb, "param %" + param.name + " has void type"))};

        types.addTemp(param.id, param.type);
        paramIds.push_back(param.id);
    }
    return {};
}

/// @brief Walks instructions within a block and invokes verifier callbacks.
/// @param fn Function containing @p bb.
/// @param bb Block whose instructions are being analyzed.
/// @param blockMap Map of reachable block labels for branch validation.
/// @param externs Known extern declarations supplied to instruction checks.
/// @param funcs Known function declarations supplied to instruction checks.
/// @param types Type inference context used to validate operand availability.
/// @param verifyInstrFn Callback providing instruction-specific verification.
/// @param warnings Accumulates non-fatal diagnostics emitted by the callback.
/// @return Propagates the first verification error produced by operand checking
///         or the callback; otherwise empty.
/// @details Stops after the first terminator to honour the single-terminator
///          rule outlined in docs/il-guide.md#reference ("Explicit control flow").
Expected<void> iterateBlockInstructions_impl(
    const Function &fn,
    const BasicBlock &bb,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    const std::unordered_map<std::string, const Extern *> &externs,
    const std::unordered_map<std::string, const Function *> &funcs,
    TypeInference &types,
    const VerifyInstrFnExpected &verifyInstrFn,
    DiagSink &sink)
{
    for (const auto &instr : bb.instructions)
    {
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

        if (auto result = verifyInstrFn(fn, bb, instr, blockMap, externs, funcs, types, sink);
            !result)
            return result;

        if (isTerminator(instr.op))
            break;
    }
    return {};
}

/// @brief Ensures each block terminates exactly once as required by the IL spec.
/// @param fn Function owning @p bb.
/// @param bb Basic block whose terminator structure is being checked.
/// @return Diagnostic if the block is empty, has multiple terminators, contains
///         instructions after a terminator, or is missing a terminator.
/// @details Implements the "explicit control flow" requirement described in
///          docs/il-guide.md#reference: every block ends with exactly one terminator.
Expected<void> checkBlockTerminators_impl(const Function &fn, const BasicBlock &bb)
{
    if (bb.instructions.empty())
        return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "empty block"))};

    bool seenTerm = false;
    for (const auto &instr : bb.instructions)
    {
        if (isTerminator(instr.op))
        {
            if (seenTerm)
                return Expected<void>{
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, "multiple terminators"))};
            seenTerm = true;
            continue;
        }
        if (seenTerm)
            return Expected<void>{makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "instruction after terminator"))};
    }

    if (!isTerminator(bb.instructions.back().op))
        return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "missing terminator"))};

    return {};
}

struct ParsedCapture
{
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

ParsedCapture parseCapturedLines(const std::string &text)
{
    ParsedCapture parsed;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (line.rfind("warning: ", 0) == 0)
            parsed.warnings.push_back(line.substr(9));
        else if (line.rfind("error: ", 0) == 0)
            parsed.errors.push_back(line.substr(7));
        else
            parsed.errors.push_back(line);
    }
    return parsed;
}

std::string joinMessages(const std::vector<std::string> &messages)
{
    std::ostringstream oss;
    for (size_t i = 0; i < messages.size(); ++i)
    {
        if (i != 0)
            oss << '\n';
        oss << messages[i];
    }
    return oss.str();
}

} // namespace

Expected<void> validateBlockParams_E(const Function &fn,
                                     const BasicBlock &bb,
                                     TypeInference &types,
                                     std::vector<unsigned> &paramIds)
{
    return validateBlockParams_impl(fn, bb, types, paramIds);
}

Expected<void> iterateBlockInstructions_E(
    const Function &fn,
    const BasicBlock &bb,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    const std::unordered_map<std::string, const Extern *> &externs,
    const std::unordered_map<std::string, const Function *> &funcs,
    TypeInference &types,
    const VerifyInstrFnExpected &verifyInstrFn,
    DiagSink &sink)
{
    return iterateBlockInstructions_impl(
        fn, bb, blockMap, externs, funcs, types, verifyInstrFn, sink);
}

Expected<void> checkBlockTerminators_E(const Function &fn, const BasicBlock &bb)
{
    return checkBlockTerminators_impl(fn, bb);
}

bool isTerminator(Opcode op)
{
    switch (op)
    {
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::Ret:
        case Opcode::Trap:
        case Opcode::TrapKind:
        case Opcode::TrapFromErr:
        case Opcode::TrapErr:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

bool validateBlockParams(const Function &fn,
                         const BasicBlock &bb,
                         TypeInference &types,
                         std::vector<unsigned> &paramIds,
                         std::ostream &err)
{
    if (auto result = validateBlockParams_E(fn, bb, types, paramIds); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool iterateBlockInstructions(VerifyInstrFn verifyInstrFn,
                              const Function &fn,
                              const BasicBlock &bb,
                              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              TypeInference &types,
                              std::ostream &err)
{
    CollectingDiagSink warnings;
    VerifyInstrFnExpected shim =
        [&](const Function &fnRef,
            const BasicBlock &bbRef,
            const Instr &instrRef,
            const std::unordered_map<std::string, const BasicBlock *> &blockMapRef,
            const std::unordered_map<std::string, const Extern *> &externsRef,
            const std::unordered_map<std::string, const Function *> &funcsRef,
            TypeInference &typesRef,
            DiagSink &warningSink) -> Expected<void>
    {
        std::ostringstream capture;
        bool ok = verifyInstrFn(
            fnRef, bbRef, instrRef, blockMapRef, externsRef, funcsRef, typesRef, capture);
        ParsedCapture parsed = parseCapturedLines(capture.str());
        for (const auto &msg : parsed.warnings)
            warningSink.report(Diag{Severity::Warning, msg, instrRef.loc});
        if (!ok)
        {
            std::string message = joinMessages(parsed.errors);
            if (message.empty())
                message = formatInstrDiag(fnRef, bbRef, instrRef, "verification failed");
            return Expected<void>{makeError(instrRef.loc, message)};
        }
        return {};
    };

    if (auto result =
            iterateBlockInstructions_E(fn, bb, blockMap, externs, funcs, types, shim, warnings);
        !result)
    {
        for (const auto &warning : warnings.diagnostics())
            il::support::printDiag(warning, err);
        il::support::printDiag(result.error(), err);
        return false;
    }

    for (const auto &warning : warnings.diagnostics())
        il::support::printDiag(warning, err);
    return true;
}

bool checkBlockTerminators(const Function &fn, const BasicBlock &bb, std::ostream &err)
{
    if (auto result = checkBlockTerminators_E(fn, bb); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyBr(const Function &fn,
              const BasicBlock &bb,
              const Instr &instr,
              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
              TypeInference &types,
              std::ostream &err)
{
    if (auto result = verifyBr_E(fn, bb, instr, blockMap, types); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyCBr(const Function &fn,
               const BasicBlock &bb,
               const Instr &instr,
               const std::unordered_map<std::string, const BasicBlock *> &blockMap,
               TypeInference &types,
               std::ostream &err)
{
    if (auto result = verifyCBr_E(fn, bb, instr, blockMap, types); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyRet(const Function &fn,
               const BasicBlock &bb,
               const Instr &instr,
               TypeInference &types,
               std::ostream &err)
{
    if (auto result = verifyRet_E(fn, bb, instr, types); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

} // namespace il::verify
