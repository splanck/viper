// File: src/il/verify/ControlFlowChecker.cpp
// Purpose: Implements control-flow specific IL verification helpers.
// Key invariants: Ensures terminators and branch arguments satisfy structural rules.
// Ownership/Lifetime: Operates with caller-provided verifier state.
// License: MIT (see LICENSE).
// Links: docs/il-reference.md

#include "il/verify/ControlFlowChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
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
using il::support::Severity;
using il::support::makeError;

using VerifyInstrFnExpected = std::function<Expected<void>(const Function &fn,
                                                           const BasicBlock &bb,
                                                           const Instr &instr,
                                                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                                           const std::unordered_map<std::string, const Extern *> &externs,
                                                           const std::unordered_map<std::string, const Function *> &funcs,
                                                           TypeInference &types,
                                                           std::vector<Diag> &warnings)>;

namespace
{
std::string formatBlockDiag(const Function &fn,
                            const BasicBlock &bb,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

std::string formatInstrDiag(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label << ": " << makeSnippet(instr);
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

/// @brief Validates block parameter declarations against IL structural rules.
/// @param fn Function owning @p bb; used for diagnostics.
/// @param bb Basic block whose parameter list is being validated.
/// @param types Type inference context seeded with parameter types.
/// @param paramIds Output container populated with the registered parameter IDs.
/// @return Empty on success; otherwise diagnostics describing duplicate names or
///         void parameters.
/// @details Ensures block parameters are unique and non-void so predecessors can
///          match arguments, per docs/il-reference.md section "Basic Blocks".
Expected<void> validateBlockParams_E(const Function &fn,
                                      const BasicBlock &bb,
                                      TypeInference &types,
                                      std::vector<unsigned> &paramIds)
{
    std::unordered_set<std::string> paramNames;
    for (const auto &param : bb.params)
    {
        if (!paramNames.insert(param.name).second)
            return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "duplicate param %" + param.name))};

        if (param.type.kind == Type::Kind::Void)
            return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "param %" + param.name + " has void type"))};

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
///          rule outlined in docs/il-reference.md ("Explicit control flow").
Expected<void> iterateBlockInstructions_E(const Function &fn,
                                           const BasicBlock &bb,
                                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                           const std::unordered_map<std::string, const Extern *> &externs,
                                           const std::unordered_map<std::string, const Function *> &funcs,
                                           TypeInference &types,
                                           const VerifyInstrFnExpected &verifyInstrFn,
                                           std::vector<Diag> &warnings)
{
    for (const auto &instr : bb.instructions)
    {
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

        if (auto result = verifyInstrFn(fn, bb, instr, blockMap, externs, funcs, types, warnings); !result)
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
///          docs/il-reference.md: every block ends with exactly one terminator.
Expected<void> checkBlockTerminators_E(const Function &fn, const BasicBlock &bb)
{
    if (bb.instructions.empty())
        return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "empty block"))};

    bool seenTerm = false;
    for (const auto &instr : bb.instructions)
    {
        if (isTerminator(instr.op))
        {
            if (seenTerm)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "multiple terminators"))};
            seenTerm = true;
            continue;
        }
        if (seenTerm)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "instruction after terminator"))};
    }

    if (!isTerminator(bb.instructions.back().op))
        return Expected<void>{makeError({}, formatBlockDiag(fn, bb, "missing terminator"))};

    return {};
}

/// @brief Validates branch argument arity and types against the target block.
/// @param fn Function providing context for diagnostics.
/// @param bb Source block containing the branch @p instr.
/// @param instr Branch instruction under validation.
/// @param target Destination block referenced by the branch.
/// @param args Optional list of provided branch arguments.
/// @param label Label string used for diagnostics.
/// @param types Type inference context queried for argument types.
/// @return Diagnostic if argument counts or kinds differ from target parameters.
/// @details Enforces docs/il-reference.md section "Basic Blocks": predecessors
///          must pass arguments matching the parameters declared by the
///          destination block.
Expected<void> verifyBranchArgs(const Function &fn,
                                const BasicBlock &bb,
                                const Instr &instr,
                                const BasicBlock &target,
                                const std::vector<Value> *args,
                                std::string_view label,
                                TypeInference &types)
{
    size_t argCount = args ? args->size() : 0;
    if (argCount != target.params.size())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "branch arg count mismatch for label " + std::string(label)))};

    for (size_t i = 0; i < argCount; ++i)
    {
        if (types.valueType((*args)[i]).kind != target.params[i].type.kind)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "arg type mismatch for label " + std::string(label)))};
    }
    return {};
}

/// @brief Verifies the structural requirements of an unconditional branch.
/// @param fn Function containing the branch.
/// @param bb Block housing the branch terminator.
/// @param instr The `br` instruction being checked.
/// @param blockMap Map of known block labels for validating jump targets.
/// @param types Type inference context used for argument type queries.
/// @return Diagnostic if the terminator has operands, the wrong number of
///         labels, or mismatched branch arguments.
/// @details Checks the IL Control Flow rule that `br` only names one target and
///          forwards arguments compatible with that block (docs/il-reference.md,
///          section "Control Flow", `br`).
Expected<void> verifyBr_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           TypeInference &types)
{
    bool argsOk = instr.operands.empty() && instr.labels.size() == 1;
    if (!argsOk)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "branch mismatch"))};

    if (auto it = blockMap.find(instr.labels[0]); it != blockMap.end())
    {
        const BasicBlock &target = *it->second;
        const std::vector<Value> *argsVec = !instr.brArgs.empty() ? &instr.brArgs[0] : nullptr;
        if (auto result = verifyBranchArgs(fn, bb, instr, target, argsVec, instr.labels[0], types); !result)
            return result;
    }

    return {};
}

