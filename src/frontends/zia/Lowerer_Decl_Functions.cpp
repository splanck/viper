//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Decl_Functions.cpp
// Purpose: Function and method declaration lowering for the Zia IL lowerer —
//          top-level functions, entity/value methods, properties, destructors,
//          global variable declarations, and generic function instantiation.
// Key invariants:
//   - Functions are created via builder_->startFunction and terminated with ret/retVoid
//   - Parameters are stored in slots for cross-block SSA correctness
//   - currentEntityType_/currentValueType_ are set/cleared around method lowering
// Ownership/Lifetime:
//   - Lowerer owns IL builder; function pointers are stable within a lowering session
// Links: src/frontends/zia/Lowerer.hpp, src/frontends/zia/Lowerer_Decl.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

#include "il/core/Linkage.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Global Variable Declaration Lowering
//=============================================================================

std::string Lowerer::getModvarAddrHelper(Type::Kind kind)
{
    switch (kind)
    {
        case Type::Kind::I64:
            return "rt_modvar_addr_i64";
        case Type::Kind::F64:
            return "rt_modvar_addr_f64";
        case Type::Kind::I1:
            return "rt_modvar_addr_i1";
        case Type::Kind::Str:
            return "rt_modvar_addr_str";
        case Type::Kind::Ptr:
        default:
            return "rt_modvar_addr_ptr";
    }
}

Lowerer::Value Lowerer::getGlobalVarAddr(const std::string &name, TypeRef type)
{
    std::string globalName = getStringGlobal(name);
    Value nameStr = emitConstStr(globalName);

    Type ilType = mapType(type);
    std::string helper = getModvarAddrHelper(ilType.kind);
    usedExterns_.insert(helper);

    Value addr = emitCallRet(Type(Type::Kind::Ptr), helper, {nameStr});
    return addr;
}

void Lowerer::lowerGlobalVarDecl(GlobalVarDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Use qualified name for globals inside namespaces
    std::string qualifiedName = qualifyName(decl.name);

    // Resolve the type
    TypeRef type = decl.type ? sema_.resolveType(decl.type.get()) : nullptr;
    if (!type && decl.initializer)
    {
        type = sema_.typeOf(decl.initializer.get());
    }

    // Try to inline literal initializers as constants (only for final declarations)
    // Mutable variables must use runtime storage even with literal initializers
    if (decl.isFinal && decl.initializer)
    {
        Expr *init = decl.initializer.get();
        bool inlinedAsConstant = false;

        // Handle integer literals
        if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
        {
            globalConstants_[qualifiedName] = Value::constInt(intLit->value);
            inlinedAsConstant = true;
        }
        // Handle number (float) literals
        else if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
        {
            globalConstants_[qualifiedName] = Value::constFloat(numLit->value);
            inlinedAsConstant = true;
        }
        // Handle boolean literals
        else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
        {
            globalConstants_[qualifiedName] = Value::constBool(boolLit->value);
            inlinedAsConstant = true;
        }
        // Handle string literals
        else if (auto *strLit = dynamic_cast<StringLiteralExpr *>(init))
        {
            std::string label = stringTable_.intern(strLit->value);
            globalConstants_[qualifiedName] = Value::constStr(label);
            inlinedAsConstant = true;
        }
        // Fold constant-expression initializers (e.g. `0 - 2147483647`, `-1`,
        // `2 * 1024`) that are not direct literals (BUG-FE-011).
        else if (auto folded = tryFoldNumericConstant(init))
        {
            globalConstants_[qualifiedName] = *folded;
            inlinedAsConstant = true;
        }

        if (inlinedAsConstant)
        {
            return;
        }
    }

    // For mutable variables, register for runtime storage
    if (!decl.isFinal && type)
    {
        globalVariables_[qualifiedName] = type;

        // Store literal initializer values for module init
        if (decl.initializer)
        {
            Expr *init = decl.initializer.get();

            // Handle integer literals
            if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
            {
                globalInitializers_[qualifiedName] = Value::constInt(intLit->value);
            }
            // Handle number (float) literals
            else if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
            {
                globalInitializers_[qualifiedName] = Value::constFloat(numLit->value);
            }
            // Handle boolean literals
            else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
            {
                globalInitializers_[qualifiedName] = Value::constBool(boolLit->value);
            }
            // Handle string literals
            else if (auto *strLit = dynamic_cast<StringLiteralExpr *>(init))
            {
                std::string label = stringTable_.intern(strLit->value);
                globalInitializers_[qualifiedName] = Value::constStr(label);
            }
        }
    }
}

