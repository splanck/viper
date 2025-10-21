//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Implements procedure-level helpers for BASIC lowering including
//          signature caching, variable discovery, and body emission.
// Key invariants: Procedure helpers operate on the active Lowerer state and do
//                 not leak per-procedure state across invocations.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <cassert>

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

namespace
{

/// @brief AST walker that discovers variable usage within procedures.
///
/// @details The walker marks symbols and arrays referenced in procedure bodies
///          so the lowering pipeline can predeclare storage, track lifetimes,
///          and request runtime helpers.  Traversal is side-effect free aside
///          from updates to the bound @ref Lowerer instance.
class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    /// @brief Construct a walker bound to the owning Lowerer context.
    ///
    /// @param lowerer Lowerer receiving symbol tracking updates.
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Mark scalar symbol usage when a variable is referenced.
    ///
    /// @param expr Variable expression encountered while walking.
    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    /// @brief Mark array usage when an array element is referenced.
    ///
    /// @param expr Array expression encountered while walking.
    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Mark arrays when LBOUND operations reference them.
    ///
    /// @param expr LBOUND expression referencing a symbol.
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Mark arrays when UBOUND operations reference them.
    ///
    /// @param expr UBOUND expression referencing a symbol.
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Record symbol metadata when encountering DIM statements.
    ///
    /// @param stmt DIM statement defining variables.
    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    /// @brief Record symbol metadata when encountering REDIM statements.
    ///
    /// @param stmt REDIM statement defining or resizing arrays.
    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    /// @brief Note loop variables referenced by FOR statements.
    ///
    /// @param stmt FOR statement under inspection.
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Note loop variables referenced by NEXT statements.
    ///
    /// @param stmt NEXT statement under inspection.
    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Note variables referenced by INPUT statements.
    ///
    /// @param stmt INPUT statement providing destination variable names.
    void before(const InputStmt &stmt)
    {
        for (const auto &name : stmt.vars)
        {
            if (!name.empty())
                lowerer_.markSymbolReferenced(name);
        }
    }

  private:
    Lowerer &lowerer_;
};

} // namespace

/// @brief Create a procedure lowering helper bound to @p lowerer.
///
/// @param lowerer Lowerer managing shared state.
ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Cache procedure signatures for later call resolution.
///
/// @param prog Program whose procedures should be recorded.
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

/// @brief Walk a list of statements to collect referenced symbols.
///
/// @param stmts Statement pointers describing a procedure body.
void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

/// @brief Collect referenced symbols from all program statements.
///
/// @param prog Program containing procedure and main statements.
void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

/// @brief Emit IL for a procedure after collecting metadata and locals.
///
/// @param name Procedure name to lower.
/// @param params Parameters describing BASIC procedure arguments.
/// @param body Body statements scheduled for lowering.
/// @param config Hooks controlling skeleton construction and final return.
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
        ctx.blockNames().resetNamer();
        return;
    }

    lowerer.lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);

    ctx.setCurrent(&f.blocks[ctx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseObjectLocals(metadata.paramNames);
    lowerer.releaseObjectParams(metadata.paramNames);
    lowerer.releaseArrayLocals(metadata.paramNames);
    lowerer.releaseArrayParams(metadata.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    ctx.blockNames().resetNamer();
}

} // namespace il::frontends::basic

