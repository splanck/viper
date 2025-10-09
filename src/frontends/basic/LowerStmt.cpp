// File: src/frontends/basic/LowerStmt.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements BASIC statement lowering routines targeting IL.
// Key invariants: Control-flow block creation remains deterministic via
//                 Lowerer::BlockNamer or NameMangler fallbacks.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

// Requires the consolidated Lowerer interface for statement lowering helpers.
#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Visitor that dispatches statement lowering to Lowerer helpers.
class LowererStmtVisitor final : public StmtVisitor
{
  public:
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visit(const LabelStmt &) override {}

    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }

    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }

    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }

    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }

    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }

    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }

    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    void visit(const GosubStmt &stmt) override
    {
        lowerer_.lowerGosub(stmt);
    }

    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }

    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }

    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }

    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }

    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }

    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    void visit(const FunctionDecl &) override {}

    void visit(const SubDecl &) override {}

    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

  private:
    Lowerer &lowerer_;
};

/// @brief Lower a BASIC statement subtree into IL form.
/// @param stmt AST statement to lower.
/// @details Dispatches on the dynamic statement type and forwards to the
///          specialized helpers. The entry point updates @ref curLoc before
///          delegating so emitted instructions and diagnostics reference the
///          statement's source location. Helpers invoked from here may mutate
///          @ref cur when they split control flow; once a RETURN emits a
///          terminator the routine relies on @ref BasicBlock::terminated to
///          avoid lowering subsequent statements in the block.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
}

void Lowerer::visit(const ClsStmt &s)
{
    curLoc = s.loc;
    requestHelper(il::runtime::RuntimeFeature::TermCls);
    emitCallRet(Type(Type::Kind::Void), "rt_term_cls", {});
}

void Lowerer::visit(const ColorStmt &s)
{
    curLoc = s.loc;
    auto fg = ensureI64(lowerExpr(*s.fg), s.loc);
    Value bgv = Value::constInt(-1);
    if (s.bg)
    {
        auto bg = ensureI64(lowerExpr(*s.bg), s.loc);
        bgv = bg.value;
    }
    Value fg32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), fg.value);
    Value bg32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), bgv);
    requestHelper(il::runtime::RuntimeFeature::TermColor);
    emitCallRet(Type(Type::Kind::Void), "rt_term_color_i32", {fg32, bg32});
}

void Lowerer::visit(const LocateStmt &s)
{
    curLoc = s.loc;
    auto row = ensureI64(lowerExpr(*s.row), s.loc);
    Value colv = Value::constInt(1);
    if (s.col)
    {
        auto col = ensureI64(lowerExpr(*s.col), s.loc);
        colv = col.value;
    }
    Value row32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), row.value);
    Value col32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), colv);
    requestHelper(il::runtime::RuntimeFeature::TermLocate);
    emitCallRet(Type(Type::Kind::Void), "rt_term_locate_i32", {row32, col32});
}

/// @brief Lower each statement within a statement list sequentially.
/// @param stmt StmtList aggregating multiple statements on one line.
/// @details Invokes @ref lowerStmt for every child while respecting
///          terminators emitted by earlier statements.
void Lowerer::lowerStmtList(const StmtList &stmt)
{
    for (const auto &child : stmt.stmts)
    {
        if (!child)
            continue;
        BasicBlock *current = context().current();
        if (current && current->terminated)
            break;
        lowerStmt(*child);
    }
}

/// @brief Lower a CALL statement invoking a SUB.
/// @param stmt CALL statement node containing the invocation expression.
/// @details Delegates to expression lowering, discarding any produced value.
void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

/// @brief Lower a RETURN statement optionally yielding a value.
/// @param stmt RETURN statement describing the result expression.
/// @details Lowers the optional return value and emits the corresponding IL
///          return terminator, mirroring the legacy dispatch logic.
void Lowerer::lowerReturn(const ReturnStmt &stmt)
{
    if (stmt.isGosubReturn)
    {
        lowerGosubReturn(stmt);
        return;
    }

    if (stmt.value)
    {
        RVal v = lowerExpr(*stmt.value);
        emitRet(v.value);
    }
    else
    {
        emitRetVoid();
    }
}

Lowerer::RVal Lowerer::normalizeChannelToI32(RVal channel, il::support::SourceLoc loc)
{
    if (channel.type.kind == Type::Kind::I32)
        return channel;

    channel = ensureI64(std::move(channel), loc);
    curLoc = loc;
    channel.value = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), channel.value);
    channel.type = Type(Type::Kind::I32);
    return channel;
}

