// File: src/il/verify/Verifier.cpp
// Purpose: Implements IL verifier checking module correctness.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// License: MIT (see LICENSE).
// Links: docs/il-spec.md

#include "il/verify/Verifier.hpp"

#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include "support/diag_expected.hpp"

using namespace il::core;

namespace il::verify
{

using VerifyInstrFnExpected = std::function<il::support::Expected<void>(
    const Function &,
    const BasicBlock &,
    const Instr &,
    const std::unordered_map<std::string, const BasicBlock *> &,
    const std::unordered_map<std::string, const Extern *> &,
    const std::unordered_map<std::string, const Function *> &,
    TypeInference &,
    std::vector<il::support::Diag> &)>;

/// @brief Ensures an instruction's opcode signature matches the declared
///        parameter and result types.
///
/// When signatures differ, the returned diagnostic describes the mismatch and
/// includes the owning function and block context for clarity.
il::support::Expected<void> verifyOpcodeSignature_expected(const Function &fn,
                                                            const BasicBlock &bb,
                                                            const Instr &instr);
/// @brief Performs full instruction verification for non-control-flow opcodes.
///
/// The helper consults extern and function signature tables as well as the
/// in-flight type inference state. Structural issues are reported via the
/// returned diagnostic while recoverable problems are appended to @p warnings.
il::support::Expected<void> verifyInstruction_expected(
    const Function &fn,
    const BasicBlock &bb,
    const Instr &instr,
    const std::unordered_map<std::string, const Extern *> &externs,
    const std::unordered_map<std::string, const Function *> &funcs,
    TypeInference &types,
    std::vector<il::support::Diag> &warnings);
/// @brief Validates that block parameters align with predecessor arguments.
///
/// Populates @p paramIds with temporary identifiers introduced for block
/// parameters so they can later be cleared from the type state. Returns a
/// diagnostic describing the first mismatch encountered.
il::support::Expected<void> validateBlockParams_expected(const Function &fn,
                                                         const BasicBlock &bb,
                                                         TypeInference &types,
                                                         std::vector<unsigned> &paramIds);
/// @brief Walks a block's instructions invoking @p verifyInstrFn for each.
///
/// The helper feeds every instruction to the provided verifier while
/// preserving diagnostic warnings collected in @p warnings. It aborts early on
/// the first verification error and returns that diagnostic.
il::support::Expected<void> iterateBlockInstructions_expected(
    const Function &fn,
    const BasicBlock &bb,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    const std::unordered_map<std::string, const Extern *> &externs,
    const std::unordered_map<std::string, const Function *> &funcs,
    TypeInference &types,
    const VerifyInstrFnExpected &verifyInstrFn,
    std::vector<il::support::Diag> &warnings);
/// @brief Ensures a block ends with a valid terminator for its control-flow
///        structure.
///
/// Returns a diagnostic if the final opcode does not satisfy terminator
/// requirements, helping pinpoint malformed control-flow shapes.
il::support::Expected<void> checkBlockTerminators_expected(const Function &fn,
                                                           const BasicBlock &bb);
/// @brief Verifies `br` instructions reference known blocks and argument types.
///
/// Uses @p blockMap to resolve the destination label and validates types with
/// the current inference state, reporting errors via diagnostics.
il::support::Expected<void> verifyBr_expected(const Function &fn,
                                             const BasicBlock &bb,
                                             const Instr &instr,
                                             const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                             TypeInference &types);
/// @brief Verifies `cbr` instructions for correct labels and operand types.
///
/// Ensures both conditional successors exist in @p blockMap and that the
/// tracked types align. Mismatches surface through the returned diagnostic.
il::support::Expected<void> verifyCBr_expected(const Function &fn,
                                              const BasicBlock &bb,
                                              const Instr &instr,
                                              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                              TypeInference &types);
/// @brief Confirms `ret` instructions match the function signature.
///
/// Validates that the returned value count and types agree with @p fn, using
/// @p types to resolve temporaries. Errors propagate as diagnostics.
il::support::Expected<void> verifyRet_expected(const Function &fn,
                                              const BasicBlock &bb,
                                              const Instr &instr,
                                              TypeInference &types);

namespace
{
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;

/// @brief Formats a diagnostic message prefix scoped to a function.
///
/// Prepends @p fn's name to @p message, allowing diagnostics emitted by the
/// verifier helpers to clearly identify the function context.
std::string formatFunctionDiag(const Function &fn, std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

/// @brief Validates extern declarations for uniqueness and runtime agreement.
///
/// Builds @p externs as a lookup table for later stages, emitting diagnostics
/// when duplicate names appear or when runtime signatures disagree with the
/// declared extern signature.
Expected<void> verifyExterns_E(const Module &m,
                               std::unordered_map<std::string, const Extern *> &externs)
{
    for (const auto &e : m.externs)
    {
        auto [it, inserted] = externs.emplace(e.name, &e);
        if (!inserted)
        {
            const Extern *prev = it->second;
            bool sigOk = prev->retType.kind == e.retType.kind && prev->params.size() == e.params.size();
            if (sigOk)
                for (size_t i = 0; i < e.params.size(); ++i)
                    if (prev->params[i].kind != e.params[i].kind)
                        sigOk = false;
            std::string message = "duplicate extern @" + e.name;
            if (!sigOk)
                message += " with mismatched signature";
            return Expected<void>{makeError({}, message)};
        }

        if (const auto *sig = il::runtime::findRuntimeSignature(e.name))
        {
            bool sigOk = e.retType.kind == sig->retType.kind && e.params.size() == sig->paramTypes.size();
            if (sigOk)
                for (size_t i = 0; i < sig->paramTypes.size(); ++i)
                    if (e.params[i].kind != sig->paramTypes[i].kind)
                        sigOk = false;
            if (!sigOk)
                return Expected<void>{makeError({}, "extern @" + e.name + " signature mismatch")};
        }
    }
    return {};
}

/// @brief Ensures global variable declarations are unique within the module.
///
/// Populates @p globals for downstream verification and reports a diagnostic on
/// the first duplicate encountered.
Expected<void> verifyGlobals_E(const Module &m,
                               std::unordered_map<std::string, const Global *> &globals)
{
    for (const auto &g : m.globals)
    {
        if (!globals.emplace(g.name, &g).second)
            return Expected<void>{makeError({}, "duplicate global @" + g.name)};
    }
    return {};
}

/// @brief Dispatches instruction verification, handling control-flow opcodes.
///
/// Routes branch and return opcodes to specialized helpers while delegating all
/// other instructions to InstructionChecker. Diagnostics are returned when an
/// error occurs; warnings accumulate in @p warnings.
Expected<void> verifyInstr_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                             const std::unordered_map<std::string, const Extern *> &externs,
                             const std::unordered_map<std::string, const Function *> &funcs,
                             TypeInference &types,
                             std::vector<Diag> &warnings)
{
    if (auto result = verifyOpcodeSignature_expected(fn, bb, instr); !result)
        return result;

    switch (instr.op)
    {
        case Opcode::Br:
            return verifyBr_expected(fn, bb, instr, blockMap, types);
        case Opcode::CBr:
            return verifyCBr_expected(fn, bb, instr, blockMap, types);
        case Opcode::Ret:
            return verifyRet_expected(fn, bb, instr, types);
        default:
            return verifyInstruction_expected(fn, bb, instr, externs, funcs, types, warnings);
    }
}

/// @brief Verifies a single basic block and maintains temporary type state.
///
/// Establishes the block's parameter temporaries, iterates instructions through
/// @p verifyInstrFn, and validates terminator structure. Warnings gathered
/// during instruction checks are appended to @p warnings, while fatal issues are
/// surfaced via the returned diagnostic.
Expected<void> verifyBlock_E(const Function &fn,
                              const BasicBlock &bb,
                              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              std::unordered_map<unsigned, Type> &temps,
                              std::vector<Diag> &warnings)
{
    std::unordered_set<unsigned> defined;
    for (const auto &kv : temps)
        defined.insert(kv.first);

    TypeInference types(temps, defined);

    std::vector<unsigned> paramIds;
    if (auto result = validateBlockParams_expected(fn, bb, types, paramIds); !result)
        return result;

    VerifyInstrFnExpected verifyInstrFn = [&](const Function &fnRef, const BasicBlock &bbRef, const Instr &instrRef,
                                              const std::unordered_map<std::string, const BasicBlock *> &blockMapRef,
                                              const std::unordered_map<std::string, const Extern *> &externsRef,
                                              const std::unordered_map<std::string, const Function *> &funcsRef,
                                              TypeInference &typesRef,
                                              std::vector<Diag> &warningSink) -> Expected<void>
    {
        return verifyInstr_E(fnRef, bbRef, instrRef, blockMapRef, externsRef, funcsRef, typesRef, warningSink);
    };

    if (auto result = iterateBlockInstructions_expected(
            fn, bb, blockMap, externs, funcs, types, verifyInstrFn, warnings);
        !result)
        return result;

    if (auto result = checkBlockTerminators_expected(fn, bb); !result)
        return result;

    for (unsigned id : paramIds)
        types.removeTemp(id);

    return {};
}

/// @brief Validates a function's structure, signatures, and block graph.
///
/// Checks entry block naming, signature compatibility with externs, unique
/// labels, instruction correctness, and branch targets. Collects non-fatal
/// diagnostics in @p warnings and returns the first fatal error diagnostic.
Expected<void> verifyFunction_E(const Function &fn,
                                const std::unordered_map<std::string, const Extern *> &externs,
                                const std::unordered_map<std::string, const Function *> &funcs,
                                std::vector<Diag> &warnings)
{
    if (fn.blocks.empty())
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "function has no blocks"))};

    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
    if (!isEntry)
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "first block must be entry"))};

    if (auto itExt = externs.find(fn.name); itExt != externs.end())
    {
        const Extern *e = itExt->second;
        bool sigOk = e->retType.kind == fn.retType.kind && e->params.size() == fn.params.size();
        if (sigOk)
            for (size_t i = 0; i < e->params.size(); ++i)
                if (e->params[i].kind != fn.params[i].type.kind)
                    sigOk = false;
        if (!sigOk)
            return Expected<void>{makeError({}, "function @" + fn.name + " signature mismatch with extern")};
    }

    std::unordered_set<std::string> labels;
    std::unordered_map<std::string, const BasicBlock *> blockMap;
    for (const auto &bb : fn.blocks)
    {
        if (!labels.insert(bb.label).second)
            return Expected<void>{makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        blockMap[bb.label] = &bb;
    }

    std::unordered_map<unsigned, Type> temps;
    for (const auto &p : fn.params)
        temps[p.id] = p.type;

    for (const auto &bb : fn.blocks)
        if (auto result = verifyBlock_E(fn, bb, blockMap, externs, funcs, temps, warnings); !result)
            return result;

    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            for (const auto &label : instr.labels)
                if (!labels.count(label))
                    return Expected<void>{makeError({}, formatFunctionDiag(fn, "unknown label " + label))};

    return {};
}

