//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Decl.cpp
// Purpose: Declaration lowering for Pascal AST to IL.
// Key invariants: Creates IL functions with proper signatures and local allocation.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/Lowerer.hpp"

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Declaration Lowering
//===----------------------------------------------------------------------===//

void Lowerer::lowerDeclarations(Program &prog)
{
    allocateLocals(prog.decls, /*isMain=*/true);
}

void Lowerer::registerGlobals(const std::vector<std::unique_ptr<Decl>> &decls)
{
    for (const auto &decl : decls)
    {
        if (!decl || decl->kind != DeclKind::Var)
            continue;

        auto &varDecl = static_cast<const VarDecl &>(*decl);
        if (!varDecl.type)
            continue;

        PasType type = sema_->resolveType(*varDecl.type);
        for (const auto &name : varDecl.names)
        {
            std::string key = toLower(name);
            globalTypes_[key] = type;
        }
    }
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

Lowerer::Value Lowerer::getGlobalVarAddr(const std::string &name, const PasType &type)
{
    Type ilType = mapType(type);
    std::string helper = getModvarAddrHelper(ilType.kind);
    usedExterns_.insert(helper);

    std::string globalName = getStringGlobal(name);
    Value nameStr = emitConstStr(globalName);
    Value addr = emitCallRet(Type(Type::Kind::Ptr), helper, {nameStr});
    return addr;
}

void Lowerer::allocateLocals(const std::vector<std::unique_ptr<Decl>> &decls, bool isMain)
{
    for (const auto &decl : decls)
    {
        if (!decl)
            continue;

        if (decl->kind == DeclKind::Var)
        {
            auto &varDecl = static_cast<const VarDecl &>(*decl);
            if (!varDecl.type)
                continue;

            // Resolve type directly from the declaration to handle procedure locals
            // (sema_->lookupVariable won't work since scope has been popped after analysis)
            PasType type = sema_->resolveType(*varDecl.type);

            for (const auto &name : varDecl.names)
            {
                std::string key = toLower(name);

                // Skip globals only when processing main - locals in procedures can shadow globals
                if (isMain && globalTypes_.find(key) != globalTypes_.end())
                    continue;

                // Store type for lowerName to retrieve
                localTypes_[key] = type;
                int64_t size = sizeOf(type);
                Value slot = emitAlloca(size);
                locals_[key] = slot;
                initializeLocal(key, type);
            }
        }
        else if (decl->kind == DeclKind::Const)
        {
            // Constants should be handled by lookup in the semantic analyzer
            // which stores the folded values. We don't need to lower them here.
            // The lowerName function will look up constant values from sema_.
        }
    }
}

void Lowerer::initializeLocal(const std::string &name, const PasType &type)
{
    auto it = locals_.find(name);
    if (it == locals_.end())
        return;

    Value slot = it->second;
    Type ilType = mapType(type);

    switch (type.kind)
    {
        case PasTypeKind::Integer:
            emitStore(ilType, slot, Value::constInt(0));
            break;
        case PasTypeKind::Real:
            emitStore(ilType, slot, Value::constFloat(0.0));
            break;
        case PasTypeKind::Boolean:
            emitStore(ilType, slot, Value::constBool(false));
            break;
        case PasTypeKind::String:
        {
            // Initialize to empty string
            std::string globalName = getStringGlobal("");
            Value strVal = emitConstStr(globalName);
            emitStore(ilType, slot, strVal);
            break;
        }
        case PasTypeKind::Pointer:
        case PasTypeKind::Class:
        case PasTypeKind::Interface:
        case PasTypeKind::Optional:
            // Initialize to nil
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
            break;
        case PasTypeKind::Array:
            // Static arrays are inline storage; no initialization needed
            // (elements will be initialized when assigned)
            break;
        default:
            // Default: zero initialize
            emitStore(ilType, slot, Value::constInt(0));
            break;
    }
}

void Lowerer::lowerFunctionDecl(FunctionDecl &decl)
{
    if (!decl.body)
        return; // Forward declaration only

    // Look up the function signature to get parameter and return types
    std::string funcKey =
        decl.isMethod() ? toLower(decl.className + "." + decl.name) : toLower(decl.name);
    auto sig = sema_->lookupFunction(funcKey);

    // Build parameter list
    std::vector<il::core::Param> params;

    // For methods, add implicit Self parameter as first parameter
    if (decl.isMethod())
    {
        il::core::Param selfParam;
        selfParam.name = "Self";
        selfParam.type = Type(Type::Kind::Ptr); // Classes are always pointers
        params.push_back(std::move(selfParam));
    }

    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        const auto &param = decl.params[i];
        il::core::Param ilParam;
        ilParam.name = param.name;
        // Get type from function signature (registered by semantic analyzer)
        if (sig && i < sig->params.size())
        {
            ilParam.type = mapType(sig->params[i].second);
        }
        else if (param.type)
        {
            // Fallback: resolve type directly from AST
            ilParam.type = mapType(sema_->resolveType(*param.type));
        }
        else
        {
            ilParam.type = Type(Type::Kind::I64);
        }
        params.push_back(std::move(ilParam));
    }

    // Determine return type
    Type returnType = Type(Type::Kind::I64);
    if (sig)
    {
        returnType = mapType(sig->returnType);
    }
    else if (decl.returnType)
    {
        returnType = mapType(sema_->resolveType(*decl.returnType));
    }

    // Create function - for methods, use ClassName.MethodName
    std::string funcName = decl.isMethod() ? (decl.className + "." + decl.name) : decl.name;
    currentFunc_ = &builder_->startFunction(funcName, returnType, params);

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Copy function parameters to entry block (required for codegen to spill registers)
    currentFunc_->blocks[entryIdx].params = currentFunc_->params;

    // Clear locals for this function
    locals_.clear();
    localTypes_.clear();
    currentFuncName_ = toLower(decl.name);
    currentClassName_ = decl.isMethod() ? decl.className : "";

    // For methods, map Self parameter to locals
    size_t paramOffset = 0;
    if (decl.isMethod() && !currentFunc_->params.empty())
    {
        unsigned selfId = currentFunc_->params[0].id;
        Value selfVal = Value::temp(selfId);
        Value selfSlot = emitAlloca(8);
        locals_["self"] = selfSlot;
        emitStore(Type(Type::Kind::Ptr), selfSlot, selfVal);
        paramOffset = 1;
    }

    // Map parameters to locals (startFunction copies params to function.params)
    for (size_t i = 0; i < decl.params.size() && (i + paramOffset) < currentFunc_->params.size();
         ++i)
    {
        const auto &param = decl.params[i];
        std::string key = toLower(param.name);

        unsigned paramId = currentFunc_->params[i + paramOffset].id;
        Value paramVal = Value::temp(paramId);

        // Record parameter type
        if (sig && i < sig->params.size())
        {
            localTypes_[key] = sig->params[i].second;
        }
        else if (param.type)
        {
            localTypes_[key] = sema_->resolveType(*param.type);
        }

        // Allocate slot and store parameter
        Value slot = emitAlloca(8);
        locals_[key] = slot;
        emitStore(currentFunc_->params[i + paramOffset].type, slot, paramVal);
    }

    // Allocate result variable for the function
    std::string resultKey = toLower(decl.name);
    Value resultSlot = emitAlloca(8);
    locals_[resultKey] = resultSlot;
    // Record return type for 'Result' variable
    if (sig)
    {
        localTypes_[resultKey] = sig->returnType;
    }
    else if (decl.returnType)
    {
        localTypes_[resultKey] = sema_->resolveType(*decl.returnType);
    }

    // Allocate local variables
    allocateLocals(decl.localDecls);

    // Lower body
    lowerBlock(*decl.body);

    // Return the result value
    Value result = emitLoad(returnType, resultSlot);
    emitRet(result);

    // Clear class name
    currentClassName_.clear();
}

