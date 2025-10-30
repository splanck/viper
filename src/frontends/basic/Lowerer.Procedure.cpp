//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Implements procedure-level helpers for BASIC lowering including
//          signature caching, variable discovery, and staged emission helpers.
// Key invariants: Procedure helpers operate on the active Lowerer state and do
//                 not leak per-procedure state across invocations.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LineUtils.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/build/IRBuilder.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>
#include <memory>
#include <utility>

#ifdef DEBUG
#include <unordered_set>
#endif

using namespace il::core;

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

namespace
{

class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

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

ProcedureLowering::LoweringContext::LoweringContext(
    Lowerer &lowerer,
    std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
    il::build::IRBuilder &builder,
    lower::Emitter &emitter,
    std::string name,
    const std::vector<Param> &params,
    const std::vector<StmtPtr> &body,
    const Lowerer::ProcedureConfig &config)
    : lowerer(lowerer), symbols(symbols), builder(builder), emitter(emitter), name(std::move(name)),
      params(params), body(body), config(config)
{
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

const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    auto it = procSignatures.find(name);
    if (it == procSignatures.end())
        return nullptr;
    return &it->second;
}

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

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

void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

ProcedureLowering::LoweringContext ProcedureLowering::makeContext(
    const std::string &name,
    const std::vector<Param> &params,
    const std::vector<StmtPtr> &body,
    const Lowerer::ProcedureConfig &config)
{
    assert(lowerer.builder && "makeContext requires an active IRBuilder");
    return LoweringContext(lowerer,
                           lowerer.symbols,
                           *lowerer.builder,
                           lowerer.emitter(),
                           name,
                           params,
                           body,
                           config);
}

void ProcedureLowering::resetContext(LoweringContext &ctx)
{
    lowerer.resetLoweringState();
    (void)ctx;
}

void ProcedureLowering::collectProcedureInfo(LoweringContext &ctx)
{
    auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
        lowerer.collectProcedureMetadata(ctx.params, ctx.body, ctx.config));
    ctx.metadata = metadata;
    ctx.paramCount = metadata->paramCount;
    ctx.bodyStmts = metadata->bodyStmts;
    ctx.paramNames = metadata->paramNames;
    ctx.irParams = metadata->irParams;
}

void ProcedureLowering::scheduleBlocks(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    auto metadata = ctx.metadata;
    auto &procCtx = lowerer.context();
    il::core::Function &f = ctx.builder.startFunction(ctx.name, config.retType, ctx.irParams);
    ctx.function = &f;
    procCtx.setFunction(&f);
    procCtx.setNextTemp(f.valueNames.size());

    lowerer.buildProcedureSkeleton(f, ctx.name, *metadata);

    if (!f.blocks.empty())
        procCtx.setCurrent(&f.blocks.front());

    lowerer.materializeParams(ctx.params);
    lowerer.allocateLocalSlots(ctx.paramNames, /*includeParams=*/false);
}

void ProcedureLowering::emitProcedureIL(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    if (!config.emitEmptyBody || !config.emitFinalReturn || !ctx.function)
        return;

    auto &procCtx = lowerer.context();

    if (ctx.bodyStmts.empty())
    {
        lowerer.curLoc = {};
        config.emitEmptyBody();
        procCtx.blockNames().resetNamer();
        return;
    }

    lowerer.lowerStatementSequence(ctx.bodyStmts, /*stopOnTerminated=*/true);

    procCtx.setCurrent(&ctx.function->blocks[procCtx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseObjectLocals(ctx.paramNames);
    lowerer.releaseObjectParams(ctx.paramNames);
    lowerer.releaseArrayLocals(ctx.paramNames);
    lowerer.releaseArrayParams(ctx.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    procCtx.blockNames().resetNamer();
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

void Lowerer::buildProcedureSkeleton(Function &f,
                                     const std::string &name,
                                     const ProcedureMetadata &metadata)
{
    ProcedureContext &ctx = context();
    ctx.blockNames().setNamer(std::make_unique<BlockNamer>(name));
    BlockNamer *blockNamer = ctx.blockNames().namer();

    auto &entry = builder->addBlock(f, blockNamer ? blockNamer->entry() : mangler.block("entry_" + name));
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

void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams)
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

void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    ProcedureConfig config;
    config.retType = Type(Type::Kind::Void);
    config.emitEmptyBody = [&]() { emitRetVoid(); };
    config.emitFinalReturn = [&]() { emitRetVoid(); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

void Lowerer::resetLoweringState()
{
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
}

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

void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}

void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    procedureLowering->collectVars(stmts);
}

void Lowerer::collectProcedureSignatures(const Program &prog)
{
    procedureLowering->collectProcedureSignatures(prog);
}

Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

Emit Lowerer::emitCommon() noexcept
{
    return Emit(*this);
}

Emit Lowerer::emitCommon(il::support::SourceLoc loc) noexcept
{
    Emit helper(*this);
    helper.at(loc);
    return helper;
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

} // namespace il::frontends::basic

