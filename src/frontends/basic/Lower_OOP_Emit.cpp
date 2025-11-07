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

/// @brief Translate a BASIC AST type into the corresponding IL core type.
///
/// @details CLASS lowering frequently needs to emit temporaries or parameters
///          using the IL type system.  The helper performs a direct mapping from
///          BASIC enum values to @ref il::core::Type::Kind entries so all call
///          sites share the same conversion logic.  Unknown enumerators fall
///          back to 64-bit integers to match historical behaviour and avoid
///          crashes during incremental language development.
///
/// @param ty BASIC type enumerator describing the source language type.
/// @return IL type descriptor used when emitting instructions for @p ty.
[[nodiscard]] il::core::Type ilTypeForAstType(AstType ty)
{
    using IlType = il::core::Type;
    switch (ty)
    {
        case AstType::I64:
            return IlType(IlType::Kind::I64);
        case AstType::F64:
            return IlType(IlType::Kind::F64);
        case AstType::Str:
            return IlType(IlType::Kind::Str);
        case AstType::Bool:
            return IlType(IlType::Kind::I1);
    }
    return IlType(IlType::Kind::I64);
}

/// @brief Extract raw statement pointers from an owning body list.
///
/// @details Constructor, destructor, and method declarations all store their
///          bodies as vectors of owning @ref StmtPtr values.  Lowering only needs
///          borrowed pointers because the @ref Lowerer never assumes ownership.
///          The helper strips the indirection while skipping null entries so
///          downstream passes receive a dense sequence of statements.
///
/// @param body Owning pointers sourced from the AST node.
/// @return Borrowed pointer list suitable for lowering routines.
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

/// @brief Allocate and initialise the implicit @c ME slot for a class member.
///
/// @details BASIC object procedures implicitly capture @c ME, a pointer to the
///          current instance.  The routine reserves a stack slot, records the
///          slot identifier in the symbol table, and stores the incoming @c self
///          parameter so later field accesses can load from a stable location.
///          The lowering location is cleared because the slot materialisation is
///          synthetic and should not inherit the caller's source location.
///
/// @param className Name of the class being lowered (used for symbol metadata).
/// @param fn        Function currently under construction.
/// @return Identifier of the stack slot holding the @c ME pointer.
unsigned Lowerer::materializeSelfSlot(const std::string &className, Function &fn)
{
    curLoc = {};
    setSymbolObjectType("Me", className);
    auto &info = ensureSymbol("Me");
    info.referenced = true;
    Value slot = emitAlloca(8);
    info.slotId = slot.id;
    emitStore(Type(Type::Kind::Ptr), slot, Value::temp(fn.params.front().id));
    return slot.id;
}

/// @brief Load the implicit @c ME pointer from the cached stack slot.
///
/// @details Resets the current source location because the operation is
///          compiler-generated, then emits a load from the previously
///          materialised slot.  Keeping the logic in a helper avoids duplicating
///          the slot bookkeeping across constructor, destructor, and method
///          bodies.
///
/// @param slotId Identifier of the @c ME stack slot materialised earlier.
/// @return Runtime value referencing the @c ME pointer.
Lowerer::Value Lowerer::loadSelfPointer(unsigned slotId)
{
    curLoc = {};
    return emitLoad(Type(Type::Kind::Ptr), Value::temp(slotId));
}

/// @brief Release reference-counted fields during destructor emission.
///
/// @details Iterates over the cached @ref ClassLayout to determine which fields
///          require runtime release calls.  String fields trigger retain/release
///          helpers, and future field kinds can extend the switch without
///          altering destructor logic.  The helper uses @c curLoc resets so the
///          emitted instructions are treated as compiler-synthesised clean-up
///          rather than user code.
///
/// @param selfPtr Pointer to the object instance being destroyed.
/// @param layout  Metadata describing field offsets and types for the class.
void Lowerer::emitFieldReleaseSequence(Value selfPtr, const ClassLayout &layout)
{
    for (const auto &field : layout.fields)
    {
        curLoc = {};
        Value fieldPtr = emitBinary(Opcode::GEP,
                                    Type(Type::Kind::Ptr),
                                    selfPtr,
                                    Value::constInt(static_cast<long long>(field.offset)));
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

/// @brief Emit the IL body for a BASIC class constructor.
///
/// @details Resets lowering state, binds the implicit @c ME parameter, materialises
///          user parameters, and drives the lowering pipeline for the constructor
///          body.  Runtime helpers required for array parameters are requested,
///          and deterministic exits are enforced by branching to the synthetic
///          exit block when user code falls through.  The routine also handles
///          releasing transient allocations before returning.
///
/// @param klass Class definition providing layout and member metadata.
/// @param ctor  AST node describing the constructor body and parameters.
void Lowerer::emitClassConstructor(const ClassDecl &klass, const ConstructorDecl &ctor)
{
    resetLoweringState();
    auto body = gatherBody(ctor.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + ctor.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"Me", Type(Type::Kind::Ptr)});
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

/// @brief Emit the IL body for a BASIC class destructor.
///
/// @details Lowers the optional user-defined destructor body, falls back to an
///          empty body when absent, and always invokes
///          @ref emitFieldReleaseSequence to clean up reference-counted fields.
///          Locals and parameters are released before returning to honour BASIC's
///          deterministic destruction semantics.
///
/// @param klass    Class definition whose instance is being destroyed.
/// @param userDtor Optional AST node for the user-authored destructor body.
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
    metadata.irParams.push_back({"Me", Type(Type::Kind::Ptr)});

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

/// @brief Emit the IL body for a BASIC class method.
///
/// @details Mirrors constructor emission by setting up the @c ME slot, mapping
///          user parameters to stack slots, and invoking the standard statement
///          lowering sequence.  The helper also ensures array parameters request
///          their runtime retain/release helpers and that fallthrough paths branch
///          to the synthesised exit block.
///
/// @param klass  Class definition that owns the method.
/// @param method AST node describing the method signature and body.
void Lowerer::emitClassMethod(const ClassDecl &klass, const MethodDecl &method)
{
    resetLoweringState();
    auto body = gatherBody(method.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + method.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"Me", Type(Type::Kind::Ptr)});
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

/// @brief Lower all class declarations and their members within a program.
///
/// @details Iterates the top-level statements looking for CLASS declarations,
///          gathers their constructor, destructor, and method members, and then
///          emits each body using the dedicated helpers.  This ensures object
///          members are materialised before ordinary procedures so runtime
///          helpers and mangled names are available to subsequent lowering steps.
///
/// @param prog BASIC program containing potential CLASS declarations.
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
        {
            emitClassConstructor(klass, *ctor);
        }
        else
        {
            const ClassInfo *info = oopIndex_.findClass(klass.name);
            if (info && info->hasSynthCtor)
            {
                ConstructorDecl synthCtor;
                synthCtor.loc = klass.loc;
                synthCtor.line = klass.line;
                emitClassConstructor(klass, synthCtor);
            }
        }
        emitClassDestructor(klass, dtor);
        for (const auto *method : methods)
            emitClassMethod(klass, *method);
    }
}

} // namespace il::frontends::basic
