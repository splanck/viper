//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Decl.cpp
/// @brief Declaration lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

namespace il::frontends::zia
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

//=============================================================================
// Compile-Time Constant Folding Helper
//=============================================================================

/// @brief Try to evaluate an initializer expression to a compile-time constant.
/// @details Handles integer/float/bool literals, unary negation and bitwise NOT,
///          and binary arithmetic on integer or float literals. String literals
///          require the string-intern table and are handled at call sites.
///          Returns nullopt for any expression that cannot be evaluated at
///          compile time (e.g., function calls, identifier references).
/// @note Fixes BUG-FE-011: non-literal final constant initializers (such as
///       `final X = 0 - 2147483647`) were previously silently dropped, causing
///       all references to resolve to constInt(0).
static std::optional<il::core::Value> tryFoldNumericConstant(Expr *init)
{
    using Value = il::core::Value;

    if (!init)
        return std::nullopt;

    if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
        return Value::constInt(intLit->value);
    if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
        return Value::constFloat(numLit->value);
    if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
        return Value::constBool(boolLit->value);

    if (auto *unary = dynamic_cast<UnaryExpr *>(init))
    {
        auto inner = tryFoldNumericConstant(unary->operand.get());
        if (inner)
        {
            if (unary->op == UnaryOp::Neg)
            {
                if (inner->kind == Value::Kind::ConstInt)
                    return Value::constInt(-inner->i64);
                if (inner->kind == Value::Kind::ConstFloat)
                    return Value::constFloat(-inner->f64);
            }
            if (unary->op == UnaryOp::BitNot && inner->kind == Value::Kind::ConstInt)
                return Value::constInt(~inner->i64);
        }
    }

    if (auto *binary = dynamic_cast<BinaryExpr *>(init))
    {
        auto lv = tryFoldNumericConstant(binary->left.get());
        auto rv = tryFoldNumericConstant(binary->right.get());

        // Integer × integer
        if (lv && rv && lv->kind == Value::Kind::ConstInt && rv->kind == Value::Kind::ConstInt)
        {
            long long l = lv->i64, r = rv->i64;
            switch (binary->op)
            {
                case BinaryOp::Add:
                    return Value::constInt(l + r);
                case BinaryOp::Sub:
                    return Value::constInt(l - r);
                case BinaryOp::Mul:
                    return Value::constInt(l * r);
                case BinaryOp::BitAnd:
                    return Value::constInt(l & r);
                case BinaryOp::BitOr:
                    return Value::constInt(l | r);
                case BinaryOp::BitXor:
                    return Value::constInt(l ^ r);
                default:
                    break;
            }
        }

        // Float × float
        if (lv && rv && lv->kind == Value::Kind::ConstFloat && rv->kind == Value::Kind::ConstFloat)
        {
            double l = lv->f64, r = rv->f64;
            switch (binary->op)
            {
                case BinaryOp::Add:
                    return Value::constFloat(l + r);
                case BinaryOp::Sub:
                    return Value::constFloat(l - r);
                case BinaryOp::Mul:
                    return Value::constFloat(l * r);
                default:
                    break;
            }
        }
    }

    return std::nullopt;
}

//=============================================================================
// Final Constant Pre-Registration
//=============================================================================

void Lowerer::registerAllFinalConstants(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        if (decl->kind == DeclKind::GlobalVar)
        {
            auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
            if (gvar->isFinal && gvar->initializer)
            {
                std::string qualifiedName = qualifyName(gvar->name);
                // Skip if already registered
                if (globalConstants_.find(qualifiedName) != globalConstants_.end())
                    continue;

                Expr *init = gvar->initializer.get();

                if (auto *intLit = dynamic_cast<IntLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constInt(intLit->value);
                else if (auto *numLit = dynamic_cast<NumberLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constFloat(numLit->value);
                else if (auto *boolLit = dynamic_cast<BoolLiteralExpr *>(init))
                    globalConstants_[qualifiedName] = Value::constBool(boolLit->value);
                else if (auto *strLit = dynamic_cast<StringLiteralExpr *>(init))
                {
                    std::string label = stringTable_.intern(strLit->value);
                    globalConstants_[qualifiedName] = Value::constStr(label);
                }
                // Fold constant-expression initializers (e.g. `0 - 2147483647`,
                // `-1`, `2 * 1024`) that are not direct literals (BUG-FE-011).
                else if (auto folded = tryFoldNumericConstant(init))
                    globalConstants_[qualifiedName] = *folded;
            }
        }
        else if (decl->kind == DeclKind::Namespace)
        {
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerAllFinalConstants(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

//=============================================================================
// Type Layout Pre-Registration (BUG-FE-006 fix)
//=============================================================================

void Lowerer::registerAllTypeLayouts(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        if (decl->kind == DeclKind::Entity)
        {
            registerEntityLayout(*static_cast<EntityDecl *>(decl.get()));
        }
        else if (decl->kind == DeclKind::Value)
        {
            registerValueLayout(*static_cast<ValueDecl *>(decl.get()));
        }
        else if (decl->kind == DeclKind::Namespace)
        {
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerAllTypeLayouts(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

void Lowerer::registerEntityLayout(EntityDecl &decl)
{
    // Skip uninstantiated generic types
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = qualifyName(decl.name);

    // Skip if already registered
    if (entityTypes_.find(qualifiedName) != entityTypes_.end())
        return;

    EntityTypeInfo info;
    info.name = qualifiedName;
    info.baseClass = decl.baseClass;
    info.totalSize = kEntityFieldsOffset;
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + qualifiedName;

    for (const auto &iface : decl.interfaces)
    {
        info.implementedInterfaces.insert(iface);
    }

    // Copy inherited fields from parent entity
    if (!decl.baseClass.empty())
    {
        auto parentIt = entityTypes_.find(decl.baseClass);
        if (parentIt != entityTypes_.end())
        {
            const EntityTypeInfo &parent = parentIt->second;
            for (const auto &parentField : parent.fields)
            {
                info.fieldIndex[parentField.name] = info.fields.size();
                info.fields.push_back(parentField);
            }
            info.totalSize = parent.totalSize;
            info.vtable = parent.vtable;
            info.vtableIndex = parent.vtableIndex;
        }
    }

    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());

            // Static fields become module-level globals, not instance fields
            if (field->isStatic)
                continue;

            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray)
            {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            }
            else
            {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);

            // Build vtable (static methods don't go in vtable)
            if (!method->isStatic)
            {
                std::string methodQualName = qualifiedName + "." + method->name;
                auto vtableIt = info.vtableIndex.find(method->name);
                if (vtableIt != info.vtableIndex.end())
                {
                    info.vtable[vtableIt->second] = methodQualName;
                }
                else
                {
                    info.vtableIndex[method->name] = info.vtable.size();
                    info.vtable.push_back(methodQualName);
                }
            }
        }
        else if (member->kind == DeclKind::Property)
        {
            // Properties are synthesized into get_X/set_X methods during lowering
            auto *prop = static_cast<PropertyDecl *>(member.get());

            // Register getter as a method
            std::string getterName = "get_" + prop->name;
            info.propertyGetters.insert(getterName);

            // Register setter if present
            if (prop->setterBody)
            {
                std::string setterName = "set_" + prop->name;
                info.propertySetters.insert(setterName);
            }
        }
    }

    entityTypes_[qualifiedName] = std::move(info);
}

void Lowerer::registerValueLayout(ValueDecl &decl)
{
    // Skip uninstantiated generic types
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = qualifyName(decl.name);

    // Skip if already registered
    if (valueTypes_.find(qualifiedName) != valueTypes_.end())
        return;

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

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray)
            {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            }
            else
            {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

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

    valueTypes_[qualifiedName] = std::move(info);
}

void Lowerer::lowerNamespaceDecl(NamespaceDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

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

void Lowerer::lowerFunctionDecl(FunctionDecl &decl)
{
    ZiaLocationScope locScope(*this, decl.loc);

    // Skip generic functions - they will be instantiated when called
    if (!decl.genericParams.empty())
        return;

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
            decl.params[i].type ? sema_.resolveType(decl.params[i].type.get()) : types::unknown();
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

const ValueTypeInfo *Lowerer::getOrCreateValueTypeInfo(const std::string &typeName)
{
    // Check existing cache
    auto it = valueTypes_.find(typeName);
    if (it != valueTypes_.end())
    {
        return &it->second;
    }

    // Check if this is an instantiated generic
    if (!sema_.isInstantiatedGeneric(typeName))
    {
        return nullptr;
    }

    // Get the original generic declaration
    Decl *genericDecl = sema_.getGenericDeclForInstantiation(typeName);
    if (!genericDecl || genericDecl->kind != DeclKind::Value)
    {
        return nullptr;
    }

    auto *valueDecl = static_cast<ValueDecl *>(genericDecl);

    // Build ValueTypeInfo for the instantiated type
    ValueTypeInfo info;
    info.name = typeName;
    info.totalSize = 0;

    for (auto &member : valueDecl->members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            // Get the substituted field type from Sema
            TypeRef fieldType = sema_.getFieldType(typeName, field->name);
            if (!fieldType)
                fieldType = types::unknown();

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray)
            {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            }
            else
            {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

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

    // Store the value type info
    valueTypes_[typeName] = std::move(info);

    // Defer method lowering until after all declarations are processed
    // (we may be in the middle of lowering another function body)
    pendingValueInstantiations_.push_back(typeName);

    return &valueTypes_[typeName];
}

const EntityTypeInfo *Lowerer::getOrCreateEntityTypeInfo(const std::string &typeName)
{
    // Check existing cache
    auto it = entityTypes_.find(typeName);
    if (it != entityTypes_.end())
    {
        return &it->second;
    }

    // Check if this is an instantiated generic
    if (!sema_.isInstantiatedGeneric(typeName))
    {
        return nullptr;
    }

    // Get the original generic declaration
    Decl *genericDecl = sema_.getGenericDeclForInstantiation(typeName);
    if (!genericDecl || genericDecl->kind != DeclKind::Entity)
    {
        return nullptr;
    }

    auto *entityDecl = static_cast<EntityDecl *>(genericDecl);

    // Build EntityTypeInfo for the instantiated type
    EntityTypeInfo info;
    info.name = typeName;
    info.baseClass = entityDecl->baseClass;
    info.totalSize = kEntityFieldsOffset; // Space for header + vtable ptr
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + typeName;

    // Store implemented interfaces
    for (const auto &iface : entityDecl->interfaces)
    {
        info.implementedInterfaces.insert(iface);
    }

    // Handle inheritance (if base class exists, copy its fields)
    if (!entityDecl->baseClass.empty())
    {
        auto parentIt = entityTypes_.find(entityDecl->baseClass);
        if (parentIt != entityTypes_.end())
        {
            const EntityTypeInfo &parent = parentIt->second;
            for (const auto &parentField : parent.fields)
            {
                info.fieldIndex[parentField.name] = info.fields.size();
                info.fields.push_back(parentField);
            }
            info.totalSize = parent.totalSize;
            info.vtable = parent.vtable;
            info.vtableIndex = parent.vtableIndex;
        }
    }

    // Process members
    for (auto &member : entityDecl->members)
    {
        if (member->kind == DeclKind::Field)
        {
            auto *field = static_cast<FieldDecl *>(member.get());
            // Get the substituted field type from Sema
            TypeRef fieldType = sema_.getFieldType(typeName, field->name);
            if (!fieldType)
                fieldType = types::unknown();

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray)
            {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            }
            else
            {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        }
        else if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);

            // Build vtable
            std::string methodQualName = typeName + "." + method->name;
            auto vtableIt = info.vtableIndex.find(method->name);
            if (vtableIt != info.vtableIndex.end())
            {
                info.vtable[vtableIt->second] = methodQualName;
            }
            else
            {
                info.vtableIndex[method->name] = info.vtable.size();
                info.vtable.push_back(methodQualName);
            }
        }
    }

    // Store the entity type info
    entityTypes_[typeName] = std::move(info);

    // Defer method lowering until after all declarations are processed
    // (we may be in the middle of lowering another function body)
    pendingEntityInstantiations_.push_back(typeName);

    return &entityTypes_[typeName];
}

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

void Lowerer::emitVtable(const EntityTypeInfo & /*info*/)
{
    // BUG-VL-011: Virtual dispatch is now handled via class_id-based dispatch
    // instead of vtable pointers. The vtable info is used at compile time
    // to generate dispatch code, not runtime vtable lookup.
    // This function is kept as a placeholder for future vtable-based dispatch.
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
            info.slotIndex[method->name] = slotIdx++;
        }
    }

    interfaceTypes_[qualifiedName] = std::move(info);

    // Note: Interface methods are not lowered directly since they're abstract.
    // The implementing entity's methods are called at runtime.
}

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
    TypeRef methodType = sema_.getMethodType(typeName, decl.name);
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
    std::string mangledName = typeName + "." + decl.name;

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
                emitCall(runtime::kStrReleaseMaybe, {fieldValue});
            }
            else if (ilFieldType.kind == Type::Kind::Ptr)
            {
                Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field.offset));
                Value fieldValue = emitLoad(fieldAddr, Type(Type::Kind::Ptr));
                emitCallRet(Type(Type::Kind::I64), runtime::kHeapRelease, {fieldValue});
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

//=============================================================================
// Interface Registration and ITable Binding
//=============================================================================

void Lowerer::emitItableInit()
{
    // Skip if no interfaces are defined (no call was emitted in start())
    if (interfaceTypes_.empty())
        return;

    // Save current function context
    Function *savedFunc = currentFunc_;
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);

    // Create __zia_iface_init() function
    auto &fn = builder_->startFunction("__zia_iface_init", Type(Type::Kind::Void), {});
    currentFunc_ = &fn;
    definedFunctions_.insert("__zia_iface_init");
    blockMgr_.bind(builder_.get(), &fn);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();

    // Create entry block
    builder_->createBlock(fn, "entry_0", {});
    setBlock(fn.blocks.size() - 1);

    // Phase 1: Register each interface
    for (const auto &[ifaceName, ifaceInfo] : interfaceTypes_)
    {
        // rt_register_interface_direct(ifaceId, qname, slotCount)
        Value qnameStr = emitConstStr(stringTable_.intern(ifaceName));
        emitCall("rt_register_interface_direct",
                 {Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                  qnameStr,
                  Value::constInt(static_cast<int64_t>(ifaceInfo.methods.size()))});
    }

    // Phase 2: For each entity implementing an interface, build and bind itable
    for (const auto &[entityName, entityInfo] : entityTypes_)
    {
        for (const auto &ifaceName : entityInfo.implementedInterfaces)
        {
            auto ifaceIt = interfaceTypes_.find(ifaceName);
            if (ifaceIt == interfaceTypes_.end())
                continue;
            const InterfaceTypeInfo &ifaceInfo = ifaceIt->second;
            if (ifaceInfo.methods.empty())
                continue;

            // Allocate itable: slotCount * 8 bytes
            size_t slotCount = ifaceInfo.methods.size();
            int64_t bytes = static_cast<int64_t>(slotCount * 8ULL);
            Value itablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc",
                                          {Value::constInt(bytes)});

            // Populate each slot with a function pointer
            for (size_t s = 0; s < slotCount; ++s)
            {
                const std::string &methodName = ifaceInfo.methods[s]->name;
                int64_t offset = static_cast<int64_t>(s * 8ULL);
                Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr),
                                           itablePtr, Value::constInt(offset));

                // Find the implementing method in the entity (or its bases)
                std::string implName;
                std::string searchEntity = entityName;
                while (!searchEntity.empty())
                {
                    auto entIt = entityTypes_.find(searchEntity);
                    if (entIt == entityTypes_.end())
                        break;
                    auto vtIt = entIt->second.vtableIndex.find(methodName);
                    if (vtIt != entIt->second.vtableIndex.end())
                    {
                        implName = entIt->second.vtable[vtIt->second];
                        break;
                    }
                    searchEntity = entIt->second.baseClass;
                }

                if (implName.empty())
                {
                    // No implementation found — store null
                    emitStore(slotPtr, Value::null(), Type(Type::Kind::Ptr));
                }
                else
                {
                    // Store function pointer
                    emitStore(slotPtr, Value::global(implName), Type(Type::Kind::Ptr));
                }
            }

            // Bind the itable: rt_bind_interface(typeId, ifaceId, itable)
            emitCall("rt_bind_interface",
                     {Value::constInt(static_cast<int64_t>(entityInfo.classId)),
                      Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                      itablePtr});
        }
    }

    emitRetVoid();

    // Restore previous function context
    currentFunc_ = savedFunc;
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
}

} // namespace il::frontends::zia