/// @brief Verifies the structural requirements of a conditional branch.
/// @param fn Function containing the branch.
/// @param bb Block housing the branch terminator.
/// @param instr The `cbr` instruction being checked.
/// @param blockMap Map of known block labels for validating jump targets.
/// @param types Type inference context queried for operand and argument types.
/// @return Diagnostic if the condition is ill-typed, labels are missing, or
///         branch arguments fail to match their destination parameters.
/// @details Enforces docs/il-reference.md section "Control Flow" (`cbr`):
///          conditions must be `i1` and each successor must receive matching
///          argument payloads.
Expected<void> verifyCBr_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                            TypeInference &types)
{
    bool condOk = instr.operands.size() == 1 && instr.labels.size() == 2 &&
                   types.valueType(instr.operands[0]).kind == Type::Kind::I1;
    if (!condOk)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "conditional branch mismatch"))};

    for (size_t t = 0; t < 2; ++t)
    {
        auto it = blockMap.find(instr.labels[t]);
        if (it == blockMap.end())
            continue;
        const BasicBlock &target = *it->second;
        const std::vector<Value> *argsVec = instr.brArgs.size() > t ? &instr.brArgs[t] : nullptr;
        if (auto result = verifyBranchArgs(fn, bb, instr, target, argsVec, instr.labels[t], types); !result)
            return result;
    }

    return {};
}

/// @brief Validates return terminators against the enclosing function signature.
/// @param fn Function whose return type defines expectations for @p instr.
/// @param bb Block containing the return instruction.
/// @param instr `ret` instruction under validation.
/// @param types Type inference context used to query operand kinds.
/// @return Diagnostic if a `void` function returns a value or a non-`void`
///         function omits or mismatches the return operand.
/// @details Implements docs/il-reference.md section "Control Flow" (`ret`),
///          ensuring the terminator conforms to the function's declared return
///          type.
Expected<void> verifyRet_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            TypeInference &types)
{
    if (fn.retType.kind == Type::Kind::Void)
    {
        if (!instr.operands.empty())
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "ret void with value"))};
    }
    else
    {
        if (instr.operands.size() != 1 || types.valueType(instr.operands[0]).kind != fn.retType.kind)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "ret value type mismatch"))};
    }
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

bool isTerminator(Opcode op)
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret || op == Opcode::Trap;
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
    std::vector<Diag> warnings;
    VerifyInstrFnExpected shim = [&](const Function &fnRef, const BasicBlock &bbRef, const Instr &instrRef,
                                     const std::unordered_map<std::string, const BasicBlock *> &blockMapRef,
                                     const std::unordered_map<std::string, const Extern *> &externsRef,
                                     const std::unordered_map<std::string, const Function *> &funcsRef,
                                     TypeInference &typesRef,
                                     std::vector<Diag> &warningSink) -> Expected<void>
    {
        std::ostringstream capture;
        bool ok = verifyInstrFn(fnRef, bbRef, instrRef, blockMapRef, externsRef, funcsRef, typesRef, capture);
        ParsedCapture parsed = parseCapturedLines(capture.str());
        for (const auto &msg : parsed.warnings)
            warningSink.push_back(Diag{Severity::Warning, msg, instrRef.loc});
        if (!ok)
        {
            std::string message = joinMessages(parsed.errors);
            if (message.empty())
                message = formatInstrDiag(fnRef, bbRef, instrRef, "verification failed");
            return Expected<void>{makeError(instrRef.loc, message)};
        }
        return {};
    };

    if (auto result = iterateBlockInstructions_E(fn, bb, blockMap, externs, funcs, types, shim, warnings); !result)
    {
        for (const auto &warning : warnings)
            il::support::printDiag(warning, err);
        il::support::printDiag(result.error(), err);
        return false;
    }

    for (const auto &warning : warnings)
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

Expected<void> validateBlockParams_expected(const Function &fn,
                                             const BasicBlock &bb,
                                             TypeInference &types,
                                             std::vector<unsigned> &paramIds)
{
    return validateBlockParams_E(fn, bb, types, paramIds);
}

Expected<void> iterateBlockInstructions_expected(const Function &fn,
                                                  const BasicBlock &bb,
                                                  const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                                  const std::unordered_map<std::string, const Extern *> &externs,
                                                  const std::unordered_map<std::string, const Function *> &funcs,
                                                  TypeInference &types,
                                                  const VerifyInstrFnExpected &verifyInstrFn,
                                                  std::vector<Diag> &warnings)
{
    return iterateBlockInstructions_E(fn, bb, blockMap, externs, funcs, types, verifyInstrFn, warnings);
}

Expected<void> checkBlockTerminators_expected(const Function &fn, const BasicBlock &bb)
{
    return checkBlockTerminators_E(fn, bb);
}

Expected<void> verifyBr_expected(const Function &fn,
                                 const BasicBlock &bb,
                                 const Instr &instr,
                                 const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                 TypeInference &types)
{
    return verifyBr_E(fn, bb, instr, blockMap, types);
}

Expected<void> verifyCBr_expected(const Function &fn,
                                  const BasicBlock &bb,
                                  const Instr &instr,
                                  const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                  TypeInference &types)
{
    return verifyCBr_E(fn, bb, instr, blockMap, types);
}

Expected<void> verifyRet_expected(const Function &fn,
                                  const BasicBlock &bb,
                                  const Instr &instr,
                                  TypeInference &types)
{
    return verifyRet_E(fn, bb, instr, types);
}

} // namespace il::verify
