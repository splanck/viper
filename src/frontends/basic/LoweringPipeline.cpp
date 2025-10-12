//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the helper pipeline used to lower BASIC source code into IL.
// The helpers collaborate with the main @c Lowerer class to perform symbol
// discovery, procedure signature construction, and statement-by-statement
// lowering.  Each stage mutates the shared @c Lowerer instance in isolation so
// callers can compose the stages depending on whether they need a full program
// lowering or procedure-specific work.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstWalker.hpp"
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
/// @brief Translate a BASIC semantic type into the corresponding IL core type.
///
/// @param ty BASIC surface type enumerator produced during semantic analysis.
/// @return IL type object with the matching primitive kind.
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

/// @brief AST walker that records variable references discovered in procedure bodies.
class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    /// @brief Create a walker that records symbols into the provided lowerer state.
    /// @param lowerer Lowering pipeline object that owns symbol tracking tables.
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Mark scalar variable usage after visiting an expression node.
    /// @param expr Variable expression encountered during traversal.
    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    /// @brief Track array references after walking an @ref ArrayExpr.
    /// @param expr Array expression carrying the referenced identifier.
    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Record lower-bound queries as array usage for allocation planning.
    /// @param expr AST node representing an @c LBOUND invocation.
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Record upper-bound queries as array usage for allocation planning.
    /// @param expr AST node representing a @c UBOUND invocation.
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Register declared variables and arrays before emitting DIM statements.
    /// @param stmt DIM statement being processed.
    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    /// @brief Track array declarations introduced by REDIM statements.
    /// @param stmt REDIM statement currently lowering.
    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    /// @brief Note loop control variables prior to emitting FOR statements.
    /// @param stmt FOR statement referencing the control variable.
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Note loop control variables for NEXT statements to ensure liveness.
    /// @param stmt NEXT statement referencing the control variable.
    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Record identifiers appearing in INPUT statements.
    /// @param stmt INPUT statement enumerating destination variables.
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

/// @brief Construct a program-level lowering driver bound to a Lowerer instance.
/// @param lowerer Shared lowering state that owns builders, symbol tables, and diagnostics.
ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Lower an entire BASIC program into IL.
///
/// @details The routine initialises the @c Lowerer state, configures the
/// @c IRBuilder, performs symbol discovery, materialises runtime support, and
/// finally emits the IL for both procedures and the main body.  Temporary
/// allocations such as the builder pointer are scoped within the call so the
/// @c Lowerer can be reused afterwards.
///
/// @param prog Parsed BASIC AST representing the whole program.
/// @param module Destination IL module populated with emitted procedures.
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

/// @brief Construct the procedure lowering helper bound to the shared Lowerer.
/// @param lowerer Lowerer storing symbol tables and IR builders.
ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Collect procedure signatures for all declared BASIC functions and subs.
///
/// @details Iterates through the program's declarations, mapping BASIC surface
/// types into IL types via @ref coreTypeForAstType and recording parameter lists
/// for later lowering.  Arrays are represented as pointer types to match the IL
/// calling convention.
///
/// @param prog Program containing procedure declarations.
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

/// @brief Gather variable usage information for a statement sequence.
///
/// @details Runs @ref VarCollectWalker over each statement pointer, populating
/// the @c Lowerer symbol tables with referenced identifiers.  Null pointers are
/// ignored so callers can pass optional statement slots.
///
/// @param stmts Statements to analyse for identifier usage.
void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

/// @brief Gather variable usage information across the entire program.
///
/// @details Flattens both procedure declarations and top-level statements into
/// a temporary pointer list before delegating to the statement-based overload of
/// @ref collectVars.
///
/// @param prog Program whose statements should be scanned.
void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

/// @brief Emit IL for a single BASIC procedure.
///
/// @details Resets lowering state, synthesises the IR function skeleton, lowers
/// the statement body (if any), and emits the configured return sequence.  Array
/// bookkeeping for parameters and locals is managed automatically before the
/// final return is emitted.
///
/// @param name Mangled procedure name to emit.
/// @param params Formal parameter list from the AST.
/// @param body Sequence of statements forming the procedure body.
/// @param config Hooks that control empty-body handling and final return emission.
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
    lowerer.releaseArrayLocals(metadata.paramNames);
    lowerer.releaseArrayParams(metadata.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    ctx.blockNames().resetNamer();
}

/// @brief Construct a statement lowering helper.
/// @param lowerer Shared lowering state used to emit IR.
StatementLowering::StatementLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Lower a sequence of BASIC statements into the current IL function.
///
/// @details The routine ensures gosub continuations are prepared, jumps into the
/// basic-block layout derived from virtual line numbers, lowers each statement,
/// and emits fall-through branches unless a terminator is produced.  The optional
/// @p beforeBranch callback enables callers to insert additional control-flow
/// plumbing before branches are emitted.
///
/// @param stmts Statement pointers describing the sequence to lower.
/// @param stopOnTerminated When true, stop lowering once a terminator is seen.
/// @param beforeBranch Optional hook invoked before emitting fall-through branches.
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
    auto &lineBlocks = ctx.blockNames().lineBlocks();

    ctx.gosub().clearContinuations();
    bool hasGosub = false;
    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const auto *gosubStmt = dynamic_cast<const GosubStmt *>(stmts[i]);
        if (!gosubStmt)
            continue;

        hasGosub = true;
        size_t contIdx = ctx.exitIndex();
        if (i + 1 < stmts.size())
        {
            int nextLine = lowerer.virtualLine(*stmts[i + 1]);
            contIdx = lineBlocks[nextLine];
        }
        ctx.gosub().registerContinuation(gosubStmt, contIdx);
    }

    if (hasGosub)
        lowerer.ensureGosubStack();
    int firstLine = lowerer.virtualLine(*stmts.front());
    lowerer.emitBr(&func->blocks[lineBlocks[firstLine]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        int vLine = lowerer.virtualLine(stmt);
        ctx.setCurrent(&func->blocks[lineBlocks[vLine]]);
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
                ? &func->blocks[lineBlocks[lowerer.virtualLine(*stmts[i + 1])]]
                : &func->blocks[ctx.exitIndex()];
        if (beforeBranch)
            beforeBranch(stmt);
        lowerer.emitBr(next);
    }
}

} // namespace il::frontends::basic