void Lowerer::lowerProcedureDecl(ProcedureDecl &decl)
{
    if (!decl.body)
        return; // Forward declaration only

    // Look up the procedure signature to get parameter types
    std::string funcKey =
        decl.isMethod() ? toLower(decl.className + "." + decl.name) : toLower(decl.name);
    auto sig = sema_->lookupFunction(funcKey);

    // Build parameter list
    std::vector<il::core::Param> params;

    // For methods, add implicit Self parameter as first parameter
    if (decl.isMethod())
    {
        il::core::Param selfParam;
        selfParam.name = "Self";
        selfParam.type = Type(Type::Kind::Ptr); // Classes are always pointers
        params.push_back(std::move(selfParam));
    }

    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        const auto &param = decl.params[i];
        il::core::Param ilParam;
        ilParam.name = param.name;
        // Get type from function signature (registered by semantic analyzer)
        if (sig && i < sig->params.size())
        {
            ilParam.type = mapType(sig->params[i].second);
        }
        else if (param.type)
        {
            // Fallback: resolve type directly from AST
            ilParam.type = mapType(sema_->resolveType(*param.type));
        }
        else
        {
            ilParam.type = Type(Type::Kind::I64);
        }
        params.push_back(std::move(ilParam));
    }

    // Create procedure (void return) - for methods, use ClassName.MethodName
    std::string funcName = decl.isMethod() ? (decl.className + "." + decl.name) : decl.name;
    currentFunc_ = &builder_->startFunction(funcName, Type(Type::Kind::Void), params);

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Copy function parameters to entry block (required for codegen to spill registers)
    currentFunc_->blocks[entryIdx].params = currentFunc_->params;

    // Clear locals for this procedure
    locals_.clear();
    localTypes_.clear();
    currentFuncName_.clear(); // Procedures don't have Result
    currentClassName_ = decl.isMethod() ? decl.className : "";

    // For methods, map Self parameter to locals
    size_t paramOffset = 0;
    if (decl.isMethod() && !currentFunc_->params.empty())
    {
        unsigned selfId = currentFunc_->params[0].id;
        Value selfVal = Value::temp(selfId);
        Value selfSlot = emitAlloca(8);
        locals_["self"] = selfSlot;
        emitStore(Type(Type::Kind::Ptr), selfSlot, selfVal);
        paramOffset = 1;
    }

    // Map parameters to locals
    for (size_t i = 0; i < decl.params.size() && (i + paramOffset) < currentFunc_->params.size();
         ++i)
    {
        const auto &param = decl.params[i];
        std::string key = toLower(param.name);

        unsigned paramId = currentFunc_->params[i + paramOffset].id;
        Value paramVal = Value::temp(paramId);

        // Record parameter type
        PasType paramType;
        if (sig && i < sig->params.size())
        {
            paramType = sig->params[i].second;
            localTypes_[key] = paramType;
        }
        else if (param.type)
        {
            paramType = sema_->resolveType(*param.type);
            localTypes_[key] = paramType;
        }

        // Interface parameters are 16 bytes (fat pointer: objPtr + itablePtr)
        // The parameter is passed as a pointer to the fat pointer
        int64_t slotSize = (paramType.kind == PasTypeKind::Interface) ? 16 : 8;
        Value slot = emitAlloca(slotSize);
        locals_[key] = slot;

        if (paramType.kind == PasTypeKind::Interface)
        {
            // Parameter is a pointer to a 16-byte fat pointer - copy the contents
            // Load objPtr from param (offset 0)
            Value srcObjPtr = emitLoad(Type(Type::Kind::Ptr), paramVal);
            emitStore(Type(Type::Kind::Ptr), slot, srcObjPtr);

            // Load itablePtr from param (offset 8)
            Value srcItablePtrAddr = emitGep(paramVal, Value::constInt(8));
            Value srcItablePtr = emitLoad(Type(Type::Kind::Ptr), srcItablePtrAddr);
            Value dstItablePtrAddr = emitGep(slot, Value::constInt(8));
            emitStore(Type(Type::Kind::Ptr), dstItablePtrAddr, srcItablePtr);
        }
        else
        {
            emitStore(currentFunc_->params[i + paramOffset].type, slot, paramVal);
        }
    }

    // Allocate local variables
    allocateLocals(decl.localDecls);

    // Lower body
    lowerBlock(*decl.body);

    // Return void
    emitRetVoid();

    // Clear class name
    currentClassName_.clear();
}

void Lowerer::lowerConstructorDecl(ConstructorDecl &decl)
{
    if (!decl.body)
        return; // Forward declaration only

    // Build parameter list - always has Self as first parameter
    std::vector<il::core::Param> params;

    // Add implicit Self parameter as first parameter
    il::core::Param selfParam;
    selfParam.name = "Self";
    selfParam.type = Type(Type::Kind::Ptr); // Classes are always pointers
    params.push_back(std::move(selfParam));

    for (const auto &param : decl.params)
    {
        il::core::Param ilParam;
        ilParam.name = param.name;
        if (param.type)
        {
            // Resolve type from the TypeNode directly
            PasType paramType = sema_->resolveType(*param.type);
            ilParam.type = mapType(paramType);
        }
        else
        {
            ilParam.type = Type(Type::Kind::I64);
        }
        params.push_back(std::move(ilParam));
    }

    // Create constructor function: ClassName.ConstructorName (void return)
    std::string funcName = decl.className + "." + decl.name;
    currentFunc_ = &builder_->startFunction(funcName, Type(Type::Kind::Void), params);

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Copy function parameters to entry block (required for codegen to spill registers)
    currentFunc_->blocks[entryIdx].params = currentFunc_->params;

    // Clear locals for this constructor
    locals_.clear();
    localTypes_.clear();
    currentFuncName_.clear(); // Constructors don't have Result
    currentClassName_ = decl.className;

    // Map Self parameter to locals
    if (!currentFunc_->params.empty())
    {
        unsigned selfId = currentFunc_->params[0].id;
        Value selfVal = Value::temp(selfId);
        Value selfSlot = emitAlloca(8);
        locals_["self"] = selfSlot;
        emitStore(Type(Type::Kind::Ptr), selfSlot, selfVal);
    }

    // Map parameters to locals (starting after Self)
    for (size_t i = 0; i < decl.params.size() && (i + 1) < currentFunc_->params.size(); ++i)
    {
        const auto &param = decl.params[i];
        std::string key = toLower(param.name);

        unsigned paramId = currentFunc_->params[i + 1].id;
        Value paramVal = Value::temp(paramId);

        Value slot = emitAlloca(8);
        locals_[key] = slot;
        emitStore(currentFunc_->params[i + 1].type, slot, paramVal);

        // Store parameter type for later use
        if (param.type)
        {
            PasType paramType = sema_->resolveType(*param.type);
            localTypes_[key] = paramType;
        }
    }

    // Allocate local variables
    allocateLocals(decl.localDecls);

    // Lower body
    lowerBlock(*decl.body);

    // Return void (constructor doesn't return anything)
    emitRetVoid();

    // Clear class name
    currentClassName_.clear();
}

void Lowerer::lowerDestructorDecl(DestructorDecl &decl)
{
    if (!decl.body)
        return; // Forward declaration only

    // Build parameter list - only Self parameter
    std::vector<il::core::Param> params;

    // Add implicit Self parameter
    il::core::Param selfParam;
    selfParam.name = "Self";
    selfParam.type = Type(Type::Kind::Ptr);
    params.push_back(std::move(selfParam));

    // Create destructor function: ClassName.DestructorName (void return)
    std::string funcName = decl.className + "." + decl.name;
    currentFunc_ = &builder_->startFunction(funcName, Type(Type::Kind::Void), params);

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Copy function parameters to entry block
    currentFunc_->blocks[entryIdx].params = currentFunc_->params;

    // Clear locals for this destructor
    locals_.clear();
    localTypes_.clear();
    currentFuncName_.clear();
    currentClassName_ = decl.className;

    // Map Self parameter to locals
    if (!currentFunc_->params.empty())
    {
        unsigned selfId = currentFunc_->params[0].id;
        Value selfVal = Value::temp(selfId);
        Value selfSlot = emitAlloca(8);
        locals_["self"] = selfSlot;
        emitStore(Type(Type::Kind::Ptr), selfSlot, selfVal);
    }

    // Allocate local variables
    allocateLocals(decl.localDecls);

    // Lower body
    lowerBlock(*decl.body);

    // Return void
    emitRetVoid();

    // Clear class name
    currentClassName_.clear();
}

//===----------------------------------------------------------------------===//
// Expression Lowering
//===----------------------------------------------------------------------===//

} // namespace il::frontends::pascal
