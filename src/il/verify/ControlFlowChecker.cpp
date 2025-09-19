// File: src/il/verify/ControlFlowChecker.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements control-flow specific IL verification helpers.
// Key invariants: Ensures terminators and branch arguments satisfy structural rules.
// Ownership/Lifetime: Operates with caller-provided verifier state.
// Links: docs/il-spec.md

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
/// @brief Format a diagnostic message scoped to a block within a function.
/// @param fn Function containing @p bb.
/// @param bb Basic block referenced by the diagnostic.
/// @param message Optional suffix appended when non-empty.
/// @return String of the form "fn:label" plus @p message when provided.
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

/// @brief Format an instruction-scoped diagnostic with snippet context.
/// @param fn Function containing @p bb and @p instr.
/// @param bb Basic block owning @p instr.
/// @param instr Instruction to render via makeSnippet().
/// @param message Optional suffix appended when non-empty.
/// @return String of the form "fn:label: <instr>" plus @p message when provided.
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

/// @brief Validate block parameters and seed type inference for SSA temps.
/// @param fn Function containing @p bb.
/// @param bb Block whose parameters are checked for duplicates and void types.
/// @param types Type inference database updated with each parameter type.
/// @param paramIds Output list extended with parameter SSA identifiers.
/// @return Empty on success; otherwise diagnostic describing the violated rule.
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

/// @brief Iterate block instructions to ensure operands are typed and invoke verification.
/// @param fn Function providing instruction context.
/// @param bb Block whose instructions are visited sequentially.
/// @param blockMap Lookup table resolving branch labels to blocks.
/// @param externs Map of extern names used by verifier callbacks.
/// @param funcs Map of function names used by verifier callbacks.
/// @param types Type inference state validated against each operand.
/// @param verifyInstrFn Callback performing instruction-specific checks and producing diagnostics.
/// @param warnings Sink that accumulates warning diagnostics emitted by @p verifyInstrFn.
/// @return Empty on success; otherwise the first diagnostic returned by helper routines or callback.
/// @note Stops iteration after encountering a terminator to mirror runtime control flow.
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

/// @brief Ensure a block ends with exactly one terminator and no trailing instructions.
/// @param fn Function containing the block under inspection.
/// @param bb Block whose terminator ordering is validated.
/// @return Empty on success; otherwise diagnostic for empty blocks, duplicates, or stray ops.
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

/// @brief Validate branch argument counts and types against a target block signature.
/// @param fn Function containing the branch and successor block.
/// @param bb Origin block holding @p instr.
/// @param instr Branch instruction generating the transfer.
/// @param target Destination block whose parameters supply expected types.
/// @param args Optional pointer to argument vector supplied for the edge.
/// @param label Label string used for diagnostics when mismatches occur.
/// @param types Type inference context queried for operand types.
/// @return Empty on success; otherwise diagnostic describing the mismatch.
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

/// @brief Check structural and argument constraints for unconditional branches.
/// @param fn Function containing the branch instruction.
/// @param bb Block that owns @p instr.
/// @param instr Branch instruction expected to have no operands and exactly one label.
/// @param blockMap Label-to-block lookup used to resolve the successor.
/// @param types Type inference state used to validate branch arguments.
/// @return Empty on success; otherwise diagnostic describing the malformed branch.
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

/// @brief Validate conditional branch operands and successor argument contracts.
/// @param fn Function containing the conditional branch.
/// @param bb Block that owns @p instr.
/// @param instr Conditional branch expected to reference two labels and one i1 operand.
/// @param blockMap Lookup for resolving successor blocks.
/// @param types Type inference state used to confirm operand and argument types.
/// @return Empty on success; otherwise diagnostic describing the failing condition.
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

/// @brief Ensure return instructions respect the function's declared return type.
/// @param fn Function whose signature defines the allowed operand shape.
/// @param bb Block containing the return instruction.
/// @param instr Return instruction to validate.
/// @param types Type inference context used to check operand types when needed.
/// @return Empty on success; otherwise diagnostic for missing/extra/mismatched values.
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

/// @brief Aggregates warnings and errors parsed from captured verifier output.
struct ParsedCapture
{
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// @brief Split captured text into warning and error buckets based on prefixes.
/// @param text Multi-line string captured from a verifier callback stream.
/// @return Struct containing categorized warning and error messages.
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

/// @brief Concatenate diagnostic message lines separated by newlines.
/// @param messages Ordered collection of message fragments to join.
/// @return Single string containing each message separated by '\n'.
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

/// @brief Check whether an opcode is considered a terminator in control-flow validation.
/// @param op Opcode under inspection.
/// @return True if @p op ends a block (br, cbr, ret, trap).
bool isTerminator(Opcode op)
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret || op == Opcode::Trap;
}

/// @brief Validate block parameters while emitting diagnostics to an output stream.
/// @param fn Function containing the block under validation.
/// @param bb Block whose parameters are checked for duplicates and void types.
/// @param types Type inference state updated on success.
/// @param paramIds Output vector appended with parameter identifiers.
/// @param err Stream receiving any diagnostics from validation failures.
/// @return True on success; false if a diagnostic was emitted.
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