//=============================================================================
// Function Declaration Lowering
//=============================================================================

void Lowerer::lowerFunctionDecl(FunctionDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Skip generic functions - they will be instantiated when called
    if (!decl.genericParams.empty())
        return;

    // Determine return type from sema's resolved declaration signature.
    TypeRef funcType = sema_.getFunctionType(&decl);
    if (!funcType)
    {
        std::vector<TypeRef> fallbackParamTypes;
        fallbackParamTypes.reserve(decl.params.size());
        for (const auto &param : decl.params)
            fallbackParamTypes.push_back(param.type ? sema_.resolveType(param.type.get())
                                                    : types::unknown());
        TypeRef fallbackReturnType =
            decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
        funcType = types::function(fallbackParamTypes, fallbackReturnType);
    }
    TypeRef returnType =
        funcType && funcType->kind == TypeKindSem::Function ? funcType->returnType()
                                                            : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list
    std::vector<il::core::Param> params;
    params.reserve(decl.params.size());
    const auto cachedParamTypes = funcType && funcType->kind == TypeKindSem::Function
                                      ? funcType->paramTypes()
                                      : std::vector<TypeRef>{};
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        TypeRef paramType =
            i < cachedParamTypes.size()
                ? cachedParamTypes[i]
                : (decl.params[i].type ? sema_.resolveType(decl.params[i].type.get())
                                       : types::unknown());
        params.push_back({decl.params[i].name, mapType(paramType)});
    }

    // Use qualified name for functions inside namespaces
    std::string qualifiedName = qualifyName(decl.name);
    std::string mangledName = sema_.loweredFunctionName(&decl);
    if (mangledName.empty())
        mangledName = mangleFunctionName(qualifiedName);

    TypeRef declaredReturnType =
        decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();

    if (decl.isAsync)
    {
        auto resetLoweringState = [&]()
        {
            blockMgr_.reset(nullptr);
            locals_.clear();
            slots_.clear();
            localTypes_.clear();
            deferredTemps_.clear();
            asyncOwnedValues_.clear();
            currentAsyncWorker_ = false;
            currentFunc_ = nullptr;
            currentReturnType_ = nullptr;
        };

        auto emitAsyncImplicitReturn = [&](TypeRef payloadType)
        {
            if (isTerminated())
                return;

            if (!payloadType || payloadType->kind == TypeKindSem::Void)
            {
                for (const auto &owned : asyncOwnedValues_)
                    emitManagedRelease(owned, /*isString=*/false);
                asyncOwnedValues_.clear();
                releaseDeferredTemps();
                emitRet(Value::null());
                return;
            }

            Type payloadIlType = mapType(payloadType);
            Value defaultValue;
            switch (payloadIlType.kind)
            {
                case Type::Kind::I1:
                    defaultValue = Value::constBool(false);
                    break;
                case Type::Kind::I64:
                case Type::Kind::I16:
                case Type::Kind::I32:
                    defaultValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    defaultValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    defaultValue = emitConstStr("");
                    break;
                case Type::Kind::Ptr:
                    defaultValue = Value::null();
                    break;
                default:
                    defaultValue = Value::constInt(0);
                    break;
            }

            Value futureValue = defaultValue;
            if (payloadType->kind == TypeKindSem::Value || payloadIlType.kind != Type::Kind::Ptr)
                futureValue = emitBoxValue(defaultValue, payloadIlType, payloadType);
            else
                emitCall("rt_obj_retain_maybe", {futureValue});

            for (const auto &owned : asyncOwnedValues_)
                emitManagedRelease(owned, /*isString=*/false);
            asyncOwnedValues_.clear();
            releaseDeferredTemps();
            emitRet(futureValue);
        };

        const std::string workerName = mangledName + "__async_worker";
        definedFunctions_.insert(workerName);

        // Emit the async worker trampoline: ptr(ptr env)
        currentFunc_ =
            &builder_->startFunction(workerName, Type(Type::Kind::Ptr), {{"__env", Type(Type::Kind::Ptr)}});
        currentReturnType_ = declaredReturnType;
        currentAsyncWorker_ = true;
        blockMgr_.bind(builder_.get(), currentFunc_);
        locals_.clear();
        slots_.clear();
        localTypes_.clear();
        deferredTemps_.clear();
        asyncOwnedValues_.clear();

        builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
        size_t workerEntryIdx = currentFunc_->blocks.size() - 1;
        setBlock(workerEntryIdx);

        const auto &workerParams = currentFunc_->blocks[workerEntryIdx].params;
        Value envPtr = Value::temp(workerParams[0].id);

        for (size_t i = 0; i < decl.params.size(); ++i)
        {
            TypeRef paramType =
                i < cachedParamTypes.size()
                    ? cachedParamTypes[i]
                    : (decl.params[i].type ? sema_.resolveType(decl.params[i].type.get())
                                           : types::unknown());
            Type ilParamType = mapType(paramType);

            Value argAddr = emitGEP(envPtr, static_cast<int64_t>(i * sizeof(void *)));
            Value ownedArg = emitLoad(argAddr, Type(Type::Kind::Ptr));
            asyncOwnedValues_.push_back(ownedArg);

            Value unpacked = ownedArg;
            if (paramType && (paramType->kind == TypeKindSem::Value || ilParamType.kind != Type::Kind::Ptr))
                unpacked = emitUnboxValue(ownedArg, ilParamType, paramType).value;

            createSlot(decl.params[i].name, ilParamType);
            storeToSlot(decl.params[i].name, unpacked, ilParamType);
            localTypes_[decl.params[i].name] = paramType;
        }

        emitManagedRelease(envPtr, /*isString=*/false);

        if (decl.body)
            lowerStmt(decl.body.get());
        emitAsyncImplicitReturn(declaredReturnType);
        resetLoweringState();

        // Emit the public wrapper that packages arguments and starts the async task.
        definedFunctions_.insert(mangledName);
        currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
        currentReturnType_ = returnType;
        if (decl.visibility == Visibility::Public)
            currentFunc_->linkage = il::core::Linkage::Export;

        blockMgr_.bind(builder_.get(), currentFunc_);
        locals_.clear();
        slots_.clear();
        localTypes_.clear();
        deferredTemps_.clear();

        builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
        size_t wrapperEntryIdx = currentFunc_->blocks.size() - 1;
        setBlock(wrapperEntryIdx);
        const auto &wrapperParams = currentFunc_->blocks[wrapperEntryIdx].params;

        Value envSize = Value::constInt(static_cast<int64_t>(decl.params.size() * sizeof(void *)));
        Value wrapperEnv = emitCallRet(Type(Type::Kind::Ptr),
                                       "rt_obj_new_i64",
                                       {Value::constInt(0), envSize});

        for (size_t i = 0; i < decl.params.size() && i < wrapperParams.size(); ++i)
        {
            TypeRef paramType =
                i < cachedParamTypes.size()
                    ? cachedParamTypes[i]
                    : (decl.params[i].type ? sema_.resolveType(decl.params[i].type.get())
                                           : types::unknown());
            Type ilParamType = mapType(paramType);
            Value paramValue = Value::temp(wrapperParams[i].id);

            Value storedValue = paramValue;
            if (paramType && (paramType->kind == TypeKindSem::Value || ilParamType.kind != Type::Kind::Ptr))
            {
                storedValue = emitBoxValue(paramValue, ilParamType, paramType);
            }
            else
            {
                emitCall("rt_obj_retain_maybe", {storedValue});
            }

            Value argAddr = emitGEP(wrapperEnv, static_cast<int64_t>(i * sizeof(void *)));
            emitStore(argAddr, storedValue, Type(Type::Kind::Ptr));
        }

        Value future = emitCallRet(Type(Type::Kind::Ptr), kAsyncRun, {Value::global(workerName), wrapperEnv});
        emitRet(future);
        resetLoweringState();
        return;
    }

    // Track this function as defined in this module
    definedFunctions_.insert(mangledName);

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    currentReturnType_ = returnType;

    // Set IL linkage from AST visibility/foreign flag.
    if (decl.isForeign)
        currentFunc_->linkage = il::core::Linkage::Import;
    else if (decl.visibility == Visibility::Public)
        currentFunc_->linkage = il::core::Linkage::Export;

    // Foreign (import) functions have no body -- just a declaration.
    if (decl.isForeign)
    {
        currentFunc_ = nullptr;
        currentReturnType_ = nullptr;
        return;
    }

    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage for cross-block SSA correctness
    // This ensures parameters are accessible in all basic blocks (if, while, guard, etc.)
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    for (size_t i = 0; i < decl.params.size() && i < blockParams.size(); ++i)
    {
        TypeRef paramType =
            i < cachedParamTypes.size()
                ? cachedParamTypes[i]
                : (decl.params[i].type ? sema_.resolveType(decl.params[i].type.get())
                                       : types::unknown());
        Type ilParamType = mapType(paramType);

        // Create slot and store the parameter value
        createSlot(decl.params[i].name, ilParamType);
        storeToSlot(decl.params[i].name, Value::temp(blockParams[i].id), ilParamType);
        localTypes_[decl.params[i].name] = paramType;
    }

    // Emit interface itable init call at start of start() (before any user code)
    // The __zia_iface_init function is emitted later by emitItableInit(); if no
    // interfaces have implementors, it emits a trivial ret-void stub.
    if (decl.name == "start" && !interfaceTypes_.empty())
    {
        emitCall("__zia_iface_init", {});
    }

    // Emit global variable initializations at start of start() (Zia entry point)
    if (decl.name == "start" && !globalInitializers_.empty())
    {
        for (const auto &[name, initValue] : globalInitializers_)
        {
            auto typeIt = globalVariables_.find(name);
            if (typeIt == globalVariables_.end())
                continue;

            TypeRef varType = typeIt->second;
            Type ilType = mapType(varType);

            // Get address of global variable
            Value addr = getGlobalVarAddr(name, varType);

            // Handle string values specially - need to emit conststr to get address
            Value valueToStore = initValue;
            if (ilType.kind == Type::Kind::Str && initValue.kind == Value::Kind::ConstStr)
            {
                valueToStore = emitConstStr(initValue.str);
            }

            // Store the initial value
            emitStore(addr, valueToStore, ilType);
        }
    }

    // Lower function body
    if (decl.body)
    {
        lowerStmt(decl.body.get());
    }

    // Add implicit return if needed (Bug #5 fix: use correct default value for each type)
    if (!isTerminated())
    {
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitRetVoid();
        }
        else
        {
            // Emit correct default value based on return type
            Value defaultValue;
            switch (ilReturnType.kind)
            {
                case Type::Kind::I1:
                    defaultValue = Value::constBool(false);
                    break;
                case Type::Kind::I64:
                case Type::Kind::I16:
                case Type::Kind::I32:
                    defaultValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    defaultValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    defaultValue = Value::constStr("");
                    break;
                case Type::Kind::Ptr:
                    defaultValue = Value::null();
                    break;
                default:
                    defaultValue = Value::constInt(0);
                    break;
            }
            emitRet(defaultValue);
        }
    }

    currentFunc_ = nullptr;
    currentReturnType_ = nullptr;
}

