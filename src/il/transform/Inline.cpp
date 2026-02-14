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

#include "il/utils/UseDefInfo.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
constexpr unsigned kMaxCallSites = 8;
constexpr char kDepthKeySep = '\0';

using BlockDepthMap = std::unordered_map<std::string, unsigned>;

struct InlineCost
{
    unsigned instrCount = 0;
    unsigned blockCount = 0;
    unsigned callSites = 0;
    unsigned nestedCalls = 0; // Number of calls within this function
    unsigned returnCount = 0; // Number of return statements
    bool recursive = false;
    bool hasEH = false;
    bool unsupportedCFG = false;
    bool hasReturn = false;

    /// @brief Check if within basic structural constraints.
    bool isInlinable() const
    {
        return !recursive && !hasEH && !unsupportedCFG && hasReturn;
    }

    /// @brief Compute adjusted cost considering bonuses.
    int adjustedCost(const InlineCostConfig &config, unsigned constArgCount) const
    {
        if (!isInlinable())
            return INT_MAX;

        int cost = static_cast<int>(instrCount);

        // Apply bonuses
        if (callSites == 1)
            cost -= static_cast<int>(config.singleUseBonus);

        if (instrCount <= 8)
            cost -= static_cast<int>(config.tinyFunctionBonus);

        // Constant arguments enable optimization
        cost -= static_cast<int>(constArgCount * config.constArgBonus);

        // Penalty for functions with many nested calls (may cause code explosion)
        cost += static_cast<int>(nestedCalls * 2);

        // Multiple returns are slightly more expensive to inline
        if (returnCount > 1)
            cost += static_cast<int>((returnCount - 1) * 2);

        return cost;
    }

    bool withinBudget(const InlineCostConfig &config, unsigned constArgCount) const
    {
        if (!isInlinable())
            return false;
        if (blockCount > config.blockBudget)
            return false;
        if (callSites > kMaxCallSites)
            return false;

        int cost = adjustedCost(config, constArgCount);
        return cost <= static_cast<int>(config.instrThreshold);
    }
};

/// @brief Build a composite key for the block-depth map.
/// @details Concatenates the function name and block label with a NUL separator
///          so that no valid identifier can collide with the combined key.
/// @param fn Function name prefix.
/// @param label Block label suffix.
/// @return Composite key suitable for BlockDepthMap lookups.
std::string depthKey(const std::string &fn, const std::string &label)
{
    return fn + kDepthKeySep + label;
}

/// @brief Query the inline depth recorded for a specific block.
/// @param depths Map of (function+label) → depth values.
/// @param fn Owning function name.
/// @param label Block label within the function.
/// @return Recorded depth, or 0 if no entry exists.
unsigned getBlockDepth(const BlockDepthMap &depths, const std::string &fn, const std::string &label)
{
    auto it = depths.find(depthKey(fn, label));
    if (it == depths.end())
        return 0;
    return it->second;
}

/// @brief Record the inline depth for a specific block.
/// @param depths Map of (function+label) → depth values (modified in-place).
/// @param fn Owning function name.
/// @param label Block label within the function.
/// @param depth Inline nesting depth to store.
void setBlockDepth(BlockDepthMap &depths,
                   const std::string &fn,
                   const std::string &label,
                   unsigned depth)
{
    depths[depthKey(fn, label)] = depth;
}

/// @brief Test whether an instruction is a direct (non-indirect) call.
/// @param I Instruction to inspect.
/// @return True when the opcode is Call and a callee name is present.
bool isDirectCall(const Instr &I)
{
    return I.op == Opcode::Call && !I.callee.empty();
}

/// @brief Test whether an instruction is part of the exception-handling framework.
/// @param I Instruction to inspect.
/// @return True for EhPush, EhPop, EhEntry, ResumeSame, ResumeNext, ResumeLabel.
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

/// @brief Test whether a terminator instruction is unsupported for inlining.
/// @details The inliner only handles Ret, Br, CBr, and SwitchI32 terminators.
/// @param I Terminator instruction to check.
/// @return True when the terminator cannot be inlined.
bool hasUnsupportedTerminator(const Instr &I)
{
    return !(I.op == Opcode::Ret || I.op == Opcode::Br || I.op == Opcode::CBr ||
             I.op == Opcode::SwitchI32);
}

/// @brief Count the total number of instructions across all blocks of a function.
/// @param F Function to measure.
/// @return Sum of instruction counts in every block.
unsigned countInstructions(const Function &F)
{
    unsigned n = 0;
    for (const auto &B : F.blocks)
        n += static_cast<unsigned>(B.instructions.size());
    return n;
}

