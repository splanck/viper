//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.cpp
// Purpose: Implement the façade that coordinates BASIC-to-IL lowering across
//          symbol management, procedure emission, and runtime discovery.
// Key invariants: Block names are deterministic via BlockNamer and symbol table
//                 entries persist only for the active compilation unit.
// Ownership/Lifetime: Operates on lowering contexts owned by the caller while
//                     storing temporary metadata inside the Lowerer instance.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

/// @brief Ensure that a symbol table entry exists for @p name.
///
/// @details Inserts a default-constructed @ref SymbolInfo when the identifier
///          has not been seen before.  New entries start with integer type and
///          no semantic flags so later inference stages can update them lazily.
///
/// @param name BASIC identifier being queried.
/// @return Reference to the symbol information stored in the table.
Lowerer::SymbolInfo &Lowerer::ensureSymbol(std::string_view name)
{
    std::string key(name);
    auto [it, inserted] = symbols.emplace(std::move(key), SymbolInfo{});
    if (inserted)
    {
        it->second.type = AstType::I64;
        it->second.hasType = false;
        it->second.isArray = false;
        it->second.isBoolean = false;
        it->second.referenced = false;
        it->second.isObject = false;
        it->second.objectClass.clear();
    }
    return it->second;
}

/// @brief Look up a mutable symbol entry by BASIC identifier.
///
/// @param name BASIC identifier to query.
/// @return Pointer to the symbol entry when present; nullptr otherwise.
Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

/// @brief Look up an immutable symbol entry by BASIC identifier.
///
/// @param name BASIC identifier to query.
/// @return Pointer to the symbol entry when present; nullptr otherwise.
const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

/// @brief Assign an explicit BASIC type to a symbol.
///
/// @details Records the declared type and marks the symbol as typed so later
///          inference does not overwrite the explicit annotation.  Boolean flags
///          are recomputed to match the assigned type.
///
/// @param name BASIC identifier to annotate.
/// @param type Declared BASIC type for the symbol.
void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

/// @brief Tag a symbol as referring to an object instance.
///
/// @details Symbols that represent objects need both the `isObject` flag and
///          the associated class name.  The helper preserves existing metadata
///          and marks the symbol as having an explicit type.
///
/// @param name BASIC identifier describing the object.
/// @param className Name of the BASIC class the symbol instantiates.
void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

/// @brief Record that a symbol appeared in source and should be materialised.
///
/// @details Ensures the symbol exists, infers a type from naming conventions
///          when none was provided, and sets the referenced flag so slot
///          materialisation allocates storage for the identifier.
///
/// @param name BASIC identifier encountered during analysis.
void Lowerer::markSymbolReferenced(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    if (!info.hasType)
    {
        info.type = inferAstTypeFromName(name);
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == AstType::Bool;
    }
    info.referenced = true;
}