void Lowerer::lowerGenericFunctionInstantiation(const std::string &mangledName, FunctionDecl *decl)
{
    // Push substitution context so type parameters resolve correctly
    bool pushedContext = sema_.pushSubstitutionContext(mangledName);

    // Get the instantiated function type from Sema
    // The types should resolve with substitutions active
    TypeRef returnType =
        decl->returnType ? sema_.resolveType(decl->returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list
    std::vector<il::core::Param> params;
    params.reserve(decl->params.size());
    for (const auto &param : decl->params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Track this function as defined in this module
    definedFunctions_.insert(mangledName);

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    currentReturnType_ = returnType;
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    // Create entry block with the function's params as block params
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    for (size_t i = 0; i < decl->params.size() && i < blockParams.size(); ++i)
    {
        TypeRef paramType =
            decl->params[i].type ? sema_.resolveType(decl->params[i].type.get()) : types::unknown();
        Type ilParamType = mapType(paramType);

        // Create slot and store the parameter value
        createSlot(decl->params[i].name, ilParamType);
        storeToSlot(decl->params[i].name, Value::temp(blockParams[i].id), ilParamType);
        localTypes_[decl->params[i].name] = paramType;
    }

    // Lower function body
    if (decl->body)
    {
        lowerStmt(decl->body.get());
    }

    // Add implicit return if needed
    if (!isTerminated())
    {
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitRetVoid();
        }
        else
        {
            Value defaultValue;
            switch (ilReturnType.kind)
            {
                case Type::Kind::I1:
                    defaultValue = Value::constBool(false);
                    break;
                case Type::Kind::I64:
                case Type::Kind::I16:
                case Type::Kind::I32:
                    defaultValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    defaultValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    defaultValue = emitConstStr("");
                    break;
                case Type::Kind::Ptr:
                    defaultValue = Value::null();
                    break;
                default:
                    defaultValue = Value::constInt(0);
                    break;
            }
            emitRet(defaultValue);
        }
    }

    // Pop substitution context
    if (pushedContext)
    {
        sema_.popTypeParams();
    }

    currentFunc_ = nullptr;
    currentReturnType_ = nullptr;
}

//=============================================================================
// Value and Entity Declaration Lowering
//=============================================================================

void Lowerer::lowerValueDecl(ValueDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Skip uninstantiated generic types - they're lowered during instantiation
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = qualifyName(decl.name);

    // BUG-FE-006 fix: Layout may already be registered by the pre-pass.
    if (valueTypes_.find(qualifiedName) == valueTypes_.end())
    {
        registerValueLayout(decl);
    }

    const ValueTypeInfo &storedInfo = valueTypes_[qualifiedName];

    // Lower all methods using qualified type name
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, qualifiedName, false);
    }
}

