//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a conservative, single-block direct-call inliner. Only small,
// non-recursive functions with one block and a `ret` terminator are inlined.
// Callee parameters are mapped to call operands, and fresh SSA temps are
// assigned for callee results in the caller.
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

#include <unordered_map>

using namespace il::core;

namespace il::transform
{

namespace
{
const Function *findFunctionByName(const Module &M, const std::string &name)
{
    for (const auto &fn : M.functions)
        if (fn.name == name)
            return &fn;
    return nullptr;
}

bool isDirectCall(const Instr &I)
{
    return I.op == Opcode::Call && !I.callee.empty();
}

unsigned countInstructions(const Function &F)
{
    unsigned n = 0;
    for (const auto &B : F.blocks)
        n += static_cast<unsigned>(B.instructions.size());
    return n;
}

bool inlineOneCall(Function &caller, BasicBlock &B, size_t callIndex, const Function &callee)
{
    // Capture call instruction data before any modifications, since inserting
    // instructions into the vector may invalidate references.
    std::optional<unsigned> callResultId;
    std::vector<Value> callOperands;
    {
        const Instr &callI = B.instructions[callIndex];
        callResultId = callI.result;
        callOperands = callI.operands;
    }

    // Only inline 1-block functions with a ret terminator.
    if (callee.blocks.size() != 1)
        return false;
    const BasicBlock &cb = callee.blocks.front();
    if (!cb.terminated || cb.instructions.empty())
        return false;
    const Instr &cTerm = cb.instructions.back();
    if (cTerm.op != Opcode::Ret)
        return false;
    if (!cb.params.empty())
        return false; // avoid phi-like params

    // Parameter mapping
    if (callOperands.size() != callee.params.size())
        return false;
    std::unordered_map<unsigned, Value> valueMap; // callee temp id -> caller value
    for (size_t i = 0; i < callee.params.size(); ++i)
    {
        valueMap[callee.params[i].id] = callOperands[i];
    }

    // Prepare to clone all non-terminator instructions
    unsigned nextId = viper::il::nextTempId(caller);
    auto remapValue = [&](const Value &v) -> Value
    {
        if (v.kind != Value::Kind::Temp)
            return v;
        auto it = valueMap.find(v.id);
        if (it != valueMap.end())
            return it->second;
        return v; // shouldn't happen for single-block simple callees
    };

    // Insert cloned instructions before the call site
    size_t insertPos = callIndex;
    for (size_t i = 0; i + 1 < cb.instructions.size(); ++i)
    {
        const Instr &CI = cb.instructions[i];
        Instr clone;
        clone.op = CI.op;
        clone.type = CI.type;
        clone.callee = CI.callee;
        clone.CallAttr = CI.CallAttr;
        clone.labels = CI.labels; // should be empty for single-block
        clone.brArgs = CI.brArgs; // should be empty for single-block
        clone.operands.reserve(CI.operands.size());
        for (const auto &op : CI.operands)
            clone.operands.push_back(remapValue(op));
        if (CI.result)
        {
            clone.result = nextId;
            valueMap[*CI.result] = Value::temp(nextId);
            ++nextId;
        }
        B.instructions.insert(B.instructions.begin() + insertPos, std::move(clone));
        ++insertPos;
    }

    // Handle return value mapping if any
    Value retVal = Value::constInt(0);
    bool hasRetVal = !cTerm.operands.empty();
    if (hasRetVal)
    {
        retVal = remapValue(cTerm.operands.front());
    }

    // Replace uses of call result and erase the call.
    // Note: after inserting cloned instructions before callIndex, the call has
    // shifted to position insertPos.
    if (callResultId && hasRetVal)
    {
        viper::il::replaceAllUses(caller, *callResultId, retVal);
    }
    // Remove the call instruction itself (now at insertPos after the insertions)
    B.instructions.erase(B.instructions.begin() + static_cast<long>(insertPos));

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

    // Build a set of inlineable callees based on heuristics
    std::unordered_map<std::string, const Function *> candidates;
    for (const auto &fn : module.functions)
    {
        // Skip recursive by name
        bool selfCalls = false;
        auto itEdges = cg.edges.find(fn.name);
        if (itEdges != cg.edges.end())
        {
            for (const auto &calleeName : itEdges->second)
            {
                if (calleeName == fn.name)
                {
                    selfCalls = true;
                    break;
                }
            }
        }
        if (selfCalls)
            continue;

        unsigned ic = countInstructions(fn);
        if (ic > instrThreshold_)
            continue;

        // Prefer small functions; if called too many times, skip to limit code growth
        unsigned calls = 0u;
        auto ctIt = cg.callCounts.find(fn.name);
        if (ctIt != cg.callCounts.end())
            calls = ctIt->second;
        if (calls > 3)
            continue;

        // Must be single-block and end in ret to be safe for our simple inliner
        if (fn.blocks.size() != 1)
            continue;
        const auto &B = fn.blocks.front();
        if (!B.terminated || B.instructions.empty())
            continue;
        if (B.instructions.back().op != Opcode::Ret)
            continue;

        candidates[fn.name] = &fn;
    }

    bool changed = false;

    for (auto &caller : module.functions)
    {
        for (auto &B : caller.blocks)
        {
            for (size_t idx = 0; idx < B.instructions.size();)
            {
                Instr &I = B.instructions[idx];
                if (!isDirectCall(I))
                {
                    ++idx;
                    continue;
                }

                // Find callee definition
                auto it = candidates.find(I.callee);
                if (it == candidates.end())
                {
                    ++idx;
                    continue;
                }
                const Function *callee = it->second;

                // Don't inline recursion or mutually recursive via name check here
                if (callee->name == caller.name)
                {
                    ++idx;
                    continue;
                }

                bool ok = inlineOneCall(caller, B, idx, *callee);
                if (ok)
                {
                    changed = true;
                    // Do not advance idx; we removed the call. Next instruction is after cloned
                    // body
                    continue;
                }
                ++idx;
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