void Lowerer::emitRuntimeErrCheck(Value err,
                                  il::support::SourceLoc loc,
                                  std::string_view labelStem,
                                  const std::function<void(Value)> &onFailure)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (!func || !original)
        return;

    size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string stem(labelStem);
    std::string failLbl = blockNamer ? blockNamer->generic(stem + "_fail")
                                     : mangler.block(stem + "_fail");
    std::string contLbl = blockNamer ? blockNamer->generic(stem + "_cont")
                                     : mangler.block(stem + "_cont");

    size_t failIdx = func->blocks.size();
    builder->addBlock(*func, failLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    BasicBlock *failBlk = &func->blocks[failIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(&func->blocks[curIdx]);
    curLoc = loc;
    Value isFail = emitBinary(Opcode::ICmpNe, ilBoolTy(), err, Value::constInt(0));
    emitCBr(isFail, failBlk, contBlk);

    ctx.setCurrent(failBlk);
    curLoc = loc;
    onFailure(err);

    ctx.setCurrent(contBlk);
}

void Lowerer::lowerOpen(const OpenStmt &stmt)
{
    if (!stmt.pathExpr || !stmt.channelExpr)
        return;

    RVal path = lowerExpr(*stmt.pathExpr);
    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value modeValue = emitUnary(Opcode::CastSiNarrowChk,
                                Type(Type::Kind::I32),
                                Value::constInt(static_cast<int32_t>(stmt.mode)));

    Value err = emitCallRet(Type(Type::Kind::I32),
                            "rt_open_err_vstr",
                            {path.value, modeValue, channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "open", [&](Value code) {
        emitTrapFromErr(code);
    });
}

void Lowerer::lowerClose(const CloseStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value err = emitCallRet(Type(Type::Kind::I32), "rt_close_err", {channel.value});

    emitRuntimeErrCheck(err, stmt.loc, "close", [&](Value code) {
        emitTrapFromErr(code);
    });
}

/// @brief Lower an assignment or array store.
/// @param stmt Assignment statement describing the destination and source.
/// @details Evaluates the right-hand side, performs BASIC-to-IL conversions
///          (boolean extension, integer/floating conversions), and writes into
///          either a scalar slot or computed array address. The routine keeps
///          @ref cur unchanged but stamps each emitted instruction with
///          @ref curLoc so downstream diagnostics and runtime traps report the
///          correct location.
void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal v = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        const auto *info = findSymbol(var->name);
        assert(info && info->slotId);
        SlotType slotInfo = getSlotType(var->name);
        Type targetTy = slotInfo.type;
        bool isArray = slotInfo.isArray;
        bool isStr = targetTy.kind == Type::Kind::Str;
        bool isF64 = targetTy.kind == Type::Kind::F64;
        bool isBool = slotInfo.isBoolean;
        if (!isArray)
        {
            if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::I1)
            {
                v = coerceToI64(std::move(v), stmt.loc);
            }
            if (isF64 && v.type.kind == Type::Kind::I64)
            {
                v = coerceToF64(std::move(v), stmt.loc);
            }
            else if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::F64)
            {
                v = coerceToI64(std::move(v), stmt.loc);
            }
        }
        Value slot = Value::temp(*info->slotId);
        curLoc = stmt.loc;
        if (isArray)
            storeArray(slot, v.value);
        else
        {
            if (targetTy.kind == Type::Kind::I1 && v.type.kind != Type::Kind::I1)
            {
                v = coerceToBool(std::move(v), stmt.loc);
            }
            if (isStr)
            {
                requireStrReleaseMaybe();
                Value oldValue = emitLoad(targetTy, slot);
                emitCall("rt_str_release_maybe", {oldValue});
                requireStrRetainMaybe();
                emitCall("rt_str_retain_maybe", {v.value});
                emitStore(targetTy, slot, v.value);
            }
            else
            {
                emitStore(targetTy, slot, v.value);
            }
        }
    }
    else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
    {
        if (v.type.kind == Type::Kind::I1)
        {
            v = coerceToI64(std::move(v), stmt.loc);
        }
        ArrayAccess access = lowerArrayAccess(*arr, ArrayAccessKind::Store);
        curLoc = stmt.loc;
        emitCall("rt_arr_i32_set", {access.base, access.index, v.value});
    }
}

/// @brief Lower a PRINT statement into runtime calls.
/// @param stmt PRINT statement describing expression/items.
/// @details Iterates over the queued print items, converting boolean results
///          to integers and selecting the runtime shim (@c rt_print_str,
///          @c rt_print_i64, or @c rt_print_f64). Commas and semicolons are
///          translated into spacing/newline control, and a trailing semicolon
///          suppresses the newline emission. The procedure does not mutate
///          @ref cur but refreshes @ref curLoc for every runtime call to
///          propagate accurate diagnostic locations.
void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    for (const auto &it : stmt.items)
    {
        switch (it.kind)
        {
            case PrintItem::Kind::Expr:
            {
                RVal value = lowerExpr(*it.expr);
                curLoc = stmt.loc;
                if (value.type.kind == Type::Kind::Str)
                {
                    emitCall("rt_print_str", {value.value});
                    break;
                }
                if (value.type.kind == Type::Kind::F64)
                {
                    emitCall("rt_print_f64", {value.value});
                    break;
                }
                value = lowerScalarExpr(std::move(value), stmt.loc);
                emitCall("rt_print_i64", {value.value});
                break;
            }
            case PrintItem::Kind::Comma:
            {
                std::string spaceLbl = getStringLabel(" ");
                Value sp = emitConstStr(spaceLbl);
                curLoc = stmt.loc;
                emitCall("rt_print_str", {sp});
                break;
            }
            case PrintItem::Kind::Semicolon:
                break;
        }
    }

    bool suppress_nl = !stmt.items.empty() && stmt.items.back().kind == PrintItem::Kind::Semicolon;
    if (!suppress_nl)
    {
        std::string nlLbl = getStringLabel("\n");
        Value nl = emitConstStr(nlLbl);
        curLoc = stmt.loc;
        emitCall("rt_print_str", {nl});
    }
}