void Lowerer::lowerEntityDecl(EntityDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Skip uninstantiated generic types - they're lowered during instantiation
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = qualifyName(decl.name);

    // BUG-FE-006 fix: Layout may already be registered by the pre-pass.
    // If not registered yet (e.g., in pending generic instantiation), do it now.
    auto it = entityTypes_.find(qualifiedName);
    if (it == entityTypes_.end())
    {
        registerEntityLayout(decl);
    }

    EntityTypeInfo &storedInfo = entityTypes_[qualifiedName];

    // Register module-level globals for static fields
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            if (field->isStatic)
            {
                TypeRef fieldType =
                    field->type ? sema_.resolveType(field->type.get()) : types::unknown();
                std::string globalName = qualifiedName + "." + field->name;
                globalVariables_[globalName] = fieldType;

                // Store literal initializer if present
                if (field->initializer)
                {
                    Expr *init = field->initializer.get();
                    if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
                        globalInitializers_[globalName] = Value::constInt(intLit->value);
                    else if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
                        globalInitializers_[globalName] = Value::constFloat(numLit->value);
                    else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
                        globalInitializers_[globalName] = Value::constBool(boolLit->value);
                    else if (auto *strLit = dynamic_cast<StringLiteralExpr *>(init))
                        globalInitializers_[globalName] = Value::constStr(strLit->value);
                }
            }
        }
    }

    // Lower all methods (so they are defined before vtable references them)
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, qualifiedName, true);
    }

    // Lower property declarations as synthesized get_/set_ methods
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Property)
        {
            auto *prop = static_cast<PropertyDecl *>(member.get());
            lowerPropertyDecl(*prop, qualifiedName, true);
        }
    }

    // Lower destructor declaration (at most one per entity)
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Destructor)
        {
            auto *dtor = static_cast<DestructorDecl *>(member.get());
            lowerDestructorDecl(*dtor, qualifiedName);
            break; // at most one destructor
        }
    }

    // Emit vtable global (array of function pointers)
    if (!storedInfo.vtable.empty())
    {
        emitVtable(storedInfo);
    }
}