/// @brief Mark a symbol as representing an array.
///
/// @details Array symbols are forced to pointer type and cannot retain boolean
///          semantics.  The helper ensures the symbol exists before toggling the
///          relevant flags.
///
/// @param name BASIC identifier to update.
void Lowerer::markArray(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

/// @brief Reset per-program symbol state while preserving pooled string labels.
///
/// @details Iterates through all symbol entries, clearing metadata for string
///          labels that must persist across compilations and erasing other
///          entries entirely.  Called before lowering a new procedure or
///          program to avoid stale state.
void Lowerer::resetSymbolState()
{
    for (auto it = symbols.begin(); it != symbols.end();)
    {
        SymbolInfo &info = it->second;
        if (!info.stringLabel.empty())
        {
            info.type = AstType::I64;
            info.hasType = false;
            info.isArray = false;
            info.isBoolean = false;
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            info.slotId.reset();
            info.arrayLengthSlot.reset();
            ++it;
            continue;
        }
        it = symbols.erase(it);
    }
}

/// @brief Compute storage details for a symbol's stack slot.
///
/// @details Uses explicit type annotations or inferred naming conventions to
///          determine the IL type, whether the symbol is an array, and how to
///          treat boolean scalars.  Object symbols are forced to pointer type.
///
/// @param name BASIC identifier whose slot type is requested.
/// @return Structure describing the IL type and auxiliary flags.
Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const
{
    SlotType info;
    AstType astTy = inferAstTypeFromName(name);
    if (const auto *sym = findSymbol(name))
    {
        if (sym->isObject)
        {
            info.type = Type(Type::Kind::Ptr);
            info.isArray = false;
            info.isBoolean = false;
            info.isObject = true;
            info.objectClass = sym->objectClass;
            return info;
        }
        if (sym->hasType)
            astTy = sym->type;
        info.isArray = sym->isArray;
        if (sym->isBoolean && !info.isArray)
            info.isBoolean = true;
        else if (!sym->hasType && !info.isArray)
            info.isBoolean = (astTy == AstType::Bool);
        else
            info.isBoolean = false;
    }
    else
    {
        info.isArray = false;
        info.isBoolean = (astTy == AstType::Bool);
    }
    if (info.isArray)
        info.type = Type(Type::Kind::Ptr);
    else
        info.type = coreTypeForAstType(info.isBoolean ? AstType::Bool : astTy);
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
      boundsChecks(boundsChecks),
      emitter_(std::make_unique<lower::Emitter>(*this))
{
}

/// @brief Destroy the lowering façade.
///
/// @details The destructor is defaulted because all owned helpers are stored in
///          smart pointers; no additional cleanup is required beyond the member
///          default destruction order.
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

/// @brief Install or clear the diagnostic emitter used during lowering.
///
/// @details Stores the pointer for later use and wires the global
///          @ref TypeRules type-error sink so semantic checks emit diagnostics
///          through the same channel.  Passing @c nullptr disconnects the sink.
///
/// @param emitter Diagnostic sink that should receive type errors; nullptr to disable.
void Lowerer::setDiagnosticEmitter(DiagnosticEmitter *emitter) noexcept
{
    diagnosticEmitter_ = emitter;
    if (emitter)
    {
        TypeRules::setTypeErrorSink(
            [emitter](const TypeRules::TypeError &error)
            {
                emitter->emit(il::support::Severity::Error,
                              error.code,
                              il::support::SourceLoc{},
                              0,
                              error.message);
            });
    }
    else
    {
        TypeRules::setTypeErrorSink({});
    }
}

/// @brief Retrieve the diagnostic emitter currently associated with the lowering pipeline.
/// @return Pointer previously passed to @ref setDiagnosticEmitter.
DiagnosticEmitter *Lowerer::diagnosticEmitter() const noexcept
{
    return diagnosticEmitter_;
}

/// @brief Discover variable usage within a statement list.
/// @param stmts Statements whose expressions are analyzed.
/// @details Populates the symbol table with references, inferred types, and array
///          flags so subsequent lowering can materialize storage for every name.
///          Array metadata is captured so bounds slots can be emitted when enabled.
///          The traversal preserves existing entries, allowing incremental
///          accumulation across different program regions.
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

/// @brief Gather metadata required to lower a BASIC procedure body.
///
/// @details Copies parameter names, collects references from the body so slots
///          can be allocated, and records IL parameter descriptors.  The helper
///          also invokes the optional @ref ProcedureConfig::postCollect callback
///          to allow callers to perform custom bookkeeping (e.g., recording
///          return types or runtime requirements).
///
/// @param params Formal parameters supplied with the procedure declaration.
/// @param body AST statements that form the procedure body.
/// @param config Configuration hooks describing post-collection behaviour.
/// @return Metadata snapshot consumed by later lowering phases.
Lowerer::ProcedureMetadata Lowerer::collectProcedureMetadata(const std::vector<Param> &params,
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
        if (p.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    return metadata;
}

/// @brief Assign a stable synthetic line number to a statement.
///
/// @details BASIC statements may lack explicit line numbers.  The helper
///          consults the memoized map and, when needed, synthesises a unique
///          virtual line so per-line blocks can be generated deterministically.
///
/// @param s Statement whose virtual line number is requested.
/// @return Real line number when present or a synthesised value otherwise.
int Lowerer::virtualLine(const Stmt &s)
{
    auto it = stmtVirtualLines_.find(&s);
    if (it != stmtVirtualLines_.end())
        return it->second;

    int line = (s.line != 0) ? s.line : synthLineBase_ + (synthSeq_++);
    stmtVirtualLines_[&s] = line;
    return line;
}

/// @brief Create entry, per-line, and exit blocks for a procedure prior to emission.
///
/// @details Resets the block namer, ensures each source line receives a
///          deterministic block, and appends an explicit return block.  The
///          skeleton allows later lowering passes to fill in instructions while
///          preserving predictable label naming.
///
/// @param f Function being populated.
/// @param name BASIC procedure name used for block hints.
/// @param metadata Metadata describing body statements and parameters.
void Lowerer::buildProcedureSkeleton(Function &f,
                                     const std::string &name,
                                     const ProcedureMetadata &metadata)
{
    ProcedureContext &ctx = context();
    ctx.blockNames().setNamer(std::make_unique<BlockNamer>(name));
    BlockNamer *blockNamer = ctx.blockNames().namer();

    auto &entry =
        builder->addBlock(f, blockNamer ? blockNamer->entry() : mangler.block("entry_" + name));
    entry.params = f.params;

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    for (const auto *stmt : metadata.bodyStmts)
    {
        int vLine = virtualLine(*stmt);
        if (lineBlocks.find(vLine) != lineBlocks.end())
            continue;
        size_t blockIdx = f.blocks.size();
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(vLine));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(vLine) + "_" + name));
        lineBlocks[vLine] = blockIdx;
    }

    ctx.setExitIndex(f.blocks.size());
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

/// @brief Allocate stack storage for referenced locals (and optionally parameters).
///
/// @details Iterates the symbol table, emitting allocas for each referenced
///          symbol that lacks a slot.  Arrays receive pointer slots and optional
///          length slots when bounds checking is enabled.  Boolean slots are
///          initialised to false while string slots are initialised to the empty
///          runtime string to maintain invariants expected by the runtime.
///
/// @param paramNames Set of parameter identifiers in scope.
/// @param includeParams When true, allocate slots for parameters in addition to locals.
void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames,
                                 bool includeParams)
{
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.slotId)
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(name);
        if (slotInfo.isArray)
        {
            Value slot = emitAlloca(8);
            info.slotId = slot.id;
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
            continue;
        }
        Value slot = emitAlloca(slotInfo.isBoolean ? 1 : 8);
        info.slotId = slot.id;
        if (slotInfo.isBoolean)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
        else if (slotInfo.type.kind == Type::Kind::Str)
        {
            Value empty = emitCallRet(slotInfo.type, "rt_str_empty", {});
            emitStore(slotInfo.type, slot, empty);
        }
    }

    if (!boundsChecks)
        return;

    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.isArray)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.arrayLengthSlot)
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        info.arrayLengthSlot = slot.id;
    }
}