/// @brief Iterate instructions using the bool-based verifier interface while relaying diagnostics.
/// @param verifyInstrFn Callback that writes diagnostics to a stream and returns success flag.
/// @param fn Function context for the block under examination.
/// @param bb Block whose instructions are traversed.
/// @param blockMap Mapping from labels to successor blocks.
/// @param externs Available extern declarations for instruction checks.
/// @param funcs Available function declarations for instruction checks.
/// @param types Type inference state updated and queried per instruction.
/// @param err Stream receiving warnings and errors surfaced during iteration.
/// @return True on success; false if the callback or operand validation reported an error.
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

/// @brief Validate block terminators using stream-based diagnostics.
/// @param fn Function containing @p bb.
/// @param bb Block whose terminator ordering is verified.
/// @param err Stream receiving printed diagnostics on failure.
/// @return True when the block has exactly one trailing terminator; false otherwise.
bool checkBlockTerminators(const Function &fn, const BasicBlock &bb, std::ostream &err)
{
    if (auto result = checkBlockTerminators_E(fn, bb); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

/// @brief Validate an unconditional branch and stream diagnostics on failure.
/// @param fn Function providing the context for @p instr.
/// @param bb Block containing the branch instruction.
/// @param instr Branch instruction subject to validation.
/// @param blockMap Mapping from labels to blocks for resolving the successor.
/// @param types Type inference state consulted for branch arguments.
/// @param err Stream receiving diagnostics when validation fails.
/// @return True if the branch is well-formed; false otherwise.
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

/// @brief Validate a conditional branch using the stream-based diagnostics interface.
/// @param fn Function providing the branch context.
/// @param bb Block containing @p instr.
/// @param instr Conditional branch instruction under validation.
/// @param blockMap Lookup for resolving successor blocks.
/// @param types Type inference state consulted for operand and argument types.
/// @param err Stream receiving diagnostics when validation fails.
/// @return True if the conditional branch passes all structural and type checks; false otherwise.
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

/// @brief Validate a return instruction and stream diagnostics on failure.
/// @param fn Function whose signature constrains @p instr.
/// @param bb Block containing the return instruction.
/// @param instr Return instruction to validate.
/// @param types Type inference state used to check operand types.
/// @param err Stream receiving diagnostics if validation fails.
/// @return True when the return instruction matches the function signature; false otherwise.
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

/// @brief Expected-returning wrapper around validateBlockParams_E for tests.
/// @param fn Function containing the block under validation.
/// @param bb Block whose parameters are inspected.
/// @param types Type inference state updated when validation succeeds.
/// @param paramIds Output vector receiving parameter identifiers.
/// @return Direct pass-through of validateBlockParams_E.
Expected<void> validateBlockParams_expected(const Function &fn,
                                             const BasicBlock &bb,
                                             TypeInference &types,
                                             std::vector<unsigned> &paramIds)
{
    return validateBlockParams_E(fn, bb, types, paramIds);
}

/// @brief Testing hook exposing iterateBlockInstructions_E without stream indirection.
/// @param fn Function providing the instruction context.
/// @param bb Block whose instructions are visited.
/// @param blockMap Mapping from labels to basic blocks.
/// @param externs Available extern descriptors.
/// @param funcs Available function descriptors.
/// @param types Type inference state mutated during iteration.
/// @param verifyInstrFn Callback returning Expected diagnostics.
/// @param warnings Warning sink populated during verification.
/// @return Result forwarded from iterateBlockInstructions_E.
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

/// @brief Testing shim exposing checkBlockTerminators_E directly.
/// @param fn Function containing the block under inspection.
/// @param bb Block whose terminators are checked.
/// @return Result forwarded from checkBlockTerminators_E.
Expected<void> checkBlockTerminators_expected(const Function &fn, const BasicBlock &bb)
{
    return checkBlockTerminators_E(fn, bb);
}

/// @brief Expected-returning variant of verifyBr_E for direct consumption.
/// @param fn Function providing branch context.
/// @param bb Block containing the branch instruction.
/// @param instr Branch instruction under validation.
/// @param blockMap Mapping from labels to blocks for successor resolution.
/// @param types Type inference state consulted for argument checking.
/// @return Result forwarded from verifyBr_E.
Expected<void> verifyBr_expected(const Function &fn,
                                 const BasicBlock &bb,
                                 const Instr &instr,
                                 const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                 TypeInference &types)
{
    return verifyBr_E(fn, bb, instr, blockMap, types);
}

/// @brief Expected-returning variant of verifyCBr_E for direct consumption.
/// @param fn Function providing branch context.
/// @param bb Block containing the conditional branch instruction.
/// @param instr Conditional branch instruction under validation.
/// @param blockMap Mapping from labels to blocks for successor resolution.
/// @param types Type inference state consulted for operand and argument checking.
/// @return Result forwarded from verifyCBr_E.
Expected<void> verifyCBr_expected(const Function &fn,
                                  const BasicBlock &bb,
                                  const Instr &instr,
                                  const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                  TypeInference &types)
{
    return verifyCBr_E(fn, bb, instr, blockMap, types);
}

/// @brief Expected-returning variant of verifyRet_E for direct consumption.
/// @param fn Function providing the signature constraints.
/// @param bb Block containing the return instruction.
/// @param instr Return instruction under validation.
/// @param types Type inference state consulted for operand checking.
/// @return Result forwarded from verifyRet_E.
Expected<void> verifyRet_expected(const Function &fn,
                                  const BasicBlock &bb,
                                  const Instr &instr,
                                  TypeInference &types)
{
    return verifyRet_E(fn, bb, instr, types);
}

} // namespace il::verify
