//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a small direct-call inliner with a simple cost model. The inliner
// targets tiny, non-recursive callees with a handful of blocks and no
// exception-handling constructs. Callee parameters (including block parameters)
// are mapped to call operands, SSA temporaries are remapped into the caller,
// and returns branch to a continuation block at the call site. A hard budget on
// instruction count, block count, and inline depth keeps code growth bounded.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Inline.hpp"

#include "il/analysis/CallGraph.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/utils/Utils.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
constexpr unsigned kMaxCallSites = 4;
constexpr char kDepthKeySep = '\0';

using BlockDepthMap = std::unordered_map<std::string, unsigned>;

struct InlineCost
{
    unsigned instrCount = 0;
    unsigned blockCount = 0;
    unsigned callSites = 0;
    bool recursive = false;
    bool hasEH = false;
    bool unsupportedCFG = false;
    bool hasReturn = false;

    bool withinBudget(unsigned instrBudget, unsigned blockBudget) const
    {
        if (recursive || hasEH || unsupportedCFG || !hasReturn)
            return false;
        if (instrCount > instrBudget || blockCount > blockBudget)
            return false;
        if (callSites > kMaxCallSites)
            return false;
        return true;
    }
};

std::string depthKey(const std::string &fn, const std::string &label)
{
    return fn + kDepthKeySep + label;
}

unsigned getBlockDepth(const BlockDepthMap &depths, const std::string &fn, const std::string &label)
{
    auto it = depths.find(depthKey(fn, label));
    if (it == depths.end())
        return 0;
    return it->second;
}

void setBlockDepth(BlockDepthMap &depths, const std::string &fn, const std::string &label, unsigned depth)
{
    depths[depthKey(fn, label)] = depth;
}

bool isDirectCall(const Instr &I)
{
    return I.op == Opcode::Call && !I.callee.empty();
}

bool isEHSensitive(const Instr &I)
{
    switch (I.op)
    {
    case Opcode::EhPush:
    case Opcode::EhPop:
    case Opcode::EhEntry:
    case Opcode::ResumeSame:
    case Opcode::ResumeNext:
    case Opcode::ResumeLabel:
        return true;
    default:
        return false;
    }
}

bool hasUnsupportedTerminator(const Instr &I)
{
    return !(I.op == Opcode::Ret || I.op == Opcode::Br || I.op == Opcode::CBr || I.op == Opcode::SwitchI32);
}

unsigned countInstructions(const Function &F)
{
    unsigned n = 0;
    for (const auto &B : F.blocks)
        n += static_cast<unsigned>(B.instructions.size());
    return n;
}

std::string lookupValueName(const Function &F, unsigned id, const std::string &fallback)
{
    if (id < F.valueNames.size() && !F.valueNames[id].empty())
        return F.valueNames[id];
    return fallback;
}

void ensureValueName(Function &F, unsigned id, const std::string &name)
{
    if (name.empty())
        return;
    if (F.valueNames.size() <= id)
        F.valueNames.resize(id + 1);
    F.valueNames[id] = name;
}

InlineCost evaluateInlineCost(const Function &fn, const viper::analysis::CallGraph &cg)
{
    InlineCost cost;
    cost.instrCount = countInstructions(fn);
    cost.blockCount = static_cast<unsigned>(fn.blocks.size());

    auto callIt = cg.callCounts.find(fn.name);
    if (callIt != cg.callCounts.end())
        cost.callSites = callIt->second;

    auto edgeIt = cg.edges.find(fn.name);
    if (edgeIt != cg.edges.end())
    {
        for (const auto &target : edgeIt->second)
        {
            if (target == fn.name)
            {
                cost.recursive = true;
                break;
            }
        }
    }

    if (fn.blocks.empty())
    {
        cost.unsupportedCFG = true;
        return cost;
    }

    if (!fn.blocks.front().params.empty())
        cost.unsupportedCFG = true; // entry params unsupported

    for (const auto &B : fn.blocks)
    {
        if (!B.terminated || B.instructions.empty())
        {
            cost.unsupportedCFG = true;
            continue;
        }

        const Instr &term = B.instructions.back();
        if (hasUnsupportedTerminator(term))
            cost.unsupportedCFG = true;

        if (term.op == Opcode::Ret)
        {
            cost.hasReturn = true;
            bool expectValue = fn.retType.kind != Type::Kind::Void;
            bool hasValue = !term.operands.empty();
            if (expectValue != hasValue)
                cost.unsupportedCFG = true;
        }

        for (const auto &I : B.instructions)
        {
            if (isEHSensitive(I))
                cost.hasEH = true;
        }
    }

    return cost;
}

std::string makeUniqueLabel(const Function &function, const std::string &base)
{
    std::string candidate = base;
    unsigned suffix = 0;
    auto labelExists = [&](const std::string &label)
    {
        for (const auto &block : function.blocks)
        {
            if (block.label == label)
                return true;
        }
        return false;
    };

    while (labelExists(candidate))
    {
        candidate = base + "." + std::to_string(++suffix);
    }
    return candidate;
}

