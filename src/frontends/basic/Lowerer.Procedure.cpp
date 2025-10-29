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

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/AstWalker.hpp"
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

/// @brief Bundles the transient state used while lowering a single procedure.
///
/// @details The lowering pipeline touches the shared symbol table, emitter, and
///          per-procedure context owned by @ref Lowerer.  This structure wires
///          those references together while providing the staged operations that
///          drive procedure lowering.  Each instance resets the borrowed
///          lowerer state before capturing the metadata required by subsequent
///          steps.  The staged helpers make the orchestration logic easier to
///          follow while keeping responsibilities focused.
struct LoweringContext
{
    using Step = std::function<void(LoweringContext &)>;

    LoweringContext(Lowerer &lowerer,
                    std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
                    il::build::IRBuilder &builder,
                    lower::Emitter &emitter,
                    const std::string &name,
                    const std::vector<Param> &params,
                    const std::vector<StmtPtr> &body,
                    const Lowerer::ProcedureConfig &config) noexcept
        : lowerer(lowerer), symbols(symbols), builder(builder), emitter(emitter), name(name),
          params(params), body(body), config(config)
    {
    }

    void collectProcedureInfo()
    {
        if (collectInfoStep)
            collectInfoStep(*this);
    }

    void scheduleBlocks()
    {
        if (scheduleBlocksStep)
            scheduleBlocksStep(*this);
    }

    void emitProcedureIL()
    {
        if (emitILStep)
            emitILStep(*this);
    }

    [[nodiscard]] bool hasHandlers() const noexcept
    {
        return static_cast<bool>(config.emitEmptyBody) && static_cast<bool>(config.emitFinalReturn);
    }

    [[nodiscard]] bool hasBody() const noexcept
    {
        return !bodyStmts.empty();
    }

    Lowerer &lowerer;
    [[maybe_unused]] std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols;
    il::build::IRBuilder &builder;
    [[maybe_unused]] lower::Emitter &emitter;
    const std::string &name;
    const std::vector<Param> &params;
    const std::vector<StmtPtr> &body;
    const Lowerer::ProcedureConfig &config;
    std::vector<const Stmt *> bodyStmts;
    std::unordered_set<std::string> paramNames;
    std::vector<il::core::Param> irParams;
    size_t paramCount{0};
    il::core::Function *function{nullptr};
    std::shared_ptr<void> metadataHandle;
    Step collectInfoStep;
    Step scheduleBlocksStep;
    Step emitILStep;
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
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
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
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
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
    LoweringContext ctx(
        lowerer, lowerer.symbols, *lowerer.builder, lowerer.emitter(), name, params, body, config);

    ctx.collectInfoStep = [&](LoweringContext &state)
    {
        lowerer.resetLoweringState();
        auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
            lowerer.collectProcedureMetadata(params, body, config));
        state.metadataHandle = metadata;
        state.paramCount = metadata->paramCount;
        state.bodyStmts = metadata->bodyStmts;
        state.paramNames = metadata->paramNames;
        state.irParams = metadata->irParams;
    };

    ctx.scheduleBlocksStep = [&](LoweringContext &state)
    {
        assert(config.emitEmptyBody && "Missing empty body return handler");
        assert(config.emitFinalReturn && "Missing final return handler");
        if (!state.hasHandlers())
            return;

        auto metadata = std::static_pointer_cast<Lowerer::ProcedureMetadata>(state.metadataHandle);
        auto &procCtx = lowerer.context();
        il::core::Function &f =
            lowerer.builder->startFunction(name, config.retType, state.irParams);
        state.function = &f;
        procCtx.setFunction(&f);
        procCtx.setNextTemp(f.valueNames.size());

        lowerer.buildProcedureSkeleton(f, name, *metadata);

        if (!f.blocks.empty())
            procCtx.setCurrent(&f.blocks.front());

        lowerer.materializeParams(params);
        lowerer.allocateLocalSlots(state.paramNames, /*includeParams=*/false);
    };

    ctx.emitILStep = [&](LoweringContext &state)
    {
        if (!state.hasHandlers() || !state.function)
            return;

        auto metadata = std::static_pointer_cast<Lowerer::ProcedureMetadata>(state.metadataHandle);
        auto &procCtx = lowerer.context();

        if (!state.hasBody())
        {
            lowerer.curLoc = {};
            config.emitEmptyBody();
            procCtx.blockNames().resetNamer();
            return;
        }

        lowerer.lowerStatementSequence(state.bodyStmts, /*stopOnTerminated=*/true);

        procCtx.setCurrent(&state.function->blocks[procCtx.exitIndex()]);
        lowerer.curLoc = {};
        lowerer.releaseObjectLocals(state.paramNames);
        lowerer.releaseObjectParams(state.paramNames);
        lowerer.releaseArrayLocals(state.paramNames);
        lowerer.releaseArrayParams(state.paramNames);
        lowerer.curLoc = {};
        config.emitFinalReturn();

        procCtx.blockNames().resetNamer();
    };

    ctx.collectProcedureInfo();
    ctx.scheduleBlocks();
    ctx.emitProcedureIL();
}

} // namespace il::frontends::basic
