//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lowering support for BASIC class bodies.  The utilities in this
// file materialise implicit `ME` slots, translate constructor and destructor
// bodies into IL, and emit the object clean-up sequences shared across member
// functions.  Keeping the logic out-of-line consolidates the object-specific
// runtime plumbing while leaving the public Lowerer interface focused on
// orchestration.
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

/// @brief Map BASIC AST types to their IL equivalents.
///
/// @details The helper provides a centralised translation between the BASIC
///          surface syntax and the IL type system.  It is used throughout the
///          lowering pipeline when building parameter lists and allocating
///          temporaries, ensuring that every code path agrees on the canonical
///          representation for primitive types.
///
/// @param ty BASIC AST type enumerator.
/// @return Corresponding IL type.
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

/// @brief Extract raw statement pointers from a statement list.
///
/// @details Constructors, destructors, and methods store their bodies as
///          `StmtPtr` vectors.  Lowering operates on bare pointers, so this
///          helper filters out null members while preserving source order.
///
/// @param body Statement list owned by an AST node.
/// @return Vector of non-null raw statement pointers.
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

/// @brief Allocate the implicit `ME` slot for a class member function.
///
/// @details The helper declares the `ME` symbol, allocates stack storage, and
///          seeds it with the incoming self parameter.  Recording the slot id in
///          the symbol table allows later code generation steps to treat `ME`
///          like any other local variable while still ensuring the runtime
///          retains a pointer to the current instance.
///
/// @param className Name of the class being lowered.
/// @param fn        Function currently under construction.
/// @return Slot identifier assigned to the `ME` local.
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

/// @brief Load the `ME` pointer from its dedicated slot.
///
/// @details Member bodies frequently need the self pointer to access fields or
///          call other methods.  By funnelling the load through this helper the
///          code documents the ownership expectations and keeps the slot access
///          consistent.
///
/// @param slotId Slot identifier returned by
///               @ref Lowerer::materializeSelfSlot.
/// @return IL value representing the current instance pointer.
Lowerer::Value Lowerer::loadSelfPointer(unsigned slotId)
{
    curLoc = {};
    return emitLoad(Type(Type::Kind::Ptr), Value::temp(slotId));
}

/// @brief Emit destruction code for reference-counted class fields.
///
/// @details The helper walks the layout metadata collected during semantic
///          analysis and synthesises releases for string fields.  Other field
///          kinds currently require no action.  Emitting releases here keeps the
///          destructor logic uniform across constructors, destructors, and early
///          exits, ensuring that object lifetimes are balanced even in the
///          presence of exceptions.
///
/// @param selfPtr Pointer to the instance being destroyed.
/// @param layout  Field layout metadata for the owning class.
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

/// @brief Lower a BASIC class constructor into IL.
///
/// @details Constructor lowering resets state, collects local variables, and
///          builds a @ref ProcedureMetadata descriptor used to configure the IL
///          function.  Parameters are allocated and stored, array arguments
///          request retain/release helpers, and the implicit `ME` pointer is
///          initialised.  After lowering the body statements the helper emits
///          field release sequences, releases array/object locals, and ends with
///          a void return.
///
/// @param klass Class declaration owning the constructor.
/// @param ctor  Constructor definition to lower.
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

/// @brief Lower the destructor for a BASIC class.
///
/// @details The generated destructor always exists even if the user does not
///          supply one.  User-provided bodies are lowered similarly to
///          constructors, but regardless of body content the helper emits a
///          release pass for string fields and balances array/object ownership
///          through the shared release helpers before returning.
///
/// @param klass     Class declaration owning the destructor.
/// @param userDtor  Optional user-defined destructor body.
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

/// @brief Lower a BASIC class method into IL.
///
/// @details Methods mirror constructor lowering: parameters are allocated, the
///          implicit `ME` pointer is materialised, and the body is lowered into
///          the entry block.  After executing the body the helper emits release
///          sequences for arrays and objects, ensuring that member invocations
///          preserve ownership discipline even when the body exits early.
///
/// @param klass  Class declaration that defines the method.
/// @param method Method definition to lower.
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

/// @brief Lower all class declarations within a BASIC program.
///
/// @details The routine scans the top-level statements for class declarations,
///          splits out constructors, destructors, and methods, and lowers each
///          member in a stable order.  The constructor is optional, the
///          destructor is always emitted (user-defined or default), and every
///          method is lowered by delegating to @ref emitClassMethod.
///
/// @param prog Program containing potential class declarations.
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

