//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the procedure-oriented portion of the BASIC lowering pipeline.
// Responsibilities include materialising procedure signatures, walking the AST
// to discover referenced symbols, and emitting the IL skeleton for each
// procedure.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements procedure-scoped helpers for the BASIC lowerer.
/// @details The logic here keeps per-procedure bookkeeping out of the main
///          Lowerer class. Concentrating it in a dedicated translation unit
///          simplifies unit testing and clarifies ownership of temporary state.

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <cassert>

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

namespace
{

/// @brief AST walker that records variable and array usage within procedures.
///
/// @details While walking a procedure body, the walker notifies the lowerer of
///          references so slots and runtime resources can be allocated before
///          code generation begins. It ensures DIM/REDIM statements update type
///          information and keeps array metadata in sync.
class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    /// @brief Construct the walker around the owning lowerer.
    ///
    /// @param lowerer Lowerer that will receive symbol notifications.
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Record variable references encountered in expressions.
    ///
    /// @param expr Variable expression node.
    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    /// @brief Record array element references.
    ///
    /// @param expr Array expression node.
    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Track lower-bound queries on arrays.
    ///
    /// @param expr AST node representing `LBOUND` usage.
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Track upper-bound queries on arrays.
    ///
    /// @param expr AST node representing `UBOUND` usage.
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Observe DIM statements to seed symbol metadata.
    ///
    /// @param stmt DIM statement node.
    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    /// @brief Observe REDIM statements to keep array metadata current.
    ///
    /// @param stmt REDIM statement node.
    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    /// @brief Mark FOR-loop induction variables as referenced.
    ///
    /// @param stmt FOR statement node.
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Mark NEXT statements to ensure loop variables stay live.
    ///
    /// @param stmt NEXT statement node.
    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Mark variables appearing in INPUT statements.
    ///
    /// @param stmt INPUT statement node.
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

/// @brief Construct the procedure helper around a lowerer instance.
///
/// @param lowerer Lowerer that will perform IL emission.
ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Extract signatures for all procedures in the program.
///
/// @details Populates the lowerer's signature cache by walking function/sub
///          declarations, translating AST parameter types to IL equivalents. The
///          cache is later consumed when materialising call sites.
///
/// @param prog BASIC program AST providing declarations.
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

/// @brief Scan a statement list for referenced variables and arrays.
///
/// @param stmts Statements comprising a procedure or main body.
void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

/// @brief Scan the entire program for variable references.
///
/// @details Aggregates statements from both the main body and procedure
///          declarations before delegating to the list-based overload.
///
/// @param prog BASIC program AST.
void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

/// @brief Emit IL for a single procedure.
///
/// @details Resets the lowerer's per-procedure state, builds the entry block,
///          materialises parameters, and lowers the body statements. Helper
///          callbacks in @p config handle empty bodies and ensure a final return
///          is emitted. The routine also releases runtime resources for object
///          and array locals before finalising the function.
///
/// @param name Mangled procedure name to emit.
/// @param params Procedure parameter list.
/// @param body Statement body to lower.
/// @param config Configuration callbacks and return type metadata.
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

