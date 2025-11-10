//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a conservative loop canonicalisation pass.  The pass ensures each
// natural loop has a dedicated preheader and optionally merges multiple trivial
// latches into a single forwarding block so downstream analyses observe a
// predictable structure.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LoopSimplify.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{
struct IncomingEdge
{
    BasicBlock *pred;
    size_t edgeIdx;
};

Instr *getTerminator(BasicBlock &block)
{
    if (!block.terminated || block.instructions.empty())
        return nullptr;
    return &block.instructions.back();
}

const Instr *getTerminator(const BasicBlock &block)
{
    if (!block.terminated || block.instructions.empty())
        return nullptr;
    return &block.instructions.back();
}

BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

unsigned nextTempId(Function &function)
{
    unsigned next = 0;
    auto update = [&](unsigned id) { next = std::max(next, id + 1); };
    for (auto &param : function.params)
        update(param.id);
    for (auto &block : function.blocks)
    {
        for (auto &param : block.params)
            update(param.id);
        for (auto &instr : block.instructions)
        {
            if (instr.result)
                update(*instr.result);
            for (auto &operand : instr.operands)
                if (operand.kind == Value::Kind::Temp)
                    update(operand.id);
            for (auto &argList : instr.brArgs)
                for (auto &arg : argList)
                    if (arg.kind == Value::Kind::Temp)
                        update(arg.id);
        }
    }
    return next;
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

bool valuesEqual(const Value &lhs, const Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;
    switch (lhs.kind)
    {
        case Value::Kind::Temp:
            return lhs.id == rhs.id;
        case Value::Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case Value::Kind::ConstFloat:
            return lhs.f64 == rhs.f64;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case Value::Kind::NullPtr:
            return true;
    }
    return false;
}

bool valueVectorsEqual(const std::vector<Value> &lhs, const std::vector<Value> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (!valuesEqual(lhs[i], rhs[i]))
            return false;
    }
    return true;
}

bool ensurePreheader(Function &function, const Loop &loop)
{
    BasicBlock *header = findBlock(function, loop.headerLabel);
    if (!header)
        return false;

    std::vector<IncomingEdge> outsideEdges;
    for (auto &block : function.blocks)
    {
        Instr *term = getTerminator(block);
        if (!term)
            continue;
        for (size_t idx = 0; idx < term->labels.size(); ++idx)
        {
            if (term->labels[idx] != header->label)
                continue;
            if (loop.contains(block.label))
                continue;
            outsideEdges.push_back({&block, idx});
        }
    }

    if (outsideEdges.empty())
        return false;

    bool hasDedicatedPreheader = false;
    if (outsideEdges.size() == 1)
    {
        const auto &edge = outsideEdges.front();
        const Instr *term = getTerminator(*edge.pred);
        hasDedicatedPreheader =
            term && term->labels.size() == 1 && term->labels.front() == header->label;
    }

    if (hasDedicatedPreheader)
        return false;

    std::string base = header->label + ".preheader";
    std::string label = makeUniqueLabel(function, base);

    BasicBlock preheader;
    preheader.label = label;

    unsigned id = nextTempId(function);
    preheader.params.reserve(header->params.size());
    for (const auto &param : header->params)
    {
        Param clone = param;
        clone.id = id++;
        preheader.params.push_back(clone);
        if (function.valueNames.size() <= clone.id)
            function.valueNames.resize(clone.id + 1);
        function.valueNames[clone.id] = clone.name;
    }

    Instr branch;
    branch.op = Opcode::Br;
    branch.type = Type(Type::Kind::Void);
    branch.labels.push_back(header->label);
    branch.brArgs.emplace_back();
    auto &forwardArgs = branch.brArgs.back();
    forwardArgs.reserve(preheader.params.size());
    for (const auto &param : preheader.params)
        forwardArgs.push_back(Value::temp(param.id));
    preheader.instructions.push_back(std::move(branch));
    preheader.terminated = true;

    for (const auto &edge : outsideEdges)
    {
        Instr *term = getTerminator(*edge.pred);
        if (!term)
            continue;
        term->labels[edge.edgeIdx] = label;
    }

    function.blocks.push_back(std::move(preheader));
    return true;
}

bool mergeTrivialLatches(Function &function, const Loop &loop)
{
    if (loop.latchLabels.size() <= 1)
        return false;

    BasicBlock *header = findBlock(function, loop.headerLabel);
    if (!header)
        return false;

    std::vector<BasicBlock *> latches;
    latches.reserve(loop.latchLabels.size());
    for (const auto &label : loop.latchLabels)
    {
        if (auto *block = findBlock(function, label))
            latches.push_back(block);
    }

    if (latches.size() <= 1)
        return false;

    std::vector<Value> canonicalArgs;
    canonicalArgs.reserve(header->params.size());

    for (size_t i = 0; i < latches.size(); ++i)
    {
        const BasicBlock *latch = latches[i];
        const Instr *term = getTerminator(*latch);
        if (!term || term->op != Opcode::Br)
            return false;
        if (latch->instructions.size() != 1)
            return false;
        if (term->labels.size() != 1 || term->labels.front() != header->label)
            return false;
        std::vector<Value> args;
        if (!term->brArgs.empty())
            args = term->brArgs.front();
        if (i == 0)
        {
            canonicalArgs = args;
        }
        else if (!valueVectorsEqual(canonicalArgs, args))
        {
            return false;
        }
    }

    std::string base = header->label + ".latch";
    std::string label = makeUniqueLabel(function, base);

    BasicBlock newLatch;
    newLatch.label = label;

    unsigned id = nextTempId(function);
    newLatch.params.reserve(header->params.size());
    for (const auto &param : header->params)
    {
        Param clone = param;
        clone.id = id++;
        newLatch.params.push_back(clone);
        if (function.valueNames.size() <= clone.id)
            function.valueNames.resize(clone.id + 1);
        function.valueNames[clone.id] = clone.name;
    }

    Instr branch;
    branch.op = Opcode::Br;
    branch.type = Type(Type::Kind::Void);
    branch.labels.push_back(header->label);
    branch.brArgs.emplace_back();
    auto &forwardArgs = branch.brArgs.back();
    forwardArgs.reserve(newLatch.params.size());
    for (const auto &param : newLatch.params)
        forwardArgs.push_back(Value::temp(param.id));
    newLatch.instructions.push_back(std::move(branch));
    newLatch.terminated = true;

    for (auto *latch : latches)
    {
        Instr *term = getTerminator(*latch);
        if (!term)
            continue;
        term->labels.front() = label;
        if (!canonicalArgs.empty())
            term->brArgs.front() = canonicalArgs;
        else if (!term->brArgs.empty())
            term->brArgs.front().clear();
    }

    function.blocks.push_back(std::move(newLatch));
    return true;
}

} // namespace

std::string_view LoopSimplify::id() const
{
    return "loop-simplify";
}

PreservedAnalyses LoopSimplify::run(Function &function, AnalysisManager &analysis)
{
    [[maybe_unused]] auto &domTree =
        analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>("loop-info", function);

    bool changed = false;
    for (const Loop &loop : loopInfo.loops())
    {
        changed |= ensurePreheader(function, loop);
        changed |= mergeTrivialLatches(function, loop);
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    return preserved;
}

} // namespace il::transform
