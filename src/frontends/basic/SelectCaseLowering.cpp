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
    size_t totalRangeCount = 0;
    for (const auto &arm : stmt.arms)
    {
        if (!arm.ranges.empty())
        {
            hasRanges = true;
            totalRangeCount += arm.ranges.size();
        }
    }

    bool hasCaseElse = !stmt.elseBody.empty();
    Blocks blocks = prepareBlocks(stmt, hasCaseElse, hasRanges);

    if (selectorIsString)
    {
        lowerStringArms(stmt, blocks, stringSelector);
    }
    else
    {
        lowerNumericDispatch(stmt, blocks, selWide, sel, hasRanges, totalRangeCount);
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
                                         il::core::Value stringSelector)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *blockNamer = ctx.blockNames().namer();

    auto *defaultBlk =
        blocks.elseIdx ? &func->blocks[*blocks.elseIdx] : &func->blocks[blocks.endIdx];
    if (defaultBlk->label.empty())
        defaultBlk->label = lowerer_.nextFallbackBlockLabel();

    size_t checkIdx = blocks.currentIdx;
    ctx.setCurrent(&func->blocks[checkIdx]);
    bool emittedComparison = false;

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        const auto &labels = stmt.arms[i].str_labels;
        if (labels.empty())
            continue;

        func = ctx.function();
        auto *armBlk = &func->blocks[blocks.armIdx[i]];
        if (armBlk->label.empty())
            armBlk->label = lowerer_.nextFallbackBlockLabel();

        for (size_t j = 0; j < labels.size(); ++j)
        {
            bool moreComparisons = (j + 1 < labels.size());
            if (!moreComparisons)
            {
                for (size_t k = i + 1; k < stmt.arms.size(); ++k)
                {
                    if (!stmt.arms[k].str_labels.empty())
                    {
                        moreComparisons = true;
                        break;
                    }
                }
            }

            size_t nextIdx;
            if (moreComparisons)
            {
                std::string checkLabel = blockNamer ? blockNamer->generic("select_check")
                                                    : lowerer_.mangler.block("select_check");
                lowerer_.builder->addBlock(*func, checkLabel);
                func = ctx.function();
                nextIdx = func->blocks.size() - 1;
            }
            else
            {
                nextIdx = blocks.elseIdx ? *blocks.elseIdx : blocks.endIdx;
            }

            func = ctx.function();
            auto *checkBlk = &func->blocks[checkIdx];
            auto *trueTarget = &func->blocks[blocks.armIdx[i]];
            if (trueTarget->label.empty())
                trueTarget->label = lowerer_.nextFallbackBlockLabel();
            auto *nextBlk = &func->blocks[nextIdx];
            if (nextBlk->label.empty())
                nextBlk->label = lowerer_.nextFallbackBlockLabel();

            ctx.setCurrent(checkBlk);
            lowerer_.curLoc = stmt.arms[i].range.begin;
            il::core::Value labelValue = lowerer_.emitConstStr(lowerer_.getStringLabel(labels[j]));
            il::core::Value cond = lowerer_.emitCallRet(
                lowerer_.ilBoolTy(), "rt_str_eq", {stringSelector, labelValue});
            lowerer_.emitCBr(cond, trueTarget, nextBlk);

            checkIdx = nextIdx;
            emittedComparison = true;
        }
    }

    if (!emittedComparison)
    {
        ctx.setCurrent(&func->blocks[blocks.currentIdx]);
        lowerer_.emitBr(defaultBlk);
        checkIdx = blocks.elseIdx ? *blocks.elseIdx : blocks.endIdx;
    }

    ctx.setCurrent(&ctx.function()->blocks[checkIdx]);
}

void SelectCaseLowering::lowerNumericDispatch(const SelectCaseStmt &stmt,
                                              const Blocks &blocks,
                                              il::core::Value selWide,
                                              il::core::Value selector,
                                              bool hasRanges,
                                              size_t totalRangeCount)
{
    NumericDispatchState state{};
    state.switchIdx = blocks.switchIdx;
    state.afterRelIdx = blocks.currentIdx;

    state.rangeChecks.reserve(totalRangeCount);
    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        for (const auto &range : stmt.arms[i].ranges)
        {
            state.rangeChecks.push_back(NumericDispatchState::RangeCheck{
                static_cast<int32_t>(range.first), static_cast<int32_t>(range.second), i});
        }
    }

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        for (const auto &rel : stmt.arms[i].rels)
            state.relChecks.push_back(NumericDispatchState::RelCheck{&rel, i});
    }

    emitRelationalChecks(stmt, blocks, selWide, state);

    if (!hasRanges)
        state.switchIdx = state.afterRelIdx;

    emitRangeChecks(stmt, blocks, selWide, state);
    emitSwitchJumpTable(stmt, blocks, selector, state);
}

