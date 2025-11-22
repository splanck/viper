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
/// @brief Implements switch functionality.
/// @param op Parameter description needed.
/// @return Return value description needed.
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

/// @brief Implements for functionality.
/// @param module.functions Parameter description needed.
/// @return Return value description needed.
    for (const auto &fn : module.functions)
    {
/// @brief Implements if functionality.
/// @param !functionMap_.emplace(fn.name Parameter description needed.
/// @param fn Parameter description needed.
/// @return Return value description needed.
        if (!functionMap_.emplace(fn.name, &fn).second)
            return Expected<void>{makeError({}, "duplicate function @" + fn.name)};
    }

/// @brief Implements for functionality.
/// @param module.functions Parameter description needed.
/// @return Return value description needed.
    for (const auto &fn : module.functions)
/// @brief Implements if functionality.
/// @param verifyFunction(fn Parameter description needed.
/// @param sink Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param fn.blocks.empty( Parameter description needed.
/// @return Return value description needed.
    if (fn.blocks.empty())
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "function has no blocks"))};

    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
/// @brief Implements if functionality.
/// @param !isEntry Parameter description needed.
/// @return Return value description needed.
    if (!isEntry)
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "first block must be entry"))};

/// @brief Implements if functionality.
/// @param externs_.find(fn.name Parameter description needed.
/// @return Return value description needed.
    if (auto it = externs_.find(fn.name); it != externs_.end())
    {
        const Extern *ext = it->second;
        bool sigOk = ext->retType.kind == fn.retType.kind && ext->params.size() == fn.params.size();
/// @brief Implements if functionality.
/// @param sigOk Parameter description needed.
/// @return Return value description needed.
        if (sigOk)
        {
/// @brief Implements for functionality.
/// @param ext->params.size( Parameter description needed.
/// @return Return value description needed.
            for (size_t i = 0; i < ext->params.size(); ++i)
/// @brief Implements if functionality.
/// @param fn.params[i].type.kind Parameter description needed.
/// @return Return value description needed.
                if (ext->params[i].kind != fn.params[i].type.kind)
                    sigOk = false;
        }
/// @brief Implements if functionality.
/// @param !sigOk Parameter description needed.
/// @return Return value description needed.
        if (!sigOk)
            return Expected<void>{
/// @brief Handles error condition.
/// @param {} Parameter description needed.
/// @param extern" Parameter description needed.
/// @return Return value description needed.
                makeError({}, "function @" + fn.name + " signature mismatch with extern")};
    }

    std::unordered_set<std::string> labels;
    std::unordered_map<std::string, const BasicBlock *> blockMap;
