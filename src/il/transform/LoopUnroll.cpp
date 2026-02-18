//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a conservative loop unrolling pass that fully unrolls small
// constant-bound loops. The pass identifies loops with a single latch, single
// exit, and a recognizable trip count pattern, then replicates the loop body
// with proper SSA value threading.
//
// The implementation focuses on correctness over aggressive optimization,
// limiting unrolling to loops that meet strict structural requirements.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LoopUnroll.hpp"

#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{

/// @brief Information about a simple counted loop.
struct CountedLoop
{
    /// Initial value of the induction variable.
    long long initValue = 0;
    /// Final value (exclusive) for the loop bound.
    long long endValue = 0;
    /// Step size per iteration.
    long long step = 1;
    /// Index of the induction variable in header params.
    size_t ivParamIndex = 0;
    /// Computed trip count.
    unsigned tripCount = 0;
};

/// @brief Find the preheader of a loop.
BasicBlock *findPreheader(Function &function,
                          const Loop &loop,
                          BasicBlock &header,
                          const CFGInfo &cfg,
                          const std::unordered_map<std::string, BasicBlock *> &blockMap)
{
    auto predsIt = cfg.predecessors.find(&header);
    if (predsIt == cfg.predecessors.end())
        return nullptr;

    BasicBlock *preheader = nullptr;
    for (const auto *pred : predsIt->second)
    {
        if (!pred || loop.contains(pred->label))
            continue;
        auto it = blockMap.find(pred->label);
        if (it == blockMap.end())
            continue;
        if (preheader && preheader != it->second)
            return nullptr; // Multiple preheaders
        preheader = it->second;
    }
    return preheader;
}

/// @brief Get the index of a label in a terminator's labels vector.
std::optional<size_t> labelIndex(const Instr &term, const std::string &target)
{
    for (size_t i = 0; i < term.labels.size(); ++i)
        if (term.labels[i] == target)
            return i;
    return std::nullopt;
}

