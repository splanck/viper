// File: src/frontends/basic/Lowerer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
//          runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
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

void Lowerer::ProcedureContext::reset() noexcept
{
    function_ = nullptr;
    current_ = nullptr;
    exitIndex_ = 0;
    nextTemp_ = 0;
    boundsCheckId_ = 0;
    blockNames_.reset();
    loopState_.reset();
    errorHandlers_.reset();
}

Function *Lowerer::ProcedureContext::function() const noexcept
{
    return function_;
}

void Lowerer::ProcedureContext::setFunction(Function *function) noexcept
{
    function_ = function;
    loopState_.setFunction(function);
}

BasicBlock *Lowerer::ProcedureContext::current() const noexcept
{
    return current_;
}

void Lowerer::ProcedureContext::setCurrent(BasicBlock *block) noexcept
{
    current_ = block;
}

size_t Lowerer::ProcedureContext::exitIndex() const noexcept
{
    return exitIndex_;
}

void Lowerer::ProcedureContext::setExitIndex(size_t index) noexcept
{
    exitIndex_ = index;
}

unsigned Lowerer::ProcedureContext::nextTemp() const noexcept
{
    return nextTemp_;
}

void Lowerer::ProcedureContext::setNextTemp(unsigned next) noexcept
{
    nextTemp_ = next;
}

unsigned Lowerer::ProcedureContext::boundsCheckId() const noexcept
{
    return boundsCheckId_;
}

void Lowerer::ProcedureContext::setBoundsCheckId(unsigned id) noexcept
{
    boundsCheckId_ = id;
}

/// @brief Return the current bounds-check identifier and advance it.
unsigned Lowerer::ProcedureContext::consumeBoundsCheckId() noexcept
{
    return boundsCheckId_++;
}

Lowerer::ProcedureContext::LoopState &Lowerer::ProcedureContext::loopState() noexcept
{
    return loopState_;
}

const Lowerer::ProcedureContext::LoopState &
Lowerer::ProcedureContext::loopState() const noexcept
{
    return loopState_;
}

Lowerer::ProcedureContext::BlockNameState &
Lowerer::ProcedureContext::blockNames() noexcept
{
    return blockNames_;
}

const Lowerer::ProcedureContext::BlockNameState &
Lowerer::ProcedureContext::blockNames() const noexcept
{
    return blockNames_;
}

Lowerer::ProcedureContext::ErrorHandlerState &
Lowerer::ProcedureContext::errorHandlers() noexcept
{
    return errorHandlers_;
}

const Lowerer::ProcedureContext::ErrorHandlerState &
Lowerer::ProcedureContext::errorHandlers() const noexcept
{
    return errorHandlers_;
}

void Lowerer::ProcedureContext::BlockNameState::reset() noexcept
{
    lineBlocks_.clear();
    namer_.reset();
}

std::unordered_map<int, size_t> &
Lowerer::ProcedureContext::BlockNameState::lineBlocks() noexcept
{
    return lineBlocks_;
}

const std::unordered_map<int, size_t> &
Lowerer::ProcedureContext::BlockNameState::lineBlocks() const noexcept
{
    return lineBlocks_;
}

Lowerer::BlockNamer *Lowerer::ProcedureContext::BlockNameState::namer() noexcept
{
    return namer_.get();
}

const Lowerer::BlockNamer *
Lowerer::ProcedureContext::BlockNameState::namer() const noexcept
{
    return namer_.get();
}

void Lowerer::ProcedureContext::BlockNameState::setNamer(
    std::unique_ptr<BlockNamer> namer) noexcept
{
    namer_ = std::move(namer);
}

void Lowerer::ProcedureContext::BlockNameState::resetNamer() noexcept
{
    namer_.reset();
}

void Lowerer::ProcedureContext::LoopState::reset() noexcept
{
    function_ = nullptr;
    exitTargetIdx_.clear();
    exitTaken_.clear();
}

void Lowerer::ProcedureContext::LoopState::setFunction(Function *function) noexcept
{
    function_ = function;
    exitTargetIdx_.clear();
    exitTaken_.clear();
}