Value remapValue(const Value &v, const std::unordered_map<unsigned, Value> &map)
{
    if (v.kind != Value::Kind::Temp)
        return v;
    auto it = map.find(v.id);
    if (it == map.end())
        return v;
    return it->second;
}

void replaceUsesInBlock(BasicBlock &block, unsigned from, const Value &replacement)
{
    for (auto &instr : block.instructions)
    {
        for (auto &op : instr.operands)
        {
            if (op.kind == Value::Kind::Temp && op.id == from)
                op = replacement;
        }

        for (auto &argList : instr.brArgs)
        {
            for (auto &arg : argList)
            {
                if (arg.kind == Value::Kind::Temp && arg.id == from)
                    arg = replacement;
            }
        }
    }
}

bool inlineCallSite(Function &caller,
                    size_t callBlockIdx,
                    size_t callIndex,
                    const Function &callee,
                    unsigned callDepth,
                    unsigned maxDepth,
                    BlockDepthMap &depths)
{
    if (callDepth >= maxDepth)
        return false;

    if (callBlockIdx >= caller.blocks.size())
        return false;

    caller.blocks.reserve(caller.blocks.size() + callee.blocks.size() + 1);

    BasicBlock &callBlock = caller.blocks[callBlockIdx];
    if (callIndex >= callBlock.instructions.size())
        return false;
    const Instr &callInstr = callBlock.instructions[callIndex];

    if (callInstr.operands.size() != callee.params.size())
        return false;

    if (callee.retType.kind != callInstr.type.kind)
        return false;

    bool returnsValue = callee.retType.kind != Type::Kind::Void;
    if (!returnsValue && callInstr.result)
        return false;

    unsigned nextId = viper::il::nextTempId(caller);

    // Value mapping from callee temps/params to caller values.
    std::unordered_map<unsigned, Value> valueMap;
    valueMap.reserve(callee.params.size() + callee.blocks.size() * 2);
    for (size_t i = 0; i < callee.params.size(); ++i)
        valueMap.emplace(callee.params[i].id, callInstr.operands[i]);

    // Build label map for cloned blocks.
    std::unordered_map<std::string, std::string> labelMap;
    labelMap.reserve(callee.blocks.size());
    for (const auto &B : callee.blocks)
    {
        std::string base = callBlock.label + ".inline." + callee.name + "." + B.label;
        labelMap.emplace(B.label, makeUniqueLabel(caller, base));
    }

    // Build continuation block from instructions after the call.
    BasicBlock continuation;
    continuation.label = makeUniqueLabel(caller, callBlock.label + ".inline.cont");
    continuation.instructions.assign(callBlock.instructions.begin() + static_cast<long>(callIndex + 1),
                                     callBlock.instructions.end());
    continuation.terminated = callBlock.terminated;

    // Remove the call and trailing instructions from the original block.
    callBlock.instructions.resize(callIndex);
    callBlock.terminated = false;

    // Replace the call result with a continuation parameter when needed.
    if (returnsValue && callInstr.result)
    {
        Param retParam;
        retParam.name = lookupValueName(caller, *callInstr.result, "ret");
        retParam.id = nextId++;
        retParam.type = callee.retType;
        continuation.params.push_back(retParam);
        ensureValueName(caller, retParam.id, retParam.name);

        Value repl = Value::temp(retParam.id);
        viper::il::replaceAllUses(caller, *callInstr.result, repl);
        replaceUsesInBlock(continuation, *callInstr.result, repl);
    }

    // Clone callee blocks.
    std::vector<BasicBlock> clonedBlocks;
    clonedBlocks.reserve(callee.blocks.size());

    for (const auto &srcBlock : callee.blocks)
    {
        BasicBlock clone;
        clone.label = labelMap.at(srcBlock.label);

        // Clone block parameters with fresh IDs.
        clone.params.reserve(srcBlock.params.size());
        for (const auto &param : srcBlock.params)
        {
            Param p = param;
            p.id = nextId++;
            clone.params.push_back(p);
            valueMap[param.id] = Value::temp(p.id);
            ensureValueName(caller, p.id, lookupValueName(callee, param.id, param.name));
        }

        for (size_t idx = 0; idx < srcBlock.instructions.size(); ++idx)
        {
            const Instr &CI = srcBlock.instructions[idx];

            if (idx + 1 == srcBlock.instructions.size() && CI.op == Opcode::Ret)
            {
                Instr bridge;
                bridge.op = Opcode::Br;
                bridge.type = Type(Type::Kind::Void);
                bridge.labels.push_back(continuation.label);

                if (!continuation.params.empty())
                {
                    bridge.brArgs.emplace_back();
                    if (!CI.operands.empty())
                        bridge.brArgs.back().push_back(remapValue(CI.operands.front(), valueMap));
                }

                clone.instructions.push_back(std::move(bridge));
                clone.terminated = true;
                continue;
            }

            Instr cloned = CI;
            cloned.operands.clear();
            cloned.labels.clear();
            cloned.brArgs.clear();

            cloned.operands.reserve(CI.operands.size());
            for (const auto &op : CI.operands)
                cloned.operands.push_back(remapValue(op, valueMap));

            cloned.labels.reserve(CI.labels.size());
            for (const auto &lab : CI.labels)
                cloned.labels.push_back(labelMap.at(lab));

            cloned.brArgs.reserve(CI.brArgs.size());
            for (const auto &argList : CI.brArgs)
            {
                std::vector<Value> remapped;
                remapped.reserve(argList.size());
                for (const auto &arg : argList)
                    remapped.push_back(remapValue(arg, valueMap));
                cloned.brArgs.push_back(std::move(remapped));
            }

            if (CI.result)
            {
                cloned.result = nextId;
                valueMap[*CI.result] = Value::temp(nextId);
                ensureValueName(caller, nextId, lookupValueName(callee, *CI.result, ""));
                ++nextId;
            }

            clone.instructions.push_back(std::move(cloned));
        }

        if (!clone.terminated && !clone.instructions.empty())
            clone.terminated = clone.instructions.back().op == Opcode::Br ||
                               clone.instructions.back().op == Opcode::CBr ||
                               clone.instructions.back().op == Opcode::SwitchI32;

        clonedBlocks.push_back(std::move(clone));
    }

    // Branch from call site to cloned entry block.
    Instr jump;
    jump.op = Opcode::Br;
    jump.type = Type(Type::Kind::Void);
    jump.labels.push_back(labelMap.at(callee.blocks.front().label));
    callBlock.instructions.push_back(std::move(jump));
    callBlock.terminated = true;

    // Record depths for new blocks.
    for (auto &B : clonedBlocks)
    {
        setBlockDepth(depths, caller.name, B.label, callDepth + 1);
        caller.blocks.push_back(std::move(B));
    }

    setBlockDepth(depths, caller.name, continuation.label, callDepth);
    caller.blocks.push_back(std::move(continuation));

    return true;
}

} // namespace