/// @brief Runs module-level verification building extern/global/function maps.
///
/// Aggregates symbol tables used by downstream checks and iterates every
/// function, preserving accumulated warnings in @p warnings. Fatal violations
/// are returned as diagnostics.
Expected<void> verifyModule_E(const Module &m, std::vector<Diag> &warnings)
{
    std::unordered_map<std::string, const Extern *> externs;
    std::unordered_map<std::string, const Global *> globals;
    std::unordered_map<std::string, const Function *> funcs;

    if (auto result = verifyExterns_E(m, externs); !result)
        return result;
    if (auto result = verifyGlobals_E(m, globals); !result)
        return result;

    for (const auto &fn : m.functions)
    {
        if (!funcs.emplace(fn.name, &fn).second)
            return Expected<void>{makeError({}, "duplicate function @" + fn.name)};

        if (auto result = verifyFunction_E(fn, externs, funcs, warnings); !result)
            return result;
    }

    return {};
}

} // namespace

/// @brief Entry point that verifies an entire module and aggregates diagnostics.
///
/// Collects warnings emitted during verification and appends them to the final
/// error message when a fatal diagnostic is returned, preserving context for
/// callers using the Expected-based API.
il::support::Expected<void> Verifier::verify(const Module &m)
{
    std::vector<Diag> warnings;
    if (auto result = verifyModule_E(m, warnings); !result)
    {
        if (warnings.empty())
            return result;

        std::ostringstream oss;
        for (const auto &warning : warnings)
            il::support::printDiag(warning, oss);
        il::support::printDiag(result.error(), oss);
        auto diag = result.error();
        diag.message = oss.str();
        return il::support::Expected<void>{std::move(diag)};
    }

    return {};
}