/// @brief Look up the debug name for an SSA value, falling back to a default.
/// @param F Function whose valueNames table is queried.
/// @param id SSA value identifier.
/// @param fallback String returned when no name is recorded for @p id.
/// @return The stored name if present and non-empty; otherwise @p fallback.
std::string lookupValueName(const Function &F, unsigned id, const std::string &fallback)
{
    if (id < F.valueNames.size() && !F.valueNames[id].empty())
        return F.valueNames[id];
    return fallback;
}

/// @brief Record a debug name for an SSA value, growing the table if needed.
/// @param F Function whose valueNames table is modified.
/// @param id SSA value identifier.
/// @param name Name to associate; empty names are silently ignored.
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
            ++cost.returnCount;
            bool expectValue = fn.retType.kind != Type::Kind::Void;
            bool hasValue = !term.operands.empty();
            if (expectValue != hasValue)
                cost.unsupportedCFG = true;
        }

        for (const auto &I : B.instructions)
        {
            if (isEHSensitive(I))
                cost.hasEH = true;

            // Count nested calls
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
                ++cost.nestedCalls;
        }
    }

    return cost;
}

/// @brief Count constant arguments in a call instruction.
unsigned countConstantArgs(const Instr &callInstr)
{
    unsigned count = 0;
    for (const auto &op : callInstr.operands)
    {
        if (op.kind == Value::Kind::ConstInt || op.kind == Value::Kind::ConstFloat ||
            op.kind == Value::Kind::NullPtr || op.kind == Value::Kind::ConstStr)
        {
            ++count;
        }
    }
    return count;
}

/// @brief Generate a block label that does not collide with existing labels.
/// @details Builds an unordered_set of existing labels for O(1) collision checks,
///          then appends increasing numeric suffixes until a unique name is found.
///          This replaces a previous O(n) linear scan per candidate, improving
///          performance from O(n*k) to O(n+k) where n = block count, k = attempts.
/// @param function Function whose blocks define the label namespace.
/// @param base Desired label prefix; returned as-is when no collision occurs.
/// @return A label guaranteed to be unique within @p function.
std::string makeUniqueLabel(const Function &function, const std::string &base)
{
    std::unordered_set<std::string> existingLabels;
    existingLabels.reserve(function.blocks.size());
    for (const auto &block : function.blocks)
        existingLabels.insert(block.label);

    std::string candidate = base;
    unsigned suffix = 0;
    while (existingLabels.count(candidate))
    {
        candidate = base + "." + std::to_string(++suffix);
    }
    return candidate;
}

/// @brief Remap a temporary value through a substitution map.
/// @param v The value to remap.
/// @param map Mapping from old temporary IDs to replacement values.
/// @return The replacement value if \p v is a temporary found in \p map,
///         otherwise \p v unchanged.
Value remapValue(const Value &v, const std::unordered_map<unsigned, Value> &map)
{
    if (v.kind != Value::Kind::Temp)
        return v;
    auto it = map.find(v.id);
    if (it == map.end())
        return v;
    return it->second;
}

/// @brief Replace all uses of a temporary in a basic block.
/// @details Scans every instruction operand and branch argument in \p block,
///          replacing any temporary whose ID matches \p from with \p replacement.
/// @param block The basic block to rewrite.
/// @param from  The temporary ID to search for.
/// @param replacement The value to substitute in place of the old temporary.
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
    continuation.instructions.assign(callBlock.instructions.begin() +
                                         static_cast<long>(callIndex + 1),
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
        viper::il::UseDefInfo useInfo(caller);
        useInfo.replaceAllUses(*callInstr.result, repl);
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

    unsigned codeGrowth = 0;

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
                    std::find(edgeIt->second.begin(), edgeIt->second.end(), caller.name) !=
                        edgeIt->second.end())
                {
                    ++instIdx;
                    continue;
                }

                // Check code growth budget
                const InlineCost &cost = costCache.at(callee->name);
                if (codeGrowth + cost.instrCount > config_.maxCodeGrowth)
                {
                    ++instIdx;
                    continue;
                }

                // Use enhanced cost model with constant argument bonuses
                unsigned constArgs = countConstantArgs(I);
                if (!cost.withinBudget(config_, constArgs))
                {
                    ++instIdx;
                    continue;
                }

                unsigned depth = getBlockDepth(depths, caller.name, block.label);
                if (!inlineCallSite(
                        caller, blockIdx, instIdx, *callee, depth, config_.maxInlineDepth, depths))
                {
                    ++instIdx;
                    continue;
                }

                // Track code growth (callee instructions minus the call itself)
                if (cost.instrCount > 1)
                    codeGrowth += cost.instrCount - 1;

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