void Lowerer::lowerPrintCh(const PrintChStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    auto lowerArgToString = [&](const Expr &expr, RVal value) -> Value {
        if (value.type.kind == Type::Kind::Str)
            return value.value;

        TypeRules::NumericType numericType = classifyNumericType(expr);
        const char *runtime = nullptr;
        RuntimeFeature feature = RuntimeFeature::StrFromDouble;

        auto narrowInteger = [&](Type::Kind target) {
            value = ensureI64(std::move(value), expr.loc);
            curLoc = expr.loc;
            value.value = emitUnary(Opcode::CastSiNarrowChk, Type(target), value.value);
            value.type = Type(target);
        };

        switch (numericType)
        {
            case TypeRules::NumericType::Integer:
                runtime = "rt_str_i16_alloc";
                feature = RuntimeFeature::StrFromI16;
                narrowInteger(Type::Kind::I16);
                break;
            case TypeRules::NumericType::Long:
                runtime = "rt_str_i32_alloc";
                feature = RuntimeFeature::StrFromI32;
                narrowInteger(Type::Kind::I32);
                break;
            case TypeRules::NumericType::Single:
                runtime = "rt_str_f_alloc";
                feature = RuntimeFeature::StrFromSingle;
                value = ensureF64(std::move(value), expr.loc);
                break;
            case TypeRules::NumericType::Double:
            default:
                runtime = "rt_str_d_alloc";
                feature = RuntimeFeature::StrFromDouble;
                value = ensureF64(std::move(value), expr.loc);
                break;
        }

        requestHelper(feature);
        curLoc = expr.loc;
        return emitCallRet(Type(Type::Kind::Str), runtime, {value.value});
    };

    if (stmt.args.empty())
    {
        if (stmt.trailingNewline)
        {
            std::string emptyLbl = getStringLabel("");
            Value empty = emitConstStr(emptyLbl);
            curLoc = stmt.loc;
            Value err = emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, empty});
            emitRuntimeErrCheck(err, stmt.loc, "printch", [&](Value code) {
                emitTrapFromErr(code);
            });
        }
        return;
    }

    for (const auto &arg : stmt.args)
    {
        if (!arg)
            continue;
        RVal value = lowerExpr(*arg);
        Value text = lowerArgToString(*arg, std::move(value));
        curLoc = arg->loc;
        Value err = emitCallRet(Type(Type::Kind::I32), "rt_println_ch_err", {channel.value, text});
        emitRuntimeErrCheck(err, arg->loc, "printch", [&](Value code) {
            emitTrapFromErr(code);
        });
    }
}

/// @brief Reserve the block skeleton for an IF/ELSE ladder.
/// @param conds Number of conditions (primary IF plus ELSE IF arms).
/// @return Index bundle describing the inserted test, then, else, and exit
///         blocks.
/// @details Extends the current function with deterministically named blocks
///          using @ref blockNamer or the @ref NameMangler fallback. The helper
///          temporarily records the current block index and restores @ref cur
///          once the new blocks are appended.
Lowerer::IfBlocks Lowerer::emitIfBlocks(size_t conds)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    size_t start = func->blocks.size();
    unsigned firstId = 0;
    for (size_t i = 0; i < conds; ++i)
    {
        unsigned id = blockNamer ? blockNamer->nextIf() : static_cast<unsigned>(i);
        if (i == 0)
            firstId = id;
        std::string testLbl = blockNamer ? blockNamer->generic("if_test")
                                         : mangler.block("if_test_" + std::to_string(i));
        std::string thenLbl = blockNamer ? blockNamer->ifThen(id)
                                         : mangler.block("if_then_" + std::to_string(i));
        builder->addBlock(*func, testLbl);
        builder->addBlock(*func, thenLbl);
    }
    std::string elseLbl = blockNamer ? blockNamer->ifElse(firstId) : mangler.block("if_else");
    std::string endLbl = blockNamer ? blockNamer->ifEnd(firstId) : mangler.block("if_exit");
    builder->addBlock(*func, elseLbl);
    builder->addBlock(*func, endLbl);
    ctx.setCurrent(&func->blocks[curIdx]);
    std::vector<size_t> testIdx(conds);
    std::vector<size_t> thenIdx(conds);
    for (size_t i = 0; i < conds; ++i)
    {
        testIdx[i] = start + 2 * i;
        thenIdx[i] = start + 2 * i + 1;
    }
    size_t elseIdx = start + 2 * conds;
    size_t exitIdx = start + 2 * conds + 1;
    return {std::move(testIdx), std::move(thenIdx), elseIdx, exitIdx};
}

/// @brief Lower the conditional branch used by an IF arm.
/// @param cond Expression providing the truth value.
/// @param testBlk Block that evaluates the condition.
/// @param thenBlk Destination when the condition is true.
/// @param falseBlk Destination when the condition is false.
/// @param loc Source location for diagnostics.
/// @details Moves @ref cur to @p testBlk, converts the expression to an I1 if
///          necessary, and emits a conditional branch that targets @p thenBlk
///          or @p falseBlk. @ref curLoc is refreshed so diagnostics generated by
///          failed conversions or runtime checks report @p loc.
void Lowerer::lowerIfCondition(const Expr &cond,
                               BasicBlock *testBlk,
                               BasicBlock *thenBlk,
                               BasicBlock *falseBlk,
                               il::support::SourceLoc loc)
{
    context().setCurrent(testBlk);
    lowerCondBranch(cond, thenBlk, falseBlk, loc);
}

/// @brief Lower the body of a single IF or ELSE branch.
/// @param stmt Statement executed when the branch is taken (may be null).
/// @param thenBlk Block that holds the branch body.
/// @param exitBlk Merge block for fall-through control flow.
/// @param loc Source location for diagnostics.
/// @return @c true when the branch falls through to @p exitBlk.
/// @details Positions @ref cur at @p thenBlk, lowers the branch body if
///          present, and emits an explicit jump to @p exitBlk when the lowered
///          code left the block unterminated. The helper sets @ref curLoc before
///          emitting the merge branch, ensuring diagnostics attribute to
///          @p loc.
bool Lowerer::lowerIfBranch(const Stmt *stmt,
                            BasicBlock *thenBlk,
                            BasicBlock *exitBlk,
                            il::support::SourceLoc loc)
{
    context().setCurrent(thenBlk);
    if (stmt)
        lowerStmt(*stmt);
    BasicBlock *current = context().current();
    if (current && !current->terminated)
    {
        curLoc = loc;
        emitBr(exitBlk);
        return true;
    }
    return false;
}

