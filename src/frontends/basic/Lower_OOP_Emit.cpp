//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "frontends/basic/ILTypeUtils.hpp"
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

        // BUG-099 fix: Handle object field release
        // BUG-105 fix: Distinguish object arrays from single objects
        if (!field.objectClassName.empty())
        {
            Value fieldValue = emitLoad(Type(Type::Kind::Ptr), fieldPtr);
            if (field.isArray)
            {
                // Object array field: use rt_arr_obj_release
                requireArrayObjRelease();
                emitCall("rt_arr_obj_release", {fieldValue});
            }
            else
            {
                // Single object field: use rt_obj_release_check0
                requestRuntimeFeature(il::runtime::RuntimeFeature::ObjReleaseChk0);
                Value needsFree =
                    emitCallRet(Type(Type::Kind::I1), "rt_obj_release_check0", {fieldValue});
                (void)needsFree; // Destructor ignores the result
            }
            continue;
        }

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
        Type ilParamTy =
            param.is_array ? Type(Type::Kind::Ptr) : type_conv::astToIlType(param.type);
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
    unsigned selfSlotId = materializeSelfSlot(klass.name, fn);

    // Initialize the object's vptr with a per-instance vtable when virtual slots exist.
    // This ensures virtual dispatch works even before a global class registry is introduced.
    // The vtable layout mirrors Semantic_OOP's slot order for the class, pointing each
    // entry at the most-derived implementation available in the class or its bases.
    if (const ClassInfo *ciInit = oopIndex_.findClass(qualify(klass.name)))
    {
        // Compute vtable size and slot->name mapping robustly from method slots.
        // Some pipelines may not have ciInit->vtable populated; derive from slots instead.
        std::size_t maxSlot = 0;
        bool hasAnyVirtual = false;
        {
            const ClassInfo *cur = ciInit;
            while (cur)
            {
                for (const auto &mp : cur->methods)
                {
                    const auto &mi = mp.second;
                    if (!mi.isVirtual || mi.slot < 0)
                        continue;
                    hasAnyVirtual = true;
                    maxSlot = std::max<std::size_t>(maxSlot, static_cast<std::size_t>(mi.slot));
                }
                if (cur->baseQualified.empty())
                    break;
                cur = oopIndex_.findClass(cur->baseQualified);
            }
        }

        const std::size_t slotCount = hasAnyVirtual ? (maxSlot + 1) : 0;
        if (slotCount > 0)
        {
            // Allocate vtable: slotCount * sizeof(void*) bytes
            const long long bytes = static_cast<long long>(slotCount * 8ULL);
            Value vtblPtr =
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
                        if (!itM->second.isAbstract)
                            return cur->qualifiedName;
                    }
                    if (cur->baseQualified.empty())
                        break;
                    cur = oopIndex_.findClass(cur->baseQualified);
                }
                return startQ; // fallback
            };

            // Populate vtable entries
            // Build slot -> method name mapping from class + bases
            std::vector<std::string> slotToName(slotCount);
            {
                const ClassInfo *cur = ciInit;
                while (cur)
                {
                    for (const auto &mp : cur->methods)
                    {
                        const auto &mname = mp.first;
                        const auto &mi = mp.second;
                        if (!mi.isVirtual || mi.slot < 0)
                            continue;
                        const std::size_t s = static_cast<std::size_t>(mi.slot);
                        if (s < slotToName.size())
                            slotToName[s] = mname; // prefer most-derived assignment first in walk
                    }
                    if (cur->baseQualified.empty())
                        break;
                    cur = oopIndex_.findClass(cur->baseQualified);
                }
            }

            for (std::size_t s = 0; s < slotCount; ++s)
            {
                const long long offset = static_cast<long long>(s * 8ULL);
                Value slotPtr = emitBinary(
                    Opcode::GEP, Type(Type::Kind::Ptr), vtblPtr, Value::constInt(offset));
                const std::string &mname = slotToName[s];
                if (mname.empty())
                {
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::null());
                }
                else
                {
                    const std::string implQ = findImplementorQClass(ciInit->qualifiedName, mname);
                    const std::string target = mangleMethod(implQ, mname);
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::global(target));
                }
            }

            // Store vptr into the object's header (offset 0)
            Value selfPtr = loadSelfPointer(selfSlotId);
            emitStore(Type(Type::Kind::Ptr), selfPtr, vtblPtr);
        }
    }
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
        // BUG-073 fix: Preserve object-class typing for parameters so member calls on params
        // resolve
        if (!param.objectClass.empty())
            setSymbolObjectType(param.name, qualify(param.objectClass));
        else
            setSymbolType(param.name, param.type);
        markSymbolReferenced(param.name);
        auto &info = ensureSymbol(param.name);
        info.slotId = slot.id;
        Type ilParamTy = (!param.objectClass.empty() || param.is_array)
                             ? Type(Type::Kind::Ptr)
                             : type_conv::astToIlType(param.type);
        Value incoming = Value::temp(fn.params[1 + i].id);
        if (param.is_array)
            storeArray(slot, incoming);
        else
            emitStore(ilParamTy, slot, incoming);
    }
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    // BUG-056: Initialize array fields declared with extents.
    // For each array field, allocate an array handle of the declared length
    // and store it into the instance field slot before executing user code.
    {
        if (const ClassLayout *layout = findClassLayout(klass.name))
        {
            Value selfPtr = loadSelfPointer(selfSlotId);
            for (const auto &field : klass.fields)
            {
                if (!field.isArray || field.arrayExtents.empty())
                    continue;

                // Compute total length as the product of inclusive extents
                // BASIC DIM uses inclusive upper bounds (e.g., DIM a(7) => 8 elements)
                long long total = 1;
                for (long long e : field.arrayExtents)
                    total *= (e + 1);
                Value length = Value::constInt(total);

                // Find field offset in layout
                const auto *fi = layout->findField(field.name);
                if (!fi)
                    continue;

                // Allocate appropriate array type
                Value handle;
                if (field.type == AstType::Str)
                {
                    requireArrayStrAlloc();
                    handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_str_alloc", {length});
                }
                else if (!field.objectClassName.empty())
                {
                    // BUG-089 fix: Allocate object array for object-typed fields
                    requireArrayObjNew();
                    handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_obj_new", {length});
                }
                else
                {
                    requireArrayI32New();
                    handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_new", {length});
                }

                // Store handle into object field
                Value fieldPtr = emitBinary(Opcode::GEP,
                                            Type(Type::Kind::Ptr),
                                            selfPtr,
                                            Value::constInt(static_cast<long long>(fi->offset)));
                emitStore(Type(Type::Kind::Ptr), fieldPtr, handle);
            }
        }
    }

    // Do not cache pointers into blocks vector; later addBlock() may reallocate.
    Function *func = ctx.function();
    const size_t exitIdx = ctx.exitIndex();

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        func = ctx.function();
        BasicBlock *exitBlock = &func->blocks[exitIdx];
        emitBr(exitBlock);
    }
    else
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
        {
            func = ctx.function();
            BasicBlock *exitBlock = &func->blocks[exitIdx];
            emitBr(exitBlock);
        }
    }

    func = ctx.function();
    ctx.setCurrent(&func->blocks[exitIdx]);
    curLoc = {};
    releaseDeferredTemps();
    releaseObjectLocals(metadata.paramNames);
    // BUG-105 fix: Don't release object/array parameters - they are borrowed references from caller
    // releaseObjectParams(metadata.paramNames);  // REMOVED
    releaseArrayLocals(metadata.paramNames);
    // releaseArrayParams(metadata.paramNames);  // REMOVED
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

    // Do not cache pointers into blocks vector; later addBlock() may reallocate.
    Function *func = ctx.function();
    const size_t exitIdx = ctx.exitIndex();

    if (!metadata.bodyStmts.empty())
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
        {
            func = ctx.function();
            BasicBlock *exitBlock = &func->blocks[exitIdx];
            emitBr(exitBlock);
        }
    }
    else
    {
        curLoc = {};
        func = ctx.function();
        BasicBlock *exitBlock = &func->blocks[exitIdx];
        emitBr(exitBlock);
    }

    func = ctx.function();
    ctx.setCurrent(&func->blocks[exitIdx]);
    curLoc = {};

    Value selfPtr = loadSelfPointer(selfSlotId);
    if (const ClassLayout *layout = findClassLayout(klass.name))
        emitFieldReleaseSequence(selfPtr, *layout);

    releaseObjectLocals(metadata.paramNames);
    // BUG-105 fix: Don't release object/array parameters - they are borrowed references from caller
    // releaseObjectParams(metadata.paramNames);  // REMOVED
    releaseArrayLocals(metadata.paramNames);
    // releaseArrayParams(metadata.paramNames);  // REMOVED
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
    metadata.paramCount = (method.isStatic ? 0 : 1) + method.params.size();
    metadata.bodyStmts = body;
    if (!method.isStatic)
        metadata.irParams.push_back({"ME", Type(Type::Kind::Ptr)});
    for (const auto &param : method.params)
    {
        // Object-typed parameters should use pointer IL type regardless of AST primitive default
        const bool isObjectParam = !param.objectClass.empty();
        Type ilParamTy = (param.is_array || isObjectParam) ? Type(Type::Kind::Ptr)
                                                           : type_conv::astToIlType(param.type);
        metadata.irParams.push_back({param.name, ilParamTy});
        if (param.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    const bool returnsValue = method.ret.has_value();
    const bool returnsObject = !method.explicitClassRetQname.empty();
    Type methodRetType = Type(Type::Kind::Void);
    std::optional<::il::frontends::basic::Type> methodRetAst;
    if (returnsValue || returnsObject)
    {
        // BUG-099 fix: Methods returning objects should use ptr type, not i64
        if (returnsObject)
        {
            methodRetType = Type(Type::Kind::Ptr);
            // Mark the return value symbol as an object type
            if (findSymbol(method.name))
            {
                std::string qualifiedClassName;
                for (size_t i = 0; i < method.explicitClassRetQname.size(); ++i)
                {
                    if (i > 0)
                        qualifiedClassName += ".";
                    qualifiedClassName += method.explicitClassRetQname[i];
                }
                setSymbolObjectType(method.name, qualifiedClassName);
            }
        }
        else if (returnsValue)
        {
            methodRetType = type_conv::astToIlType(*method.ret);
            methodRetAst = method.ret;
            // BUG-084 fix: Set the return type for the method name symbol (VB-style implicit
            // return). This ensures the function return value slot is allocated with the correct
            // type. Must be done after collectVars() but before allocateLocalSlots().
            if (findSymbol(method.name))
            {
                setSymbolType(method.name, *method.ret);
            }
        }
    }

    std::string name = mangleMethod(qualify(klass.name), method.name);
    Function &fn = builder->startFunction(name, methodRetType, metadata.irParams);

    auto &ctx = context();
    ctx.setFunction(&fn);
    ctx.setNextTemp(fn.valueNames.size());

    buildProcedureSkeleton(fn, name, metadata);

    ctx.setCurrent(&fn.blocks.front());
    if (!method.isStatic)
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
        // Preserve object-class typing for parameters so member calls on params resolve
        if (!param.objectClass.empty())
            setSymbolObjectType(param.name, qualify(param.objectClass));
        else
            setSymbolType(param.name, param.type);
        markSymbolReferenced(param.name);
        auto &info = ensureSymbol(param.name);
        info.slotId = slot.id;
        Type ilParamTy = (!param.objectClass.empty() || param.is_array)
                             ? Type(Type::Kind::Ptr)
                             : type_conv::astToIlType(param.type);
        Value incoming = Value::temp(fn.params[(method.isStatic ? 0 : 1) + i].id);
        if (param.is_array)
            storeArray(slot, incoming);
        else
            emitStore(ilParamTy, slot, incoming);
    }
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    // Do not cache pointers into blocks vector; later addBlock() may reallocate.
    Function *func = ctx.function();
    const size_t exitIdx = ctx.exitIndex();

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        func = ctx.function();
        BasicBlock *exitBlock = &func->blocks[exitIdx];
        emitBr(exitBlock);
    }
    else
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
        {
            func = ctx.function();
            BasicBlock *exitBlock = &func->blocks[exitIdx];
            emitBr(exitBlock);
        }
    }

    func = ctx.function();
    ctx.setCurrent(&func->blocks[exitIdx]);
    curLoc = {};
    // BUG-099 fix: Exclude method name from release if it returns an object
    std::unordered_set<std::string> excludeNames = metadata.paramNames;
    if (returnsObject)
    {
        excludeNames.insert(method.name);
    }
    releaseObjectLocals(excludeNames);
    // BUG-105 fix: Don't release object/array parameters - they are borrowed references from caller
    // releaseObjectParams(metadata.paramNames);  // REMOVED
    releaseArrayLocals(metadata.paramNames);
    // releaseArrayParams(metadata.paramNames);  // REMOVED
    curLoc = {};
    if (returnsValue)
    {
        Value retValue = Value::constInt(0);
        // BUG-068 fix: Check for VB-style implicit return via function name assignment
        auto methodNameSym = findSymbol(method.name);
        if (methodNameSym && methodNameSym->slotId.has_value())
        {
            // Function name was assigned - load from that variable
            // BUG-099 fix: Use methodRetType which handles object returns correctly
            Value slot = Value::temp(*methodNameSym->slotId);
            retValue = emitLoad(methodRetType, slot);
        }
        else
        {
            // No assignment - use default value
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
        }
        emitRet(retValue);
    }
    else
        emitRetVoid();
    ctx.blockNames().resetNamer();
}