/// @brief Implements for functionality.
/// @param fn.blocks Parameter description needed.
/// @return Return value description needed.
    for (const auto &bb : fn.blocks)
    {
/// @brief Implements if functionality.
/// @param !labels.insert(bb.label Parameter description needed.
/// @return Return value description needed.
        if (!labels.insert(bb.label).second)
            return Expected<void>{
/// @brief Handles error condition.
/// @param {} Parameter description needed.
/// @param formatFunctionDiag(fn Parameter description needed.
/// @param bb.label Parameter description needed.
/// @return Return value description needed.
                makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        blockMap[bb.label] = &bb;
    }

    handlerInfo_.clear();

    std::unordered_map<unsigned, Type> temps;
/// @brief Implements for functionality.
/// @param fn.params Parameter description needed.
/// @return Return value description needed.
    for (const auto &param : fn.params)
        temps[param.id] = param.type;

/// @brief Implements for functionality.
/// @param fn.blocks Parameter description needed.
/// @return Return value description needed.
    for (const auto &bb : fn.blocks)
/// @brief Implements if functionality.
/// @param verifyBlock(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param blockMap Parameter description needed.
/// @param temps Parameter description needed.
/// @param sink Parameter description needed.
/// @return Return value description needed.
        if (auto result = verifyBlock(fn, bb, blockMap, temps, sink); !result)
            return result;

/// @brief Implements for functionality.
/// @param fn.blocks Parameter description needed.
/// @return Return value description needed.
    for (const auto &bb : fn.blocks)
    {
/// @brief Implements for functionality.
/// @param bb.instructions Parameter description needed.
/// @return Return value description needed.
        for (const auto &instr : bb.instructions)
        {
/// @brief Implements if functionality.
/// @param Opcode::EhPush Parameter description needed.
/// @return Return value description needed.
            if (instr.op != Opcode::EhPush)
                continue;
/// @brief Implements if functionality.
/// @param instr.labels.empty( Parameter description needed.
/// @return Return value description needed.
            if (instr.labels.empty())
                continue;
            const std::string &target = instr.labels.front();
/// @brief Implements if functionality.
/// @param handlerInfo_.find(target Parameter description needed.
/// @return Return value description needed.
            if (handlerInfo_.find(target) == handlerInfo_.end())
            {
                std::ostringstream message;
                message << "eh.push target ^" << target << " must name a handler block";
                return Expected<void>{
/// @brief Handles error condition.
/// @param instr.loc Parameter description needed.
/// @param formatInstrDiag(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param message.str( Parameter description needed.
/// @return Return value description needed.
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            }
        }
    }

/// @brief Implements for functionality.
/// @param fn.blocks Parameter description needed.
/// @return Return value description needed.
    for (const auto &bb : fn.blocks)
/// @brief Implements for functionality.
/// @param bb.instructions Parameter description needed.
/// @return Return value description needed.
        for (const auto &instr : bb.instructions)
/// @brief Implements for functionality.
/// @param instr.labels Parameter description needed.
/// @return Return value description needed.
            for (const auto &label : instr.labels)
/// @brief Implements if functionality.
/// @param !labels.count(label Parameter description needed.
/// @return Return value description needed.
                if (!labels.count(label))
                    return Expected<void>{
/// @brief Handles error condition.
/// @param {} Parameter description needed.
/// @param formatFunctionDiag(fn Parameter description needed.
/// @param label Parameter description needed.
/// @return Return value description needed.
                        makeError({}, formatFunctionDiag(fn, "unknown label " + label))};

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
/// @param sink Diagnostic sink receiving instruction-level messages.
/// @return Success or a diagnostic describing the failure.
Expected<void> FunctionVerifier::verifyBlock(
    const Function &fn,
    const BasicBlock &bb,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    std::unordered_map<unsigned, Type> &temps,
    DiagSink &sink)
{
    std::unordered_set<unsigned> defined;
/// @brief Implements for functionality.
/// @param temps Parameter description needed.
/// @return Return value description needed.
    for (const auto &entry : temps)
        defined.insert(entry.first);

    TypeInference types(temps, defined);

    std::vector<unsigned> paramIds;
/// @brief Implements if functionality.
/// @param validateBlockParams_E(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param types Parameter description needed.
/// @param paramIds Parameter description needed.
/// @return Return value description needed.
    if (auto result = validateBlockParams_E(fn, bb, types, paramIds); !result)
        return result;

    std::optional<HandlerSignature> handlerSignature;
    auto handlerCheck = analyzeHandlerBlock(fn, bb);
/// @brief Implements if functionality.
/// @param !handlerCheck Parameter description needed.
/// @return Return value description needed.
    if (!handlerCheck)
        return Expected<void>{handlerCheck.error()};
    handlerSignature = handlerCheck.value();
/// @brief Implements if functionality.
/// @param handlerSignature Parameter description needed.
/// @return Return value description needed.
    if (handlerSignature)
        handlerInfo_[bb.label] = *handlerSignature;

    std::unordered_set<unsigned> released;

/// @brief Implements for functionality.
/// @param bb.instructions Parameter description needed.
/// @return Return value description needed.
    for (const auto &instr : bb.instructions)
    {
/// @brief Implements if functionality.
/// @param types.ensureOperandsDefined_E(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @return Return value description needed.
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

/// @brief Implements if functionality.
/// @param bb.instructions.front( Parameter description needed.
/// @return Return value description needed.
        if (instr.op == Opcode::EhEntry && &instr != &bb.instructions.front())
            return Expected<void>{makeError(
                instr.loc,
/// @brief Implements formatInstrDiag functionality.
/// @return Return value description needed.
                formatInstrDiag(
                    fn, bb, instr, "eh.entry only allowed as first instruction of handler block"))};

/// @brief Implements if functionality.
/// @param isResumeOpcode(instr.op Parameter description needed.
/// @return Return value description needed.
        if (isResumeOpcode(instr.op))
        {
/// @brief Implements if functionality.
/// @param !handlerSignature Parameter description needed.
/// @return Return value description needed.
            if (!handlerSignature)
                return Expected<void>{makeError(
                    instr.loc,
/// @brief Implements formatInstrDiag functionality.
/// @param fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param block" Parameter description needed.
/// @return Return value description needed.
                    formatInstrDiag(fn, bb, instr, "resume.* only allowed in handler block"))};
/// @brief Implements if functionality.
/// @param instr.operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (instr.operands.empty() || instr.operands[0].kind != Value::Kind::Temp ||
                instr.operands[0].id != handlerSignature->resumeTokenParam)
            {
                return Expected<void>{makeError(
                    instr.loc,
/// @brief Implements formatInstrDiag functionality.
/// @param fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param parameter" Parameter description needed.
/// @return Return value description needed.
                    formatInstrDiag(fn, bb, instr, "resume.* must use handler %tok parameter"))};
            }
        }

/// @brief Implements if functionality.
/// @param isErrAccessOpcode(instr.op Parameter description needed.
/// @return Return value description needed.
        if (isErrAccessOpcode(instr.op))
        {
/// @brief Implements if functionality.
/// @param !handlerSignature Parameter description needed.
/// @return Return value description needed.
            if (!handlerSignature)
            {
                return Expected<void>{makeError(
                    instr.loc,
/// @brief Implements formatInstrDiag functionality.
/// @param fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param block" Parameter description needed.
/// @return Return value description needed.
                    formatInstrDiag(fn, bb, instr, "err.get_* only allowed in handler block"))};
            }
        }

/// @brief Implements if functionality.
/// @param isRuntimeArrayRelease(instr Parameter description needed.
/// @return Return value description needed.
        if (isRuntimeArrayRelease(instr))
        {
/// @brief Implements if functionality.
/// @param !instr.operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (!instr.operands.empty() && instr.operands[0].kind == Value::Kind::Temp)
            {
                const unsigned id = instr.operands[0].id;
/// @brief Implements if functionality.
/// @param released.count(id Parameter description needed.
/// @return Return value description needed.
                if (released.count(id) != 0)
                {
                    std::ostringstream message;
                    message << "double release of %" << id;
                    return Expected<void>{
/// @brief Handles error condition.
/// @param instr.loc Parameter description needed.
/// @param formatInstrDiag(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param message.str( Parameter description needed.
/// @return Return value description needed.
                        makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
                }
            }
        }
        else
        {
            const auto checkValue = [&](const Value &value) -> Expected<void>
            {
/// @brief Implements if functionality.
/// @param Value::Kind::Temp Parameter description needed.
/// @return Return value description needed.
                if (value.kind != Value::Kind::Temp)
                    return Expected<void>{};
                const unsigned id = value.id;
/// @brief Implements if functionality.
/// @param released.count(id Parameter description needed.
/// @return Return value description needed.
                if (released.count(id) == 0)
                    return Expected<void>{};
                std::ostringstream message;
                message << "use after release of %" << id;
                return Expected<void>{
/// @brief Handles error condition.
/// @param instr.loc Parameter description needed.
/// @param formatInstrDiag(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param message.str( Parameter description needed.
/// @return Return value description needed.
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            };

/// @brief Implements for functionality.
/// @param instr.operands Parameter description needed.
/// @return Return value description needed.
            for (const auto &operand : instr.operands)
/// @brief Implements if functionality.
/// @param checkValue(operand Parameter description needed.
/// @return Return value description needed.
                if (auto result = checkValue(operand); !result)
                    return result;

/// @brief Implements for functionality.
/// @param instr.brArgs Parameter description needed.
/// @return Return value description needed.
            for (const auto &bundle : instr.brArgs)
/// @brief Implements for functionality.
/// @param bundle Parameter description needed.
/// @return Return value description needed.
                for (const auto &argument : bundle)
/// @brief Implements if functionality.
/// @param checkValue(argument Parameter description needed.
/// @return Return value description needed.
                    if (auto result = checkValue(argument); !result)
                        return result;
        }

/// @brief Implements if functionality.
/// @param verifyInstruction(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @param instr Parameter description needed.
/// @param blockMap Parameter description needed.
/// @param types Parameter description needed.
/// @param sink Parameter description needed.
/// @return Return value description needed.
        if (auto result = verifyInstruction(fn, bb, instr, blockMap, types, sink); !result)
            return result;

/// @brief Implements if functionality.
/// @param isRuntimeArrayRelease(instr Parameter description needed.
/// @return Return value description needed.
        if (isRuntimeArrayRelease(instr) && !instr.operands.empty() &&
            instr.operands[0].kind == Value::Kind::Temp)
        {
            released.insert(instr.operands[0].id);
        }

/// @brief Implements if functionality.
/// @param isTerminator(instr.op Parameter description needed.
/// @return Return value description needed.
        if (isTerminator(instr.op))
            break;
    }

/// @brief Implements if functionality.
/// @param checkBlockTerminators_E(fn Parameter description needed.
/// @param bb Parameter description needed.
/// @return Return value description needed.
    if (auto result = checkBlockTerminators_E(fn, bb); !result)
        return result;

/// @brief Implements for functionality.
/// @param paramIds Parameter description needed.
/// @return Return value description needed.
    for (unsigned id : paramIds)
        types.removeTemp(id);

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
Expected<void> FunctionVerifier::verifyInstruction(
    const Function &fn,
    const BasicBlock &bb,
    const Instr &instr,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    TypeInference &types,
    DiagSink &sink)
{
    VerifyCtx ctx{sink, types, externs_, functionMap_, fn, bb, instr};
/// @brief Implements if functionality.
/// @param verifyOpcodeSignature_E(ctx Parameter description needed.
/// @return Return value description needed.
    if (auto result = verifyOpcodeSignature_E(ctx); !result)
        return result;

/// @brief Implements for functionality.
/// @param strategies_ Parameter description needed.
/// @return Return value description needed.
    for (const auto &strategy : strategies_)
    {
/// @brief Implements if functionality.
/// @param !strategy->matches(instr Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param !message.empty( Parameter description needed.
/// @return Return value description needed.
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify
