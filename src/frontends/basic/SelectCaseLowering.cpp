//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SelectCaseLowering.cpp
// Purpose: Implements the SelectCaseLowering helper for BASIC frontend lowering.
// Key invariants: Respects Lowerer block allocation and terminator rules.
// Ownership/Lifetime: Borrows Lowerer state; does not allocate persistent memory.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements lowering of BASIC SELECT CASE statements into IL control flow.
/// @details The helper orchestrates block creation, comparison emission, and jump
///          table construction so SELECT CASE lowering can share logic across
///          numeric and string selector modes while preserving deterministic
///          control-flow graphs.

#include "frontends/basic/SelectCaseLowering.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SelectCaseRange.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

#include "il/core/Module.hpp"

#include <cassert>
#include <cstdlib>
#include <string>

namespace il::frontends::basic
{

/// @brief Bind the lowering helper to the owning @ref Lowerer instance.
///
/// @param lowerer Lowerer providing builder access, naming utilities, and
///        mangling helpers for block generation.
SelectCaseLowering::SelectCaseLowering(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

/// @brief Lower a BASIC SELECT CASE statement into IL blocks and dispatch logic.
///
/// @details Evaluates the selector expression, creates the necessary dispatch
///          blocks, and emits either string or numeric comparisons depending on
///          the selector type. Arm bodies are lowered into the blocks prepared
///          by @ref prepareBlocks. Empty selectors terminate early because the
///          front end treats them as no-ops.
///
/// @param stmt Parsed SELECT CASE statement containing selector, arms, and else.
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
        sel = lowerer_.emitCommon(stmt.selector->loc).narrow_to(selectorVal.value, 64, 32);
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

/// @brief Materialise the block skeleton required by a SELECT CASE statement.
///
/// @details Allocates per-arm entry blocks, optional CASE ELSE and dispatch
///          blocks, and a shared end block. The helper records the indices of
///          the created blocks so later lowering stages can emit instructions
///          without recomputing positions. The current block is restored before
///          returning to keep builder state consistent.
///
/// @param stmt SELECT CASE AST node describing the arms being lowered.
/// @param hasCaseElse True when the statement includes a CASE ELSE body.
/// @param needsDispatch True when numeric dispatch requires a jump-table block.
/// @return Structure enumerating the indices of the created blocks.
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

/// @brief Emit string-comparison dispatch for SELECT CASE arms.
///
/// @details Builds a comparison plan covering every string label and CASE ELSE,
///          then emits a chain of conditional branches that invoke the runtime
///          string equality helper. When no string labels exist the selector
///          falls through directly to the default block without performing
///          comparisons.
///
/// @param stmt SELECT CASE node describing available arms and labels.
/// @param blocks Block indices prepared by @ref prepareBlocks.
/// @param stringSelector Evaluated selector value produced by expression lowering.
void SelectCaseLowering::lowerStringArms(const SelectCaseStmt &stmt,
                                         const Blocks &blocks,
                                         il::core::Value stringSelector)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();

    auto *defaultBlk =
        blocks.elseIdx ? &func->blocks[*blocks.elseIdx] : &func->blocks[blocks.endIdx];

    std::vector<CasePlanEntry> plan;
    size_t labelCount = 0;
    for (const auto &arm : stmt.arms)
        labelCount += arm.str_labels.size();
    plan.reserve(labelCount + 1);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        const auto &labels = stmt.arms[i].str_labels;
        if (labels.empty())
            continue;

        auto *armBlk = &func->blocks[blocks.armIdx[i]];
        for (const auto &label : labels)
        {
            CasePlanEntry entry{};
            entry.kind = CasePlanEntry::Kind::StringLabel;
            entry.armIndex = i;
            entry.target = armBlk;
            entry.loc = stmt.arms[i].range.begin;
            entry.strLiteral = label;
            plan.push_back(entry);
        }
    }

    CasePlanEntry defaultEntry{};
    defaultEntry.kind = CasePlanEntry::Kind::Default;
    defaultEntry.target = defaultBlk;
    defaultEntry.loc = stmt.range.end;
    plan.push_back(defaultEntry);

    if (plan.size() == 1)
    {
        ctx.setCurrent(&func->blocks[blocks.currentIdx]);
        lowerer_.curLoc = stmt.loc;
        // Blocks that skip comparisons fall through directly to the default arm.
        lowerer_.emitBr(defaultBlk);
        ctx.setCurrent(defaultBlk);
        return;
    }

    ConditionEmitter emitter = [this, stringSelector](const CasePlanEntry &entry)
    {
        assert(entry.kind == CasePlanEntry::Kind::StringLabel);
        std::string labelStr(entry.strLiteral);
        il::core::Value labelValue = lowerer_.emitConstStr(lowerer_.getStringLabel(labelStr));
        return lowerer_.emitCallRet(lowerer_.ilBoolTy(), "rt_str_eq", {stringSelector, labelValue});
    };

    emitCompareChain(blocks.currentIdx, plan, emitter);
}

/// @brief Emit numeric dispatch for SELECT CASE arms.
///
/// @details Builds a comparison plan for relational guards and ranges, emits a
///          chain of conditional branches using the 64-bit selector, and finally
///          constructs a jump table for discrete labels. Range-heavy statements
///          route through a dedicated dispatch block to keep fall-through logic
///          straightforward.
///
/// @param stmt SELECT CASE statement currently being lowered.
/// @param blocks Block indices prepared by @ref prepareBlocks.
/// @param selWide 64-bit widened selector used for comparisons.
/// @param selector Narrowed selector used when building the switch table.
/// @param hasRanges Indicates whether any arm provides range guards.
void SelectCaseLowering::lowerNumericDispatch(const SelectCaseStmt &stmt,
                                              const Blocks &blocks,
                                              il::core::Value selWide,
                                              il::core::Value selector,
                                              bool hasRanges)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();

    std::vector<CasePlanEntry> plan;
    size_t estCount = 0;
    for (const auto &arm : stmt.arms)
        estCount += arm.rels.size() + arm.ranges.size();
    plan.reserve(estCount + 1);

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        auto *armBlk = &func->blocks[blocks.armIdx[i]];
        for (const auto &rel : stmt.arms[i].rels)
        {
            CasePlanEntry entry{};
            entry.armIndex = i;
            entry.target = armBlk;
            entry.loc = stmt.arms[i].range.begin;
            switch (rel.op)
            {
                case CaseArm::CaseRel::Op::LT:
                    entry.kind = CasePlanEntry::Kind::RelLT;
                    entry.valueRange.second = static_cast<int32_t>(rel.rhs);
                    break;
                case CaseArm::CaseRel::Op::LE:
                    entry.kind = CasePlanEntry::Kind::RelLE;
                    entry.valueRange.second = static_cast<int32_t>(rel.rhs);
                    break;
                case CaseArm::CaseRel::Op::EQ:
                    entry.kind = CasePlanEntry::Kind::RelEQ;
                    entry.valueRange.first = static_cast<int32_t>(rel.rhs);
                    entry.valueRange.second = entry.valueRange.first;
                    break;
                case CaseArm::CaseRel::Op::GE:
                    entry.kind = CasePlanEntry::Kind::RelGE;
                    entry.valueRange.first = static_cast<int32_t>(rel.rhs);
                    break;
                case CaseArm::CaseRel::Op::GT:
                    entry.kind = CasePlanEntry::Kind::RelGT;
                    entry.valueRange.first = static_cast<int32_t>(rel.rhs);
                    break;
            }
            plan.push_back(entry);
        }

        for (const auto &range : stmt.arms[i].ranges)
        {
            CasePlanEntry entry{};
            entry.kind = CasePlanEntry::Kind::Range;
            entry.armIndex = i;
            entry.target = armBlk;
            entry.loc = stmt.arms[i].range.begin;
            entry.valueRange.first = static_cast<int32_t>(range.first);
            entry.valueRange.second = static_cast<int32_t>(range.second);
            plan.push_back(entry);
        }
    }

    const bool hasComparisons = !plan.empty();

    CasePlanEntry defaultEntry{};
    defaultEntry.kind = CasePlanEntry::Kind::Default;
    if (hasRanges)
    {
        defaultEntry.target = &func->blocks[blocks.switchIdx];
    }
    else if (hasComparisons)
    {
        defaultEntry.target = nullptr;
    }
    else
    {
        defaultEntry.target = &func->blocks[blocks.switchIdx];
    }
    defaultEntry.loc = stmt.loc;
    plan.push_back(defaultEntry);

    ConditionEmitter emitter = [this, selWide, &stmt](const CasePlanEntry &entry)
    {
        assert(entry.kind != CasePlanEntry::Kind::Default);
        switch (entry.kind)
        {
            case CasePlanEntry::Kind::RelLT:
                return lowerer_.emitBinary(
                    il::core::Opcode::SCmpLT,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.second)));
            case CasePlanEntry::Kind::RelLE:
                return lowerer_.emitBinary(
                    il::core::Opcode::SCmpLE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.second)));
            case CasePlanEntry::Kind::RelEQ:
                return lowerer_.emitBinary(
                    il::core::Opcode::ICmpEq,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.first)));
            case CasePlanEntry::Kind::RelGE:
                return lowerer_.emitBinary(
                    il::core::Opcode::SCmpGE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.first)));
            case CasePlanEntry::Kind::RelGT:
                return lowerer_.emitBinary(
                    il::core::Opcode::SCmpGT,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.first)));
            case CasePlanEntry::Kind::Range:
            {
                il::core::Value ge = lowerer_.emitBinary(
                    il::core::Opcode::SCmpGE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.first)));
                il::core::Value le = lowerer_.emitBinary(
                    il::core::Opcode::SCmpLE,
                    lowerer_.ilBoolTy(),
                    selWide,
                    il::core::Value::constInt(static_cast<long long>(entry.valueRange.second)));
                // The And opcode requires i64 operands; extend the booleans and truncate back.
                il::core::Value ge64 = lowerer_.emitZext1ToI64(ge);
                il::core::Value le64 = lowerer_.emitZext1ToI64(le);
                il::core::Value both64 =
                    lowerer_.emitCommon(stmt.selector->loc).logical_and(ge64, le64);
                return lowerer_.emitUnary(il::core::Opcode::Trunc1, lowerer_.ilBoolTy(), both64);
            }
            case CasePlanEntry::Kind::StringLabel:
            case CasePlanEntry::Kind::Default:
                break;
        }
        std::abort();
    };

    size_t switchIdx = emitCompareChain(blocks.currentIdx, plan, emitter);
    emitSwitchJumpTable(stmt, blocks, selector, switchIdx);
}

/// @brief Emit a sequence of conditional branches for the comparison plan.
///
/// @details Iterates over every non-default entry in @p plan, emitting
///          conditional branches that either jump to the arm block on success or
///          continue to the next comparison block on failure. The final entry is
///          treated as the default destination and becomes the active current
///          block when the routine finishes.
///
/// @param startIdx Index of the block from which the compare chain should start.
/// @param plan Comparison plan built by string or numeric lowering.
/// @param emitCond Callback that materialises the condition for a plan entry.
/// @return Index of the block that represents the default fall-through path.
size_t SelectCaseLowering::emitCompareChain(size_t startIdx,
                                            std::vector<CasePlanEntry> &plan,
                                            const ConditionEmitter &emitCond)
{
    if (plan.empty())
        return startIdx;

    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    auto *blockNamer = ctx.blockNames().namer();

    auto &defaultEntry = plan.back();
    assert(defaultEntry.kind == CasePlanEntry::Kind::Default);
    auto *defaultBlk = defaultEntry.target;

    if (!defaultBlk)
    {
        std::string label = blockNamer
                                ? blockNamer->generic(std::string(blockTagFor(defaultEntry)))
                                : lowerer_.mangler.block(std::string(blockTagFor(defaultEntry)));
        lowerer_.builder->addBlock(*func, label);
        func = ctx.function();
        size_t idx = func->blocks.size() - 1;
        defaultBlk = &func->blocks[idx];
        defaultEntry.target = defaultBlk;
    }

    if (defaultBlk->label.empty())
        defaultBlk->label = lowerer_.nextFallbackBlockLabel();

    size_t currentIdx = startIdx;
    for (size_t i = 0; i + 1 < plan.size(); ++i)
    {
        auto &entry = plan[i];
        func = ctx.function();
        auto *checkBlk = &func->blocks[currentIdx];
        auto *trueTarget = entry.target;
        if (trueTarget->label.empty())
            trueTarget->label = lowerer_.nextFallbackBlockLabel();

        bool needIntermediate = plan[i + 1].kind != CasePlanEntry::Kind::Default;
        il::core::BasicBlock *falseTarget = defaultBlk;
        size_t nextIdx = static_cast<size_t>(defaultBlk - &func->blocks[0]);
        if (needIntermediate)
        {
            std::string label = blockNamer
                                    ? blockNamer->generic(std::string(blockTagFor(plan[i + 1])))
                                    : lowerer_.mangler.block(std::string(blockTagFor(plan[i + 1])));
            lowerer_.builder->addBlock(*func, label);
            func = ctx.function();
            nextIdx = func->blocks.size() - 1;
            falseTarget = &func->blocks[nextIdx];
            if (falseTarget->label.empty())
                falseTarget->label = lowerer_.nextFallbackBlockLabel();
        }

        ctx.setCurrent(checkBlk);
        lowerer_.curLoc = entry.loc;
        il::core::Value cond = emitCond(entry);
        // Each comparison produces a terminating conditional branch; no fallthrough remains.
        lowerer_.emitCBr(cond, trueTarget, falseTarget);
        currentIdx = nextIdx;
    }

    ctx.setCurrent(defaultBlk);
    return static_cast<size_t>(defaultBlk - &ctx.function()->blocks[0]);
}

