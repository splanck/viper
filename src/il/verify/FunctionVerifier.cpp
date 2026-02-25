//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Coordinate function-level IL verification by combining block checks
//          with opcode-specific instruction strategies.
// Key invariants: Each function must expose a valid entry block, maintain
//                 unique labels, and respect extern/runtime signatures and
//                 handler semantics.
// Ownership/Lifetime: Operates on module-provided IR structures without
//                     retaining ownership beyond the call scope.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/FunctionVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/InstructionStrategies.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/VerifyCtx.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::verify
{
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;

namespace
{

/// @brief Identify whether an opcode belongs to the resume-family terminators.
///
/// @details Resume opcodes have additional verifier requirements: they are only
///          legal inside handler blocks and must forward the `%tok` parameter.
///          Recognising them allows the verifier to enforce those constraints
///          uniformly.
///
/// @param op Opcode under classification.
/// @return @c true when the opcode is resume.same/next/label.
bool isResumeOpcode(Opcode op)
{
    return op == Opcode::ResumeSame || op == Opcode::ResumeNext || op == Opcode::ResumeLabel;
}

/// @brief Detect opcodes that read fields from an error value.
///
/// @details Used to prevent `err.get_*` opcodes from appearing outside handler
///          blocks where the `%tok` parameter is available.
///
/// @param op Opcode under inspection.
/// @return @c true when @p op accesses error metadata.
bool isErrAccessOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetIp:
        case Opcode::ErrGetLine:
            return true;
        default:
            return false;
    }
}

/// @brief Recognise runtime helper calls that release array handles.
///
/// @details The verifier tracks releases so it can flag double-free and
///          use-after-release errors on SSA temporaries that reference runtime
///          arrays.
///
/// @param instr Instruction being analysed.
/// @return @c true when the instruction is the runtime array release helper.
bool isRuntimeArrayRelease(const Instr &instr)
{
    return instr.op == Opcode::Call && instr.callee == "rt_arr_i32_release";
}

} // namespace

Expected<void> validateBlockParams_E(const Function &fn,
                                     const BasicBlock &bb,
                                     TypeInference &types,
                                     std::vector<unsigned> &paramIds);
Expected<void> checkBlockTerminators_E(const Function &fn, const BasicBlock &bb);
Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx);
Expected<void> verifyInstruction_E(const Function &fn,
                                   const BasicBlock &bb,
                                   const Instr &instr,
                                   const std::unordered_map<std::string, const Extern *> &externs,
                                   const std::unordered_map<std::string, const Function *> &funcs,
                                   TypeInference &types,
                                   DiagSink &sink);

/// @brief Construct a verifier with knowledge of extern signatures.
///
/// @details The extern map is cached so that call instructions can be checked
///          against known signatures.  Instruction strategies are seeded with
///          the default collection used to validate every opcode.
///
/// @param externs Map from extern names to their declarations.
FunctionVerifier::FunctionVerifier(const ExternMap &externs)
    : externs_(externs), strategies_(makeDefaultInstructionStrategies())
{
}

/// @brief Verify every function in a module for structural correctness.
///
/// @details Builds a name-to-function map to detect duplicates before invoking
///          @ref verifyFunction on each function.  Verification stops at the
///          first failure so the most relevant diagnostic can be reported to
///          users immediately.
///
/// @param module Module containing functions to verify.
/// @param sink Diagnostic sink receiving instruction-level messages.
/// @return Empty Expected on success or the first failure diagnostic.
Expected<void> FunctionVerifier::run(const Module &module, DiagSink &sink)
{
    functionMap_.clear();

    for (const auto &fn : module.functions)
    {
        if (!functionMap_.emplace(fn.name, &fn).second)
            return Expected<void>{makeError({}, "duplicate function @" + fn.name)};
    }

    for (const auto &fn : module.functions)
        if (auto result = verifyFunction(fn, sink); !result)
            return result;

    return {};
}