void Lowerer::ProcedureContext::LoopState::push(BasicBlock *exitBlock)
{
    if (function_)
    {
        auto base = &function_->blocks[0];
        exitTargetIdx_.push_back(static_cast<size_t>(exitBlock - base));
    }
    else
    {
        exitTargetIdx_.push_back(0);
    }
    exitTaken_.push_back(false);
}

void Lowerer::ProcedureContext::LoopState::pop()
{
    if (exitTargetIdx_.empty())
        return;
    exitTargetIdx_.pop_back();
    exitTaken_.pop_back();
}

BasicBlock *Lowerer::ProcedureContext::LoopState::current() const
{
    if (exitTargetIdx_.empty() || !function_)
        return nullptr;
    size_t idx = exitTargetIdx_.back();
    if (idx >= function_->blocks.size())
        return nullptr;
    return &function_->blocks[idx];
}

void Lowerer::ProcedureContext::LoopState::markTaken()
{
    if (!exitTaken_.empty())
        exitTaken_.back() = true;
}

void Lowerer::ProcedureContext::LoopState::refresh(BasicBlock *exitBlock)
{
    if (exitTargetIdx_.empty() || !function_)
        return;
    auto base = &function_->blocks[0];
    exitTargetIdx_.back() = static_cast<size_t>(exitBlock - base);
}

bool Lowerer::ProcedureContext::LoopState::taken() const
{
    return !exitTaken_.empty() && exitTaken_.back();
}

void Lowerer::ProcedureContext::ErrorHandlerState::reset() noexcept
{
    active_ = false;
    activeIndex_.reset();
    activeLine_.reset();
    blocks_.clear();
    handlerTargets_.clear();
}

bool Lowerer::ProcedureContext::ErrorHandlerState::active() const noexcept
{
    return active_;
}

void Lowerer::ProcedureContext::ErrorHandlerState::setActive(bool active) noexcept
{
    active_ = active;
}

std::optional<size_t>
Lowerer::ProcedureContext::ErrorHandlerState::activeIndex() const noexcept
{
    return activeIndex_;
}

void Lowerer::ProcedureContext::ErrorHandlerState::setActiveIndex(
    std::optional<size_t> index) noexcept
{
    activeIndex_ = index;
}

std::optional<int>
Lowerer::ProcedureContext::ErrorHandlerState::activeLine() const noexcept
{
    return activeLine_;
}

void Lowerer::ProcedureContext::ErrorHandlerState::setActiveLine(
    std::optional<int> line) noexcept
{
    activeLine_ = line;
}

std::unordered_map<int, size_t> &
Lowerer::ProcedureContext::ErrorHandlerState::blocks() noexcept
{
    return blocks_;
}

const std::unordered_map<int, size_t> &
Lowerer::ProcedureContext::ErrorHandlerState::blocks() const noexcept
{
    return blocks_;
}

std::unordered_map<size_t, int> &
Lowerer::ProcedureContext::ErrorHandlerState::handlerTargets() noexcept
{
    return handlerTargets_;
}

