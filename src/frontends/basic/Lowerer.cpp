// File: src/frontends/basic/Lowerer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
//          runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <functional>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

namespace
{

/// @brief Translate a BASIC AST scalar type into the IL core representation.
/// @param ty BASIC semantic type sourced from the front-end AST.
/// @return Corresponding IL type used for stack slots and temporaries.
/// @note BOOL lowers to the IL `i1` type while string scalars lower to the
///       canonical string handle type. Unrecognized enumerators fall back to
///       I64, although the parser should prevent that from happening.
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

/// @brief Infer the BASIC AST type for an identifier by inspecting its suffix.
/// @param name Identifier to analyze.
/// @return BASIC type derived from the suffix; defaults to integer for
///         suffix-free names.
::il::frontends::basic::Type astTypeFromName(std::string_view name)
{
    using AstType = ::il::frontends::basic::Type;
    if (!name.empty())
    {
        switch (name.back())
        {
            case '$':
                return AstType::Str;
            case '#':
                return AstType::F64;
            default:
                break;
        }
    }
    return AstType::I64;
}

/// @brief Expression visitor accumulating referenced variable and array names.
class VarCollectExprVisitor final : public ExprVisitor
{
  public:
    VarCollectExprVisitor(
        std::unordered_set<std::string> &vars,
        std::unordered_set<std::string> &arrays,
        std::unordered_map<std::string, ::il::frontends::basic::Type> &varTypes)
        : vars_(vars), arrays_(arrays), varTypes_(varTypes)
    {
    }

    void visit(const IntExpr &) override {}

    void visit(const FloatExpr &) override {}

    void visit(const StringExpr &) override {}

    void visit(const BoolExpr &) override {}

    void visit(const VarExpr &expr) override
    {
        vars_.insert(expr.name);
        recordImplicitType(expr.name);
    }

    void visit(const ArrayExpr &expr) override
    {
        vars_.insert(expr.name);
        arrays_.insert(expr.name);
        recordImplicitType(expr.name);
        if (expr.index)
            expr.index->accept(*this);
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

    void visit(const BuiltinCallExpr &expr) override
    {
        for (const auto &arg : expr.args)
            if (arg)
                arg->accept(*this);
    }

    void visit(const CallExpr &expr) override
    {
        for (const auto &arg : expr.args)
            if (arg)
                arg->accept(*this);
    }

  private:
    void recordImplicitType(const std::string &name)
    {
        if (name.empty())
            return;
        if (varTypes_.find(name) != varTypes_.end())
            return;
        varTypes_[name] = astTypeFromName(name);
    }

    std::unordered_set<std::string> &vars_;
    std::unordered_set<std::string> &arrays_;
    std::unordered_map<std::string, ::il::frontends::basic::Type> &varTypes_;
};

/// @brief Statement visitor walking child expressions/statements to collect names.
class VarCollectStmtVisitor final : public StmtVisitor
{
  public:
    VarCollectStmtVisitor(VarCollectExprVisitor &exprVisitor,
                          std::unordered_set<std::string> &vars,
                          std::unordered_set<std::string> &arrays,
                          std::unordered_map<std::string, ::il::frontends::basic::Type> &varTypes)
        : exprVisitor_(exprVisitor), vars_(vars), arrays_(arrays), varTypes_(varTypes)
    {
    }