void Lowerer::lowerInterfaceDecl(InterfaceDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Use qualified name for interfaces inside namespaces
    std::string qualifiedName = qualifyName(decl.name);

    // Store interface information for itable dispatch
    InterfaceTypeInfo info;
    info.name = qualifiedName;
    info.ifaceId = nextIfaceId_++;

    size_t slotIdx = 0;
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
            info.slotIndex[sema_.methodSlotKey(qualifiedName, method)] = slotIdx++;
        }
    }

    interfaceTypes_[qualifiedName] = std::move(info);

    // Note: Interface methods are not lowered directly since they're abstract.
    // The implementing entity's methods are called at runtime.
}

//=============================================================================
// Method Declaration Lowering
//=============================================================================

void Lowerer::lowerMethodDecl(MethodDecl &decl, const std::string &typeName, bool isEntity)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Find the type info
    if (isEntity)
    {
        auto it = entityTypes_.find(typeName);
        if (it == entityTypes_.end())
            return;
        currentEntityType_ = &it->second;
        currentValueType_ = nullptr;
    }
    else
    {
        const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(typeName);
        if (!valueInfo)
            return;
        currentValueType_ = valueInfo;
        currentEntityType_ = nullptr;
    }

    // Look up cached method type - this has already-substituted types for generics
    TypeRef methodType = sema_.getMethodType(typeName, &decl);
    if (!methodType)
        methodType = sema_.getMethodType(typeName, decl.name);
    std::vector<TypeRef> cachedParamTypes;
    TypeRef returnType = types::voidType();
    if (methodType && methodType->kind == TypeKindSem::Function)
    {
        cachedParamTypes = methodType->paramTypes();
        returnType = methodType->returnType();
    }
    else
    {
        // Fallback to direct resolution for non-generic types
        returnType = decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
        for (const auto &param : decl.params)
        {
            TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
            cachedParamTypes.push_back(paramType);
        }
    }
    Type ilReturnType = mapType(returnType);

    // Build parameter list: self (ptr) + declared params (unless static)
    std::vector<il::core::Param> params;
    params.reserve(decl.params.size() + (decl.isStatic ? 0 : 1));
    if (!decl.isStatic)
    {
        params.push_back({"self", Type(Type::Kind::Ptr)});
    }

    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Use cached param type if available, otherwise resolve from AST
        TypeRef paramType = (i < cachedParamTypes.size()) ? cachedParamTypes[i] : types::unknown();
        params.push_back({decl.params[i].name, mapType(paramType)});
    }

    // Mangle method name: TypeName.methodName
    std::string mangledName = sema_.loweredMethodName(typeName, &decl);
    if (mangledName.empty())
        mangledName = typeName + "." + decl.name;

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    currentReturnType_ = returnType;
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage for cross-block SSA correctness
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    if (!decl.isStatic && !blockParams.empty())
    {
        // 'self' is first block param - store in slot
        createSlot("self", Type(Type::Kind::Ptr));
        storeToSlot("self", Value::temp(blockParams[0].id), Type(Type::Kind::Ptr));
    }

    // Define other parameters using slot-based storage
    size_t paramOffset = decl.isStatic ? 0 : 1;
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Block param i+offset corresponds to method param i (after self if not static)
        if (i + paramOffset < blockParams.size())
        {
            // Use cached param type if available
            TypeRef paramType =
                (i < cachedParamTypes.size()) ? cachedParamTypes[i] : types::unknown();
            Type ilParamType = mapType(paramType);

            createSlot(decl.params[i].name, ilParamType);
            storeToSlot(
                decl.params[i].name, Value::temp(blockParams[i + paramOffset].id), ilParamType);
            // Store parameter type for expression lowering
            localTypes_[decl.params[i].name] = paramType;
        }
    }

    // Lower method body
    if (decl.body)
    {
        lowerStmt(decl.body.get());
    }

    // Add implicit return if needed (Bug #5 fix: use correct default value for each type)
    if (!isTerminated())
    {
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitRetVoid();
        }
        else
        {
            // Emit correct default value based on return type
            Value defaultValue;
            switch (ilReturnType.kind)
            {
                case Type::Kind::I1:
                    defaultValue = Value::constBool(false);
                    break;
                case Type::Kind::I64:
                case Type::Kind::I16:
                case Type::Kind::I32:
                    defaultValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    defaultValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    defaultValue = Value::constStr("");
                    break;
                case Type::Kind::Ptr:
                    defaultValue = Value::null();
                    break;
                default:
                    defaultValue = Value::constInt(0);
                    break;
            }
            emitRet(defaultValue);
        }
    }

    currentFunc_ = nullptr;
    currentReturnType_ = nullptr;
    currentValueType_ = nullptr;
    currentEntityType_ = nullptr;
}