const std::unordered_map<size_t, int> &
Lowerer::ProcedureContext::ErrorHandlerState::handlerTargets() const noexcept
{
    return handlerTargets_;
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

unsigned Lowerer::BlockNamer::nextDo()
{
    return loopCounter++;
}

std::string Lowerer::BlockNamer::doHead(unsigned id) const
{
    return "do_head_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::doBody(unsigned id) const
{
    return "do_body_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::doEnd(unsigned id) const
{
    return "do_end_" + std::to_string(id) + "_" + proc;
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

void Lowerer::setDiagnosticEmitter(DiagnosticEmitter *emitter) noexcept
{
    diagnosticEmitter_ = emitter;
    if (emitter)
    {
        TypeRules::setTypeErrorSink([emitter](const TypeRules::TypeError &error) {
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

    builder->addBlock(
        f,
        blockNamer ? blockNamer->entry()
                   : mangler.block("entry_" + name));

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
            builder->addBlock(
                f,
                mangler.block("L" + std::to_string(vLine) + "_" + name));
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


TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    using NumericType = TypeRules::NumericType;

    auto classifyBinary = [&](const BinaryExpr &bin) -> NumericType {
        if (!bin.lhs || !bin.rhs)
            return NumericType::Long;
        NumericType lhsTy = classifyNumericType(*bin.lhs);
        NumericType rhsTy = classifyNumericType(*bin.rhs);
        switch (bin.op)
        {
            case BinaryExpr::Op::Add:
                return TypeRules::resultType('+', lhsTy, rhsTy);
            case BinaryExpr::Op::Sub:
                return TypeRules::resultType('-', lhsTy, rhsTy);
            case BinaryExpr::Op::Mul:
                return TypeRules::resultType('*', lhsTy, rhsTy);
            case BinaryExpr::Op::Div:
                return TypeRules::resultType('/', lhsTy, rhsTy);
            case BinaryExpr::Op::IDiv:
                return TypeRules::resultType('\\', lhsTy, rhsTy);
            case BinaryExpr::Op::Mod:
                return TypeRules::resultType("MOD", lhsTy, rhsTy);
            case BinaryExpr::Op::Pow:
                return TypeRules::resultType('^', lhsTy, rhsTy);
            default:
                return NumericType::Long;
        }
    };

    if (const auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        const long long value = i->value;
        if (value >= std::numeric_limits<int16_t>::min() && value <= std::numeric_limits<int16_t>::max())
            return NumericType::Integer;
        return NumericType::Long;
    }
    if (dynamic_cast<const BoolExpr *>(&expr))
        return NumericType::Integer;
    if (dynamic_cast<const StringExpr *>(&expr))
        return NumericType::Double;
    if (dynamic_cast<const FloatExpr *>(&expr))
        return NumericType::Double;
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr))
    {
        if (const auto *info = findSymbol(var->name))
        {
            if (info->hasType)
            {
                if (info->type == AstType::F64)
                    return NumericType::Double;
                return NumericType::Long;
            }
        }
        AstType astTy = inferAstTypeFromName(var->name);
        return (astTy == AstType::F64) ? NumericType::Double : NumericType::Long;
    }
    if (const auto *arr = dynamic_cast<const ArrayExpr *>(&expr))
    {
        (void)arr;
        return NumericType::Long;
    }
    if (const auto *lb = dynamic_cast<const LBoundExpr *>(&expr))
    {
        (void)lb;
        return NumericType::Long;
    }
    if (const auto *ub = dynamic_cast<const UBoundExpr *>(&expr))
    {
        (void)ub;
        return NumericType::Long;
    }
    if (const auto *un = dynamic_cast<const UnaryExpr *>(&expr))
    {
        if (!un->expr)
            return NumericType::Long;
        NumericType operand = classifyNumericType(*un->expr);
        return operand;
    }
    if (const auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
        return classifyBinary(*bin);
    if (const auto *call = dynamic_cast<const BuiltinCallExpr *>(&expr))
    {
        switch (call->builtin)
        {
            case BuiltinCallExpr::Builtin::Cint:
                return NumericType::Integer;
            case BuiltinCallExpr::Builtin::Clng:
                return NumericType::Long;
            case BuiltinCallExpr::Builtin::Csng:
                return NumericType::Single;
            case BuiltinCallExpr::Builtin::Cdbl:
                return NumericType::Double;
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
                return NumericType::Double;
            case BuiltinCallExpr::Builtin::Str:
                if (!call->args.empty() && call->args[0])
                    return classifyNumericType(*call->args[0]);
                return NumericType::Long;
            default:
                return NumericType::Double;
        }
    }
    if (const auto *callExpr = dynamic_cast<const CallExpr *>(&expr))
    {
        if (const auto *sig = findProcSignature(callExpr->callee))
        {
            switch (sig->retType.kind)
            {
                case Type::Kind::I16:
                    return NumericType::Integer;
                case Type::Kind::I32:
                case Type::Kind::I64:
                    return NumericType::Long;
                case Type::Kind::F64:
                    return NumericType::Double;
                default:
                    break;
            }
        }
        return NumericType::Long;
    }
    return NumericType::Long;
}
} // namespace il::frontends::basic