/// @brief Verifies extern declarations and prints failures to @p err.
///
/// Builds @p externs for reuse by callers; returns false after emitting the
/// associated diagnostic stream when duplicates or signature mismatches occur.
bool Verifier::verifyExterns(const Module &m,
                             std::ostream &err,
                             std::unordered_map<std::string, const Extern *> &externs)
{
    if (auto result = verifyExterns_E(m, externs); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

/// @brief Verifies global declarations and writes diagnostics to @p err.
///
/// Populates @p globals for caller reuse and reports duplicate declarations via
/// the diagnostic stream before returning false.
bool Verifier::verifyGlobals(const Module &m,
                             std::ostream &err,
                             std::unordered_map<std::string, const Global *> &globals)
{
    if (auto result = verifyGlobals_E(m, globals); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

/// @brief Verifies a function using pre-built extern and function maps.
///
/// Propagates warnings to @p err prior to any fatal diagnostic, ensuring callers
/// see the full context for soft and hard verification findings.
bool Verifier::verifyFunction(const Function &fn,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              std::ostream &err)
{
    std::vector<Diag> warnings;
    if (auto result = verifyFunction_E(fn, externs, funcs, warnings); !result)
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

/// @brief Verifies a single block with shared symbol tables and temp types.
///
/// Emits collected warnings to @p err even when verification succeeds so callers
/// can surface non-fatal issues encountered during instruction analysis.
bool Verifier::verifyBlock(const Function &fn,
                           const BasicBlock &bb,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           std::unordered_map<unsigned, Type> &temps,
                           std::ostream &err)
{
    std::vector<Diag> warnings;
    if (auto result = verifyBlock_E(fn, bb, blockMap, externs, funcs, temps, warnings); !result)
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

/// @brief Verifies an instruction in the context of its block and type state.
///
/// Prints all accumulated warnings to @p err before returning the fatal error,
/// ensuring diagnostic ordering matches the Expected-returning helper used by
/// the modular verification pipeline.
bool Verifier::verifyInstr(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &in,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           TypeInference &types,
                           std::ostream &err)
{
    std::vector<Diag> warnings;
    if (auto result = verifyInstr_E(fn, bb, in, blockMap, externs, funcs, types, warnings); !result)
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

} // namespace il::verify
