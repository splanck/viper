// File: src/frontends/basic/LoweringPipeline.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helper structs that orchestrate BASIC lowering stages.
// Key invariants: Helpers mutate Lowerer state in isolation per stage invocation.
// Ownership/Lifetime: Helpers borrow Lowerer references owned by callers.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include <cassert>

namespace il::frontends::basic
{

namespace pipeline_detail
{
il::core::Type coreTypeForAstType(::il::frontends::basic::Type ty)
{
    using il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return Type(Type::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return Type(Type::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return Type(Type::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}
} // namespace pipeline_detail

using pipeline_detail::coreTypeForAstType;

namespace
{

/// @brief Expression visitor accumulating symbol metadata for referenced variables and arrays.
class VarCollectExprVisitor final : public ExprVisitor
{
  public:
    explicit VarCollectExprVisitor(Lowerer &lowerer) : lowerer_(lowerer) {}

    void visit(const IntExpr &) override {}

    void visit(const FloatExpr &) override {}

    void visit(const StringExpr &) override {}

    void visit(const BoolExpr &) override {}

    void visit(const VarExpr &expr) override
    {
        lowerer_.markSymbolReferenced(expr.name);
    }

    void visit(const ArrayExpr &expr) override
    {
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
        if (expr.index)
            expr.index->accept(*this);
    }

    void visit(const LBoundExpr &expr) override
    {
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
    }

    void visit(const UBoundExpr &expr) override
    {
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
    }

    void visit(const UnaryExpr &expr) override
    {
        if (expr.expr)
            expr.expr->accept(*this);
    }

    void visit(const BinaryExpr &expr) override
    {
        if (expr.lhs)
            expr.lhs->accept(*this);
        if (expr.rhs)
            expr.rhs->accept(*this);
    }

    void visit(const CallExpr &expr) override
    {
        for (const auto &arg : expr.args)
            if (arg)
                arg->accept(*this);
    }

    void visit(const BuiltinCallExpr &expr) override
    {
        for (const auto &arg : expr.args)
            if (arg)
                arg->accept(*this);
    }

  private:
    Lowerer &lowerer_;
};

/// @brief Statement visitor walking child expressions/statements to collect names.
class VarCollectStmtVisitor final : public StmtVisitor
{
  public:
    VarCollectStmtVisitor(VarCollectExprVisitor &exprVisitor, Lowerer &lowerer)
        : exprVisitor_(exprVisitor), lowerer_(lowerer)
    {
    }

    void visit(const PrintStmt &stmt) override
    {
        for (const auto &item : stmt.items)
            if (item.kind == PrintItem::Kind::Expr && item.expr)
                item.expr->accept(exprVisitor_);
    }

    void visit(const PrintChStmt &stmt) override
    {
        if (stmt.channelExpr)
            stmt.channelExpr->accept(exprVisitor_);
        for (const auto &arg : stmt.args)
            if (arg)
                arg->accept(exprVisitor_);
    }

    void visit(const LetStmt &stmt) override
    {
        if (stmt.target)
            stmt.target->accept(exprVisitor_);
        if (stmt.expr)
            stmt.expr->accept(exprVisitor_);
    }

    void visit(const DimStmt &stmt) override
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
        if (stmt.size)
            stmt.size->accept(exprVisitor_);
    }

    void visit(const ReDimStmt &stmt) override
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
        if (stmt.size)
            stmt.size->accept(exprVisitor_);
    }

    void visit(const RandomizeStmt &stmt) override
    {
        if (stmt.seed)
            stmt.seed->accept(exprVisitor_);
    }

    void visit(const IfStmt &stmt) override
    {
        if (stmt.cond)
            stmt.cond->accept(exprVisitor_);
        if (stmt.then_branch)
            stmt.then_branch->accept(*this);
        for (const auto &elseif : stmt.elseifs)
        {
            if (elseif.cond)
                elseif.cond->accept(exprVisitor_);
            if (elseif.then_branch)
                elseif.then_branch->accept(*this);
        }
        if (stmt.else_branch)
            stmt.else_branch->accept(*this);
    }

    void visit(const WhileStmt &stmt) override
    {
        if (stmt.cond)
            stmt.cond->accept(exprVisitor_);
        for (const auto &sub : stmt.body)
            if (sub)
                sub->accept(*this);
    }

    void visit(const DoStmt &stmt) override
    {
        if (stmt.cond)
            stmt.cond->accept(exprVisitor_);
        for (const auto &sub : stmt.body)
            if (sub)
                sub->accept(*this);
    }

    void visit(const ForStmt &stmt) override
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
        if (stmt.start)
            stmt.start->accept(exprVisitor_);
        if (stmt.end)
            stmt.end->accept(exprVisitor_);
        if (stmt.step)
            stmt.step->accept(exprVisitor_);
        for (const auto &sub : stmt.body)
            if (sub)
                sub->accept(*this);
    }

    void visit(const NextStmt &stmt) override
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void visit(const ExitStmt &) override {}

    void visit(const GotoStmt &) override {}

    void visit(const OpenStmt &stmt) override
    {
        if (stmt.pathExpr)
            stmt.pathExpr->accept(exprVisitor_);
        if (stmt.channelExpr)
            stmt.channelExpr->accept(exprVisitor_);
    }

    void visit(const CloseStmt &stmt) override
    {
        if (stmt.channelExpr)
            stmt.channelExpr->accept(exprVisitor_);
    }

    void visit(const OnErrorGoto &) override {}

    void visit(const Resume &) override {}

    void visit(const EndStmt &) override {}

    void visit(const InputStmt &stmt) override
    {
        if (stmt.prompt)
            stmt.prompt->accept(exprVisitor_);
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void visit(const LineInputChStmt &stmt) override
    {
        if (stmt.channelExpr)
            stmt.channelExpr->accept(exprVisitor_);
        if (stmt.targetVar)
            stmt.targetVar->accept(exprVisitor_);
    }

    void visit(const ReturnStmt &stmt) override
    {
        if (stmt.value)
            stmt.value->accept(exprVisitor_);
    }

    void visit(const FunctionDecl &stmt) override
    {
        for (const auto &bodyStmt : stmt.body)
            if (bodyStmt)
                bodyStmt->accept(*this);
    }

    void visit(const SubDecl &stmt) override
    {
        for (const auto &bodyStmt : stmt.body)
            if (bodyStmt)
                bodyStmt->accept(*this);
    }

    void visit(const StmtList &stmt) override
    {
        for (const auto &sub : stmt.stmts)
            if (sub)
                sub->accept(*this);
    }

  private:
    VarCollectExprVisitor &exprVisitor_;
    Lowerer &lowerer_;
};

} // namespace

ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void ProgramLowering::run(const Program &prog, il::core::Module &module)
{
    lowerer.mod = &module;
    build::IRBuilder builder(module);
    lowerer.builder = &builder;

    lowerer.mangler = NameMangler();
    auto &ctx = lowerer.context();
    ctx.reset();
    lowerer.symbols.clear();
    lowerer.nextStringId = 0;
    lowerer.procSignatures.clear();

    lowerer.runtimeTracker.reset();
    lowerer.resetManualHelpers();

    lowerer.scanProgram(prog);
    lowerer.declareRequiredRuntime(builder);
    lowerer.emitProgram(prog);

    lowerer.builder = nullptr;
    lowerer.mod = nullptr;
}

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void ProcedureLowering::collectProcedureSignatures(const Program &prog)
{
    lowerer.procSignatures.clear();
    for (const auto &decl : prog.procs)
    {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = coreTypeForAstType(fn->ret);
            sig.paramTypes.reserve(fn->params.size());
            for (const auto &p : fn->params)
            {
                il::core::Type ty = p.is_array
                                        ? il::core::Type(il::core::Type::Kind::Ptr)
                                        : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(fn->name, std::move(sig));
        }
        else if (auto *sub = dynamic_cast<const SubDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = il::core::Type(il::core::Type::Kind::Void);
            sig.paramTypes.reserve(sub->params.size());
            for (const auto &p : sub->params)
            {
                il::core::Type ty = p.is_array
                                        ? il::core::Type(il::core::Type::Kind::Ptr)
                                        : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(sub->name, std::move(sig));
        }
    }
}

void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectExprVisitor exprVisitor(lowerer);
    VarCollectStmtVisitor stmtVisitor(exprVisitor, lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            stmt->accept(stmtVisitor);
}

void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

void ProcedureLowering::emit(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const Lowerer::ProcedureConfig &config)
{
    lowerer.resetLoweringState();
    auto &ctx = lowerer.context();

    Lowerer::ProcedureMetadata metadata =
        lowerer.collectProcedureMetadata(params, body, config);

    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    il::core::Function &f =
        lowerer.builder->startFunction(name, config.retType, metadata.irParams);
    ctx.setFunction(&f);
    ctx.setNextTemp(f.valueNames.size());

    lowerer.buildProcedureSkeleton(f, name, metadata);

    ctx.setCurrent(&f.blocks.front());
    lowerer.materializeParams(params);
    lowerer.allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    if (metadata.bodyStmts.empty())
    {
        lowerer.curLoc = {};
        config.emitEmptyBody();
        ctx.resetBlockNamer();
        return;
    }

    lowerer.lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);

    ctx.setCurrent(&f.blocks[ctx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseArrayLocals(metadata.paramNames);
    lowerer.releaseArrayParams(metadata.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    ctx.resetBlockNamer();
}

StatementLowering::StatementLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void StatementLowering::lowerSequence(
    const std::vector<const Stmt *> &stmts,
    bool stopOnTerminated,
    const std::function<void(const Stmt &)> &beforeBranch)
{
    if (stmts.empty())
        return;

    lowerer.curLoc = {};
    auto &ctx = lowerer.context();
    auto *func = ctx.function();
    assert(func && "lowerSequence requires an active function");
    auto &lineBlocks = ctx.lineBlocks();
    lowerer.emitBr(&func->blocks[lineBlocks[stmts.front()->line]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        ctx.setCurrent(&func->blocks[lineBlocks[stmt.line]]);
        lowerer.lowerStmt(stmt);
        auto *current = ctx.current();
        if (current && current->terminated)
        {
            if (stopOnTerminated)
                break;
            continue;
        }
        auto *next =
            (i + 1 < stmts.size())
                ? &func->blocks[lineBlocks[stmts[i + 1]->line]]
                : &func->blocks[ctx.exitIndex()];
        if (beforeBranch)
            beforeBranch(stmt);
        lowerer.emitBr(next);
    }
}

} // namespace il::frontends::basic
