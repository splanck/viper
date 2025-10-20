// File: src/frontends/basic/SelectCaseLowering.cpp
// Purpose: Implements the SelectCaseLowering helper for BASIC frontend lowering.
// Key invariants: Respects Lowerer block allocation and terminator rules.
// Ownership/Lifetime: Borrows Lowerer state; does not allocate persistent memory.
// Links: docs/codemap.md

#include "frontends/basic/SelectCaseLowering.hpp"

#include "frontends/basic/Lowerer.hpp"

#include "il/core/Module.hpp"

#include <cassert>

namespace il::frontends::basic
{

SelectCaseLowering::SelectCaseLowering(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

void SelectCaseLowering::lower(const SelectCaseStmt &stmt)
{
    if (!stmt.selector)
        return;

    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *current = ctx.current();
    if (!func || !current)
        return;

    lowerer_.curLoc = stmt.selector->loc;
    Lowerer::RVal selectorVal = lowerer_.lowerExpr(*stmt.selector);
    bool selectorIsString = selectorVal.type.kind == il::core::Type::Kind::Str;
    il::core::Value stringSelector = selectorVal.value;
    il::core::Value selWide{};
    il::core::Value sel{};
    if (!selectorIsString)
    {
        selectorVal = lowerer_.ensureI64(std::move(selectorVal), stmt.selector->loc);
        selWide = selectorVal.value;
        lowerer_.curLoc = stmt.selector->loc;
        sel = lowerer_.emitUnary(il::core::Opcode::CastSiNarrowChk,
                                 il::core::Type(il::core::Type::Kind::I32),
                                 selectorVal.value);
    }

    func = ctx.function();
    current = ctx.current();
    if (!func || !current)
        return;

    bool hasRanges = false;
    ComparePlan plan;
    plan.reserve(stmt.arms.size());

    if (selectorIsString)
    {
        size_t labelCount = 0;
        for (const auto &arm : stmt.arms)
            labelCount += arm.str_labels.size();
        plan.clear();
        plan.reserve(labelCount);
        for (size_t i = 0; i < stmt.arms.size(); ++i)
        {
            const auto &arm = stmt.arms[i];
            for (const auto &label : arm.str_labels)
            {
                ComparePlanEntry entry{};
                entry.kind = CaseKind::String;
                entry.armIndex = i;
                entry.loc = arm.range.begin;
                entry.strLabel = &label;
                plan.push_back(entry);
            }
        }
    }
    else
    {
        size_t approxChecks = 0;
        for (const auto &arm : stmt.arms)
        {
            approxChecks += arm.rels.size() + arm.ranges.size();
            if (!arm.ranges.empty())
                hasRanges = true;
        }

        plan.clear();
        plan.reserve(approxChecks);
        for (size_t i = 0; i < stmt.arms.size(); ++i)
        {
            const auto &arm = stmt.arms[i];
            for (const auto &rel : arm.rels)
            {
                ComparePlanEntry entry{};
                entry.kind = CaseKind::Rel;
                entry.armIndex = i;
                entry.loc = arm.range.begin;
                entry.rel = &rel;
                plan.push_back(entry);
            }

            for (const auto &range : arm.ranges)
            {
                ComparePlanEntry entry{};
                entry.kind = CaseKind::Range;
                entry.armIndex = i;
                entry.loc = arm.range.begin;
                entry.lo = static_cast<int32_t>(range.first);
                entry.hi = static_cast<int32_t>(range.second);
                plan.push_back(entry);
            }
        }
    }

    bool hasCaseElse = !stmt.elseBody.empty();
    Blocks blocks = prepareBlocks(stmt, hasCaseElse, !selectorIsString && hasRanges);

    if (selectorIsString)
    {
        lowerStringArms(stmt, blocks, stringSelector, plan);
    }
    else
    {
        lowerNumericDispatch(stmt, blocks, selWide, sel, plan);
    }

    func = ctx.function();
    auto *endBlk = &func->blocks[blocks.endIdx];

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        auto *armBlk = &func->blocks[blocks.armIdx[i]];
        emitArmBody(stmt.arms[i].body, armBlk, stmt.arms[i].range.begin, endBlk);
    }

    if (hasCaseElse)
    {
        auto *caseElseBlk = &func->blocks[*blocks.elseIdx];
        emitArmBody(stmt.elseBody, caseElseBlk, stmt.range.end, endBlk);
    }

    ctx.setCurrent(endBlk);
}

SelectCaseLowering::Blocks SelectCaseLowering::prepareBlocks(const SelectCaseStmt &stmt,
                                                             bool hasCaseElse,
                                                             bool needsDispatch)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *current = ctx.current();
    assert(func && current);

    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    auto *blockNamer = ctx.blockNames().namer();
    size_t startIdx = func->blocks.size();