/// @brief Lower an IF/ELSEIF/ELSE cascade.
/// @param stmt IF statement containing branches and optional else.
/// @details Allocates the block layout via @ref emitIfBlocks, sequentially
///          lowers each condition and branch, and merges control flow into the
///          shared exit block when at least one branch falls through. The
///          routine mutates @ref cur as it walks through the generated blocks
///          and keeps @ref curLoc aligned with the IF source span for emitted
///          diagnostics.
void Lowerer::lowerIf(const IfStmt &stmt)
{
    size_t conds = 1 + stmt.elseifs.size();
    IfBlocks blocks = emitIfBlocks(conds);
    std::vector<const Expr *> condExprs;
    std::vector<const Stmt *> thenStmts;
    condExprs.push_back(stmt.cond.get());
    thenStmts.push_back(stmt.then_branch.get());
    for (const auto &e : stmt.elseifs)
    {
        condExprs.push_back(e.cond.get());
        thenStmts.push_back(e.then_branch.get());
    }

    curLoc = stmt.loc;
    Function *func = context().function();
    assert(func && "lowerIf requires an active function");
    emitBr(&func->blocks[blocks.tests[0]]);

    bool fallthrough = false;
    for (size_t i = 0; i < conds; ++i)
    {
        BasicBlock *testBlk = &func->blocks[blocks.tests[i]];
        BasicBlock *thenBlk = &func->blocks[blocks.thens[i]];
        BasicBlock *falseBlk = (i + 1 < conds)
                                   ? &func->blocks[blocks.tests[i + 1]]
                                   : &func->blocks[blocks.elseIdx];
        lowerIfCondition(*condExprs[i], testBlk, thenBlk, falseBlk, stmt.loc);
        thenBlk = &func->blocks[blocks.thens[i]];
        BasicBlock *exitBlk = &func->blocks[blocks.exitIdx];
        bool branchFall = lowerIfBranch(thenStmts[i], thenBlk, exitBlk, stmt.loc);
        fallthrough = fallthrough || branchFall;
    }

    BasicBlock *elseBlk = &func->blocks[blocks.elseIdx];
    BasicBlock *exitBlk = &func->blocks[blocks.exitIdx];
    bool elseFall = lowerIfBranch(stmt.else_branch.get(), elseBlk, exitBlk, stmt.loc);
    fallthrough = fallthrough || elseFall;

    if (!fallthrough)
    {
        func->blocks.pop_back();
        context().setCurrent(&func->blocks[blocks.elseIdx]);
        return;
    }

    context().setCurrent(&func->blocks[blocks.exitIdx]);
}

/// @brief Lower statements forming a loop body until a terminator is hit.
/// @param body Sequence of statements comprising the loop body.
/// @details Iterates @p body and delegates to @ref lowerStmt while respecting
///          the current block's termination state. When @ref cur becomes null
///          or terminated the helper stops lowering additional statements.
void Lowerer::lowerLoopBody(const std::vector<StmtPtr> &body)
{
    for (const auto &stmt : body)
    {
        if (!stmt)
            continue;
        lowerStmt(*stmt);
        BasicBlock *current = context().current();
        if (!current || current->terminated)
            break;
    }
}