    void visit(const PrintStmt &stmt) override
    {
        for (const auto &item : stmt.items)
            if (item.kind == PrintItem::Kind::Expr && item.expr)
                item.expr->accept(exprVisitor_);
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
        vars_.insert(stmt.name);
        varTypes_[stmt.name] = stmt.type;
        if (stmt.isArray)
        {
            arrays_.insert(stmt.name);
            if (stmt.size)
                stmt.size->accept(exprVisitor_);
        }
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

    void visit(const ForStmt &stmt) override
    {
        if (!stmt.var.empty())
        {
            vars_.insert(stmt.var);
            if (varTypes_.find(stmt.var) == varTypes_.end())
                varTypes_[stmt.var] = astTypeFromName(stmt.var);
        }
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
            vars_.insert(stmt.var);
    }

    void visit(const GotoStmt &) override {}

    void visit(const EndStmt &) override {}

    void visit(const InputStmt &stmt) override
    {
        if (stmt.prompt)
            stmt.prompt->accept(exprVisitor_);
        if (!stmt.var.empty())
        {
            vars_.insert(stmt.var);
            if (varTypes_.find(stmt.var) == varTypes_.end())
                varTypes_[stmt.var] = astTypeFromName(stmt.var);
        }
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
    std::unordered_set<std::string> &vars_;
    std::unordered_set<std::string> &arrays_;
    std::unordered_map<std::string, ::il::frontends::basic::Type> &varTypes_;

    void recordImplicitType(const std::string &name)
    {
        if (name.empty())
            return;
        if (varTypes_.find(name) != varTypes_.end())
            return;
        varTypes_[name] = astTypeFromName(name);
    }
};

} // namespace

Lowerer::BlockNamer::BlockNamer(std::string p) : proc(std::move(p)) {}

std::string Lowerer::BlockNamer::entry() const
{
    return "entry_" + proc;
}

std::string Lowerer::BlockNamer::ret() const
{
    return "ret_" + proc;
}

std::string Lowerer::BlockNamer::line(int line) const
{
    return "L" + std::to_string(line) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextIf()
{
    return ifCounter++;
}

std::string Lowerer::BlockNamer::ifTest(unsigned id) const
{
    return "if_test_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifThen(unsigned id) const
{
    return "if_then_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifElse(unsigned id) const
{
    return "if_else_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifEnd(unsigned id) const
{
    return "if_end_" + std::to_string(id) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextWhile()
{
    return loopCounter++;
}

std::string Lowerer::BlockNamer::whileHead(unsigned id) const
{
    return "while_head_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::whileBody(unsigned id) const
{
    return "while_body_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::whileEnd(unsigned id) const
{
    return "while_end_" + std::to_string(id) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextFor()
{
    return loopCounter++;
}

unsigned Lowerer::BlockNamer::nextCall()
{
    return loopCounter++;
}

std::string Lowerer::BlockNamer::forHead(unsigned id) const
{
    return "for_head_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forBody(unsigned id) const
{
    return "for_body_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forInc(unsigned id) const
{
    return "for_inc_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forEnd(unsigned id) const
{
    return "for_end_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::callCont(unsigned id) const
{
    return "call_cont_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::generic(const std::string &hint)
{
    auto &n = genericCounters[hint];
    std::string label = hint + "_" + std::to_string(n++) + "_" + proc;
    return label;
}

std::string Lowerer::BlockNamer::tag(const std::string &base) const
{
    return base + "_" + proc;
}

Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const
{
    SlotType info;
    std::string key(name);
    using AstType = ::il::frontends::basic::Type;
    auto it = varTypes.find(key);
    AstType astTy = (it != varTypes.end()) ? it->second : astTypeFromName(name);
    info.isArray = arrays.find(key) != arrays.end();
    info.isBoolean = !info.isArray && astTy == AstType::Bool;
    info.type = info.isArray ? Type(Type::Kind::Ptr) : coreTypeForAstType(astTy);
    return info;
}

/// @brief Construct a lowering context.
/// @param boundsChecks When true, enable allocation of auxiliary slots used to
///        emit runtime array bounds checks during lowering.
/// @note The constructor merely stores configuration; transient lowering state
///       is reset each time a program or procedure is processed.
Lowerer::Lowerer(bool boundsChecks) : boundsChecks(boundsChecks) {}

/// @brief Lower a full BASIC program into an IL module.
/// @param prog Parsed program containing procedures and top-level statements.
/// @return Newly constructed module with all runtime declarations and lowered
///         procedures.
/// @details The method resets every per-run cache (name mangler, variable
///          tracking, runtime requirements) and performs a three stage pipeline:
///          (1) scan to identify runtime helpers, (2) declare those helpers in
///          the module, and (3) emit procedure bodies plus a synthetic @main.
///          `mod`, `builder`, and numerous tracking maps are updated in-place
///          while the temporary `Module m` owns the resulting IR.
Module Lowerer::lowerProgram(const Program &prog)
{
    // Procs first, then a synthetic @main for top-level statements.
    Module m;
    mod = &m;
    build::IRBuilder b(m);
    builder = &b;

    mangler = NameMangler();
    lineBlocks.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    varTypes.clear();
    strings.clear();
    arrays.clear();
    boundsCheckId = 0;

    runtimeFeatures.reset();
    runtimeOrder.clear();
    runtimeSet.clear();

    scanProgram(prog);
    declareRequiredRuntime(b);
    emitProgram(prog);

    return m;
}

/// @brief Backward-compatible alias for @ref lowerProgram.
/// @param prog Program lowered into a fresh module instance.
/// @return Result from delegating to @ref lowerProgram.
Module Lowerer::lower(const Program &prog)
{
    return lowerProgram(prog);
}

/// @brief Discover variable usage within a statement list.
/// @param stmts Statements whose expressions are analyzed.
/// @details Populates `vars`, `arrays`, and `varTypes` so subsequent lowering can
///          materialize storage for every name. Array metadata is captured so
///          bounds slots can be emitted when enabled. The traversal preserves
///          existing set entries, allowing incremental accumulation across
///          different program regions.
void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectExprVisitor exprVisitor(vars, arrays, varTypes);
    VarCollectStmtVisitor stmtVisitor(exprVisitor, vars, arrays, varTypes);
    for (const auto *stmt : stmts)
        if (stmt)
            stmt->accept(stmtVisitor);
}

Lowerer::ProcedureMetadata Lowerer::collectProcedureMetadata(
    const std::vector<Param> &params,
    const std::vector<StmtPtr> &body,
    const ProcedureConfig &config)
{
    ProcedureMetadata metadata;
    metadata.paramCount = params.size();
    metadata.bodyStmts.reserve(body.size());
    for (const auto &stmt : body)
        metadata.bodyStmts.push_back(stmt.get());

    collectVars(metadata.bodyStmts);

    if (config.postCollect)
        config.postCollect();

    metadata.irParams.reserve(params.size());
    for (const auto &p : params)
    {
        metadata.paramNames.insert(p.name);
        Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
        metadata.irParams.push_back({p.name, ty});
    }

    return metadata;
}

void Lowerer::buildProcedureSkeleton(Function &f,
                                     const std::string &name,
                                     const ProcedureMetadata &metadata)
{
    blockNamer = std::make_unique<BlockNamer>(name);

    builder->addBlock(f, blockNamer->entry());

    for (size_t i = 0; i < metadata.paramCount; ++i)
        mangler.nextTemp();

    size_t blockIndex = 1;
    for (const auto *stmt : metadata.bodyStmts)
    {
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(stmt->line));
        else
            builder->addBlock(
                f,
                mangler.block("L" + std::to_string(stmt->line) + "_" + name));
        lineBlocks[stmt->line] = blockIndex++;
    }

    fnExit = f.blocks.size();
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

void Lowerer::allocateLocals(const std::unordered_set<std::string> &paramNames)
{
    for (const auto &v : vars)
    {
        if (paramNames.count(v))
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(v);
        if (slotInfo.isArray)
        {
            Value slot = emitAlloca(8);
            varSlots[v] = slot.id;
            continue;
        }
        Value slot = emitAlloca(slotInfo.isBoolean ? 1 : 8);
        varSlots[v] = slot.id;
        if (slotInfo.isBoolean)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
    }

    if (!boundsChecks)
        return;

    for (const auto &a : arrays)
    {
        if (paramNames.count(a))
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        arrayLenSlots[a] = slot.id;
    }
}

void Lowerer::lowerStatementSequence(
    const std::vector<const Stmt *> &stmts,
    bool stopOnTerminated,
    const std::function<void(const Stmt &)> &beforeBranch)
{
    if (stmts.empty())
        return;

    curLoc = {};
    emitBr(&func->blocks[lineBlocks[stmts.front()->line]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        cur = &func->blocks[lineBlocks[stmt.line]];
        lowerStmt(stmt);
        if (cur->terminated)
        {
            if (stopOnTerminated)
                break;
            continue;
        }
        BasicBlock *next = (i + 1 < stmts.size())
                               ? &func->blocks[lineBlocks[stmts[i + 1]->line]]
                               : &func->blocks[fnExit];
        if (beforeBranch)
            beforeBranch(stmt);
        emitBr(next);
    }
}

/// @brief Lower a single BASIC procedure using the provided configuration.
/// @param name Symbol name used for both mangling and emitted function labels.
/// @param params Formal parameters describing incoming values.
/// @param body Statements comprising the procedure body.
/// @param config Callbacks and metadata controlling return emission and
///        post-collection bookkeeping.
/// @details Clears any state from prior procedures, collects variable
///          references from @p body, and then constructs the IL function
///          skeleton: entry block, per-line blocks, and exit block. Parameter
///          and local stack slots are materialized before walking statements.
///          The helper drives `lowerStmt` for each statement and finally invokes
///          the configured return generator. Numerous members (`func`, `cur`,
///          `lineBlocks`, `varSlots`, `arrays`, `blockNamer`) are mutated to
///          reflect the active procedure.
void Lowerer::lowerProcedure(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const ProcedureConfig &config)
{
    resetLoweringState();

    ProcedureMetadata metadata =
        collectProcedureMetadata(params, body, config);

    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    Function &f = builder->startFunction(name, config.retType, metadata.irParams);
    func = &f;

    buildProcedureSkeleton(f, name, metadata);

    cur = &f.blocks.front();
    materializeParams(params);
    allocateLocals(metadata.paramNames);

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        config.emitEmptyBody();
        blockNamer.reset();
        return;
    }

    lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);

    cur = &f.blocks[fnExit];
    curLoc = {};
    config.emitFinalReturn();

    blockNamer.reset();
}

/// @brief Lower a FUNCTION declaration into an IL function definition.
/// @param decl BASIC FUNCTION metadata and body.
/// @details Configures a @ref ProcedureConfig that materializes default return
///          values when the body falls through. The function result type is
///          cached in `varTypes` so the caller may bind its slot when invoking
///          the function.
void Lowerer::lowerFunctionDecl(const FunctionDecl &decl)
{
    auto defaultRet = [&]()
    {
        switch (decl.ret)
        {
            case ::il::frontends::basic::Type::I64:
                return Value::constInt(0);
            case ::il::frontends::basic::Type::F64:
                return Value::constFloat(0.0);
            case ::il::frontends::basic::Type::Str:
                return emitConstStr(getStringLabel(""));
            case ::il::frontends::basic::Type::Bool:
                return emitBoolConst(false);
        }
        return Value::constInt(0);
    };

    ProcedureConfig config;
    config.retType = coreTypeForAstType(decl.ret);
    config.postCollect = [&]() { varTypes[decl.name] = decl.ret; };
    config.emitEmptyBody = [&]() { emitRet(defaultRet()); };
    config.emitFinalReturn = [&]() { emitRet(defaultRet()); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

/// @brief Lower a SUB declaration into an IL procedure.
/// @param decl BASIC SUB metadata and body.
/// @details SUBs have no return value; the configured lowering emits void
///          returns for empty bodies and at the exit block.
void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    ProcedureConfig config;
    config.retType = Type(Type::Kind::Void);
    config.emitEmptyBody = [&]() { emitRetVoid(); };
    config.emitFinalReturn = [&]() { emitRetVoid(); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

/// @brief Clear per-procedure lowering caches.
/// @details Invoked before lowering each procedure to drop variable maps, array
///          bookkeeping, and line-to-block mappings. The bounds-check counter is
///          reset so diagnostics and helper labels remain deterministic.
void Lowerer::resetLoweringState()
{
    vars.clear();
    arrays.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    varTypes.clear();
    lineBlocks.clear();
    boundsCheckId = 0;
}

/// @brief Allocate stack storage for incoming parameters and record their types.
/// @param params BASIC formal parameters for the current procedure.
/// @details Emits an alloca per parameter and stores the incoming SSA value into
///          the slot. Array parameters are remembered in `arrays` to avoid
///          copying the referenced buffer, while boolean parameters request a
///          single-byte allocation. Side effects update `varSlots`, `varTypes`,
///          and potentially `arrays`.
void Lowerer::materializeParams(const std::vector<Param> &params)
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        bool isBoolParam = !p.is_array && p.type == AstType::Bool;
        Value slot = emitAlloca(isBoolParam ? 1 : 8);
        varSlots[p.name] = slot.id;
        varTypes[p.name] = p.type;
        il::core::Type ty = func->params[i].type;
        Value incoming = Value::temp(func->params[i].id);
        emitStore(ty, slot, incoming);
        if (p.is_array)
            arrays.insert(p.name);
    }
}

/// @brief Collect variable usage for every procedure and the main body.
/// @param prog Program whose statements are scanned.
/// @details Aggregates pointers to all statements and forwards to the granular
///          collector so that `vars` and `arrays` contain every referenced name
///          before lowering begins.
void Lowerer::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}


} // namespace il::frontends::basic