/// @brief Compute a diagnostic label describing a case-plan entry.
///
/// @param entry Plan entry whose block tag should be determined.
/// @return Short tag used for generated block names (e.g. "select_rel").
std::string_view SelectCaseLowering::blockTagFor(const CasePlanEntry &entry)
{
    switch (entry.kind)
    {
        case CasePlanEntry::Kind::StringLabel:
            return "select_check";
        case CasePlanEntry::Kind::RelLT:
        case CasePlanEntry::Kind::RelLE:
        case CasePlanEntry::Kind::RelEQ:
        case CasePlanEntry::Kind::RelGE:
        case CasePlanEntry::Kind::RelGT:
            return "select_rel";
        case CasePlanEntry::Kind::Range:
            return "select_range";
        case CasePlanEntry::Kind::Default:
            return "select_dispatch";
    }
    return "select_dispatch";
}

/// @brief Emit the IL switch instruction for discrete SELECT CASE labels.
///
/// @details Collects all literal labels, validates their ranges, and writes a
///          `switch` instruction that jumps to per-arm blocks or the default
///          CASE ELSE block. Invalid labels surface diagnostics via the active
///          emitter without aborting lowering, matching historical behaviour.
///
/// @param stmt SELECT CASE statement providing labels and diagnostics context.
/// @param blocks Block indices prepared by @ref prepareBlocks.
/// @param selector Narrow selector operand used by the emitted switch.
/// @param switchIdx Index of the block that should contain the jump table.
void SelectCaseLowering::emitSwitchJumpTable(const SelectCaseStmt &stmt,
                                             const Blocks &blocks,
                                             il::core::Value selector,
                                             size_t switchIdx)
{
    auto &ctx = lowerer_.context();
    auto *func = ctx.function();
    ctx.setCurrent(&func->blocks[switchIdx]);

    std::vector<std::pair<int32_t, il::core::BasicBlock *>> caseTargets;
    auto *diag = lowerer_.diagnosticEmitter();
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
            if (rawLabel < kCaseLabelMin || rawLabel > kCaseLabelMax)
            {
                if (diag)
                {
                    lowerer_.curLoc = stmt.arms[i].range.begin;
                    diag->emit(il::support::Severity::Error,
                               std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                               stmt.arms[i].range.begin,
                               1,
                               makeSelectCaseLabelRangeMessage(rawLabel));
                }
                continue;
            }

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
    // Switch terminators complete the block; successors are encoded in the table.
    switchBlk->terminated = true;
}

/// @brief Lower the statements associated with a single CASE arm.
///
/// @details Sets the current builder block to @p entry, lowers each statement in
///          order, and ensures fall-through control transfers to @p endBlk when
///          the body does not already terminate. Empty statements are skipped so
///          sparse bodies work naturally.
///
/// @param body AST nodes forming the arm body.
/// @param entry Block that serves as the entry point for the arm.
/// @param loc Source location used for synthesized branch diagnostics.
/// @param endBlk Successor block executed after the arm completes.
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

/// @brief Entrypoint that lowers a SELECT CASE statement via @ref SelectCaseLowering.
///
/// @param stmt AST node representing the SELECT CASE statement to lower.
void Lowerer::lowerSelectCase(const SelectCaseStmt &stmt)
{
    CtrlState state = emitSelect(stmt);
    if (state.cur)
        context().setCurrent(state.cur);
}

} // namespace il::frontends::basic