    Blocks blocks{};
    blocks.currentIdx = curIdx;
    blocks.switchIdx = curIdx;
    blocks.armIdx.resize(stmt.arms.size());

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        std::string label = blockNamer ? blockNamer->generic("select_arm")
                                       : lowerer_.mangler.block("select_arm_" + std::to_string(i));
        lowerer_.builder->addBlock(*func, label);
    }

    if (hasCaseElse)
    {
        std::string defaultLabel = blockNamer ? blockNamer->generic("select_default")
                                              : lowerer_.mangler.block("select_default");
        lowerer_.builder->addBlock(*func, defaultLabel);
        blocks.elseIdx = startIdx + stmt.arms.size();
    }

    if (needsDispatch)
    {
        std::string dispatchLabel = blockNamer ? blockNamer->generic("select_dispatch")
                                               : lowerer_.mangler.block("select_dispatch");
        lowerer_.builder->addBlock(*func, dispatchLabel);
        blocks.switchIdx = startIdx + stmt.arms.size() + (hasCaseElse ? 1 : 0);
    }

    std::string endLabel =
        blockNamer ? blockNamer->generic("select_end") : lowerer_.mangler.block("select_end");
    lowerer_.builder->addBlock(*func, endLabel);
    blocks.endIdx = startIdx + stmt.arms.size() + (hasCaseElse ? 1 : 0) + (needsDispatch ? 1 : 0);

    func = ctx.function();
    current = &func->blocks[curIdx];
    ctx.setCurrent(current);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
        blocks.armIdx[i] = startIdx + i;

    return blocks;
}

void SelectCaseLowering::lowerStringArms(const SelectCaseStmt &stmt,
                                         const Blocks &blocks,
                                         il::core::Value stringSelector,
                                         const ComparePlan &plan)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *defaultBlk =
        blocks.elseIdx ? &func->blocks[*blocks.elseIdx] : &func->blocks[blocks.endIdx];
    il::core::BasicBlock *fallthrough = emitCompareChain(
        blocks, blocks.currentIdx, defaultBlk, plan, stringSelector, il::core::Value{}, stmt.loc);
    ctx.setCurrent(fallthrough);
}

void SelectCaseLowering::lowerNumericDispatch(const SelectCaseStmt &stmt,
                                              const Blocks &blocks,
                                              il::core::Value selWide,
                                              il::core::Value selector,
                                              const ComparePlan &plan)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *switchBlk = &func->blocks[blocks.switchIdx];
    il::core::BasicBlock *dispatch = emitCompareChain(
        blocks, blocks.currentIdx, switchBlk, plan, il::core::Value{}, selWide, stmt.loc);
    ctx.setCurrent(dispatch);
    emitSwitchJumpTable(stmt, blocks, selector);
}

il::core::BasicBlock *SelectCaseLowering::emitCompareChain(const Blocks &blocks,
                                                           size_t startIdx,
                                                           il::core::BasicBlock *defaultBlk,
                                                           const ComparePlan &plan,
                                                           il::core::Value stringSelector,
                                                           il::core::Value selWide,
                                                           il::support::SourceLoc fallbackLoc)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();

    if (defaultBlk->label.empty())
        defaultBlk->label = lowerer_.nextFallbackBlockLabel();

    if (plan.empty())
    {
        ctx.setCurrent(&func->blocks[startIdx]);
        lowerer_.curLoc = fallbackLoc;
        // No comparisons means we fall straight through to the default target.
        lowerer_.emitBr(defaultBlk);
        ctx.setCurrent(defaultBlk);
        return defaultBlk;
    }

    auto *blockNamer = ctx.blockNames().namer();
    std::vector<size_t> checkIdxs;
    checkIdxs.reserve(plan.size());
    checkIdxs.push_back(startIdx);

    auto makeLabel = [&](CaseKind kind)
    {
        const char *base = "select_check";
        switch (kind)
        {
            case CaseKind::String:
                base = "select_check";
                break;
            case CaseKind::Rel:
                base = "select_rel";
                break;
            case CaseKind::Range:
                base = "select_range";
                break;
        }
        return blockNamer ? blockNamer->generic(base) : lowerer_.mangler.block(base);
    };

    for (size_t i = 1; i < plan.size(); ++i)
    {
        std::string label = makeLabel(plan[i - 1].kind);
        lowerer_.builder->addBlock(*func, label);
        func = ctx.function();
        checkIdxs.push_back(func->blocks.size() - 1);
    }

    for (size_t i = 0; i < plan.size(); ++i)
    {
        func = ctx.function();
        auto *checkBlk = &func->blocks[checkIdxs[i]];
        auto *trueTarget = &func->blocks[blocks.armIdx[plan[i].armIndex]];
        if (trueTarget->label.empty())
            trueTarget->label = lowerer_.nextFallbackBlockLabel();

        il::core::BasicBlock *nextBlk =
            (i + 1 < plan.size()) ? &func->blocks[checkIdxs[i + 1]] : defaultBlk;
        if (nextBlk->label.empty())
            nextBlk->label = lowerer_.nextFallbackBlockLabel();

        ctx.setCurrent(checkBlk);
        lowerer_.curLoc = plan[i].loc;

        il::core::Value cond{};
        switch (plan[i].kind)
        {
            case CaseKind::String:
            {
                const std::string &label = *plan[i].strLabel;
                il::core::Value labelValue = lowerer_.emitConstStr(lowerer_.getStringLabel(label));
                cond = lowerer_.emitCallRet(
                    lowerer_.ilBoolTy(), "rt_str_eq", {stringSelector, labelValue});
                break;
            }
            case CaseKind::Rel:
            {
                auto *rel = plan[i].rel;
                il::core::Opcode cmpOp = il::core::Opcode::ICmpEq;
                switch (rel->op)
                {
                    case CaseArm::CaseRel::Op::LT:
                        cmpOp = il::core::Opcode::SCmpLT;
                        break;
                    case CaseArm::CaseRel::Op::LE:
                        cmpOp = il::core::Opcode::SCmpLE;
                        break;
                    case CaseArm::CaseRel::Op::EQ:
                        cmpOp = il::core::Opcode::ICmpEq;
                        break;
                    case CaseArm::CaseRel::Op::GE:
                        cmpOp = il::core::Opcode::SCmpGE;
                        break;
                    case CaseArm::CaseRel::Op::GT:
                        cmpOp = il::core::Opcode::SCmpGT;
                        break;
                }
                il::core::Value rhs = il::core::Value::constInt(static_cast<long long>(rel->rhs));
                cond = lowerer_.emitBinary(cmpOp, lowerer_.ilBoolTy(), selWide, rhs);
                break;
            }
            case CaseKind::Range:
            {
                il::core::Value ge = lowerer_.emitBinary(
                    il::core::Opcode::SCmpGE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(plan[i].lo)));
                il::core::Value le = lowerer_.emitBinary(
                    il::core::Opcode::SCmpLE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(plan[i].hi)));
                cond = lowerer_.emitBinary(il::core::Opcode::And, lowerer_.ilBoolTy(), ge, le);
                break;
            }
        }

        // Each comparison terminates the block; a false edge falls through to the next check
        // (or to the default dispatch once the plan is exhausted).
        lowerer_.emitCBr(cond, trueTarget, nextBlk);
    }

    ctx.setCurrent(defaultBlk);
    return defaultBlk;
}