/// @brief Lower a WHILE loop into head/body/done blocks.
/// @param stmt WHILE statement describing the loop structure.
/// @details Adds head/body/done blocks, branches into the head, and enforces an
///          I1 condition before branching to the loop body or exit. The body
///          reuses @ref lowerStmt for nested statements and, when it does not
///          terminate, jumps back to the head to re-evaluate the condition. The
///          method mutates @ref cur as it traverses head, body, and done blocks
///          and refreshes @ref curLoc for diagnostics tied to loop locations.
void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    // Adding blocks may reallocate the function's block list; capture index and
    // reacquire pointers to guarantee stability.
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerWhile requires an active function");
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextWhile() : 0;
    std::string headLbl = blockNamer ? blockNamer->whileHead(id) : mangler.block("loop_head");
    std::string bodyLbl = blockNamer ? blockNamer->whileBody(id) : mangler.block("loop_body");
    std::string doneLbl = blockNamer ? blockNamer->whileEnd(id) : mangler.block("done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);
    size_t headIdx = start;
    size_t bodyIdx = start + 1;
    size_t doneIdx = start + 2;
    BasicBlock *head = &func->blocks[headIdx];
    BasicBlock *body = &func->blocks[bodyIdx];
    BasicBlock *done = &func->blocks[doneIdx];

    ctx.loopState().push(done);

    curLoc = stmt.loc;
    emitBr(head);

    // head
    head = &func->blocks[headIdx];
    ctx.setCurrent(head);
    curLoc = stmt.loc;
    lowerCondBranch(*stmt.cond, body, done, stmt.loc);

    body = &func->blocks[bodyIdx];
    done = &func->blocks[doneIdx];

    // body
    ctx.setCurrent(body);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = current && current->terminated;
    if (!term)
    {
        head = &func->blocks[headIdx];
        curLoc = stmt.loc;
        emitBr(head);
    }

    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

void Lowerer::lowerDo(const DoStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerDo requires an active function");
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextDo() : 0;
    std::string headLbl = blockNamer ? blockNamer->doHead(id) : mangler.block("do_head");
    std::string bodyLbl = blockNamer ? blockNamer->doBody(id) : mangler.block("do_body");
    std::string doneLbl = blockNamer ? blockNamer->doEnd(id) : mangler.block("do_done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);
    size_t headIdx = start;
    size_t bodyIdx = start + 1;
    size_t doneIdx = start + 2;
    BasicBlock *done = &func->blocks[doneIdx];

    ctx.loopState().push(done);

    auto emitHead = [&]() {
        func->blocks[headIdx].label = headLbl;
        func->blocks[bodyIdx].label = bodyLbl;
        BasicBlock *head = &func->blocks[headIdx];
        ctx.setCurrent(head);
        curLoc = stmt.loc;
        if (stmt.condKind == DoStmt::CondKind::None)
        {
            emitBr(&func->blocks[bodyIdx]);
            return;
        }
        assert(stmt.cond && "DO loop missing condition for conditional form");
        BasicBlock *body = &func->blocks[bodyIdx];
        BasicBlock *doneBlk = &func->blocks[doneIdx];
        if (stmt.condKind == DoStmt::CondKind::While)
        {
            lowerCondBranch(*stmt.cond, body, doneBlk, stmt.loc);
        }
        else
        {
            lowerCondBranch(*stmt.cond, doneBlk, body, stmt.loc);
        }
    };

    switch (stmt.testPos)
    {
        case DoStmt::TestPos::Pre:
        {
            curLoc = stmt.loc;
            func->blocks[headIdx].label = headLbl;
            emitBr(&func->blocks[headIdx]);
            emitHead();
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
        }
        case DoStmt::TestPos::Post:
        {
            curLoc = stmt.loc;
            func->blocks[bodyIdx].label = bodyLbl;
            emitBr(&func->blocks[bodyIdx]);
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
        }
    }

    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = current && current->terminated;

    if (!term)
    {
        curLoc = stmt.loc;
        func->blocks[headIdx].label = headLbl;
        emitBr(&func->blocks[headIdx]);
    }

    if (stmt.testPos == DoStmt::TestPos::Post)
    {
        emitHead();
    }

    func->blocks[doneIdx].label = doneLbl;
    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

/// @brief Create the block layout shared by FOR loops.
/// @param varStep Whether the loop has a variable (runtime) step expression.
/// @return Descriptor pointing to the inserted head/body/inc/done blocks.
/// @details Appends the necessary blocks to @ref func using deterministic names
///          and restores @ref cur to the block active before allocation. When
///          @p varStep is @c true the helper adds both positive and negative
///          heads so the loop can branch based on the computed step sign.
Lowerer::ForBlocks Lowerer::setupForBlocks(bool varStep)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    size_t base = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextFor() : 0;
    ForBlocks fb;
    if (varStep)
    {
        std::string headPosLbl =
            blockNamer ? blockNamer->generic("for_head_pos") : mangler.block("for_head_pos");
        std::string headNegLbl =
            blockNamer ? blockNamer->generic("for_head_neg") : mangler.block("for_head_neg");
        builder->addBlock(*func, headPosLbl);
        builder->addBlock(*func, headNegLbl);
        fb.headPosIdx = base;
        fb.headNegIdx = base + 1;
        base += 2;
    }
    else
    {
        std::string headLbl = blockNamer ? blockNamer->forHead(id) : mangler.block("for_head");
        builder->addBlock(*func, headLbl);
        fb.headIdx = base;
        base += 1;
    }
    std::string bodyLbl = blockNamer ? blockNamer->forBody(id) : mangler.block("for_body");
    std::string incLbl = blockNamer ? blockNamer->forInc(id) : mangler.block("for_inc");
    std::string doneLbl = blockNamer ? blockNamer->forEnd(id) : mangler.block("for_done");
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, incLbl);
    builder->addBlock(*func, doneLbl);
    fb.bodyIdx = base;
    fb.incIdx = base + 1;
    fb.doneIdx = base + 2;
    ctx.setCurrent(&func->blocks[curIdx]);
    return fb;
}

/// @brief Lower a FOR loop whose STEP is a compile-time constant.
/// @param stmt Source FOR statement.
/// @param slot Storage slot for the induction variable.
/// @param end Evaluated end expression.
/// @param step Evaluated step expression.
/// @param stepConst Constant integer value of @p step.
/// @details Builds the canonical head/body/inc/done blocks and compares the
///          induction variable against the end bound using @c SCmpLE or
///          @c SCmpGE depending on the sign of @p stepConst. When the body does
///          not terminate it advances the induction variable via
///          @ref emitForStep and loops back to the head. The helper mutates
///          @ref cur as control moves across the loop blocks and tags emitted
///          instructions with @ref curLoc for diagnostics.
void Lowerer::lowerForConstStep(
    const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst)
{
    ForBlocks fb = setupForBlocks(false);
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerForConstStep requires an active function");
    size_t doneIdx = fb.doneIdx;
    BasicBlock *done = &func->blocks[doneIdx];
    ctx.loopState().push(done);
    curLoc = stmt.loc;
    emitBr(&func->blocks[fb.headIdx]);
    ctx.setCurrent(&func->blocks[fb.headIdx]);
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    Opcode cmp = stepConst >= 0 ? Opcode::SCmpLE : Opcode::SCmpGE;
    curLoc = stmt.loc;
    Value cond = emitBinary(cmp, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cond, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.bodyIdx]);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = current && current->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        ctx.setCurrent(&func->blocks[fb.incIdx]);
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.headIdx]);
    }
    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

/// @brief Lower a FOR loop whose STEP expression is evaluated at runtime.
/// @param stmt Source FOR statement.
/// @param slot Storage slot for the induction variable.
/// @param end Evaluated end expression.
/// @param step Evaluated step expression.
/// @details Computes the step sign, emits a branch to either the non-negative or
///          negative comparison head, and shares a single body/inc/done chain.
///          The method mutates @ref cur as it traverses these blocks and
///          updates @ref curLoc before each comparison and branch to preserve
///          accurate diagnostics.
void Lowerer::lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step)
{
    curLoc = stmt.loc;
    Value stepNonNeg =
        emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), step.value, Value::constInt(0));
    ForBlocks fb = setupForBlocks(true);
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerForVarStep requires an active function");
    size_t doneIdx = fb.doneIdx;
    BasicBlock *done = &func->blocks[doneIdx];
    ctx.loopState().push(done);
    curLoc = stmt.loc;
    emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    ctx.setCurrent(&func->blocks[fb.headPosIdx]);
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpPos = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpPos, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.headNegIdx]);
    curLoc = stmt.loc;
    curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpNeg = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpNeg, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.bodyIdx]);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = current && current->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        ctx.setCurrent(&func->blocks[fb.incIdx]);
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    }
    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

