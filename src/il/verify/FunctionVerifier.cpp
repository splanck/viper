// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Implements function-level verification by coordinating block and instruction checks.
// Key invariants: Functions must start with an entry block, maintain unique labels, and respect
// call signatures. Ownership/Lifetime: Operates on module-provided data; no allocations persist
// beyond verification. Links: docs/il-guide.md#reference

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

bool isResumeOpcode(Opcode op)
{
    return op == Opcode::ResumeSame || op == Opcode::ResumeNext || op == Opcode::ResumeLabel;
}

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

FunctionVerifier::FunctionVerifier(const ExternMap &externs)
    : externs_(externs), strategies_(makeDefaultInstructionStrategies())
{
}

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
                makeError({}, "function @" + fn.name + " signature mismatch with extern")};
    }

    std::unordered_set<std::string> labels;
    std::unordered_map<std::string, const BasicBlock *> blockMap;
    for (const auto &bb : fn.blocks)
    {
        if (!labels.insert(bb.label).second)
            return Expected<void>{
                makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        blockMap[bb.label] = &bb;
    }

    handlerInfo_.clear();

    std::unordered_map<unsigned, Type> temps;
    for (const auto &param : fn.params)
        temps[param.id] = param.type;

    for (const auto &bb : fn.blocks)
        if (auto result = verifyBlock(fn, bb, blockMap, temps, sink); !result)
            return result;

    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op != Opcode::EhPush)
                continue;
            if (instr.labels.empty())
                continue;
            const std::string &target = instr.labels.front();
            if (handlerInfo_.find(target) == handlerInfo_.end())
            {
                std::ostringstream message;
                message << "eh.push target ^" << target << " must name a handler block";
                return Expected<void>{
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            }
        }
    }

    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            for (const auto &label : instr.labels)
                if (!labels.count(label))
                    return Expected<void>{
                        makeError({}, formatFunctionDiag(fn, "unknown label " + label))};

    return {};
}

Expected<void> FunctionVerifier::verifyBlock(
    const Function &fn,
    const BasicBlock &bb,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
    std::unordered_map<unsigned, Type> &temps,
    DiagSink &sink)
{
    std::unordered_set<unsigned> defined;
    for (const auto &entry : temps)
        defined.insert(entry.first);

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
                if (released.count(id) != 0)
                {
                    std::ostringstream message;
                    message << "double release of %" << id;
                    return Expected<void>{
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
                if (released.count(id) == 0)
                    return Expected<void>{};
                std::ostringstream message;
                message << "use after release of %" << id;
                return Expected<void>{
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

    for (unsigned id : paramIds)
        types.removeTemp(id);

    return {};
}

Expected<void> FunctionVerifier::verifyInstruction(
    const Function &fn,
    const BasicBlock &bb,
    const Instr &instr,
    const std::unordered_map<std::string, const BasicBlock *> &blockMap,
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

std::string FunctionVerifier::formatFunctionDiag(const Function &fn, std::string_view message) const
{
    std::ostringstream oss;
    oss << fn.name;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify
