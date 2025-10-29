//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Purpose: Implement the orchestration layer that converts BASIC ASTs into IL
//          modules.  The lowering facade coordinates symbol tracking,
//          procedure-level scheduling, and the shared emitter utilities used by
//          the specialised lowering helpers.
// Key invariants: Lowering state (symbol tables, block name mapping, gosub
//                 prologue information) is reset between procedures so state
//                 never leaks across runs.  Emitted IL must respect the type and
//                 bounds metadata recorded during collection.
// Ownership/Lifetime: The facade owns helper singletons (program, procedure,
//                     and statement lowerers plus the emitter) and borrows AST
//                     nodes when traversing.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief BASIC front-end lowering façade implementation.
/// @details Provides symbol tracking, procedure orchestration, and helper
///          utilities that allow the BASIC lowering pipeline to emit IL modules
///          deterministically.  The definitions here glue together the
///          individual lowering stages implemented in other translation units.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LineUtils.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <functional>
#include <limits>
#include <utility>
#include <vector>
#ifdef DEBUG
#include <unordered_set>
#endif

using namespace il::core;

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

/// @brief Retrieve an existing symbol entry or initialise a default record.
/// @details Ensures the symbol table contains an entry for @p name by inserting
///          a placeholder record when absent.  Newly created entries assume the
///          default integer type until explicit type information is provided by
///          semantic analysis or by suffix inference.
/// @param name BASIC identifier being queried.
/// @return Reference to the symbol metadata structure associated with @p name.
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

/// @brief Look up mutable symbol metadata for a BASIC identifier.
/// @param name Identifier to query.
/// @return Pointer to the associated symbol entry or nullptr when absent.
Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

/// @brief Look up immutable symbol metadata for a BASIC identifier.
/// @param name Identifier to query.
/// @return Pointer to the associated symbol entry or nullptr when absent.
const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

/// @brief Record an explicit BASIC type for the named symbol.
/// @details Updates the symbol entry created by @ref ensureSymbol with the
///          supplied type and tracks whether the identifier represents a boolean
///          scalar so lowering can emit specialised storage for logical values.
/// @param name Identifier being annotated.
/// @param type BASIC type deduced for the identifier.
void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

/// @brief Annotate a symbol as referring to an object instance.
/// @details Marks the symbol as object-typed, records the resolved class name,
///          and ensures the general symbol metadata reflects object semantics
///          (notably by disabling boolean classification).
/// @param name Identifier associated with the object.
/// @param className Fully qualified BASIC class name bound to the symbol.
void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

/// @brief Mark that a symbol was referenced in the current lowering scope.
/// @details Creates the symbol entry on demand, infers a type when one has not
///          been declared, and records that the symbol participates in lowering
///          so storage allocation and runtime helpers can be emitted later.
/// @param name Identifier that appeared in a statement or expression.
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

/// @brief Record that the symbol represents an array variable.
/// @details Array symbols require pointer storage and, when bounds checks are
///          enabled, auxiliary length slots.  This helper toggles the array flag
///          while clearing any stale boolean classification.
/// @param name Identifier representing an array.
void Lowerer::markArray(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

/// @brief Clear transient symbol metadata between lowering runs.
/// @details Removes non-string symbols entirely while resetting cached type and
///          slot information for string literals that must persist across runs.
///          The routine prepares the symbol table for a fresh procedure or
///          program without invalidating pre-registered string pools.
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

/// @brief Compute the storage requirements for a symbol's stack slot.
/// @details Synthesises the IL type, boolean-ness, and array status for the
///          named symbol by combining explicit metadata, inferred type suffixes,
///          and object annotations.  The result guides how `alloca` instructions
///          are emitted for locals and parameters.
/// @param name Identifier whose slot information is required.
/// @return Structure describing the IL type, boolean flag, array flag, and
///         object metadata for the symbol.
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

/// @brief Construct the lowering facade with optional bounds checks enabled.
/// @details Instantiates the program, procedure, and statement lowering helpers
///          alongside the shared emitter.  Bounds-check configuration is stored
///          for later use when emitting array operations.  Per-procedure state
///          is reset on each run, so the constructor only seeds persistent
///          collaborators.
/// @param boundsChecks Whether lowered code should emit runtime bounds checks.
Lowerer::Lowerer(bool boundsChecks)
    : programLowering(std::make_unique<ProgramLowering>(*this)),
      procedureLowering(std::make_unique<ProcedureLowering>(*this)),
      statementLowering(std::make_unique<StatementLowering>(*this)), boundsChecks(boundsChecks),
      emitter_(std::make_unique<lower::Emitter>(*this))
{
}

/// @brief Destroy the lowering facade and release helper instances.
Lowerer::~Lowerer() = default;

/// @brief Lower a full BASIC program into a freshly constructed IL module.
/// @details Delegates to @ref ProgramLowering::run while allocating a temporary
///          module that receives the emitted IR.  The helper orchestrates
///          runtime helper discovery, declaration emission, and procedure body
///          lowering before returning the populated module by value.
/// @param prog Program containing declarations and statements to lower.
/// @return Module populated with the lowered program.
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

/// @brief Gather lowering metadata for a procedure prior to emission.
/// @details Copies the statement pointers, records parameter information, and
///          invokes optional callbacks so downstream lowering stages have the
///          information required to allocate slots and emit runtime helpers.
/// @param params Formal parameter declarations attached to the procedure.
/// @param body Owning vector of statement nodes comprising the body.
/// @param config Hooks that customise metadata collection.
/// @return Filled metadata block describing parameters, statements, and runtime
///         requirements.
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

/// @brief Compute a stable virtual line number for a statement.
/// @details Returns the original source line when available, otherwise assigns
///          a synthetic line number that is unique within the procedure.  The
///          mapping is cached so repeated queries return the same value.
/// @param s Statement whose virtual line is required.
/// @return Virtual line number used for block naming.
int Lowerer::virtualLine(const Stmt &s)
{
    auto it = stmtVirtualLines_.find(&s);
    if (it != stmtVirtualLines_.end())
        return it->second;

    const int userLine = s.line;
    if (hasUserLine(userLine))
    {
        stmtVirtualLines_[&s] = userLine;
        return userLine;
    }

    int synthLine = synthLineBase_ + (synthSeq_++);
    stmtVirtualLines_[&s] = synthLine;
    return synthLine;
}

/// @brief Initialise the block structure for a procedure prior to lowering.
/// @details Creates the entry block, one block per numbered BASIC line, and a
///          dedicated exit block.  The procedure context is seeded with the new
///          blocks so later lowering stages can reference them by virtual line.
/// @param f Function being populated.
/// @param name BASIC procedure name (post-mangling).
/// @param metadata Metadata describing statements and parameter usage.
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

#ifdef DEBUG
    std::vector<int> keys;
    keys.reserve(metadata.bodyStmts.size());
#endif

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
#ifdef DEBUG
        keys.push_back(vLine);
#endif
    }

#ifdef DEBUG
    {
        std::unordered_set<int> seen;
        for (int k : keys)
        {
            assert(seen.insert(k).second &&
                   "Duplicate block key; unlabeled statements must have unique synthetic keys");
        }
    }
#endif

    ctx.setExitIndex(f.blocks.size());
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

/// @brief Materialise stack slots for referenced locals (and optionally parameters).
/// @details Walks the symbol table and emits `alloca` instructions for every
///          referenced symbol, initialising booleans and strings as required.
///          When bounds checks are enabled additional slots are allocated to
///          track array lengths.
/// @param paramNames Set of parameter names used to filter symbols.
/// @param includeParams Whether parameter storage should be (re)allocated.
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

/// @brief Lower a sequence of statements with optional terminator short-circuiting.
/// @details Delegates to the @ref StatementLowering helper while wiring the
///          optional @p beforeBranch callback used to patch up fallthrough edges
///          after each statement.
/// @param stmts Statements to lower in order.
/// @param stopOnTerminated Whether to stop after emitting a terminator.
/// @param beforeBranch Callback invoked before stitching fallthrough edges.
void Lowerer::lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                     bool stopOnTerminated,
                                     const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
}

/// @brief Ensure the gosub stack has been initialised for the active procedure.
/// @details Lazily emits the prologue code that allocates the stack pointer and
///          storage buffer used to implement gosub/return behaviour.  The
///          prologue is emitted once per procedure and reused for subsequent
///          lowering steps.
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

/// @brief Access the mutable procedure context for the current lowering run.
/// @return Reference to the context owned by the lowerer.
Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

/// @brief Access the immutable procedure context for the current lowering run.
/// @return Const reference to the context owned by the lowerer.
const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

/// @brief Infer the BASIC numeric category produced by an expression.
/// @details Walks the expression using a dedicated visitor that mirrors the
///          type promotion rules implemented by BASIC.  The classification is
///          later fed into @ref TypeRules to compute result types for compound
///          expressions.
/// @param expr Expression to classify.
/// @return Numeric category describing how the expression should be treated.
TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    using NumericType = TypeRules::NumericType;

    /// @brief Expression visitor that deduces the numeric category of an expression tree.
    class Classifier final : public ExprVisitor
    {
      public:
        /// @brief Bind the classifier to the owning @ref Lowerer instance.
        explicit Classifier(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

        /// @brief Retrieve the computed numeric category.
        NumericType result() const noexcept
        {
            return result_;
        }

        /// @brief Classify integer literals using suffix information.
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

        /// @brief Classify floating-point literals based on their suffix.
        void visit(const FloatExpr &f) override
        {
            result_ =
                (f.suffix == FloatExpr::Suffix::Single) ? NumericType::Single : NumericType::Double;
        }

        /// @brief Strings coerce numeric operations to double precision.
        void visit(const StringExpr &) override
        {
            result_ = NumericType::Double;
        }

        /// @brief Boolean literals behave like 16-bit integers.
        void visit(const BoolExpr &) override
        {
            result_ = NumericType::Integer;
        }

        /// @brief Classify variables using recorded symbol metadata and suffixes.
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

        /// @brief Array references are treated as 64-bit integers (pointers).
        void visit(const ArrayExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief Classify unary expressions by delegating to the operand.
        void visit(const UnaryExpr &un) override
        {
            if (!un.expr)
            {
                result_ = NumericType::Long;
                return;
            }
            result_ = lowerer_.classifyNumericType(*un.expr);
        }

        /// @brief Classify binary expressions using operand categories and operators.
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

        /// @brief Classify intrinsic calls according to builtin semantics.
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

        /// @brief Lower bound queries produce 64-bit integers.
        void visit(const LBoundExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief Upper bound queries produce 64-bit integers.
        void visit(const UBoundExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief Classify user-defined procedure calls using recorded signatures.
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

        /// @brief Object creation returns pointers encoded as 64-bit integers.
        void visit(const NewExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief `ME` resolves to the current object pointer (64-bit integer).
        void visit(const MeExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief Member access yields a pointer, represented as a 64-bit integer.
        void visit(const MemberAccessExpr &) override
        {
            result_ = NumericType::Long;
        }

        /// @brief Method calls produce pointers by default when return type is unknown.
        void visit(const MethodCallExpr &) override
        {
            result_ = NumericType::Long;
        }

      private:
        Lowerer &lowerer_;
        NumericType result_{NumericType::Long};
    };

    Classifier classifier(*this);
    expr.accept(classifier);
    return classifier.result();
}

/// @brief Reserve a new temporary identifier for value naming.
/// @details Either delegates to the IR builder (when present) or increments the
///          procedure-local counter.  When a function is active the helper also
///          seeds the value name table with a deterministic fallback label.
/// @return Fresh temporary identifier unique within the current function.
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

/// @brief Generate a deterministic fallback basic-block label.
/// @return Mangled label incorporating a monotonically increasing identifier.
std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

/// @brief Create a lightweight emitter helper bound to this lowerer.
/// @details Returns an @ref Emit facade that shares the lowerer's bookkeeping
///          so call sites can emit IL without touching the owning
///          @ref lower::Emitter directly.  The helper captures the current
///          procedure context and uses the caller's source location when set via
///          @ref Emit::at.
/// @return Emit helper scoped to the active lowering session.
Emit Lowerer::emitCommon() noexcept
{
    return Emit(*this);
}

/// @brief Create an emitter helper seeded with an explicit source location.
/// @details Wraps @ref emitCommon() while pre-populating the helper with
///          @p loc so subsequent builder calls annotate instructions with the
///          desired source metadata without extra boilerplate.
/// @param loc Source location to associate with emitted instructions.
/// @return Emit helper scoped to the active lowering session.
Emit Lowerer::emitCommon(il::support::SourceLoc loc) noexcept
{
    Emit helper(*this);
    helper.at(loc);
    return helper;
}

/// @brief Access the lowering emitter responsible for IR construction.
/// @return Reference to the emitter owned by the lowerer.
lower::Emitter &Lowerer::emitter() noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

/// @brief Access the lowering emitter (const-qualified overload).
/// @return Const reference to the emitter owned by the lowerer.
const lower::Emitter &Lowerer::emitter() const noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}
} // namespace il::frontends::basic