/// @brief Lower a BASIC FOR statement.
/// @param stmt Parsed FOR statement containing bounds and optional step.
/// @details Evaluates the start/end/step expressions once, stores the initial
///          value into the induction slot, and forwards to the sign-sensitive
///          lowering that selects the positive or negative branch at runtime.
///          The helper updates @ref curLoc for each emitted instruction and
///          leaves @ref cur at the block chosen by the delegated lowering
///          routine.
void Lowerer::lowerFor(const ForStmt &stmt)
{
    RVal start = lowerScalarExpr(*stmt.start);
    RVal end = lowerScalarExpr(*stmt.end);
    RVal step = stmt.step ? lowerScalarExpr(*stmt.step)
                          : RVal{Value::constInt(1), Type(Type::Kind::I64)};
    const auto *info = findSymbol(stmt.var);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    curLoc = stmt.loc;
    emitStore(Type(Type::Kind::I64), slot, start.value);

    lowerForVarStep(stmt, slot, end, step);
}

/// @brief Handle a NEXT marker.
/// @param next NEXT statement (ignored).
/// @details The lowering pipeline already encodes loop back-edges inside FOR
///          lowering, so NEXT does not emit IL and leaves @ref cur untouched.
void Lowerer::lowerNext(const NextStmt &next)
{
    (void)next;
}

void Lowerer::lowerExit(const ExitStmt &stmt)
{
    ProcedureContext &ctx = context();
    BasicBlock *target = ctx.loopState().current();
    curLoc = stmt.loc;
    if (!target)
    {
        emitTrap();
        return;
    }
    emitBr(target);
    ctx.loopState().markTaken();
}

void Lowerer::lowerGosub(const GosubStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    ensureGosubStack();

    auto &gosubState = ctx.gosub();
    auto contIndex = gosubState.indexFor(&stmt);
    if (!contIndex)
        contIndex = gosubState.registerContinuation(&stmt, ctx.exitIndex());

    curLoc = stmt.loc;
    Value sp = emitLoad(Type(Type::Kind::I64), gosubState.spSlot());

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    auto destIt = lineBlocks.find(stmt.targetLine);
    if (destIt == lineBlocks.end())
        return;

    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string overflowLbl = blockNamer ? blockNamer->generic("gosub_overflow")
                                         : mangler.block("gosub_overflow");
    std::string pushLbl = blockNamer ? blockNamer->generic("gosub_push")
                                     : mangler.block("gosub_push");

    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    size_t overflowIdx = func->blocks.size();
    builder->addBlock(*func, overflowLbl);
    size_t pushIdx = func->blocks.size();
    builder->addBlock(*func, pushLbl);

    func = ctx.function();
    BasicBlock *overflowBlk = &func->blocks[overflowIdx];
    BasicBlock *pushBlk = &func->blocks[pushIdx];
    current = &func->blocks[curIdx];
    ctx.setCurrent(current);

    Value limit = Value::constInt(kGosubStackDepth);
    Value overflow = emitBinary(Opcode::SCmpGE, ilBoolTy(), sp, limit);
    emitCBr(overflow, overflowBlk, pushBlk);

    ctx.setCurrent(overflowBlk);
    curLoc = stmt.loc;
    requireTrap();
    std::string overflowMsg = getStringLabel("gosub: stack overflow");
    Value overflowStr = emitConstStr(overflowMsg);
    emitCall("rt_trap", {overflowStr});
    emitTrap();

    ctx.setCurrent(pushBlk);
    curLoc = stmt.loc;

    Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64), sp, Value::constInt(4));
    Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), gosubState.stackSlot(), offset);
    emitStore(Type(Type::Kind::I32),
              slotPtr,
              Value::constInt(static_cast<long long>(*contIndex)));

    Value nextSp = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), sp, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), gosubState.spSlot(), nextSp);

    BasicBlock *target = &func->blocks[destIt->second];
    emitBr(target);
}

/// @brief Lower a GOTO jump.
/// @param stmt GOTO statement naming a BASIC line label.
/// @details Looks up the destination basic block recorded during statement
///          discovery and emits an unconditional branch. @ref curLoc is set so
///          diagnostics reference the jump site, and the resulting branch marks
///          the current block as terminated.
void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    auto &lineBlocks = context().blockNames().lineBlocks();
    auto it = lineBlocks.find(stmt.target);
    if (it != lineBlocks.end())
    {
        curLoc = stmt.loc;
        Function *func = context().function();
        assert(func && "lowerGoto requires an active function");
        emitBr(&func->blocks[it->second]);
    }
}

void Lowerer::lowerGosubReturn(const ReturnStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    ensureGosubStack();

    auto &gosubState = ctx.gosub();

    curLoc = stmt.loc;
    Value sp = emitLoad(Type(Type::Kind::I64), gosubState.spSlot());

    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string emptyLbl = blockNamer ? blockNamer->generic("gosub_ret_empty")
                                      : mangler.block("gosub_ret_empty");
    std::string contLbl = blockNamer ? blockNamer->generic("gosub_ret_cont")
                                     : mangler.block("gosub_ret_cont");

    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    size_t emptyIdx = func->blocks.size();
    builder->addBlock(*func, emptyLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    func = ctx.function();
    BasicBlock *emptyBlk = &func->blocks[emptyIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];
    current = &func->blocks[curIdx];
    ctx.setCurrent(current);

    Value isEmpty = emitBinary(Opcode::ICmpEq, ilBoolTy(), sp, Value::constInt(0));
    emitCBr(isEmpty, emptyBlk, contBlk);

    ctx.setCurrent(emptyBlk);
    curLoc = stmt.loc;
    requireTrap();
    std::string emptyMsg = getStringLabel("gosub: empty return stack");
    Value emptyStr = emitConstStr(emptyMsg);
    emitCall("rt_trap", {emptyStr});
    emitTrap();

    ctx.setCurrent(contBlk);
    curLoc = stmt.loc;

    Value nextSp = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), sp, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), gosubState.spSlot(), nextSp);

    Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64), nextSp, Value::constInt(4));
    Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), gosubState.stackSlot(), offset);
    Value idxVal = emitLoad(Type(Type::Kind::I32), slotPtr);

    std::string invalidLbl = blockNamer ? blockNamer->generic("gosub_ret_invalid")
                                        : mangler.block("gosub_ret_invalid");
    size_t invalidIdx = func->blocks.size();
    builder->addBlock(*func, invalidLbl);
    func = ctx.function();
    BasicBlock *invalidBlk = &func->blocks[invalidIdx];

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(idxVal);
    if (invalidBlk->label.empty())
        invalidBlk->label = nextFallbackBlockLabel();
    sw.labels.push_back(invalidBlk->label);
    sw.brArgs.push_back({});

    const auto &continuations = gosubState.continuations();
    for (unsigned i = 0; i < continuations.size(); ++i)
    {
        sw.operands.push_back(Value::constInt(static_cast<long long>(i)));
        BasicBlock *target = &func->blocks[continuations[i]];
        if (target->label.empty())
            target->label = nextFallbackBlockLabel();
        sw.labels.push_back(target->label);
        sw.brArgs.emplace_back();
    }
    sw.loc = stmt.loc;
    contBlk->instructions.push_back(std::move(sw));
    contBlk->terminated = true;

    ctx.setCurrent(invalidBlk);
    curLoc = stmt.loc;
    emitTrap();
}

