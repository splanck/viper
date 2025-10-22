// File: src/frontends/basic/Lowerer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
//          runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "frontends/basic/lower/TypeRules.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cstdint>
#include <cassert>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

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

Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

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

void Lowerer::markArray(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

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

int Lowerer::virtualLine(const Stmt &s)
{
    auto it = stmtVirtualLines_.find(&s);
    if (it != stmtVirtualLines_.end())
        return it->second;

    int line = (s.line != 0) ? s.line : synthLineBase_ + (synthSeq_++);
    stmtVirtualLines_[&s] = line;
    return line;
}

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

void Lowerer::lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                     bool stopOnTerminated,
                                     const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
}

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

Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

enum class AstKind : std::uint8_t
{
    Int,
    Float,
    String,
    Bool,
    Var,
    Array,
    LBound,
    UBound,
    Unary,
    Binary,
    BuiltinCall,
    Call,
    Other,
};

constexpr std::size_t AstKind_Count = static_cast<std::size_t>(AstKind::Other) + 1;

static AstKind detectKind(const Expr &expr) noexcept
{
    if (dynamic_cast<const IntExpr *>(&expr))
        return AstKind::Int;
    if (dynamic_cast<const FloatExpr *>(&expr))
        return AstKind::Float;
    if (dynamic_cast<const StringExpr *>(&expr))
        return AstKind::String;
    if (dynamic_cast<const BoolExpr *>(&expr))
        return AstKind::Bool;
    if (dynamic_cast<const VarExpr *>(&expr))
        return AstKind::Var;
    if (dynamic_cast<const ArrayExpr *>(&expr))
        return AstKind::Array;
    if (dynamic_cast<const LBoundExpr *>(&expr))
        return AstKind::LBound;
    if (dynamic_cast<const UBoundExpr *>(&expr))
        return AstKind::UBound;
    if (dynamic_cast<const UnaryExpr *>(&expr))
        return AstKind::Unary;
    if (dynamic_cast<const BinaryExpr *>(&expr))
        return AstKind::Binary;
    if (dynamic_cast<const BuiltinCallExpr *>(&expr))
        return AstKind::BuiltinCall;
    if (dynamic_cast<const CallExpr *>(&expr))
        return AstKind::Call;
    return AstKind::Other;
}

static bool hasSuffix(const std::string &name, char suffix) noexcept
{
    return !name.empty() && name.back() == suffix;
}

struct LowerCtx
{
    LowerCtx(Lowerer &lowerer, const Expr &node) noexcept
        : lowerer(lowerer), node(node), kind(detectKind(node))
    {
    }

    void setResult(TypeRules::NumericType ty) noexcept
    {
        result = ty;
        handled = true;
    }

    TypeRules::NumericType classify(const Expr &expr) noexcept
    {
        return lowerer.classifyNumericType(expr);
    }

    Lowerer &lowerer;
    const Expr &node;
    AstKind kind;
    TypeRules::NumericType result{TypeRules::NumericType::Long};
    bool handled{false};
};