std::string_view Inliner::id() const
{
    return "inline";
}

PreservedAnalyses Inliner::run(Module &module, AnalysisManager &)
{
    viper::analysis::CallGraph cg = viper::analysis::buildCallGraph(module);

    std::unordered_map<std::string, const Function *> functionLookup;
    std::unordered_map<std::string, InlineCost> costCache;

    functionLookup.reserve(module.functions.size());
    costCache.reserve(module.functions.size());

    for (const auto &fn : module.functions)
    {
        functionLookup.emplace(fn.name, &fn);
        costCache.emplace(fn.name, evaluateInlineCost(fn, cg));
    }

    BlockDepthMap depths;
    for (const auto &fn : module.functions)
        for (const auto &B : fn.blocks)
            setBlockDepth(depths, fn.name, B.label, 0);

    bool changed = false;

    for (size_t fnIdx = 0; fnIdx < module.functions.size(); ++fnIdx)
    {
        Function &caller = module.functions[fnIdx];
        for (size_t blockIdx = 0; blockIdx < caller.blocks.size(); ++blockIdx)
        {
            BasicBlock &block = caller.blocks[blockIdx];
            size_t instIdx = 0;
            while (instIdx < block.instructions.size())
            {
                const Instr &I = block.instructions[instIdx];
                if (!isDirectCall(I))
                {
                    ++instIdx;
                    continue;
                }

                auto calleeIt = functionLookup.find(I.callee);
                if (calleeIt == functionLookup.end())
                {
                    ++instIdx;
                    continue;
                }
                const Function *callee = calleeIt->second;
                if (callee->name == caller.name)
                {
                    ++instIdx;
                    continue;
                }

                auto edgeIt = cg.edges.find(callee->name);
                if (edgeIt != cg.edges.end() &&
                    std::find(edgeIt->second.begin(), edgeIt->second.end(), caller.name) != edgeIt->second.end())
                {
                    ++instIdx;
                    continue;
                }

                const InlineCost &cost = costCache.at(callee->name);
                if (!cost.withinBudget(instrThreshold_, blockBudget_))
                {
                    ++instIdx;
                    continue;
                }

                unsigned depth = getBlockDepth(depths, caller.name, block.label);
                if (!inlineCallSite(caller, blockIdx, instIdx, *callee, depth, maxInlineDepth_, depths))
                {
                    ++instIdx;
                    continue;
                }

                changed = true;
                break; // block reshaped; move to next block
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();
    return PreservedAnalyses{}; // invalidate all analyses for simplicity
}

void registerInlinePass(PassRegistry &registry)
{
    registry.registerModulePass("inline",
                                [](core::Module &module, AnalysisManager &analysis)
                                {
                                    Inliner inliner;
                                    return inliner.run(module, analysis);
                                });
}

} // namespace il::transform