void Lowerer::lowerOnErrorGoto(const OnErrorGoto &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    curLoc = stmt.loc;

    if (stmt.toZero)
    {
        clearActiveErrorHandler();
        return;
    }

    clearActiveErrorHandler();

    BasicBlock *handler = ensureErrorHandlerBlock(stmt.target);
    emitEhPush(handler);

    size_t idx = static_cast<size_t>(handler - &func->blocks[0]);
    ctx.errorHandlers().setActive(true);
    ctx.errorHandlers().setActiveIndex(idx);
    ctx.errorHandlers().setActiveLine(stmt.target);
}

void Lowerer::lowerResume(const Resume &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    if (!func)
        return;

    std::optional<size_t> handlerIndex;

    auto &handlersByLine = ctx.errorHandlers().blocks();
    if (auto it = handlersByLine.find(stmt.line); it != handlersByLine.end())
    {
        handlerIndex = it->second;
    }
    else if (auto active = ctx.errorHandlers().activeIndex())
    {
        handlerIndex = *active;
    }

    if (!handlerIndex || *handlerIndex >= func->blocks.size())
        return;

    BasicBlock &handlerBlock = func->blocks[*handlerIndex];
    if (handlerBlock.terminated)
        return;

    if (handlerBlock.params.size() < 2)
        return;

    unsigned tokId = handlerBlock.params[1].id;
    if (func->valueNames.size() <= tokId)
        func->valueNames.resize(tokId + 1);
    if (func->valueNames[tokId].empty())
        func->valueNames[tokId] = handlerBlock.params[1].name;

    Value resumeTok = Value::temp(tokId);

    Instr instr;
    instr.type = Type(Type::Kind::Void);
    instr.loc = curLoc;
    instr.operands.push_back(resumeTok);

    switch (stmt.mode)
    {
        case Resume::Mode::Same:
            instr.op = Opcode::ResumeSame;
            break;
        case Resume::Mode::Next:
            instr.op = Opcode::ResumeNext;
            break;
        case Resume::Mode::Label:
        {
            instr.op = Opcode::ResumeLabel;
            auto &lineBlocks = ctx.blockNames().lineBlocks();
            auto targetIt = lineBlocks.find(stmt.target);
            if (targetIt == lineBlocks.end())
                return;
            size_t targetIdx = targetIt->second;
            if (targetIdx >= func->blocks.size())
                return;
            instr.labels.push_back(func->blocks[targetIdx].label);
            break;
        }
    }

    handlerBlock.instructions.push_back(std::move(instr));
    handlerBlock.terminated = true;
}

/// @brief Lower an END statement.
/// @param stmt END statement closing the program.
/// @details Emits a return from the current block so execution does not fall
///          through to subsequent statements. The return uses @ref curLoc for
///          diagnostics and leaves the block terminated immediately.
void Lowerer::lowerEnd(const EndStmt &stmt)
{
    curLoc = stmt.loc;
    emitRet(Value::constInt(0));
}

/// @brief Lower an INPUT statement.
/// @param stmt INPUT statement providing optional prompt and destination.
/// @details Optionally prints the prompt via @c rt_print_str, then calls the
///          runtime @c rt_input_line helper to obtain a string. Numeric targets
///          convert the string with @c rt_to_int before storing. The routine
///          does not mutate @ref cur but refreshes @ref curLoc prior to each
///          runtime interaction for diagnostics.
void Lowerer::lowerInput(const InputStmt &stmt)
{
    curLoc = stmt.loc;
    if (stmt.prompt)
    {
        if (auto *se = dynamic_cast<const StringExpr *>(stmt.prompt.get()))
        {
            std::string lbl = getStringLabel(se->value);
            Value v = emitConstStr(lbl);
            emitCall("rt_print_str", {v});
        }
    }
    if (stmt.vars.empty())
        return;

    Value line = emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});

    auto storeField = [&](const std::string &name, Value field) {
        SlotType slotInfo = getSlotType(name);
        const auto *info = findSymbol(name);
        assert(info && info->slotId);
        Value target = Value::temp(*info->slotId);
        if (slotInfo.type.kind == Type::Kind::Str)
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::Str), target, field);
            return;
        }

        if (slotInfo.type.kind == Type::Kind::F64)
        {
            Value f = emitCallRet(Type(Type::Kind::F64), "rt_to_double", {field});
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::F64), target, f);
            requireStrReleaseMaybe();
            curLoc = stmt.loc;
            emitCall("rt_str_release_maybe", {field});
            return;
        }

        Value n = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {field});
        if (slotInfo.isBoolean)
        {
            Value b = coerceToBool({n, Type(Type::Kind::I64)}, stmt.loc).value;
            curLoc = stmt.loc;
            emitStore(ilBoolTy(), target, b);
        }
        else
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::I64), target, n);
        }
        requireStrReleaseMaybe();
        curLoc = stmt.loc;
        emitCall("rt_str_release_maybe", {field});
    };

    if (stmt.vars.size() == 1)
    {
        storeField(stmt.vars.front(), line);
        return;
    }

    const long long fieldCount = static_cast<long long>(stmt.vars.size());
    Value fields = emitAlloca(static_cast<int>(fieldCount * 8));
    emitCallRet(Type(Type::Kind::I64), "rt_split_fields", {line, fields, Value::constInt(fieldCount)});
    requireStrReleaseMaybe();
    curLoc = stmt.loc;
    emitCall("rt_str_release_maybe", {line});

    for (std::size_t i = 0; i < stmt.vars.size(); ++i)
    {
        long long offset = static_cast<long long>(i * 8);
        Value slot = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), fields, Value::constInt(offset));
        Value field = emitLoad(Type(Type::Kind::Str), slot);
        storeField(stmt.vars[i], field);
    }
}

