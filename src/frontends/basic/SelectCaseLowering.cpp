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
    for (const auto &arm : stmt.arms)
    {
        if (!arm.ranges.empty())
        {
            hasRanges = true;
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
        lowerNumericDispatch(stmt, blocks, selWide, sel, hasRanges);
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

    size_t defaultIdx = blocks.elseIdx ? *blocks.elseIdx : blocks.endIdx;
    auto &defaultBlk = func->blocks[defaultIdx];
    if (defaultBlk.label.empty())
        defaultBlk.label = lowerer_.nextFallbackBlockLabel();

    std::vector<CasePlanEntry> plan;
    size_t labelCount = 0;
    for (const auto &arm : stmt.arms)
        labelCount += arm.str_labels.size();
    plan.reserve(labelCount);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        const auto &labels = stmt.arms[i].str_labels;
        if (labels.empty())
            continue;

        for (const auto &label : labels)
        {
            CasePlanEntry entry{};
            entry.kind = CaseKind::StringEq;
            entry.targetIdx = blocks.armIdx[i];
            entry.loc = stmt.arms[i].range.begin;
            entry.strPayload = label;
            plan.push_back(entry);
        }
    }

    if (plan.empty())
    {
        ctx.setCurrent(&ctx.function()->blocks[blocks.currentIdx]);
        // No comparisons means the entry block must terminate by jumping to the default.
        lowerer_.emitBr(&defaultBlk);
        ctx.setCurrent(&ctx.function()->blocks[defaultIdx]);
        return;
    }

    auto builder = [&](const CasePlanEntry &entry) -> il::core::Value {
        il::core::Value labelValue =
            lowerer_.emitConstStr(lowerer_.getStringLabel(entry.strPayload));
        return lowerer_.emitCallRet(
            lowerer_.ilBoolTy(), "rt_str_eq", {stringSelector, labelValue});
    };

    // Every comparison block ends with a conditional branch. False edges fall through to the
    // next plan entry while the true edge transfers control to the matching arm.
    emitDecisionChain(plan, blocks.currentIdx, defaultIdx, builder);
}

void SelectCaseLowering::lowerNumericDispatch(const SelectCaseStmt &stmt,
                                             const Blocks &blocks,
                                             il::core::Value selWide,
                                             il::core::Value selector,
                                             bool hasRanges)
{
    auto &ctx = lowerer_.context();

    std::vector<CasePlanEntry> plan;
    size_t decisionCount = 0;
    for (const auto &arm : stmt.arms)
        decisionCount += arm.rels.size() + arm.ranges.size();
    plan.reserve(decisionCount);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        for (const auto &rel : stmt.arms[i].rels)
        {
            CasePlanEntry entry{};
            entry.loc = stmt.arms[i].range.begin;
            entry.targetIdx = blocks.armIdx[i];
            entry.range.lo = static_cast<int32_t>(rel.rhs);
            entry.range.hi = entry.range.lo;
            switch (rel.op)
            {
                case CaseArm::CaseRel::Op::LT:
                    entry.kind = CaseKind::RelLT;
                    break;
                case CaseArm::CaseRel::Op::LE:
                    entry.kind = CaseKind::RelLE;
                    break;
                case CaseArm::CaseRel::Op::EQ:
                    entry.kind = CaseKind::RelEQ;
                    break;
                case CaseArm::CaseRel::Op::GE:
                    entry.kind = CaseKind::RelGE;
                    break;
                case CaseArm::CaseRel::Op::GT:
                    entry.kind = CaseKind::RelGT;
                    break;
            }
            plan.push_back(entry);
        }
    }

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        for (const auto &range : stmt.arms[i].ranges)
        {
            CasePlanEntry entry{};
            entry.kind = CaseKind::RangeInclusive;
            entry.targetIdx = blocks.armIdx[i];
            entry.loc = stmt.arms[i].range.begin;
            entry.range.lo = static_cast<int32_t>(range.first);
            entry.range.hi = static_cast<int32_t>(range.second);
            plan.push_back(entry);
        }
    }

    std::optional<size_t> dispatchFallback = hasRanges ? std::optional<size_t>(blocks.switchIdx)
                                                       : std::optional<size_t>();
    auto builder = [&](const CasePlanEntry &entry) -> il::core::Value {
        switch (entry.kind)
        {
            case CaseKind::RelLT:
                return lowerer_.emitBinary(il::core::Opcode::SCmpLT,
                                           lowerer_.ilBoolTy(),
                                           selWide,
                                           il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
            case CaseKind::RelLE:
                return lowerer_.emitBinary(il::core::Opcode::SCmpLE,
                                           lowerer_.ilBoolTy(),
                                           selWide,
                                           il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
            case CaseKind::RelEQ:
                return lowerer_.emitBinary(il::core::Opcode::ICmpEq,
                                           lowerer_.ilBoolTy(),
                                           selWide,
                                           il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
            case CaseKind::RelGE:
                return lowerer_.emitBinary(il::core::Opcode::SCmpGE,
                                           lowerer_.ilBoolTy(),
                                           selWide,
                                           il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
            case CaseKind::RelGT:
                return lowerer_.emitBinary(il::core::Opcode::SCmpGT,
                                           lowerer_.ilBoolTy(),
                                           selWide,
                                           il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
            case CaseKind::RangeInclusive: {
                il::core::Value ge = lowerer_.emitBinary(
                    il::core::Opcode::SCmpGE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.range.lo)));
                il::core::Value le = lowerer_.emitBinary(
                    il::core::Opcode::SCmpLE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.range.hi)));
                return lowerer_.emitBinary(il::core::Opcode::And, lowerer_.ilBoolTy(), ge, le);
            }
            case CaseKind::StringEq:
                assert(false && "StringEq is not valid for numeric dispatch");
                break;
        }
        return il::core::Value{};
    };

    // The helper returns the final fall-through block index. When ranges are present the
    // dispatch block was preallocated in prepareBlocks and is provided as the default branch
    // destination, so the return value is only relevant when we build the chain ourselves.
    size_t dispatchIdx = emitDecisionChain(plan, blocks.currentIdx, dispatchFallback, builder);
    if (hasRanges)
        dispatchIdx = blocks.switchIdx;

    emitSwitchJumpTable(stmt, blocks, selector, dispatchIdx);
}

size_t SelectCaseLowering::emitDecisionChain(const std::vector<CasePlanEntry> &plan,
                                             size_t entryIdx,
                                             std::optional<size_t> defaultIdx,
                                             const ConditionBuilder &builder)
{
    auto &ctx = lowerer_.context();
    if (plan.empty())
    {
        ctx.setCurrent(&ctx.function()->blocks[entryIdx]);
        return entryIdx;
    }

    size_t currentIdx = entryIdx;

    for (size_t i = 0; i < plan.size(); ++i)
    {
        const CasePlanEntry &entry = plan[i];
        auto *func = ctx.function();
        auto &currentBlk = func->blocks[currentIdx];
        if (currentBlk.label.empty())
            currentBlk.label = lowerer_.nextFallbackBlockLabel();

        func = ctx.function();
        auto &targetBlk = func->blocks[entry.targetIdx];
        if (targetBlk.label.empty())
            targetBlk.label = lowerer_.nextFallbackBlockLabel();

        size_t nextIdx;
        if (i + 1 < plan.size())
        {
            std::string label = chainLabelFor(entry.kind);
            lowerer_.builder->addBlock(*func, label);
            func = ctx.function();
            nextIdx = func->blocks.size() - 1;
        }
        else if (defaultIdx)
        {
            nextIdx = *defaultIdx;
        }
        else
        {
            std::string label = chainLabelFor(entry.kind);
            lowerer_.builder->addBlock(*func, label);
            func = ctx.function();
            nextIdx = func->blocks.size() - 1;
        }

        func = ctx.function();
        auto &nextBlk = func->blocks[nextIdx];
        if (nextBlk.label.empty())
            nextBlk.label = lowerer_.nextFallbackBlockLabel();

        ctx.setCurrent(&currentBlk);
        lowerer_.curLoc = entry.loc;
        il::core::Value cond = builder(entry);
        // Each block terminates with a conditional branch; the false edge is the fall-through to
        // either the next comparison block or the default target.
        lowerer_.emitCBr(cond, &targetBlk, &nextBlk);

        currentIdx = nextIdx;
    }

    ctx.setCurrent(&ctx.function()->blocks[currentIdx]);
    return currentIdx;
}

std::string SelectCaseLowering::chainLabelFor(CaseKind kind)
{
    auto &ctx = lowerer_.context();
    auto *blockNamer = ctx.blockNames().namer();
    const char *base = "select_rel";
    switch (kind)
    {
        case CaseKind::StringEq:
            base = "select_check";
            break;
        case CaseKind::RangeInclusive:
            base = "select_range";
            break;
        case CaseKind::RelLT:
        case CaseKind::RelLE:
        case CaseKind::RelEQ:
        case CaseKind::RelGE:
        case CaseKind::RelGT:
            base = "select_rel";
            break;
    }
    return blockNamer ? blockNamer->generic(base)
                      : lowerer_.mangler.block(base);
}

void SelectCaseLowering::emitSwitchJumpTable(const SelectCaseStmt &stmt,
                                            const Blocks &blocks,
                                            il::core::Value selector,
                                            size_t dispatchIdx)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    ctx.setCurrent(&func->blocks[dispatchIdx]);

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
    // Switch is a terminator; control transfers to one of the labeled targets with no fall-through.
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
