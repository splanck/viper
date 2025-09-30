// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Implements function-level verification by coordinating block and instruction checks.
// Key invariants: Functions must start with an entry block, maintain unique labels, and respect call signatures.
// Ownership/Lifetime: Operates on module-provided data; no allocations persist beyond verification.
// Links: docs/il-guide.md#reference

#include "il/verify/FunctionVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/TypeInference.hpp"

#include <algorithm>
#include <deque>
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

using HandlerInfo = std::pair<unsigned, unsigned>;

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

Expected<std::optional<HandlerInfo>> analyzeHandlerBlock(const Function &fn,
                                                         const BasicBlock &bb)
{
    if (bb.instructions.empty())
        return std::optional<HandlerInfo>{};

    const Instr &first = bb.instructions.front();
    if (first.op != Opcode::EhEntry)
    {
        for (size_t idx = 1; idx < bb.instructions.size(); ++idx)
        {
            if (bb.instructions[idx].op == Opcode::EhEntry)
            {
                return Expected<std::optional<HandlerInfo>>{
                    makeError(bb.instructions[idx].loc,
                              formatInstrDiag(fn, bb, bb.instructions[idx],
                                              "eh.entry only allowed as first instruction of handler block"))};
            }
        }
        return std::optional<HandlerInfo>{};
    }

    if (bb.params.size() != 2)
        return Expected<std::optional<HandlerInfo>>{
            makeError({}, formatBlockDiag(fn, bb, "handler blocks must declare (%err:Error, %tok:ResumeTok)"))};

    if (bb.params[0].type.kind != Type::Kind::Error || bb.params[1].type.kind != Type::Kind::ResumeTok)
        return Expected<std::optional<HandlerInfo>>{
            makeError({}, formatBlockDiag(fn, bb, "handler params must be (%err:Error, %tok:ResumeTok)"))};

    if (bb.params[0].name != "err" || bb.params[1].name != "tok")
        return Expected<std::optional<HandlerInfo>>{
            makeError({}, formatBlockDiag(fn, bb, "handler params must be named %err and %tok"))};

    HandlerInfo sig = {bb.params[0].id, bb.params[1].id};
    return std::optional<HandlerInfo>{sig};
}

struct EhState
{
    const BasicBlock *block = nullptr;
    int depth = 0;
    int parent = -1;
};