void SelectCaseLowering::emitRelationalChecks(const SelectCaseStmt &stmt,
                                              const Blocks &blocks,
                                              il::core::Value selWide,
                                              NumericDispatchState &state)
{
    auto &ctx = lowerer_.context();
    auto *blockNamer = ctx.blockNames().namer();

    size_t checkIdx = state.afterRelIdx;
    for (const auto &check : state.relChecks)
    {
        auto *func = ctx.function();
        auto *checkBlk = &func->blocks[checkIdx];
        std::string label =
            blockNamer ? blockNamer->generic("select_rel") : lowerer_.mangler.block("select_rel");
        lowerer_.builder->addBlock(*func, label);

        func = ctx.function();
        size_t nextIdx = func->blocks.size() - 1;
        checkBlk = &func->blocks[checkIdx];
        auto *trueTarget = &func->blocks[blocks.armIdx[check.armIndex]];
        if (trueTarget->label.empty())
            trueTarget->label = lowerer_.nextFallbackBlockLabel();
        auto *nextBlk = &func->blocks[nextIdx];
        if (nextBlk->label.empty())
            nextBlk->label = lowerer_.nextFallbackBlockLabel();

        ctx.setCurrent(checkBlk);
        lowerer_.curLoc = stmt.arms[check.armIndex].range.begin;
        il::core::Opcode cmpOp = il::core::Opcode::ICmpEq;
        switch (check.rel->op)
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
        il::core::Value rhs = il::core::Value::constInt(static_cast<long long>(check.rel->rhs));
        il::core::Value cond = lowerer_.emitBinary(cmpOp, lowerer_.ilBoolTy(), selWide, rhs);
        lowerer_.emitCBr(cond, trueTarget, nextBlk);
        checkIdx = nextIdx;
    }

    state.afterRelIdx = checkIdx;
    ctx.setCurrent(&ctx.function()->blocks[state.afterRelIdx]);
}

void SelectCaseLowering::emitRangeChecks(const SelectCaseStmt &stmt,
                                         const Blocks &blocks,
                                         il::core::Value selWide,
                                         NumericDispatchState &state)
{
    auto &ctx = lowerer_.context();
    auto *blockNamer = ctx.blockNames().namer();

    if (state.rangeChecks.empty())
    {
        ctx.setCurrent(&ctx.function()->blocks[state.switchIdx]);
        return;
    }

    size_t rangeBlockIdx = state.afterRelIdx;
    for (size_t idx = 0; idx < state.rangeChecks.size(); ++idx)
    {
        auto *func = ctx.function();
        const auto &check = state.rangeChecks[idx];
        auto *rangeBlk = &func->blocks[rangeBlockIdx];
        auto *trueTarget = &func->blocks[blocks.armIdx[check.armIndex]];
        if (trueTarget->label.empty())
            trueTarget->label = lowerer_.nextFallbackBlockLabel();

        size_t nextIdx;
        if (idx + 1 < state.rangeChecks.size())
        {
            std::string label = blockNamer ? blockNamer->generic("select_range")
                                           : lowerer_.mangler.block("select_range");
            lowerer_.builder->addBlock(*func, label);
            func = ctx.function();
            nextIdx = func->blocks.size() - 1;
        }
        else
        {
            nextIdx = state.switchIdx;
        }

        auto *nextBlk = &func->blocks[nextIdx];
        if (nextBlk->label.empty())
            nextBlk->label = lowerer_.nextFallbackBlockLabel();

        ctx.setCurrent(rangeBlk);
        lowerer_.curLoc = stmt.arms[check.armIndex].range.begin;
        il::core::Value ge =
            lowerer_.emitBinary(il::core::Opcode::SCmpGE,
                                lowerer_.ilBoolTy(),
                                selWide,
                                il::core::Value::constInt(static_cast<long long>(check.lo)));
        il::core::Value le =
            lowerer_.emitBinary(il::core::Opcode::SCmpLE,
                                lowerer_.ilBoolTy(),
                                selWide,
                                il::core::Value::constInt(static_cast<long long>(check.hi)));
        il::core::Value cond =
            lowerer_.emitBinary(il::core::Opcode::And, lowerer_.ilBoolTy(), ge, le);
        lowerer_.emitCBr(cond, trueTarget, nextBlk);

        rangeBlockIdx = nextIdx;
    }

    ctx.setCurrent(&ctx.function()->blocks[state.switchIdx]);
}

void SelectCaseLowering::emitSwitchJumpTable(const SelectCaseStmt &stmt,
                                             const Blocks &blocks,
                                             il::core::Value selector,
                                             NumericDispatchState &state)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    ctx.setCurrent(&func->blocks[state.switchIdx]);

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