/// @brief Validate a single function's blocks, labels, and handler metadata.
///
/// @details Ensures the first block is an entry block, checks for extern
///          signature parity, records handler signatures, and validates that all
///          referenced labels exist. Block-level checks are delegated to
///          @ref verifyBlock.
///
/// @param fn Function being verified.
/// @param sink Diagnostic sink for detailed messages.
/// @return Success or a diagnostic describing the first failure.
Expected<void> FunctionVerifier::verifyFunction(const Function &fn, DiagSink &sink)
{
    if (fn.blocks.empty())
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "function has no blocks"))};

    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
    if (!isEntry)
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "first block must be entry"))};

    if (auto it = externs_.find(fn.name); it != externs_.end())
    {
        const Extern *ext = it->second;
        bool sigOk = ext->retType.kind == fn.retType.kind && ext->params.size() == fn.params.size();
        if (sigOk)
        {
            for (size_t i = 0; i < ext->params.size(); ++i)
                if (ext->params[i].kind != fn.params[i].type.kind)
                    sigOk = false;
        }
        if (!sigOk)
            return Expected<void>{
                /// @brief Handles error condition.
                makeError({}, "function @" + fn.name + " signature mismatch with extern")};
    }

    std::unordered_set<std::string> labels;
    BlockMap blockMap;
    blockMap.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks)
    {
        if (!labels.insert(bb.label).second)
            return Expected<void>{
                /// @brief Handles error condition.
                makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        // Use string_view key referencing bb.label; the Function outlives this map.
        blockMap.emplace(std::string_view{bb.label}, &bb);
    }

    handlerInfo_.clear();

    std::unordered_map<unsigned, Type> temps;
    for (const auto &param : fn.params)
    {
        temps[param.id] = param.type;
    }

    // ===== PASS 1: Pre-collect all definitions for type information =====
    // This is necessary because SimplifyCFG and other transforms may reorder blocks
    // such that definitions appear later in declaration order but still dominate uses.
    // By collecting all definitions first, we have complete type information for
    // cross-block operand references.
    //
    // We also track which block each definition comes from so that verifyBlock can
    // still detect within-block use-before-def errors.
    std::unordered_map<unsigned, const BasicBlock *> definingBlock;

    for (const auto &bb : fn.blocks)
    {
        // Block parameters define temporaries
        for (const auto &param : bb.params)
        {
            if (temps.find(param.id) == temps.end())
            {
                temps[param.id] = param.type;
                definingBlock[param.id] = &bb;
            }
        }

        // Instructions with results define temporaries
        for (const auto &instr : bb.instructions)
        {
            if (instr.result.has_value())
            {
                if (temps.find(*instr.result) == temps.end())
                {
                    temps[*instr.result] = instr.type;
                    definingBlock[*instr.result] = &bb;
                }
            }
        }
    }

    // ===== PASS 2: Full verification with complete type info =====
    // Collect EhPush targets and label references during single pass over blocks.
    // This avoids two additional O(blocks × instructions) traversals.
    struct EhPushCheck
    {
        const BasicBlock *bb;
        const Instr *instr;
        std::string target;
    };

    std::vector<EhPushCheck> ehPushChecks;
    std::vector<std::string> labelRefs;

    for (const auto &bb : fn.blocks)
    {
        if (auto result = verifyBlock(fn, bb, blockMap, temps, definingBlock, sink); !result)
            return result;

        // Collect EhPush targets and all label references in single pass
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush && !instr.labels.empty())
                ehPushChecks.push_back({&bb, &instr, instr.labels.front()});

            for (const auto &label : instr.labels)
                labelRefs.push_back(label);
        }
    }

    // Validate EhPush targets exist in handlerInfo_ (populated during verifyBlock)
    for (const auto &check : ehPushChecks)
    {
        if (handlerInfo_.find(check.target) == handlerInfo_.end())
        {
            std::ostringstream message;
            message << "eh.push target ^" << check.target << " must name a handler block";
            return Expected<void>{makeError(
                check.instr->loc, formatInstrDiag(fn, *check.bb, *check.instr, message.str()))};
        }
    }

    // Validate all label references exist
    for (const auto &label : labelRefs)
    {
        if (!labels.contains(label))
            return Expected<void>{makeError({}, formatFunctionDiag(fn, "unknown label " + label))};
    }

    return {};
}

/// @brief Run block-level verification including handler semantics.
///
/// @details Establishes a type inference context seeded with incoming
///          temporaries, validates block parameters, records handler metadata,
///          enforces resume and error accessor placement rules, tracks runtime
///          array releases, dispatches opcode-specific verification, and finally
///          ensures terminators are well-formed. Parameter temporaries are
///          removed from the inference context after the block is processed.
///
/// @param fn Enclosing function definition.
/// @param bb Block under inspection.
/// @param blockMap Mapping from labels to block pointers for CFG lookups.
/// @param temps Table of SSA temporaries and their known types.
/// @param definingBlock Maps temp IDs to their defining blocks for within-block ordering.
/// @param sink Diagnostic sink receiving instruction-level messages.
/// @return Success or a diagnostic describing the failure.
Expected<void> FunctionVerifier::verifyBlock(
    const Function &fn,
    const BasicBlock &bb,
    const BlockMap &blockMap,
    std::unordered_map<unsigned, Type> &temps,
    const std::unordered_map<unsigned, const BasicBlock *> &definingBlock,
    DiagSink &sink)
{
    // Initialize defined set with definitions from OTHER blocks.
    // This allows cross-block uses to pass verification even when the defining
    // block appears later in declaration order (which is valid after SimplifyCFG).
    // Within-block definitions are added incrementally to detect within-block
    // use-before-def errors.
    std::unordered_set<unsigned> defined;
    for (const auto &entry : temps)
    {
        auto it = definingBlock.find(entry.first);
        if (it == definingBlock.end() || it->second != &bb)
        {
            // Definition is from another block or a function parameter
            defined.insert(entry.first);
        }
    }

    TypeInference types(temps, defined);

    std::vector<unsigned> paramIds;
    if (auto result = validateBlockParams_E(fn, bb, types, paramIds); !result)
        return result;

    std::optional<HandlerSignature> handlerSignature;
    auto handlerCheck = analyzeHandlerBlock(fn, bb);
    if (!handlerCheck)
        return Expected<void>{handlerCheck.error()};
    handlerSignature = handlerCheck.value();
    if (handlerSignature)
        handlerInfo_[bb.label] = *handlerSignature;

    std::unordered_set<unsigned> released;

    for (const auto &instr : bb.instructions)
    {
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

        if (instr.op == Opcode::EhEntry && &instr != &bb.instructions.front())
            return Expected<void>{makeError(
                instr.loc,
                formatInstrDiag(
                    fn, bb, instr, "eh.entry only allowed as first instruction of handler block"))};

        if (isResumeOpcode(instr.op))
        {
            if (!handlerSignature)
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "resume.* only allowed in handler block"))};
            if (instr.operands.empty() || instr.operands[0].kind != Value::Kind::Temp ||
                instr.operands[0].id != handlerSignature->resumeTokenParam)
            {
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "resume.* must use handler %tok parameter"))};
            }
        }

        if (isErrAccessOpcode(instr.op))
        {
            if (!handlerSignature)
            {
                return Expected<void>{makeError(
                    instr.loc,
                    formatInstrDiag(fn, bb, instr, "err.get_* only allowed in handler block"))};
            }
        }

        if (isRuntimeArrayRelease(instr))
        {
            if (!instr.operands.empty() && instr.operands[0].kind == Value::Kind::Temp)
            {
                const unsigned id = instr.operands[0].id;
                if (released.contains(id))
                {
                    std::ostringstream message;
                    message << "double release of %" << id;
                    return Expected<void>{
                        /// @brief Handles error condition.
                        makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
                }
            }
        }
        else
        {
            const auto checkValue = [&](const Value &value) -> Expected<void>
            {
                if (value.kind != Value::Kind::Temp)
                    return Expected<void>{};
                const unsigned id = value.id;
                if (!released.contains(id))
                    return Expected<void>{};
                std::ostringstream message;
                message << "use after release of %" << id;
                return Expected<void>{
                    /// @brief Handles error condition.
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            };

            for (const auto &operand : instr.operands)
                if (auto result = checkValue(operand); !result)
                    return result;

            for (const auto &bundle : instr.brArgs)
                for (const auto &argument : bundle)
                    if (auto result = checkValue(argument); !result)
                        return result;
        }

        if (auto result = verifyInstruction(fn, bb, instr, blockMap, types, sink); !result)
            return result;

        if (isRuntimeArrayRelease(instr) && !instr.operands.empty() &&
            instr.operands[0].kind == Value::Kind::Temp)
        {
            released.insert(instr.operands[0].id);
        }

        if (isTerminator(instr.op))
            break;
    }

    if (auto result = checkBlockTerminators_E(fn, bb); !result)
        return result;

    // Block params persist in the type map for cross-block use.  The pre-collection
    // pass already registers all definitions so successor blocks can reference them.
    // (Removed per-block removeTemp that prevented valid cross-block references
    //  after inlining.)
    (void)paramIds;

    return {};
}

/// @brief Dispatch verification logic for a single instruction.
///
/// @details Constructs a @ref VerifyCtx populated with the surrounding context,
///          validates operand/result signature contracts, and iterates the
///          registered strategy list until one claims the opcode.  The selected
///          strategy performs opcode-specific checks and returns its result.
///
/// @param fn Function containing the instruction.
/// @param bb Block containing the instruction.
/// @param instr Instruction to verify.
/// @param blockMap Mapping from labels to block pointers for CFG queries.
/// @param types Type inference state for SSA values.
/// @param sink Diagnostic sink receiving verification messages.
/// @return Success or a diagnostic describing the error.
Expected<void> FunctionVerifier::verifyInstruction(const Function &fn,
                                                   const BasicBlock &bb,
                                                   const Instr &instr,
                                                   const BlockMap &blockMap,
                                                   TypeInference &types,
                                                   DiagSink &sink)
{
    VerifyCtx ctx{sink, types, externs_, functionMap_, fn, bb, instr};
    if (auto result = verifyOpcodeSignature_E(ctx); !result)
        return result;

    for (const auto &strategy : strategies_)
    {
        if (!strategy->matches(instr))
            continue;
        return strategy->verify(fn, bb, instr, blockMap, externs_, functionMap_, types, sink);
    }

    return Expected<void>{makeError({}, formatFunctionDiag(fn, "no instruction strategy for op"))};
}

/// @brief Compose a function-scoped diagnostic prefix.
///
/// @details Formats the function name and appends an optional suffix so callers
///          can reuse the string as a consistent diagnostic prefix when no
///          specific instruction location is available.
///
/// @param fn Function associated with the diagnostic.
/// @param message Additional context appended after the name.
/// @return Human-readable string describing the function context.
std::string FunctionVerifier::formatFunctionDiag(const Function &fn, std::string_view message) const
{
    std::ostringstream oss;
    oss << fn.name;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify
