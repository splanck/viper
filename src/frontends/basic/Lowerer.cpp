// File: src/frontends/basic/Lowerer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
//          runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
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

using pipeline_detail::coreTypeForAstType;

void Lowerer::ProcedureState::reset()
{
    fnExit = 0;
    nextTemp = 0;
    boundsCheckId = 0;
    lineBlocks.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    varTypes.clear();
    vars.clear();
    arrays.clear();
    runtimeFeatures.reset();
    runtimeOrder.clear();
    runtimeSet.clear();
}

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
    auto it = procState.varTypes.find(key);
    AstType astTy = (it != procState.varTypes.end()) ? it->second : inferAstTypeFromName(name);
    info.isArray = procState.arrays.find(key) != procState.arrays.end();
    info.isBoolean = !info.isArray && astTy == AstType::Bool;
    info.type = info.isArray ? Type(Type::Kind::Ptr) : coreTypeForAstType(astTy);
    return info;
}

/// @brief Lookup a cached procedure signature by BASIC identifier.
/// @param name BASIC procedure name including suffix.
/// @return Pointer to the signature when recorded, nullptr otherwise.
const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    auto it = procSignatures.find(name);
    if (it == procSignatures.end())
        return nullptr;
    return &it->second;
}

/// @brief Construct a lowering context.
/// @param boundsChecks When true, enable allocation of auxiliary slots used to
///        emit runtime array bounds checks during lowering.
/// @note The constructor merely stores configuration; transient lowering state
///       is reset each time a program or procedure is processed.
Lowerer::Lowerer(bool boundsChecks)
    : programLowering(std::make_unique<ProgramLowering>(*this)),
      procedureLowering(std::make_unique<ProcedureLowering>(*this)),
      statementLowering(std::make_unique<StatementLowering>(*this)),
      boundsChecks(boundsChecks)
{
}

Lowerer::~Lowerer() = default;

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
    Module m;
    programLowering->run(prog, m);
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
    procedureLowering->collectVars(stmts);
}

/// @brief Cache declared signatures for all user-defined procedures in a program.
/// @param prog BASIC program supplying FUNCTION and SUB declarations.
/// @details Signatures are recorded prior to lowering so call expressions can
///          coerce arguments and results even for forward references.
void Lowerer::collectProcedureSignatures(const Program &prog)
{
    procedureLowering->collectProcedureSignatures(prog);
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

    size_t blockIndex = 1;
    for (const auto *stmt : metadata.bodyStmts)
    {
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(stmt->line));
        else
            builder->addBlock(
                f,
                mangler.block("L" + std::to_string(stmt->line) + "_" + name));
        procState.lineBlocks[stmt->line] = blockIndex++;
    }

    procState.fnExit = f.blocks.size();
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames,
                                 bool includeParams)
{
    for (const auto &v : procState.vars)
    {
        bool isParam = paramNames.find(v) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (procState.varSlots.find(v) != procState.varSlots.end())
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(v);
        if (slotInfo.isArray)
        {
            Value slot = emitAlloca(8);
            procState.varSlots[v] = slot.id;
            continue;
        }
        Value slot = emitAlloca(slotInfo.isBoolean ? 1 : 8);
        procState.varSlots[v] = slot.id;
        if (slotInfo.isBoolean)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
    }

    if (!boundsChecks)
        return;

    for (const auto &a : procState.arrays)
    {
        bool isParam = paramNames.find(a) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (procState.arrayLenSlots.find(a) != procState.arrayLenSlots.end())
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        procState.arrayLenSlots[a] = slot.id;
    }
}

void Lowerer::lowerStatementSequence(
    const std::vector<const Stmt *> &stmts,
    bool stopOnTerminated,
    const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
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
    procedureLowering->emit(name, params, body, config);
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
    config.postCollect = [&]() { procState.varTypes[decl.name] = decl.ret; };
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
    procState.reset();
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
        procState.varSlots[p.name] = slot.id;
        procState.varTypes[p.name] = p.type;
        il::core::Type ty = func->params[i].type;
        Value incoming = Value::temp(func->params[i].id);
        emitStore(ty, slot, incoming);
        if (p.is_array)
            procState.arrays.insert(p.name);
    }
}

/// @brief Collect variable usage for every procedure and the main body.
/// @param prog Program whose statements are scanned.
/// @details Aggregates pointers to all statements and forwards to the granular
///          collector so that `vars` and `arrays` contain every referenced name
///          before lowering begins.
void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}


} // namespace il::frontends::basic