namespace
{

using NumericType = TypeRules::NumericType;
using AstNode = Expr;
using LowerHandler = void (*)(LowerCtx &, const AstNode &);

constexpr std::size_t toIndex(AstKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

void resolveVar(LowerCtx &ctx, const AstNode &node)
{
    const auto &var = static_cast<const VarExpr &>(node);
    if (var.name.empty())
        return;
    const auto *info = ctx.lowerer.findSymbol(var.name);
    if (!info || !info->hasType)
        return;
    if (info->type == ::il::frontends::basic::Type::F64)
    {
        if (hasSuffix(var.name, '!'))
        {
            ctx.setResult(NumericType::Single);
            return;
        }
        if (hasSuffix(var.name, '#'))
        {
            ctx.setResult(NumericType::Double);
            return;
        }
        ctx.setResult(NumericType::Double);
        return;
    }
    if (hasSuffix(var.name, '%'))
    {
        ctx.setResult(NumericType::Integer);
        return;
    }
    if (hasSuffix(var.name, '&'))
    {
        ctx.setResult(NumericType::Long);
        return;
    }
    ctx.setResult(NumericType::Long);
}

void typeInt(LowerCtx &ctx, const AstNode &node)
{
    const auto &intExpr = static_cast<const IntExpr &>(node);
    switch (intExpr.suffix)
    {
        case IntExpr::Suffix::Integer:
            ctx.setResult(NumericType::Integer);
            return;
        case IntExpr::Suffix::Long:
            ctx.setResult(NumericType::Long);
            return;
        case IntExpr::Suffix::None:
            break;
    }
    const long long value = intExpr.value;
    if (value >= std::numeric_limits<int16_t>::min() &&
        value <= std::numeric_limits<int16_t>::max())
    {
        ctx.setResult(NumericType::Integer);
        return;
    }
    ctx.setResult(NumericType::Long);
}

void typeFloat(LowerCtx &ctx, const AstNode &node)
{
    const auto &floatExpr = static_cast<const FloatExpr &>(node);
    if (floatExpr.suffix == FloatExpr::Suffix::Single)
        ctx.setResult(NumericType::Single);
    else
        ctx.setResult(NumericType::Double);
}

void typeBool(LowerCtx &ctx, const AstNode &)
{
    ctx.setResult(NumericType::Integer);
}

void typeString(LowerCtx &ctx, const AstNode &)
{
    ctx.setResult(NumericType::Double);
}

void typeVar(LowerCtx &ctx, const AstNode &node)
{
    const auto &var = static_cast<const VarExpr &>(node);
    if (var.name.empty())
        return;
    if (hasSuffix(var.name, '!'))
    {
        ctx.setResult(NumericType::Single);
        return;
    }
    if (hasSuffix(var.name, '#'))
    {
        ctx.setResult(NumericType::Double);
        return;
    }
    if (hasSuffix(var.name, '%'))
    {
        ctx.setResult(NumericType::Integer);
        return;
    }
    if (hasSuffix(var.name, '&'))
    {
        ctx.setResult(NumericType::Long);
        return;
    }
    ::il::frontends::basic::Type astTy = inferAstTypeFromName(var.name);
    if (astTy == ::il::frontends::basic::Type::F64)
        ctx.setResult(NumericType::Double);
    else
        ctx.setResult(NumericType::Long);
}

void typeUnary(LowerCtx &ctx, const AstNode &node)
{
    const auto &unary = static_cast<const UnaryExpr &>(node);
    if (!unary.expr)
        return;
    ctx.setResult(ctx.classify(*unary.expr));
}

void typeBinary(LowerCtx &ctx, const AstNode &node)
{
    const auto &binary = static_cast<const BinaryExpr &>(node);
    if (!binary.lhs || !binary.rhs)
        return;
    NumericType lhsTy = ctx.classify(*binary.lhs);
    NumericType rhsTy = ctx.classify(*binary.rhs);
    ctx.setResult(lower::classifyBinaryNumericResult(binary, lhsTy, rhsTy));
}

void controlArray(LowerCtx &ctx, const AstNode &)
{
    ctx.setResult(NumericType::Long);
}

void controlBound(LowerCtx &ctx, const AstNode &)
{
    ctx.setResult(NumericType::Long);
}

void callBuiltin(LowerCtx &ctx, const AstNode &node)
{
    const auto &call = static_cast<const BuiltinCallExpr &>(node);
    std::optional<NumericType> firstArgType;
    if (!call.args.empty() && call.args[0])
        firstArgType = ctx.classify(*call.args[0]);
    ctx.setResult(lower::classifyBuiltinCall(call, firstArgType));
}

void callProcedure(LowerCtx &ctx, const AstNode &node)
{
    ctx.setResult(lower::classifyProcedureCall(ctx.lowerer, static_cast<const CallExpr &>(node)));
}

void lowerResolveNames(LowerCtx &ctx, const AstNode &node)
{
    if (ctx.handled)
        return;
    static const LowerHandler kHandlers[AstKind_Count] = {
        nullptr, // Int
        nullptr, // Float
        nullptr, // String
        nullptr, // Bool
        &resolveVar,
        nullptr, // Array
        nullptr, // LBound
        nullptr, // UBound
        nullptr, // Unary
        nullptr, // Binary
        nullptr, // BuiltinCall
        nullptr, // Call
        nullptr, // Other
    };
    if (auto handler = kHandlers[toIndex(ctx.kind)])
        handler(ctx, node);
}

void lowerTypes(LowerCtx &ctx, const AstNode &node)
{
    if (ctx.handled)
        return;
    static const LowerHandler kHandlers[AstKind_Count] = {
        &typeInt,
        &typeFloat,
        &typeString,
        &typeBool,
        &typeVar,
        nullptr,
        nullptr,
        nullptr,
        &typeUnary,
        &typeBinary,
        nullptr,
        nullptr,
        nullptr,
    };
    if (auto handler = kHandlers[toIndex(ctx.kind)])
        handler(ctx, node);
}

void lowerControlFlow(LowerCtx &ctx, const AstNode &node)
{
    if (ctx.handled)
        return;
    static const LowerHandler kHandlers[AstKind_Count] = {
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &controlArray,
        &controlBound,
        &controlBound,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    if (auto handler = kHandlers[toIndex(ctx.kind)])
        handler(ctx, node);
}

void lowerCalls(LowerCtx &ctx, const AstNode &node)
{
    if (ctx.handled)
        return;
    static const LowerHandler kHandlers[AstKind_Count] = {
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &callBuiltin,
        &callProcedure,
        nullptr,
    };
    if (auto handler = kHandlers[toIndex(ctx.kind)])
        handler(ctx, node);
}

} // namespace

TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    LowerCtx ctx(*this, expr);
    lowerResolveNames(ctx, expr);
    if (!ctx.handled)
        lowerTypes(ctx, expr);
    if (!ctx.handled)
        lowerControlFlow(ctx, expr);
    if (!ctx.handled)
        lowerCalls(ctx, expr);
    return ctx.result;
}

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

std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

lower::Emitter &Lowerer::emitter() noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

const lower::Emitter &Lowerer::emitter() const noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}
} // namespace il::frontends::basic