/// @brief Lower a sequence of statements with optional branch callbacks.
///
/// @details Delegates to the specialised statement-lowering helper, forwarding
///          control-flow hooks so callers can observe branch boundaries and
///          optionally stop once a terminating statement is emitted.
///
/// @param stmts Ordered list of statements to lower.
/// @param stopOnTerminated Whether to halt once a terminating statement executes.
/// @param beforeBranch Callback invoked immediately before a branch is emitted.
void Lowerer::lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                     bool stopOnTerminated,
                                     const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
}

/// @brief Materialise the implicit stack used to service GOSUB/RETURN statements.
///
/// @details Lazily emits allocas for the stack pointer and stack buffer the
///          first time a procedure requires them.  The prologue is inserted at
///          the entry block so subsequent lowering can assume the storage exists.
void Lowerer::ensureGosubStack()
{
    ProcedureContext &ctx = context();
    auto &state = ctx.gosub();
    if (state.hasPrologue())
        return;

    Function *func = ctx.function();
    if (!func)
        return;

    BasicBlock *savedBlock = ctx.current();
    BasicBlock *entry = &func->blocks.front();
    ctx.setCurrent(entry);

    auto savedLoc = curLoc;
    curLoc = {};

    Value spSlot = emitAlloca(8);
    Value stackSlot = emitAlloca(kGosubStackDepth * 4);
    emitStore(Type(Type::Kind::I64), spSlot, Value::constInt(0));
    state.setPrologue(spSlot, stackSlot);

    curLoc = savedLoc;
    ctx.setCurrent(savedBlock);
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
///          the configured return generator while the procedure context tracks
///          the active function, current block, and block mappings for the
///          duration of lowering.
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
///          recorded in the symbol table so the caller may bind its slot when
///          invoking the function.
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
    config.postCollect = [&]() { setSymbolType(decl.name, decl.ret); };
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
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
}

/// @brief Allocate stack storage for incoming parameters and record their types.
/// @param params BASIC formal parameters for the current procedure.
/// @details Emits an alloca per parameter and stores the incoming SSA value into
///          the slot. Array parameters are flagged in the symbol table to avoid
///          copying the referenced buffer, while boolean parameters request a
///          single-byte allocation. Side effects update the associated
///          SymbolInfo entries so later loads and stores reuse the recorded
///          slots.
void Lowerer::materializeParams(const std::vector<Param> &params)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "materializeParams requires an active function");
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        bool isBoolParam = !p.is_array && p.type == AstType::Bool;
        Value slot = emitAlloca(isBoolParam ? 1 : 8);
        if (p.is_array)
        {
            markArray(p.name);
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        setSymbolType(p.name, p.type);
        markSymbolReferenced(p.name);
        auto &info = ensureSymbol(p.name);
        info.slotId = slot.id;
        il::core::Type ty = func->params[i].type;
        Value incoming = Value::temp(func->params[i].id);
        if (p.is_array)
        {
            storeArray(slot, incoming);
        }
        else
        {
            emitStore(ty, slot, incoming);
        }
    }
}

/// @brief Collect variable usage for every procedure and the main body.
/// @param prog Program whose statements are scanned.
/// @details Aggregates pointers to all statements and forwards to the granular
///          collector so that symbol metadata reflects every referenced name
///          before lowering begins.
void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}

/// @brief Access the mutable procedure context associated with the lowering run.
/// @return Reference to the internal context structure.
Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

/// @brief Access the immutable procedure context associated with the lowering run.
/// @return Const reference to the internal context structure.
const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

/// @brief Classify the numeric category produced by an expression.
///
/// @details Dispatches to a local visitor that mirrors BASIC's type promotion
///          rules and identifier suffix conventions.  The result informs the
///          folding and lowering pipeline which IL type is appropriate for a
///          given expression.
///
/// @param expr Expression to analyse.
/// @return Numeric category describing the expression's result.
TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    using NumericType = TypeRules::NumericType;

    /// @brief Visitor that computes the numeric category for expressions.
    class Classifier final : public ExprVisitor
    {
    public:
        explicit Classifier(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

        NumericType result() const noexcept { return result_; }

        void visit(const IntExpr &i) override
        {
            switch (i.suffix)
            {
                case IntExpr::Suffix::Integer:
                    result_ = NumericType::Integer;
                    return;
                case IntExpr::Suffix::Long:
                    result_ = NumericType::Long;
                    return;
                case IntExpr::Suffix::None:
                    break;
            }

            const long long value = i.value;
            if (value >= std::numeric_limits<int16_t>::min() &&
                value <= std::numeric_limits<int16_t>::max())
            {
                result_ = NumericType::Integer;
            }
            else
            {
                result_ = NumericType::Long;
            }
        }

        void visit(const FloatExpr &f) override
        {
            result_ = (f.suffix == FloatExpr::Suffix::Single) ? NumericType::Single
                                                              : NumericType::Double;
        }

        void visit(const StringExpr &) override { result_ = NumericType::Double; }

        void visit(const BoolExpr &) override { result_ = NumericType::Integer; }

        void visit(const VarExpr &var) override
        {
            if (const auto *info = lowerer_.findSymbol(var.name))
            {
                if (info->hasType)
                {
                    if (info->type == AstType::F64)
                    {
                        if (!var.name.empty())
                        {
                            switch (var.name.back())
                            {
                                case '!':
                                    result_ = NumericType::Single;
                                    return;
                                case '#':
                                    result_ = NumericType::Double;
                                    return;
                                default:
                                    break;
                            }
                        }
                        result_ = NumericType::Double;
                        return;
                    }
                    if (!var.name.empty())
                    {
                        switch (var.name.back())
                        {
                            case '%':
                                result_ = NumericType::Integer;
                                return;
                            case '&':
                                result_ = NumericType::Long;
                                return;
                            default:
                                break;
                        }
                    }
                    result_ = NumericType::Long;
                    return;
                }
            }

            if (!var.name.empty())
            {
                switch (var.name.back())
                {
                    case '!':
                        result_ = NumericType::Single;
                        return;
                    case '#':
                        result_ = NumericType::Double;
                        return;
                    case '%':
                        result_ = NumericType::Integer;
                        return;
                    case '&':
                        result_ = NumericType::Long;
                        return;
                    default:
                        break;
                }
            }

            AstType astTy = inferAstTypeFromName(var.name);
            result_ = (astTy == AstType::F64) ? NumericType::Double : NumericType::Long;
        }

        void visit(const ArrayExpr &) override { result_ = NumericType::Long; }

        void visit(const UnaryExpr &un) override
        {
            if (!un.expr)
            {
                result_ = NumericType::Long;
                return;
            }
            result_ = lowerer_.classifyNumericType(*un.expr);
        }

        void visit(const BinaryExpr &bin) override
        {
            if (!bin.lhs || !bin.rhs)
            {
                result_ = NumericType::Long;
                return;
            }

            NumericType lhsTy = lowerer_.classifyNumericType(*bin.lhs);
            NumericType rhsTy = lowerer_.classifyNumericType(*bin.rhs);

            switch (bin.op)
            {
                case BinaryExpr::Op::Add:
                    result_ = TypeRules::resultType('+', lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::Sub:
                    result_ = TypeRules::resultType('-', lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::Mul:
                    result_ = TypeRules::resultType('*', lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::Div:
                    result_ = TypeRules::resultType('/', lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::IDiv:
                    result_ = TypeRules::resultType('\\', lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::Mod:
                    result_ = TypeRules::resultType("MOD", lhsTy, rhsTy);
                    return;
                case BinaryExpr::Op::Pow:
                    result_ = TypeRules::resultType('^', lhsTy, rhsTy);
                    return;
                default:
                    result_ = NumericType::Long;
                    return;
            }
        }

        void visit(const BuiltinCallExpr &call) override
        {
            switch (call.builtin)
            {
                case BuiltinCallExpr::Builtin::Cint:
                    result_ = NumericType::Integer;
                    return;
                case BuiltinCallExpr::Builtin::Clng:
                    result_ = NumericType::Long;
                    return;
                case BuiltinCallExpr::Builtin::Csng:
                    result_ = NumericType::Single;
                    return;
                case BuiltinCallExpr::Builtin::Cdbl:
                    result_ = NumericType::Double;
                    return;
                case BuiltinCallExpr::Builtin::Int:
                case BuiltinCallExpr::Builtin::Fix:
                case BuiltinCallExpr::Builtin::Round:
                case BuiltinCallExpr::Builtin::Sqr:
                case BuiltinCallExpr::Builtin::Abs:
                case BuiltinCallExpr::Builtin::Floor:
                case BuiltinCallExpr::Builtin::Ceil:
                case BuiltinCallExpr::Builtin::Sin:
                case BuiltinCallExpr::Builtin::Cos:
                case BuiltinCallExpr::Builtin::Pow:
                case BuiltinCallExpr::Builtin::Rnd:
                case BuiltinCallExpr::Builtin::Val:
                    result_ = NumericType::Double;
                    return;
                case BuiltinCallExpr::Builtin::Str:
                    if (!call.args.empty() && call.args[0])
                    {
                        result_ = lowerer_.classifyNumericType(*call.args[0]);
                    }
                    else
                    {
                        result_ = NumericType::Long;
                    }
                    return;
                default:
                    result_ = NumericType::Double;
                    return;
            }
        }

        void visit(const LBoundExpr &) override { result_ = NumericType::Long; }

        void visit(const UBoundExpr &) override { result_ = NumericType::Long; }

        void visit(const CallExpr &callExpr) override
        {
            if (const auto *sig = lowerer_.findProcSignature(callExpr.callee))
            {
                switch (sig->retType.kind)
                {
                    case Type::Kind::I16:
                        result_ = NumericType::Integer;
                        return;
                    case Type::Kind::I32:
                    case Type::Kind::I64:
                        result_ = NumericType::Long;
                        return;
                    case Type::Kind::F64:
                        result_ = NumericType::Double;
                        return;
                    default:
                        break;
                }
            }
            result_ = NumericType::Long;
        }

        void visit(const NewExpr &) override { result_ = NumericType::Long; }

        void visit(const MeExpr &) override { result_ = NumericType::Long; }

        void visit(const MemberAccessExpr &) override { result_ = NumericType::Long; }

        void visit(const MethodCallExpr &) override { result_ = NumericType::Long; }

    private:
        Lowerer &lowerer_;
        NumericType result_{NumericType::Long};
    };

    Classifier classifier(*this);
    expr.accept(classifier);
    return classifier.result();
}

/// @brief Reserve a fresh temporary identifier for IL value emission.
///
/// @details Prefers the builder's ID allocator when present; otherwise falls
///          back to the context-managed counter.  The helper also ensures the
///          function's value name vector is large enough for debug dumps.
///
/// @return Identifier for the next temporary value.
unsigned Lowerer::nextTempId()
{
    ProcedureContext &ctx = context();
    unsigned id = 0;
    if (builder)
    {
        id = builder->reserveTempId();
    }
    else
    {
        id = ctx.nextTemp();
        ctx.setNextTemp(id + 1);
    }
    if (Function *func = ctx.function())
    {
        if (func->valueNames.size() <= id)
            func->valueNames.resize(id + 1);
        if (func->valueNames[id].empty())
            func->valueNames[id] = "%t" + std::to_string(id);
    }
    if (ctx.nextTemp() <= id)
        ctx.setNextTemp(id + 1);
    return id;
}

/// @brief Generate a deterministic label for a synthesized fallback block.
///
/// @details Uses the mangler to produce a unique label based on an incrementing
///          counter so ad-hoc blocks (e.g., for error handling) remain stable
///          across runs.
///
/// @return Fresh block label owned by the caller.
std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

/// @brief Access the helper responsible for emitting IL instructions.
/// @return Reference to the lowering emitter; asserts when uninitialised.
lower::Emitter &Lowerer::emitter() noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

/// @brief Access the instruction emitter from a const context.
/// @return Const reference to the lowering emitter; asserts when uninitialised.
const lower::Emitter &Lowerer::emitter() const noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}
} // namespace il::frontends::basic
