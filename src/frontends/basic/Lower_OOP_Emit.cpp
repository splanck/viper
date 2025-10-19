//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Emit.cpp
// Purpose: Emit constructor, destructor, and method bodies for BASIC CLASS nodes.
// Key invariants: Functions bind the implicit ME parameter and share lowering
//                 scaffolding with procedure emission.
// Ownership/Lifetime: Operates on Lowerer state borrowed from the lowering
//                     pipeline; owns no persistent resources.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{
namespace
{
using AstType = ::il::frontends::basic::Type;

[[nodiscard]] il::core::Type ilTypeForAstType(AstType ty)
{
    using IlType = il::core::Type;
    switch (ty)
    {
        case AstType::I64: return IlType(IlType::Kind::I64);
        case AstType::F64: return IlType(IlType::Kind::F64);
        case AstType::Str: return IlType(IlType::Kind::Str);
        case AstType::Bool: return IlType(IlType::Kind::I1);
    }
    return IlType(IlType::Kind::I64);
}

[[nodiscard]] std::vector<const Stmt *> gatherBody(const std::vector<StmtPtr> &body)
{
    std::vector<const Stmt *> out;
    out.reserve(body.size());
    for (const auto &stmt : body)
    {
        if (stmt)
            out.push_back(stmt.get());
    }
    return out;
}

} // namespace

unsigned Lowerer::materializeSelfSlot(const std::string &className, Function &fn)
{
    curLoc = {};
    setSymbolObjectType("ME", className);
    auto &info = ensureSymbol("ME");
    info.referenced = true;
    Value slot = emitAlloca(8);
    info.slotId = slot.id;
    emitStore(Type(Type::Kind::Ptr), slot, Value::temp(fn.params.front().id));
    return slot.id;
}

Lowerer::Value Lowerer::loadSelfPointer(unsigned slotId)
{
    curLoc = {};
    return emitLoad(Type(Type::Kind::Ptr), Value::temp(slotId));
}

void Lowerer::emitFieldReleaseSequence(Value selfPtr, const ClassLayout &layout)
{
    for (const auto &field : layout.fields)
    {
        curLoc = {};
        Value fieldPtr =
            emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), selfPtr, Value::constInt(static_cast<long long>(field.offset)));
        switch (field.type)
        {
            case AstType::Str:
            {
                Value fieldValue = emitLoad(Type(Type::Kind::Str), fieldPtr);
                requireStrReleaseMaybe();
                emitCall("rt_str_release_maybe", {fieldValue});
                break;
            }
            case AstType::I64:
            case AstType::F64:
            case AstType::Bool:
            default:
                break;
        }
    }
}

void Lowerer::emitClassConstructor(const ClassDecl &klass, const ConstructorDecl &ctor)
{
    resetLoweringState();
    auto body = gatherBody(ctor.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + ctor.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"self", Type(Type::Kind::Ptr)});
    for (const auto &param : ctor.params)
    {
        Type ilParamTy = param.is_array ? Type(Type::Kind::Ptr) : ilTypeForAstType(param.type);
        metadata.irParams.push_back({param.name, ilParamTy});
        if (param.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    std::string name = mangleClassCtor(klass.name);
    Function &fn = builder->startFunction(name, Type(Type::Kind::Void), metadata.irParams);

    auto &ctx = context();
    ctx.setFunction(&fn);
    ctx.setNextTemp(fn.valueNames.size());

    buildProcedureSkeleton(fn, name, metadata);

    ctx.setCurrent(&fn.blocks.front());
    materializeSelfSlot(klass.name, fn);
    for (std::size_t i = 0; i < ctor.params.size(); ++i)
    {
        const auto &param = ctor.params[i];
        metadata.paramNames.insert(param.name);
        curLoc = param.loc;
        Value slot = emitAlloca((!param.is_array && param.type == AstType::Bool) ? 1 : 8);
        if (param.is_array)
        {
            markArray(param.name);
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        setSymbolType(param.name, param.type);
        markSymbolReferenced(param.name);
        auto &info = ensureSymbol(param.name);
        info.slotId = slot.id;
        Type ilParamTy = param.is_array ? Type(Type::Kind::Ptr) : ilTypeForAstType(param.type);
        Value incoming = Value::temp(fn.params[1 + i].id);
        if (param.is_array)
            storeArray(slot, incoming);
        else
            emitStore(ilParamTy, slot, incoming);
    }
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    Function *func = ctx.function();
    BasicBlock *exitBlock = &func->blocks[ctx.exitIndex()];

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        emitBr(exitBlock);
    }
    else
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
            emitBr(exitBlock);
    }

    ctx.setCurrent(exitBlock);
    curLoc = {};
    releaseObjectLocals(metadata.paramNames);
    releaseObjectParams(metadata.paramNames);
    releaseArrayLocals(metadata.paramNames);
    releaseArrayParams(metadata.paramNames);
    curLoc = {};
    emitRetVoid();
    ctx.blockNames().resetNamer();
}

void Lowerer::emitClassDestructor(const ClassDecl &klass, const DestructorDecl *userDtor)
{
    resetLoweringState();
    std::vector<const Stmt *> body;
    if (userDtor)
    {
        body = gatherBody(userDtor->body);
        collectVars(body);
    }

    ProcedureMetadata metadata;
    metadata.paramCount = 1;
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"self", Type(Type::Kind::Ptr)});

    std::string name = mangleClassDtor(klass.name);
    Function &fn = builder->startFunction(name, Type(Type::Kind::Void), metadata.irParams);

    auto &ctx = context();
    ctx.setFunction(&fn);
    ctx.setNextTemp(fn.valueNames.size());

    buildProcedureSkeleton(fn, name, metadata);

    ctx.setCurrent(&fn.blocks.front());
    unsigned selfSlotId = materializeSelfSlot(klass.name, fn);
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    Function *func = ctx.function();
    BasicBlock *exitBlock = &func->blocks[ctx.exitIndex()];

    if (!metadata.bodyStmts.empty())
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
            emitBr(exitBlock);
    }
    else
    {
        curLoc = {};
        emitBr(exitBlock);
    }

    ctx.setCurrent(exitBlock);
    curLoc = {};

    Value selfPtr = loadSelfPointer(selfSlotId);
    auto layoutIt = classLayouts_.find(klass.name);
    if (layoutIt != classLayouts_.end())
        emitFieldReleaseSequence(selfPtr, layoutIt->second);

    releaseObjectLocals(metadata.paramNames);
    releaseObjectParams(metadata.paramNames);
    releaseArrayLocals(metadata.paramNames);
    releaseArrayParams(metadata.paramNames);
    curLoc = {};
    emitRetVoid();
    ctx.blockNames().resetNamer();
}

void Lowerer::emitClassMethod(const ClassDecl &klass, const MethodDecl &method)
{
    resetLoweringState();
    auto body = gatherBody(method.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + method.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"self", Type(Type::Kind::Ptr)});
    for (const auto &param : method.params)
    {
        Type ilParamTy = param.is_array ? Type(Type::Kind::Ptr) : ilTypeForAstType(param.type);
        metadata.irParams.push_back({param.name, ilParamTy});
        if (param.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    std::string name = mangleMethod(klass.name, method.name);
    Function &fn = builder->startFunction(name, Type(Type::Kind::Void), metadata.irParams);

    auto &ctx = context();
    ctx.setFunction(&fn);
    ctx.setNextTemp(fn.valueNames.size());

    buildProcedureSkeleton(fn, name, metadata);

    ctx.setCurrent(&fn.blocks.front());
    materializeSelfSlot(klass.name, fn);
    for (std::size_t i = 0; i < method.params.size(); ++i)
    {
        const auto &param = method.params[i];
        metadata.paramNames.insert(param.name);
        curLoc = param.loc;
        Value slot = emitAlloca((!param.is_array && param.type == AstType::Bool) ? 1 : 8);
        if (param.is_array)
        {
            markArray(param.name);
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        setSymbolType(param.name, param.type);
        markSymbolReferenced(param.name);
        auto &info = ensureSymbol(param.name);
        info.slotId = slot.id;
        Type ilParamTy = param.is_array ? Type(Type::Kind::Ptr) : ilTypeForAstType(param.type);
        Value incoming = Value::temp(fn.params[1 + i].id);
        if (param.is_array)
            storeArray(slot, incoming);
        else
            emitStore(ilParamTy, slot, incoming);
    }
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    Function *func = ctx.function();
    BasicBlock *exitBlock = &func->blocks[ctx.exitIndex()];

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        emitBr(exitBlock);
    }
    else
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
            emitBr(exitBlock);
    }

    ctx.setCurrent(exitBlock);
    curLoc = {};
    releaseObjectLocals(metadata.paramNames);
    releaseObjectParams(metadata.paramNames);
    releaseArrayLocals(metadata.paramNames);
    releaseArrayParams(metadata.paramNames);
    curLoc = {};
    emitRetVoid();
    ctx.blockNames().resetNamer();
}

void Lowerer::emitOopDeclsAndBodies(const Program &prog)
{
    if (!builder)
        return;

    for (const auto &stmt : prog.main)
    {
        if (!stmt || stmt->stmtKind() != Stmt::Kind::ClassDecl)
            continue;

        const auto &klass = static_cast<const ClassDecl &>(*stmt);
        const ConstructorDecl *ctor = nullptr;
        const DestructorDecl *dtor = nullptr;
        std::vector<const MethodDecl *> methods;
        methods.reserve(klass.members.size());

        for (const auto &member : klass.members)
        {
            if (!member)
                continue;
            switch (member->stmtKind())
            {
                case Stmt::Kind::ConstructorDecl:
                    ctor = static_cast<const ConstructorDecl *>(member.get());
                    break;
                case Stmt::Kind::DestructorDecl:
                    dtor = static_cast<const DestructorDecl *>(member.get());
                    break;
                case Stmt::Kind::MethodDecl:
                    methods.push_back(static_cast<const MethodDecl *>(member.get()));
                    break;
                default:
                    break;
            }
        }

        if (ctor)
            emitClassConstructor(klass, *ctor);
        emitClassDestructor(klass, dtor);
        for (const auto *method : methods)
            emitClassMethod(klass, *method);
    }
}

} // namespace il::frontends::basic