//=============================================================================
// Property Declaration Lowering
//=============================================================================

void Lowerer::lowerPropertyDecl(PropertyDecl &decl, const std::string &typeName, bool isEntity)
{
    ZiaLocationScope locScope(*this, decl.loc);

    TypeRef propType = decl.type ? sema_.resolveType(decl.type.get()) : types::unknown();
    Type ilPropType = mapType(propType);

    // Set current entity/value type context
    if (isEntity)
    {
        auto it = entityTypes_.find(typeName);
        if (it == entityTypes_.end())
            return;
        currentEntityType_ = &it->second;
        currentValueType_ = nullptr;
    }

    // --- Synthesize getter: get_PropertyName(self: Ptr) -> Type ---
    {
        std::string getterName = typeName + ".get_" + decl.name;

        std::vector<il::core::Param> params;
        if (!decl.isStatic)
            params.push_back({"self", Type(Type::Kind::Ptr)});

        currentFunc_ = &builder_->startFunction(getterName, ilPropType, params);
        currentReturnType_ = propType;
        blockMgr_.bind(builder_.get(), currentFunc_);
        locals_.clear();
        slots_.clear();
        localTypes_.clear();
        deferredTemps_.clear();

        builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
        size_t entryIdx = currentFunc_->blocks.size() - 1;
        setBlock(entryIdx);

        const auto &blockParams = currentFunc_->blocks[entryIdx].params;
        if (!decl.isStatic && !blockParams.empty())
        {
            createSlot("self", Type(Type::Kind::Ptr));
            storeToSlot("self", Value::temp(blockParams[0].id), Type(Type::Kind::Ptr));
        }

        // Lower getter body
        if (decl.getterBody)
            lowerStmt(decl.getterBody.get());

        // Add implicit return if needed
        if (!isTerminated())
        {
            Value defaultValue;
            switch (ilPropType.kind)
            {
                case Type::Kind::I1:
                    defaultValue = Value::constBool(false);
                    break;
                case Type::Kind::I64:
                case Type::Kind::I16:
                case Type::Kind::I32:
                    defaultValue = Value::constInt(0);
                    break;
                case Type::Kind::F64:
                    defaultValue = Value::constFloat(0.0);
                    break;
                case Type::Kind::Str:
                    defaultValue = Value::constStr("");
                    break;
                case Type::Kind::Ptr:
                    defaultValue = Value::null();
                    break;
                default:
                    defaultValue = Value::constInt(0);
                    break;
            }
            emitRet(defaultValue);
        }

        definedFunctions_.insert(getterName);
        currentFunc_ = nullptr;
        currentReturnType_ = nullptr;
    }

    // --- Synthesize setter: set_PropertyName(self: Ptr, value: Type) -> Void ---
    if (decl.setterBody)
    {
        std::string setterName = typeName + ".set_" + decl.name;

        std::vector<il::core::Param> params;
        if (!decl.isStatic)
            params.push_back({"self", Type(Type::Kind::Ptr)});
        params.push_back({decl.setterParam, ilPropType});

        currentFunc_ = &builder_->startFunction(setterName, Type(Type::Kind::Void), params);
        currentReturnType_ = types::voidType();
        blockMgr_.bind(builder_.get(), currentFunc_);
        locals_.clear();
        slots_.clear();
        localTypes_.clear();
        deferredTemps_.clear();

        builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
        size_t entryIdx = currentFunc_->blocks.size() - 1;
        setBlock(entryIdx);

        const auto &blockParams = currentFunc_->blocks[entryIdx].params;
        size_t paramIdx = 0;
        if (!decl.isStatic && paramIdx < blockParams.size())
        {
            createSlot("self", Type(Type::Kind::Ptr));
            storeToSlot("self", Value::temp(blockParams[paramIdx].id), Type(Type::Kind::Ptr));
            ++paramIdx;
        }
        if (paramIdx < blockParams.size())
        {
            createSlot(decl.setterParam, ilPropType);
            storeToSlot(decl.setterParam, Value::temp(blockParams[paramIdx].id), ilPropType);
            localTypes_[decl.setterParam] = propType;
        }

        // Lower setter body
        lowerStmt(decl.setterBody.get());

        // Add implicit return void
        if (!isTerminated())
        {
            emitRetVoid();
        }

        definedFunctions_.insert(setterName);
        currentFunc_ = nullptr;
        currentReturnType_ = nullptr;
    }

    currentEntityType_ = nullptr;
    currentValueType_ = nullptr;
}