void SelectCaseLowering::emitSwitchJumpTable(const SelectCaseStmt &stmt,
                                             const Blocks &blocks,
                                             il::core::Value selector)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    ctx.setCurrent(&func->blocks[blocks.switchIdx]);

    std::vector<std::pair<int32_t, il::core::BasicBlock *>> caseTargets;
    size_t labelCount = 0;
    for (const auto &arm : stmt.arms)
        labelCount += arm.labels.size();
    caseTargets.reserve(labelCount);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        auto *armBlk = &func->blocks[blocks.armIdx[i]];
        if (armBlk->label.empty())
            armBlk->label = lowerer_.nextFallbackBlockLabel();
        for (int64_t rawLabel : stmt.arms[i].labels)
        {
            int32_t narrowed = static_cast<int32_t>(rawLabel);
            caseTargets.emplace_back(narrowed, armBlk);
        }
    }

    il::core::Instr sw;
    sw.op = il::core::Opcode::SwitchI32;
    sw.type = il::core::Type(il::core::Type::Kind::Void);
    sw.operands.push_back(selector);

    auto *caseElseBlk =
        blocks.elseIdx ? &func->blocks[*blocks.elseIdx] : &func->blocks[blocks.endIdx];
    if (caseElseBlk->label.empty())
        caseElseBlk->label = lowerer_.nextFallbackBlockLabel();
    sw.labels.push_back(caseElseBlk->label);
    sw.brArgs.emplace_back();

    for (const auto &[value, target] : caseTargets)
    {
        if (target->label.empty())
            target->label = lowerer_.nextFallbackBlockLabel();
        sw.operands.push_back(il::core::Value::constInt(static_cast<long long>(value)));
        sw.labels.push_back(target->label);
        sw.brArgs.emplace_back();
    }
    sw.loc = stmt.loc;

    auto *switchBlk = ctx.current();
    // The switch instruction is the terminator for the dispatch block; nothing may follow it.
    switchBlk->instructions.push_back(std::move(sw));
    switchBlk->terminated = true;
}

void SelectCaseLowering::emitArmBody(const std::vector<StmtPtr> &body,
                                     il::core::BasicBlock *entry,
                                     il::support::SourceLoc loc,
                                     il::core::BasicBlock *endBlk)
{
    auto &ctx = lowerer_.context();
    ctx.setCurrent(entry);
    for (const auto &node : body)
    {
        if (!node)
            continue;
        lowerer_.lowerStmt(*node);
        auto *bodyCur = ctx.current();
        if (!bodyCur || bodyCur->terminated)
            break;
    }

    auto *bodyCur = ctx.current();
    if (bodyCur && !bodyCur->terminated)
    {
        lowerer_.curLoc = loc;
        lowerer_.emitBr(endBlk);
    }
}

void Lowerer::lowerSelectCase(const SelectCaseStmt &stmt)
{
    CtrlState state = emitSelect(stmt);
    if (state.cur)
        context().setCurrent(state.cur);
}

} // namespace il::frontends::basic