/// @brief Analyze a loop to determine if it's a simple counted loop.
std::optional<CountedLoop> analyzeCountedLoop(
    Function &function,
    const Loop &loop,
    BasicBlock &header,
    BasicBlock &latch,
    BasicBlock *preheader,
    const std::unordered_map<std::string, BasicBlock *> &blockMap)
{
    // Require single latch
    if (loop.latchLabels.size() != 1)
        return std::nullopt;

    // Require single exit
    if (loop.exits.size() != 1)
        return std::nullopt;

    // Header must have a conditional branch
    if (header.instructions.empty())
        return std::nullopt;
    const Instr &headerTerm = header.instructions.back();
    if (headerTerm.op != Opcode::CBr)
        return std::nullopt;
    if (headerTerm.labels.size() != 2)
        return std::nullopt;

    // Identify which branch goes to exit vs stays in loop
    const std::string &exitTarget = loop.exits[0].to;
    size_t exitBranchIdx = 0;
    if (headerTerm.labels[0] == exitTarget)
        exitBranchIdx = 0;
    else if (headerTerm.labels[1] == exitTarget)
        exitBranchIdx = 1;
    else
        return std::nullopt; // Exit not from header

    // The condition must be a comparison involving a header param
    if (headerTerm.operands.empty())
        return std::nullopt;
    const Value &condVal = headerTerm.operands[0];
    if (condVal.kind != Value::Kind::Temp)
        return std::nullopt;

    // Find the comparison instruction
    const Instr *cmpInstr = nullptr;
    for (const auto &instr : header.instructions)
    {
        if (instr.result && *instr.result == condVal.id)
        {
            cmpInstr = &instr;
            break;
        }
    }
    if (!cmpInstr)
        return std::nullopt;

    // Must be a signed comparison
    Opcode cmpOp = cmpInstr->op;
    if (cmpOp != Opcode::SCmpLT && cmpOp != Opcode::SCmpLE && cmpOp != Opcode::SCmpGT &&
        cmpOp != Opcode::SCmpGE && cmpOp != Opcode::ICmpEq && cmpOp != Opcode::ICmpNe)
        return std::nullopt;

    if (cmpInstr->operands.size() != 2)
        return std::nullopt;

    // One operand should be a header param, the other a constant
    const Value &lhs = cmpInstr->operands[0];
    const Value &rhs = cmpInstr->operands[1];

    size_t ivParamIndex = 0;
    long long boundValue = 0;
    bool ivIsLhs = false;

    // Check if LHS is a header param and RHS is constant
    if (lhs.kind == Value::Kind::Temp && rhs.kind == Value::Kind::ConstInt)
    {
        bool found = false;
        for (size_t i = 0; i < header.params.size(); ++i)
        {
            if (header.params[i].id == lhs.id)
            {
                ivParamIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
            return std::nullopt;
        boundValue = rhs.i64;
        ivIsLhs = true;
    }
    else if (rhs.kind == Value::Kind::Temp && lhs.kind == Value::Kind::ConstInt)
    {
        bool found = false;
        for (size_t i = 0; i < header.params.size(); ++i)
        {
            if (header.params[i].id == rhs.id)
            {
                ivParamIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
            return std::nullopt;
        boundValue = lhs.i64;
        ivIsLhs = false;
    }
    else
    {
        return std::nullopt;
    }

    // Get initial value from preheader's branch args
    if (!preheader || preheader->instructions.empty())
        return std::nullopt;
    const Instr &phTerm = preheader->instructions.back();
    auto toHeaderIdx = labelIndex(phTerm, header.label);
    if (!toHeaderIdx || *toHeaderIdx >= phTerm.brArgs.size())
        return std::nullopt;
    const auto &initArgs = phTerm.brArgs[*toHeaderIdx];
    if (ivParamIndex >= initArgs.size())
        return std::nullopt;
    const Value &initVal = initArgs[ivParamIndex];
    if (initVal.kind != Value::Kind::ConstInt)
        return std::nullopt;
    long long initValue = initVal.i64;

    // Find the step in the latch
    if (latch.instructions.empty())
        return std::nullopt;
    const Instr &latchTerm = latch.instructions.back();
    auto toHeaderFromLatch = labelIndex(latchTerm, header.label);
    if (!toHeaderFromLatch || *toHeaderFromLatch >= latchTerm.brArgs.size())
        return std::nullopt;
    const auto &latchArgs = latchTerm.brArgs[*toHeaderFromLatch];
    if (ivParamIndex >= latchArgs.size())
        return std::nullopt;
    const Value &nextVal = latchArgs[ivParamIndex];
    if (nextVal.kind != Value::Kind::Temp)
        return std::nullopt;

    // Find instruction that produces nextVal
    const Instr *stepInstr = nullptr;
    for (const auto &instr : latch.instructions)
    {
        if (instr.result && *instr.result == nextVal.id)
        {
            stepInstr = &instr;
            break;
        }
    }
    // Also check header for step instruction
    if (!stepInstr)
    {
        for (const auto &instr : header.instructions)
        {
            if (instr.result && *instr.result == nextVal.id)
            {
                stepInstr = &instr;
                break;
            }
        }
    }
    if (!stepInstr)
        return std::nullopt;

    // Step instruction should be add/sub with the IV param
    long long step = 0;
    if (stepInstr->op == Opcode::Add || stepInstr->op == Opcode::IAddOvf)
    {
        if (stepInstr->operands.size() != 2)
            return std::nullopt;
        const Value &a = stepInstr->operands[0];
        const Value &b = stepInstr->operands[1];

        // Find which operand is the IV
        unsigned ivId = header.params[ivParamIndex].id;
        bool foundIv = false;

        // Check header->latch to find latch param that receives IV
        auto toLatchIdx = labelIndex(header.instructions.back(), latch.label);
        if (toLatchIdx && *toLatchIdx < header.instructions.back().brArgs.size())
        {
            const auto &argsToLatch = header.instructions.back().brArgs[*toLatchIdx];
            for (size_t i = 0; i < argsToLatch.size() && i < latch.params.size(); ++i)
            {
                if (argsToLatch[i].kind == Value::Kind::Temp && argsToLatch[i].id == ivId)
                {
                    ivId = latch.params[i].id;
                    break;
                }
            }
        }

        if (a.kind == Value::Kind::Temp && a.id == ivId && b.kind == Value::Kind::ConstInt)
        {
            step = b.i64;
            foundIv = true;
        }
        else if (b.kind == Value::Kind::Temp && b.id == ivId && a.kind == Value::Kind::ConstInt)
        {
            step = a.i64;
            foundIv = true;
        }
        if (!foundIv)
            return std::nullopt;
    }
    else if (stepInstr->op == Opcode::Sub || stepInstr->op == Opcode::ISubOvf)
    {
        if (stepInstr->operands.size() != 2)
            return std::nullopt;
        const Value &a = stepInstr->operands[0];
        const Value &b = stepInstr->operands[1];

        unsigned ivId = header.params[ivParamIndex].id;
        auto toLatchIdx = labelIndex(header.instructions.back(), latch.label);
        if (toLatchIdx && *toLatchIdx < header.instructions.back().brArgs.size())
        {
            const auto &argsToLatch = header.instructions.back().brArgs[*toLatchIdx];
            for (size_t i = 0; i < argsToLatch.size() && i < latch.params.size(); ++i)
            {
                if (argsToLatch[i].kind == Value::Kind::Temp && argsToLatch[i].id == ivId)
                {
                    ivId = latch.params[i].id;
                    break;
                }
            }
        }

        if (a.kind == Value::Kind::Temp && a.id == ivId && b.kind == Value::Kind::ConstInt)
        {
            step = -b.i64;
        }
        else
        {
            return std::nullopt;
        }
    }
    else
    {
        return std::nullopt;
    }

    if (step == 0)
        return std::nullopt;

    // Compute trip count based on comparison and exit branch
    // exitBranchIdx tells us which branch exits: 0 = true branch exits, 1 = false branch exits
    // The loop continues when the condition is true (if exitBranchIdx == 1) or false (if
    // exitBranchIdx == 0)
    bool loopWhileTrue = (exitBranchIdx == 1);

    unsigned tripCount = 0;

    // Simulate the loop to count iterations (conservative approach)
    long long iv = initValue;
    const unsigned maxIterations = 1000; // Safety limit
    for (unsigned iter = 0; iter < maxIterations; ++iter)
    {
        bool condResult = false;
        long long left = ivIsLhs ? iv : boundValue;
        long long right = ivIsLhs ? boundValue : iv;

        switch (cmpOp)
        {
            case Opcode::SCmpLT:
                condResult = (left < right);
                break;
            case Opcode::SCmpLE:
                condResult = (left <= right);
                break;
            case Opcode::SCmpGT:
                condResult = (left > right);
                break;
            case Opcode::SCmpGE:
                condResult = (left >= right);
                break;
            case Opcode::ICmpEq:
                condResult = (left == right);
                break;
            case Opcode::ICmpNe:
                condResult = (left != right);
                break;
            default:
                return std::nullopt;
        }

        // Loop continues if (condResult == loopWhileTrue)
        if (condResult != loopWhileTrue)
        {
            tripCount = iter;
            break;
        }

        iv += step;

        if (iter == maxIterations - 1)
            return std::nullopt; // Too many iterations
    }

    if (tripCount == 0)
        return std::nullopt;

    CountedLoop result;
    result.initValue = initValue;
    result.endValue = boundValue;
    result.step = step;
    result.ivParamIndex = ivParamIndex;
    result.tripCount = tripCount;
    return result;
}

/// @brief Count instructions in a loop.
size_t countLoopInstructions(const Loop &loop,
                             Function &function,
                             const std::unordered_map<std::string, BasicBlock *> &blockMap)
{
    size_t count = 0;
    for (const auto &label : loop.blockLabels)
    {
        auto it = blockMap.find(label);
        if (it != blockMap.end())
            count += it->second->instructions.size();
    }
    return count;
}

/// @brief Fully unroll a simple loop.
bool fullyUnrollLoop(Function &function,
                     const Loop &loop,
                     BasicBlock &header,
                     BasicBlock &latch,
                     BasicBlock *preheader,
                     const CountedLoop &counted,
                     std::unordered_map<std::string, BasicBlock *> &blockMap)
{
    // For full unrolling, we:
    // 1. Clone the loop body (tripCount) times into the preheader
    // 2. Thread values from each iteration to the next
    // 3. Redirect preheader to the exit block with final values
    // 4. Remove the original loop blocks

    // This is a simplified implementation for single-block loops
    // (header and latch are the same block or latch immediately follows header)

    if (loop.blockLabels.size() > 2)
        return false; // Only handle simple loops

    // Get the exit target and its arguments
    const std::string &exitLabel = loop.exits[0].to;
    auto exitBlockIt = blockMap.find(exitLabel);
    if (exitBlockIt == blockMap.end())
        return false;

    const Instr &headerTerm = header.instructions.back();
    if (headerTerm.op != Opcode::CBr || headerTerm.labels.size() != 2)
        return false;

    // Find which branch index goes to exit
    size_t exitBranchIdx = (headerTerm.labels[0] == exitLabel) ? 0 : 1;

    // Get exit branch args (what values to pass when exiting)
    std::vector<Value> exitArgs;
    if (!headerTerm.brArgs.empty() && exitBranchIdx < headerTerm.brArgs.size())
        exitArgs = headerTerm.brArgs[exitBranchIdx];

    // Get the body instructions (everything except terminator)
    std::vector<Instr> bodyInstrs;
    for (size_t i = 0; i < header.instructions.size() - 1; ++i)
        bodyInstrs.push_back(header.instructions[i]);

    // Include latch body if different from header
    if (latch.label != header.label)
    {
        for (size_t i = 0; i < latch.instructions.size() - 1; ++i)
            bodyInstrs.push_back(latch.instructions[i]);
    }

    // Current values for header params (start with initial values from preheader)
    const Instr &phTerm = preheader->instructions.back();
    auto toHeaderIdx = labelIndex(phTerm, header.label);
    if (!toHeaderIdx || *toHeaderIdx >= phTerm.brArgs.size())
        return false;

    std::vector<Value> currentValues = phTerm.brArgs[*toHeaderIdx];
    if (currentValues.size() != header.params.size())
        return false;

    // Find insertion point (before preheader terminator)
    size_t insertIdx = preheader->instructions.size() - 1;

    unsigned nextId = viper::il::nextTempId(function);

    // Build value map: original temp id -> current value
    std::unordered_map<unsigned, Value> valueMap;

    // Unroll the loop
    for (unsigned iter = 0; iter < counted.tripCount; ++iter)
    {
        // Map header params to current iteration values
        valueMap.clear();
        for (size_t i = 0; i < header.params.size(); ++i)
            valueMap[header.params[i].id] = currentValues[i];

        // If latch is separate, also map latch params
        if (latch.label != header.label)
        {
            // Find args from header to latch
            auto toLatchIdx = labelIndex(header.instructions.back(), latch.label);
            if (toLatchIdx && *toLatchIdx < header.instructions.back().brArgs.size())
            {
                const auto &argsToLatch = headerTerm.brArgs[*toLatchIdx];
                for (size_t i = 0; i < latch.params.size() && i < argsToLatch.size(); ++i)
                {
                    Value mappedVal = argsToLatch[i];
                    if (mappedVal.kind == Value::Kind::Temp)
                    {
                        auto it = valueMap.find(mappedVal.id);
                        if (it != valueMap.end())
                            mappedVal = it->second;
                    }
                    valueMap[latch.params[i].id] = mappedVal;
                }
            }
        }

        // Clone and insert body instructions
        for (const Instr &orig : bodyInstrs)
        {
            Instr cloned = orig;

            // Remap operands
            for (Value &op : cloned.operands)
            {
                if (op.kind == Value::Kind::Temp)
                {
                    auto it = valueMap.find(op.id);
                    if (it != valueMap.end())
                        op = it->second;
                }
            }

            // Allocate new result ID if needed
            if (cloned.result)
            {
                unsigned oldId = *cloned.result;
                unsigned newId = nextId++;
                cloned.result = newId;
                valueMap[oldId] = Value::temp(newId);
            }

            preheader->instructions.insert(
                preheader->instructions.begin() + static_cast<long>(insertIdx), std::move(cloned));
            ++insertIdx;
        }

        // Compute next iteration values
        // Get values that would be passed back to header
        const Instr &latchTerm = latch.instructions.back();
        auto toHeaderFromLatch = labelIndex(latchTerm, header.label);
        if (toHeaderFromLatch && *toHeaderFromLatch < latchTerm.brArgs.size())
        {
            const auto &nextArgs = latchTerm.brArgs[*toHeaderFromLatch];
            for (size_t i = 0; i < nextArgs.size() && i < currentValues.size(); ++i)
            {
                Value nextVal = nextArgs[i];
                if (nextVal.kind == Value::Kind::Temp)
                {
                    auto it = valueMap.find(nextVal.id);
                    if (it != valueMap.end())
                        nextVal = it->second;
                }
                currentValues[i] = nextVal;
            }
        }
    }

    // Map final exit args
    for (Value &arg : exitArgs)
    {
        if (arg.kind == Value::Kind::Temp)
        {
            auto it = valueMap.find(arg.id);
            if (it != valueMap.end())
                arg = it->second;
        }
    }

    // Update preheader terminator to branch to exit
    Instr &newTerm = preheader->instructions.back();
    newTerm.op = Opcode::Br;
    newTerm.labels = {exitLabel};
    newTerm.operands.clear();
    newTerm.brArgs = {exitArgs};

    // Remove original loop blocks from function
    std::unordered_set<std::string> loopBlockLabels(loop.blockLabels.begin(),
                                                    loop.blockLabels.end());
    function.blocks.erase(std::remove_if(function.blocks.begin(),
                                         function.blocks.end(),
                                         [&](const BasicBlock &b)
                                         { return loopBlockLabels.count(b.label) > 0; }),
                          function.blocks.end());

    return true;
}

} // namespace

std::string_view LoopUnroll::id() const
{
    return "loop-unroll";
}

PreservedAnalyses LoopUnroll::run(Function &function, AnalysisManager &analysis)
{
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>(kAnalysisLoopInfo, function);
    auto &cfg = analysis.getFunctionResult<CFGInfo>(kAnalysisCFG, function);
    (void)analysis.getFunctionResult<viper::analysis::DomTree>(kAnalysisDominators, function);

    std::unordered_map<std::string, BasicBlock *> blockMap;
    blockMap.reserve(function.blocks.size());
    for (auto &blk : function.blocks)
        blockMap.emplace(blk.label, &blk);

    bool changed = false;

    // Process loops from innermost to outermost (reverse order in LoopInfo)
    // LoopInfo stores loops with innermost first
    for (const Loop &loop : loopInfo.loops())
    {
        // Skip nested loops (only unroll innermost)
        if (!loop.childHeaders.empty())
            continue;

        BasicBlock *header = blockMap[loop.headerLabel];
        if (!header)
            continue;

        if (loop.latchLabels.size() != 1)
            continue;

        BasicBlock *latch = blockMap[loop.latchLabels[0]];
        if (!latch)
            continue;

        BasicBlock *preheader = findPreheader(function, loop, *header, cfg, blockMap);
        if (!preheader)
            continue;

        // Check loop size
        size_t loopSize = countLoopInstructions(loop, function, blockMap);
        if (loopSize > config_.maxLoopSize)
            continue;

        // Analyze for counted loop pattern
        auto counted = analyzeCountedLoop(function, loop, *header, *latch, preheader, blockMap);
        if (!counted)
            continue;

        // Check trip count threshold for full unrolling
        if (counted->tripCount > config_.fullUnrollThreshold)
            continue;

        // Attempt full unroll
        if (fullyUnrollLoop(function, loop, *header, *latch, preheader, *counted, blockMap))
        {
            changed = true;
            // Rebuild block map after modification
            blockMap.clear();
            for (auto &blk : function.blocks)
                blockMap.emplace(blk.label, &blk);
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    // Invalidate most analyses since we modified CFG structure
    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    return preserved;
}

void registerLoopUnrollPass(PassRegistry &registry)
{
    registry.registerFunctionPass("loop-unroll", []() { return std::make_unique<LoopUnroll>(); });
}

} // namespace il::transform
