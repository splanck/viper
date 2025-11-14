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

class FieldScopeGuard
{
  public:
    FieldScopeGuard(Lowerer &lowerer, const std::string &className) noexcept : lowerer_(lowerer)
    {
        lowerer_.pushFieldScope(className);
    }

    FieldScopeGuard(const FieldScopeGuard &) = delete;
    FieldScopeGuard &operator=(const FieldScopeGuard &) = delete;

    ~FieldScopeGuard()
    {
        lowerer_.popFieldScope();
    }

  private:
    Lowerer &lowerer_;
};

} // namespace

namespace
{
class ClassContextGuard
{
  public:
    explicit ClassContextGuard(Lowerer &lowerer, const std::string &qualifiedName)
        : lowerer_(lowerer)
    {
        lowerer_.pushClass(qualifiedName);
    }

    ClassContextGuard(const ClassContextGuard &) = delete;
    ClassContextGuard &operator=(const ClassContextGuard &) = delete;

    ~ClassContextGuard()
    {
        lowerer_.popClass();
    }

  private:
    Lowerer &lowerer_;
};
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
    setSymbolObjectType("ME", className);
    auto &info = ensureSymbol("ME");
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
    ClassContextGuard classGuard(*this, qualify(klass.name));
    FieldScopeGuard fieldScope(*this, klass.name);
    auto body = gatherBody(ctor.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + ctor.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"ME", Type(Type::Kind::Ptr)});
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

    std::string name = mangleClassCtor(qualify(klass.name));
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
    ClassContextGuard classGuard(*this, qualify(klass.name));
    FieldScopeGuard fieldScope(*this, klass.name);
    std::vector<const Stmt *> body;
    if (userDtor)
    {
        body = gatherBody(userDtor->body);
        collectVars(body);
    }

    ProcedureMetadata metadata;
    metadata.paramCount = 1;
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"ME", Type(Type::Kind::Ptr)});

    std::string name = mangleClassDtor(qualify(klass.name));
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
    ClassContextGuard classGuard(*this, qualify(klass.name));
    FieldScopeGuard fieldScope(*this, klass.name);
    auto body = gatherBody(method.body);
    collectVars(body);

    ProcedureMetadata metadata;
    metadata.paramCount = 1 + method.params.size();
    metadata.bodyStmts = body;
    metadata.irParams.push_back({"ME", Type(Type::Kind::Ptr)});
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

    const bool returnsValue = method.ret.has_value();
    Type methodRetType = Type(Type::Kind::Void);
    std::optional<::il::frontends::basic::Type> methodRetAst;
    if (returnsValue)
    {
        methodRetType = ilTypeForAstType(*method.ret);
        methodRetAst = method.ret;
    }

    std::string name = mangleMethod(qualify(klass.name), method.name);
    Function &fn = builder->startFunction(name, methodRetType, metadata.irParams);

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
    if (returnsValue)
    {
        Value retValue = Value::constInt(0);
        switch (*methodRetAst)
        {
            case ::il::frontends::basic::Type::I64:
                retValue = Value::constInt(0);
                break;
            case ::il::frontends::basic::Type::F64:
                retValue = Value::constFloat(0.0);
                break;
            case ::il::frontends::basic::Type::Str:
                retValue = emitConstStr(getStringLabel(""));
                break;
            case ::il::frontends::basic::Type::Bool:
                retValue = emitBoolConst(false);
                break;
        }
        emitRet(retValue);
    }
    else
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

    // Walk the program and nested namespaces to emit class/interface members.
    std::function<void(const std::vector<StmtPtr> &)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts)
    {
        for (const auto &stmt : stmts)
        {
            if (!stmt)
                continue;
            if (stmt->stmtKind() == Stmt::Kind::NamespaceDecl)
            {
                const auto &ns = static_cast<const NamespaceDecl &>(*stmt);
                // Enter namespace for qualification
                pushNamespace(ns.path);
                scan(ns.body);
                // Leave namespace
                popNamespace(ns.path.size());
                continue;
            }
            if (stmt->stmtKind() != Stmt::Kind::ClassDecl)
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
    };

    // Start recursive scan from the program's main statements.
    scan(prog.main);

    // Synthesize interface registration and binding thunks, and a module init.
    // 1) Interface registration thunks
    std::vector<std::string> regThunks;
    for (const auto &p : oopIndex_.interfacesByQname())
    {
        const std::string &qname = p.first;
        const InterfaceInfo &iface = p.second;
        const std::string fn = mangleIfaceRegThunk(qname);
        regThunks.push_back(fn);
        // fn(): void
        Function &f = builder->startFunction(fn, Type(Type::Kind::Void), {});
        context().setFunction(&f);
        context().setNextTemp(f.valueNames.size());
        // Create entry block for thunk body
        builder->addBlock(f, "entry");
        context().setCurrent(&f.blocks.front());
        f.blocks.front().terminated = false;
        // Call rt_register_interface_direct(ifaceId, "qname", slot_count)
        emitCall("rt_register_interface_direct",
                 {Value::constInt(iface.ifaceId),
                  emitConstStr(qname),
                  Value::constInt((long long)iface.slots.size())});
        emitRetVoid();
    }

    // 2) Class->interface binding thunks (allocate + populate itable arrays)
    std::vector<std::string> bindThunks;
    for (const auto &entry : oopIndex_.classes())
    {
        const ClassInfo &ci = entry.second;
        // Resolve type id from class layout cache (by unqualified name)
        auto itLayout = classLayouts_.find(ci.name);
        if (itLayout == classLayouts_.end())
            continue;
        const long long typeId = (long long)itLayout->second.classId;
        for (int ifaceId : ci.implementedInterfaces)
        {
            // Find iface qname
            const InterfaceInfo *iface = nullptr;
            for (const auto &ip : oopIndex_.interfacesByQname())
            {
                if (ip.second.ifaceId == ifaceId)
                {
                    iface = &ip.second;
                    break;
                }
            }
            if (!iface)
                continue;
            const std::string thunk = mangleIfaceBindThunk(ci.qualifiedName, iface->qualifiedName);
            bindThunks.push_back(thunk);
            Function &fb = builder->startFunction(thunk, Type(Type::Kind::Void), {});
            context().setFunction(&fb);
            context().setNextTemp(fb.valueNames.size());
            // Create entry block for thunk body
            builder->addBlock(fb, "entry");
            context().setCurrent(&fb.blocks.front());
            fb.blocks.front().terminated = false;
            // Allocate a persistent itable: slot_count * sizeof(void*)
            const std::size_t slotCount = iface->slots.size();
            const long long bytes = static_cast<long long>(slotCount * 8ULL);
            Value itablePtr =
                emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(bytes)});

            // Helper: find concrete implementor along base chain for method @name
            auto findImplementorQClass = [&](const std::string &startQ,
                                             const std::string &mname) -> std::string
            {
                const ClassInfo *cur = oopIndex_.findClass(startQ);
                while (cur)
                {
                    auto itM = cur->methods.find(mname);
                    if (itM != cur->methods.end())
                    {
                        // Prefer non-abstract method implementation
                        if (!itM->second.isAbstract)
                            return cur->qualifiedName;
                    }
                    if (cur->baseQualified.empty())
                        break;
                    cur = oopIndex_.findClass(cur->baseQualified);
                }
                return startQ; // fallback
            };

            // Populate itable slots in interface slot order
            auto mapIt = ci.ifaceSlotImpl.find(ifaceId);
            for (std::size_t s = 0; s < slotCount; ++s)
            {
                const long long offset = static_cast<long long>(s * 8ULL);
                Value slotPtr = emitBinary(
                    Opcode::GEP, Type(Type::Kind::Ptr), itablePtr, Value::constInt(offset));
                // Resolve method name for this slot; may be empty for abstract/missing
                std::string mname;
                if (mapIt != ci.ifaceSlotImpl.end() && s < mapIt->second.size())
                    mname = mapIt->second[s];
                if (mname.empty())
                {
                    // Store null for missing implementations (keeps layout deterministic)
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::null());
                }
                else
                {
                    const std::string implQ = findImplementorQClass(ci.qualifiedName, mname);
                    const std::string targetLabel = mangleMethod(implQ, mname);
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::global(targetLabel));
                }
            }

            // Bind the populated itable to (typeId, ifaceId)
            emitCall("rt_bind_interface",
                     {Value::constInt(typeId), Value::constInt((long long)ifaceId), itablePtr});
            emitRetVoid();
        }
    }

    // 3) Module init that calls reg thunks first, then bind thunks.
    const std::string initName = mangleOopModuleInit();
    Function &initF = builder->startFunction(initName, Type(Type::Kind::Void), {});
    context().setFunction(&initF);
    context().setNextTemp(initF.valueNames.size());
    // Create entry block for module init body
    builder->addBlock(initF, "entry");
    context().setCurrent(&initF.blocks.front());
    initF.blocks.front().terminated = false;
    for (const auto &fn : regThunks)
        emitCall(fn, {});
    for (const auto &fn : bindThunks)
        emitCall(fn, {});
    emitRetVoid();

    // Call module init at the start of main by emitting a call in program emission.
    // Note: Program emission will run after this; ensure ProgramLowering invokes this init.
}

} // namespace il::frontends::basic