void Lowerer::emitClassMethodWithBody(const ClassDecl &klass,
                                      const MethodDecl &method,
                                      const std::vector<const Stmt *> &bodyStmts)
{
    resetLoweringState();
    ClassContextGuard classGuard(*this, qualify(klass.name));
    FieldScopeGuard fieldScope(*this, klass.name);
    collectVars(bodyStmts);

    ProcedureMetadata metadata;
    metadata.paramCount = (method.isStatic ? 0 : 1) + method.params.size();
    metadata.bodyStmts = bodyStmts;
    if (!method.isStatic)
        metadata.irParams.push_back({"ME", Type(Type::Kind::Ptr)});
    for (const auto &param : method.params)
    {
        const bool isObjectParam = !param.objectClass.empty();
        Type ilParamTy = (param.is_array || isObjectParam) ? Type(Type::Kind::Ptr)
                                                           : type_conv::astToIlType(param.type);
        metadata.irParams.push_back({param.name, ilParamTy});
        if (param.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    const bool returnsValue = method.ret.has_value();
    const bool returnsObject = !method.explicitClassRetQname.empty();
    Type methodRetType = Type(Type::Kind::Void);
    std::optional<::il::frontends::basic::Type> methodRetAst;
    if (returnsValue || returnsObject)
    {
        if (returnsObject)
        {
            methodRetType = Type(Type::Kind::Ptr);
            if (findSymbol(method.name))
            {
                std::string qualifiedClassName;
                for (size_t i = 0; i < method.explicitClassRetQname.size(); ++i)
                {
                    if (i > 0)
                        qualifiedClassName += ".";
                    qualifiedClassName += method.explicitClassRetQname[i];
                }
                setSymbolObjectType(method.name, qualifiedClassName);
            }
        }
        else if (returnsValue)
        {
            methodRetType = type_conv::astToIlType(*method.ret);
            methodRetAst = method.ret;
            if (findSymbol(method.name))
            {
                setSymbolType(method.name, *method.ret);
            }
        }
    }

    std::string name = mangleMethod(qualify(klass.name), method.name);
    Function &fn = builder->startFunction(name, methodRetType, metadata.irParams);

    auto &ctx = context();
    ctx.setFunction(&fn);
    ctx.setNextTemp(fn.valueNames.size());

    buildProcedureSkeleton(fn, name, metadata);

    ctx.setCurrent(&fn.blocks.front());
    if (!method.isStatic)
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
        if (!param.objectClass.empty())
            setSymbolObjectType(param.name, qualify(param.objectClass));
        else
            setSymbolType(param.name, param.type);
        markSymbolReferenced(param.name);
        auto &info = ensureSymbol(param.name);
        info.slotId = slot.id;
        Type ilParamTy = (!param.objectClass.empty() || param.is_array)
                             ? Type(Type::Kind::Ptr)
                             : type_conv::astToIlType(param.type);
        Value incoming = Value::temp(fn.params[(method.isStatic ? 0 : 1) + i].id);
        if (param.is_array)
            storeArray(slot, incoming);
        else
            emitStore(ilParamTy, slot, incoming);
    }
    allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    Function *func = ctx.function();
    const size_t exitIdx = ctx.exitIndex();

    if (metadata.bodyStmts.empty())
    {
        curLoc = {};
        func = ctx.function();
        BasicBlock *exitBlock = &func->blocks[exitIdx];
        emitBr(exitBlock);
    }
    else
    {
        lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
        {
            func = ctx.function();
            BasicBlock *exitBlock = &func->blocks[exitIdx];
            emitBr(exitBlock);
        }
    }

    func = ctx.function();
    ctx.setCurrent(&func->blocks[exitIdx]);
    curLoc = {};
    std::unordered_set<std::string> excludeNames = metadata.paramNames;
    if (returnsObject)
    {
        excludeNames.insert(method.name);
    }
    releaseObjectLocals(excludeNames);
    releaseArrayLocals(metadata.paramNames);
    curLoc = {};
    if (returnsValue)
    {
        Value retValue = Value::constInt(0);
        auto methodNameSym = findSymbol(method.name);
        if (methodNameSym && methodNameSym->slotId.has_value())
        {
            Value slot = Value::temp(*methodNameSym->slotId);
            retValue = emitLoad(methodRetType, slot);
        }
        else if (methodRetAst)
        {
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

    // Emit module-scope globals for static fields in all classes (once per module)
    for (const auto &entry : oopIndex_.classes())
    {
        const ClassInfo &ci = entry.second;
        for (const auto &sf : ci.staticFields)
        {
            il::core::Global g;
            // Use qualified class name to keep names unique and readable
            g.name = ci.qualifiedName + "::" + sf.name;
            // Object/static string fields are pointers/strings in IL
            if (!sf.objectClassName.empty())
                g.type = Type(Type::Kind::Ptr);
            else
                g.type = type_conv::astToIlType(sf.type);
            g.init = std::string(); // zero-initialized by default
            mod->globals.push_back(std::move(g));
        }
    }

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
            const ConstructorDecl *staticCtor = nullptr;
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
                    {
                        auto *c = static_cast<const ConstructorDecl *>(member.get());
                        if (c->isStatic)
                            staticCtor = c;
                        else
                            ctor = c;
                        break;
                    }
                    case Stmt::Kind::DestructorDecl:
                        dtor = static_cast<const DestructorDecl *>(member.get());
                        break;
                    case Stmt::Kind::MethodDecl:
                        methods.push_back(static_cast<const MethodDecl *>(member.get()));
                        break;
                    case Stmt::Kind::PropertyDecl:
                    {
                        const auto *prop = static_cast<const PropertyDecl *>(member.get());
                        // Synthesize and emit getter
                        if (prop->get.present)
                        {
                            MethodDecl getter;
                            getter.loc = prop->loc;
                            getter.name = std::string("get_") + prop->name;
                            getter.access = prop->get.access;
                            getter.params = {}; // no extra params
                            getter.ret = prop->type;
                            getter.isStatic = prop->isStatic;
                            auto bodyStmts = gatherBody(prop->get.body);
                            emitClassMethodWithBody(klass, getter, bodyStmts);
                        }
                        // Synthesize and emit setter
                        if (prop->set.present)
                        {
                            MethodDecl setter;
                            setter.loc = prop->loc;
                            setter.name = std::string("set_") + prop->name;
                            setter.access = prop->set.access;
                            Param p;
                            p.name = prop->set.paramName;
                            p.type = prop->type;
                            setter.params = {p};
                            setter.isStatic = prop->isStatic;
                            auto bodyStmts = gatherBody(prop->set.body);
                            emitClassMethodWithBody(klass, setter, bodyStmts);
                        }
                        break;
                    }
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

            // Emit static constructor thunk and register in module-init
            if (staticCtor)
            {
                resetLoweringState();
                ClassContextGuard classGuard(*this, qualify(klass.name));
                auto body = gatherBody(staticCtor->body);
                collectVars(body);
                ProcedureMetadata metadata;
                metadata.paramCount = 0;
                metadata.bodyStmts = body;
                const std::string cctorName = mangleClassCtor(qualify(klass.name)) + "$static";
                Function &fn = builder->startFunction(cctorName, Type(Type::Kind::Void), {});
                context().setFunction(&fn);
                context().setNextTemp(fn.valueNames.size());
                buildProcedureSkeleton(fn, cctorName, metadata);
                context().setCurrent(&fn.blocks.front());
                // Lower static ctor body
                lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);
                if (context().current() && !context().current()->terminated)
                {
                    Function *func = context().function();
                    BasicBlock *exitBlock = &func->blocks[context().exitIndex()];
                    emitBr(exitBlock);
                }
                Function *func = context().function();
                context().setCurrent(&func->blocks[context().exitIndex()]);
                emitRetVoid();
                // Mark for module init call
                procNameAliases[cctorName] = "__static_ctor";
            }
        }
    };

    // Start recursive scan from the program's main statements.
    scan(prog.main);

    // Synthesize interface registration, binding thunks, and a module init.

    // 2) Interface registration thunks
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

    // 3) Module init that calls iface reg thunks, then bind thunks.
    const std::string initName = mangleOopModuleInit();
    Function &initF = builder->startFunction(initName, Type(Type::Kind::Void), {});
    context().setFunction(&initF);
    context().setNextTemp(initF.valueNames.size());
    // Create entry block for module init body
    builder->addBlock(initF, "entry");
    context().setCurrent(&initF.blocks.front());
    initF.blocks.front().terminated = false;

    // Register each class with its qualified name so Object.ToString works
    for (const auto &entry : oopIndex_.classes())
    {
        const ClassInfo &ci = entry.second;
        auto itLayout = classLayouts_.find(ci.name);
        if (itLayout == classLayouts_.end())
            continue;
        const long long typeId = (long long)itLayout->second.classId;
        // Allocate a minimal vtable (8 bytes) for the class
        Value vtablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(8LL)});
        // Register the class with rt_register_class_direct(typeId, vtable, qname, 0)
        std::string qnameLabel = getStringLabel(ci.qualifiedName);
        emitCall(
            "rt_register_class_direct",
            {Value::constInt(typeId), vtablePtr, emitConstStr(qnameLabel), Value::constInt(0LL)});
    }

    for (const auto &fn : regThunks)
        emitCall(fn, {});
    for (const auto &fn : bindThunks)
        emitCall(fn, {});
    // Call per-class static constructors in class declaration order
    for (const auto &entry : oopIndex_.classes())
    {
        const ClassInfo &ci = entry.second;
        if (ci.hasStaticCtor)
        {
            const std::string cctorName = mangleClassCtor(ci.qualifiedName) + "$static";
            emitCall(cctorName, {});
        }
    }
    emitRetVoid();

    // Call module init at the start of main by emitting a call in program emission.
    // Note: Program emission will run after this; ensure ProgramLowering invokes this init.
}

} // namespace il::frontends::basic