//=============================================================================
// Destructor Declaration Lowering
//=============================================================================

void Lowerer::lowerDestructorDecl(DestructorDecl &decl, const std::string &typeName)
{
    ZiaLocationScope locScope(*this, decl.loc);

    auto it = entityTypes_.find(typeName);
    if (it == entityTypes_.end())
        return;
    currentEntityType_ = &it->second;
    currentValueType_ = nullptr;

    // Emit __dtor function: TypeName.__dtor(self: Ptr) -> Void
    std::string dtorName = typeName + ".__dtor";

    std::vector<il::core::Param> params;
    params.push_back({"self", Type(Type::Kind::Ptr)});

    currentFunc_ = &builder_->startFunction(dtorName, Type(Type::Kind::Void), params);
    currentReturnType_ = types::voidType();
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    if (!blockParams.empty())
    {
        createSlot("self", Type(Type::Kind::Ptr));
        storeToSlot("self", Value::temp(blockParams[0].id), Type(Type::Kind::Ptr));
    }

    // Lower user-defined destructor body
    if (decl.body)
        lowerStmt(decl.body.get());

    // Release reference-typed fields (Str and Ptr)
    if (!isTerminated())
    {
        Value selfPtr = loadFromSlot("self", Type(Type::Kind::Ptr));
        const EntityTypeInfo &info = *currentEntityType_;

        for (const auto &field : info.fields)
        {
            Type ilFieldType = mapType(field.type);
            if (ilFieldType.kind == Type::Kind::Str)
            {
                Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field.offset));
                Value fieldValue = emitLoad(fieldAddr, Type(Type::Kind::Str));
                emitManagedRelease(fieldValue, /*isString=*/true);
            }
            else if (ilFieldType.kind == Type::Kind::Ptr)
            {
                Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field.offset));
                Value fieldValue = emitLoad(fieldAddr, Type(Type::Kind::Ptr));
                emitManagedRelease(fieldValue, /*isString=*/false);
            }
        }
    }

    // Emit return void
    if (!isTerminated())
        emitRetVoid();

    definedFunctions_.insert(dtorName);
    currentFunc_ = nullptr;
    currentReturnType_ = nullptr;
    currentEntityType_ = nullptr;
    currentValueType_ = nullptr;
}

} // namespace il::frontends::zia