bool isTerminatorForEh(Opcode op)
{
    switch (op)
    {
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::Ret:
        case Opcode::Trap:
        case Opcode::TrapKind:
        case Opcode::TrapErr:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

std::vector<const BasicBlock *> gatherSuccessors(const Instr &terminator,
                                                const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    std::vector<const BasicBlock *> successors;
    switch (terminator.op)
    {
        case Opcode::Br:
            if (!terminator.labels.empty())
            {
                if (auto it = blockMap.find(terminator.labels[0]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::CBr:
            for (size_t idx = 0; idx < terminator.labels.size(); ++idx)
            {
                if (auto it = blockMap.find(terminator.labels[idx]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::ResumeLabel:
            if (!terminator.labels.empty())
            {
                if (auto it = blockMap.find(terminator.labels[0]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        default:
            break;
    }
    return successors;
}

std::vector<const BasicBlock *> buildPath(const std::vector<EhState> &states, int index)
{
    std::vector<const BasicBlock *> path;
    for (int cur = index; cur >= 0; cur = states[cur].parent)
        path.push_back(states[cur].block);
    std::reverse(path.begin(), path.end());
    return path;
}

std::string formatPathString(const std::vector<const BasicBlock *> &path)
{
    std::ostringstream oss;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i != 0)
            oss << " -> ";
        oss << path[i]->label;
    }
    return oss.str();
}

Expected<void> checkEhStackBalance(const Function &fn,
                                   const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    if (fn.blocks.empty())
        return {};

    std::deque<int> worklist;
    std::vector<EhState> states;
    std::unordered_map<const BasicBlock *, std::unordered_set<int>> visited;

    states.push_back({&fn.blocks.front(), 0, -1});
    worklist.push_back(0);
    visited[&fn.blocks.front()].insert(0);

    while (!worklist.empty())
    {
        const int stateIndex = worklist.front();
        worklist.pop_front();

        const EhState &state = states[stateIndex];
        const BasicBlock &bb = *state.block;
        int depth = state.depth;

        const Instr *terminator = nullptr;
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush)
            {
                ++depth;
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (depth == 0)
                {
                    std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
                    std::string message = formatInstrDiag(
                        fn,
                        bb,
                        instr,
                        std::string("eh.pop without matching eh.push; path: ") + formatPathString(path));
                    return Expected<void>{makeError(instr.loc, message)};
                }
                --depth;
            }

            if (isTerminatorForEh(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            continue;

        if ((terminator->op == Opcode::Ret || terminator->op == Opcode::Trap ||
             terminator->op == Opcode::TrapKind || terminator->op == Opcode::TrapErr ||
             terminator->op == Opcode::ResumeSame || terminator->op == Opcode::ResumeNext) && depth != 0)
        {
            std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
            std::string message = formatInstrDiag(
                fn,
                bb,
                *terminator,
                std::string("unmatched eh.push depth ") + std::to_string(depth) +
                    "; path: " + formatPathString(path));
            return Expected<void>{makeError(terminator->loc, message)};
        }

        const std::vector<const BasicBlock *> successors = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            if (!visited[succ].insert(depth).second)
                continue;
            const int nextIndex = static_cast<int>(states.size());
            states.push_back({succ, depth, stateIndex});
            worklist.push_back(nextIndex);
        }
    }

    return {};
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
Expected<void> verifyOpcodeSignature_E(const Function &fn, const BasicBlock &bb, const Instr &instr);
Expected<void> verifyInstruction_E(const Function &fn,
                                   const BasicBlock &bb,
                                   const Instr &instr,
                                   const std::unordered_map<std::string, const Extern *> &externs,
                                   const std::unordered_map<std::string, const Function *> &funcs,
                                   TypeInference &types,
                                   DiagSink &sink);
Expected<void> verifyBr_E(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          TypeInference &types);
Expected<void> verifyCBr_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           TypeInference &types);
Expected<void> verifyRet_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           TypeInference &types);

namespace
{

class ControlFlowStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::Ret;
    }

    Expected<void> verify(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          const std::unordered_map<std::string, const Extern *> &,
                          const std::unordered_map<std::string, const Function *> &,
                          TypeInference &types,
                          DiagSink &) const override
    {
        switch (instr.op)
        {
            case Opcode::Br:
                return verifyBr_E(fn, bb, instr, blockMap, types);
            case Opcode::CBr:
                return verifyCBr_E(fn, bb, instr, blockMap, types);
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
                          const std::unordered_map<std::string, const BasicBlock *> &,
                          const std::unordered_map<std::string, const Extern *> &externs,
                          const std::unordered_map<std::string, const Function *> &funcs,
                          TypeInference &types,
                          DiagSink &sink) const override
    {
        return verifyInstruction_E(fn, bb, instr, externs, funcs, types, sink);
    }
};

} // namespace

FunctionVerifier::FunctionVerifier(const ExternMap &externs) : externs_(externs)
{
    strategies_.push_back(std::make_unique<ControlFlowStrategy>());
    strategies_.push_back(std::make_unique<DefaultInstructionStrategy>());
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

    handlerInfo_.clear();

    std::unordered_map<unsigned, Type> temps;
    for (const auto &param : fn.params)
        temps[param.id] = param.type;

    for (const auto &bb : fn.blocks)
        if (auto result = verifyBlock(fn, bb, blockMap, temps, sink); !result)
            return result;

    if (auto result = checkEhStackBalance(fn, blockMap); !result)
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
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
            }
        }
    }

    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            for (const auto &label : instr.labels)
                if (!labels.count(label))
                    return Expected<void>{makeError({}, formatFunctionDiag(fn, "unknown label " + label))};

    return {};
}

Expected<void> FunctionVerifier::verifyBlock(const Function &fn,
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

    std::optional<HandlerInfo> handlerSignature;
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
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "eh.entry only allowed as first instruction of handler block"))};

        if (isResumeOpcode(instr.op))
        {
            if (!handlerSignature)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "resume.* only allowed in handler block"))};
            if (instr.operands.empty() || instr.operands[0].kind != Value::Kind::Temp ||
                instr.operands[0].id != handlerSignature->second)
            {
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "resume.* must use handler %tok parameter"))};
            }
        }

        if (isErrAccessOpcode(instr.op))
        {
            if (!handlerSignature)
            {
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "err.get_* only allowed in handler block"))};
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
                    return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
                }
            }
        }
        else
        {
            const auto checkValue = [&](const Value &value) -> Expected<void> {
                if (value.kind != Value::Kind::Temp)
                    return Expected<void>{};
                const unsigned id = value.id;
                if (released.count(id) == 0)
                    return Expected<void>{};
                std::ostringstream message;
                message << "use after release of %" << id;
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, message.str()))};
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

        if (isRuntimeArrayRelease(instr) && !instr.operands.empty() && instr.operands[0].kind == Value::Kind::Temp)
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

Expected<void> FunctionVerifier::verifyInstruction(const Function &fn,
                                                   const BasicBlock &bb,
                                                   const Instr &instr,
                                                   const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                                   TypeInference &types,
                                                   DiagSink &sink)
{
    if (auto result = verifyOpcodeSignature_E(fn, bb, instr); !result)
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
