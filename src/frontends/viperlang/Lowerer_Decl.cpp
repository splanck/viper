//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Decl.cpp
/// @brief Declaration lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
{

using namespace runtime;

//=============================================================================
// Declaration Lowering
//=============================================================================

void Lowerer::lowerDecl(Decl *decl)
{
    if (!decl)
        return;

    switch (decl->kind)
    {
        case DeclKind::Function:
            lowerFunctionDecl(*static_cast<FunctionDecl *>(decl));
            break;
        case DeclKind::Value:
            lowerValueDecl(*static_cast<ValueDecl *>(decl));
            break;
        case DeclKind::Entity:
            lowerEntityDecl(*static_cast<EntityDecl *>(decl));
            break;
        case DeclKind::Interface:
            lowerInterfaceDecl(*static_cast<InterfaceDecl *>(decl));
            break;
        case DeclKind::GlobalVar:
            lowerGlobalVarDecl(*static_cast<GlobalVarDecl *>(decl));
            break;
        case DeclKind::Namespace:
            lowerNamespaceDecl(*static_cast<NamespaceDecl *>(decl));
            break;
        default:
            break;
    }
}

std::string Lowerer::qualifyName(const std::string &name) const
{
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

void Lowerer::lowerNamespaceDecl(NamespaceDecl &decl)
{
    // Save current namespace prefix
    std::string savedPrefix = namespacePrefix_;

    // Compute new prefix
    if (namespacePrefix_.empty())
        namespacePrefix_ = decl.name;
    else
        namespacePrefix_ = namespacePrefix_ + "." + decl.name;

    // Lower all declarations inside the namespace
    for (auto &innerDecl : decl.declarations)
    {
        lowerDecl(innerDecl.get());
    }

    // Restore previous prefix
    namespacePrefix_ = savedPrefix;
}

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

void Lowerer::lowerFunctionDecl(FunctionDecl &decl)
{
    // Determine return type
    TypeRef returnType =
        decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list
    std::vector<il::core::Param> params;
    params.reserve(decl.params.size());
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Use qualified name for functions inside namespaces
    std::string qualifiedName = qualifyName(decl.name);
    std::string mangledName = mangleFunctionName(qualifiedName);

    // Track this function as defined in this module
    definedFunctions_.insert(mangledName);

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    currentReturnType_ = returnType;
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();

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
            decl.params[i].type ? sema_.resolveType(decl.params[i].type.get()) : types::unknown();
        Type ilParamType = mapType(paramType);

        // Create slot and store the parameter value
        createSlot(decl.params[i].name, ilParamType);
        storeToSlot(decl.params[i].name, Value::temp(blockParams[i].id), ilParamType);
        localTypes_[decl.params[i].name] = paramType;
    }

    // Emit global variable initializations at start of main()
    if (decl.name == "main" && !globalInitializers_.empty())
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

void Lowerer::lowerValueDecl(ValueDecl &decl)
{
    // Use qualified name for value types inside namespaces
    std::string qualifiedName = qualifyName(decl.name);

    // Compute field layout
    ValueTypeInfo info;
    info.name = qualifiedName;
    info.totalSize = 0;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            Type ilFieldType = mapType(fieldType);
            size_t alignment = getILTypeAlignment(ilFieldType);

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, alignment);
            layout.size = getILTypeSize(ilFieldType);

            // Add to lookup map before pushing to vector
            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    // Store the value type info and get reference for method lowering
    valueTypes_[qualifiedName] = std::move(info);
    const ValueTypeInfo &storedInfo = valueTypes_[qualifiedName];

    // Lower all methods using qualified type name
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, qualifiedName, false);
    }
}

void Lowerer::lowerEntityDecl(EntityDecl &decl)
{
    // Compute field layout (entity fields start after object header + vtable ptr)
    // Use qualified name for entities inside namespaces
    std::string qualifiedName = qualifyName(decl.name);

    EntityTypeInfo info;
    info.name = qualifiedName;
    info.baseClass = decl.baseClass; // Store parent class for super calls (may need qualifying)
    info.totalSize = kEntityFieldsOffset; // Space for header + vtable ptr
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + qualifiedName;

    // BUG-VL-010 fix: Store implemented interfaces for interface method dispatch
    for (const auto &iface : decl.interfaces)
    {
        info.implementedInterfaces.insert(iface);
    }

    // BUG-VL-006 fix: Copy inherited fields from parent entity
    // BUG-VL-011 fix: Also copy vtable from parent and override methods
    if (!decl.baseClass.empty())
    {
        auto parentIt = entityTypes_.find(decl.baseClass);
        if (parentIt != entityTypes_.end())
        {
            const EntityTypeInfo &parent = parentIt->second;
            // Copy all parent fields to this entity (they keep the same offsets)
            for (const auto &parentField : parent.fields)
            {
                info.fieldIndex[parentField.name] = info.fields.size();
                info.fields.push_back(parentField);
            }
            // Start child fields after parent's fields
            info.totalSize = parent.totalSize;

            // Inherit parent's vtable
            info.vtable = parent.vtable;
            info.vtableIndex = parent.vtableIndex;
        }
    }

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            Type ilFieldType = mapType(fieldType);
            size_t alignment = getILTypeAlignment(ilFieldType);

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, alignment);
            layout.size = getILTypeSize(ilFieldType);

            // Add to lookup map before pushing to vector
            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);

            // Build vtable: check if method overrides parent or add new slot
            std::string methodQualName = qualifiedName + "." + method->name;
            auto vtableIt = info.vtableIndex.find(method->name);
            if (vtableIt != info.vtableIndex.end())
            {
                // Override parent method - update vtable entry
                info.vtable[vtableIt->second] = methodQualName;
            }
            else
            {
                // New method - add to vtable
                info.vtableIndex[method->name] = info.vtable.size();
                info.vtable.push_back(methodQualName);
            }
        }
    }

    // Store the entity type info and get reference for method lowering
    entityTypes_[qualifiedName] = std::move(info);
    EntityTypeInfo &storedInfo = entityTypes_[qualifiedName];

    // Lower all methods first (so they are defined before vtable references them)
    for (auto *method : storedInfo.methods)
    {
        lowerMethodDecl(*method, qualifiedName, true);
    }

    // Emit vtable global (array of function pointers)
    if (!storedInfo.vtable.empty())
    {
        emitVtable(storedInfo);
    }
}

void Lowerer::emitVtable(const EntityTypeInfo & /*info*/)
{
    // BUG-VL-011: Virtual dispatch is now handled via class_id-based dispatch
    // instead of vtable pointers. The vtable info is used at compile time
    // to generate dispatch code, not runtime vtable lookup.
    // This function is kept as a placeholder for future vtable-based dispatch.
}

void Lowerer::lowerInterfaceDecl(InterfaceDecl &decl)
{
    // Use qualified name for interfaces inside namespaces
    std::string qualifiedName = qualifyName(decl.name);

    // Store interface information for vtable dispatch
    InterfaceTypeInfo info;
    info.name = qualifiedName;

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    interfaceTypes_[qualifiedName] = std::move(info);

    // Note: Interface methods are not lowered directly since they're abstract.
    // The implementing entity's methods are called at runtime.
}

void Lowerer::lowerMethodDecl(MethodDecl &decl, const std::string &typeName, bool isEntity)
{
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
        auto it = valueTypes_.find(typeName);
        if (it == valueTypes_.end())
            return;
        currentValueType_ = &it->second;
        currentEntityType_ = nullptr;
    }

    // Determine return type
    TypeRef returnType =
        decl.returnType ? sema_.resolveType(decl.returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build parameter list: self (ptr) + declared params
    std::vector<il::core::Param> params;
    params.reserve(decl.params.size() + 1);
    params.push_back({"self", Type(Type::Kind::Ptr)});

    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Mangle method name: TypeName.methodName
    std::string mangledName = typeName + "." + decl.name;

    // Create function
    currentFunc_ = &builder_->startFunction(mangledName, ilReturnType, params);
    blockMgr_.bind(builder_.get(), currentFunc_);
    locals_.clear();
    slots_.clear();

    // Create entry block with the function's params as block params
    // (required for proper VM argument passing)
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Define parameters using slot-based storage for cross-block SSA correctness
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    if (!blockParams.empty())
    {
        // 'self' is first block param - store in slot
        createSlot("self", Type(Type::Kind::Ptr));
        storeToSlot("self", Value::temp(blockParams[0].id), Type(Type::Kind::Ptr));
    }

    // Define other parameters using slot-based storage
    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        // Block param i+1 corresponds to method param i (after self)
        if (i + 1 < blockParams.size())
        {
            TypeRef paramType = decl.params[i].type ? sema_.resolveType(decl.params[i].type.get())
                                                    : types::unknown();
            Type ilParamType = mapType(paramType);

            createSlot(decl.params[i].name, ilParamType);
            storeToSlot(decl.params[i].name, Value::temp(blockParams[i + 1].id), ilParamType);
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
    currentValueType_ = nullptr;
    currentEntityType_ = nullptr;
}


} // namespace il::frontends::viperlang