void Lowerer::lowerLineInputCh(const LineInputChStmt &stmt)
{
    if (!stmt.channelExpr || !stmt.targetVar)
        return;

    RVal channel = lowerExpr(*stmt.channelExpr);
    channel = normalizeChannelToI32(std::move(channel), stmt.loc);

    curLoc = stmt.loc;
    Value outSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::Ptr), outSlot, Value::null());

    Value err = emitCallRet(Type(Type::Kind::I32), "rt_line_input_ch_err", {channel.value, outSlot});

    emitRuntimeErrCheck(err, stmt.loc, "lineinputch", [&](Value code) {
        emitTrapFromErr(code);
    });

    curLoc = stmt.loc;
    Value line = emitLoad(Type(Type::Kind::Str), outSlot);

    if (const auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get()))
    {
        const auto *info = findSymbol(var->name);
        if (!info || !info->slotId)
            return;
        Value slot = Value::temp(*info->slotId);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::Str), slot, line);
    }
}

/// @brief Lower a DIM array allocation.
/// @param stmt DIM statement describing the array name and size.
/// @details Evaluates the requested element count, allocates backing storage
///          via @c rt_arr_i32_new, and stores the resulting handle into the
///          array slot. When @ref boundsChecks is enabled it also records the
///          logical length so runtime bounds checks can read it. Instructions
///          are tagged with @ref curLoc for diagnostics; @ref cur itself
///          remains unchanged.
void Lowerer::lowerDim(const DimStmt &stmt)
{
    RVal bound = lowerExpr(*stmt.size);
    bound = ensureI64(std::move(bound), stmt.loc);
    curLoc = stmt.loc;

    Value length = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), bound.value, Value::constInt(1));

    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (func && original)
    {
        size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
        BlockNamer *blockNamer = ctx.blockNames().namer();
        std::string failLbl = blockNamer ? blockNamer->generic("dim_len_fail")
                                         : mangler.block("dim_len_fail");
        std::string contLbl = blockNamer ? blockNamer->generic("dim_len_cont")
                                         : mangler.block("dim_len_cont");

        size_t failIdx = func->blocks.size();
        builder->addBlock(*func, failLbl);
        size_t contIdx = func->blocks.size();
        builder->addBlock(*func, contLbl);

        BasicBlock *failBlk = &func->blocks[failIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[curIdx]);
        curLoc = stmt.loc;
        Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), length, Value::constInt(0));
        emitCBr(isNeg, failBlk, contBlk);

        ctx.setCurrent(failBlk);
        curLoc = stmt.loc;
        emitTrap();

        ctx.setCurrent(contBlk);
    }

    curLoc = stmt.loc;
    Value handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_new", {length});
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    storeArray(Value::temp(*info->slotId), handle);
    if (boundsChecks)
    {
        if (info && info->arrayLengthSlot)
            emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
    }
}

/// @brief Lower a REDIM array reallocation.
/// @param stmt REDIM statement describing the new size.
/// @details Re-evaluates the target length, invokes @c rt_arr_i32_resize to
///          adjust the array storage, and stores the returned handle into the
///          tracked array slot, mirroring DIM lowering semantics.
void Lowerer::lowerReDim(const ReDimStmt &stmt)
{
    RVal bound = lowerExpr(*stmt.size);
    bound = ensureI64(std::move(bound), stmt.loc);
    curLoc = stmt.loc;

    Value length = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), bound.value, Value::constInt(1));

    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (func && original)
    {
        size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
        BlockNamer *blockNamer = ctx.blockNames().namer();
        std::string failLbl = blockNamer ? blockNamer->generic("redim_len_fail")
                                         : mangler.block("redim_len_fail");
        std::string contLbl = blockNamer ? blockNamer->generic("redim_len_cont")
                                         : mangler.block("redim_len_cont");

        size_t failIdx = func->blocks.size();
        builder->addBlock(*func, failLbl);
        size_t contIdx = func->blocks.size();
        builder->addBlock(*func, contLbl);

        BasicBlock *failBlk = &func->blocks[failIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[curIdx]);
        curLoc = stmt.loc;
        Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), length, Value::constInt(0));
        emitCBr(isNeg, failBlk, contBlk);

        ctx.setCurrent(failBlk);
        curLoc = stmt.loc;
        emitTrap();

        ctx.setCurrent(contBlk);
    }

    curLoc = stmt.loc;
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value current = emitLoad(Type(Type::Kind::Ptr), slot);
    Value resized = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_resize", {current, length});
    storeArray(slot, resized);
    if (boundsChecks && info && info->arrayLengthSlot)
        emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
}

/// @brief Lower a RANDOMIZE seed update.
/// @param stmt RANDOMIZE statement carrying the seed expression.
/// @details Converts the seed expression to a 64-bit integer when necessary and
///          invokes the runtime @c rt_randomize_i64 helper. @ref curLoc is
///          updated to associate diagnostics with the statement while leaving
///          @ref cur unchanged.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic

