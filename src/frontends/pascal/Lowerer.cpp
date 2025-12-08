//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer.cpp
// Purpose: Implements Pascal AST to IL lowering.
// Key invariants: Produces valid SSA with deterministic block naming.
// Ownership/Lifetime: Borrows AST; produces new Module.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/common/CharUtils.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive lookup
using common::char_utils::toLowercase;

// Alias for compatibility with existing code
inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

Lowerer::Lowerer() = default;

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

Lowerer::Module Lowerer::lower(Program &prog, SemanticAnalyzer &sema)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    sema_ = &sema;
    locals_.clear();
    localTypes_.clear();
    constants_.clear();
    stringTable_.clear();
    // Set up the StringTable emitter to register globals with the builder
    stringTable_.setEmitter([this](const std::string &label, const std::string &content) {
        builder_->addGlobalStr(label, content);
    });
    loopStack_.clear();
    usedExterns_.clear();
    blockCounter_ = 0;

    // First, lower all function/procedure declarations in the program
    for (const auto &decl : prog.decls)
    {
        if (!decl)
            continue;

        if (decl->kind == DeclKind::Function)
        {
            auto &funcDecl = static_cast<FunctionDecl &>(*decl);
            lowerFunctionDecl(funcDecl);
        }
        else if (decl->kind == DeclKind::Procedure)
        {
            auto &procDecl = static_cast<ProcedureDecl &>(*decl);
            lowerProcedureDecl(procDecl);
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            auto &ctorDecl = static_cast<ConstructorDecl &>(*decl);
            lowerConstructorDecl(ctorDecl);
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            auto &dtorDecl = static_cast<DestructorDecl &>(*decl);
            lowerDestructorDecl(dtorDecl);
        }
    }

    // Create @main function
    currentFunc_ = &builder_->startFunction(
        "main", Type(Type::Kind::I64), {});

    // Clear locals from any previously lowered functions
    locals_.clear();
    localTypes_.clear();
    currentFuncName_.clear();  // main doesn't have Result

    // Create entry block
    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Allocate local variables from declarations
    allocateLocals(prog.decls);

    // Lower main body
    if (prog.body)
    {
        lowerBlock(*prog.body);
    }

    // Ensure function ends with ret 0
    emitRet(Value::constInt(0));

    // Add extern declarations for used runtime functions
    for (const auto &externName : usedExterns_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(std::string(desc->name), desc->signature.retType,
                                desc->signature.paramTypes);
        }
    }

    // Return the built module
    return std::move(*module_);
}

Lowerer::Module Lowerer::lower(Unit &unit, SemanticAnalyzer &sema)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    sema_ = &sema;
    locals_.clear();
    localTypes_.clear();
    constants_.clear();
    stringTable_.clear();
    // Set up the StringTable emitter to register globals with the builder
    stringTable_.setEmitter([this](const std::string &label, const std::string &content) {
        builder_->addGlobalStr(label, content);
    });
    loopStack_.clear();
    usedExterns_.clear();
    blockCounter_ = 0;

    // Lower all function/procedure declarations from implementation
    for (const auto &decl : unit.implDecls)
    {
        if (!decl)
            continue;

        if (decl->kind == DeclKind::Function)
        {
            auto &funcDecl = static_cast<FunctionDecl &>(*decl);
            lowerFunctionDecl(funcDecl);
        }
        else if (decl->kind == DeclKind::Procedure)
        {
            auto &procDecl = static_cast<ProcedureDecl &>(*decl);
            lowerProcedureDecl(procDecl);
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            auto &ctorDecl = static_cast<ConstructorDecl &>(*decl);
            lowerConstructorDecl(ctorDecl);
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            auto &dtorDecl = static_cast<DestructorDecl &>(*decl);
            lowerDestructorDecl(dtorDecl);
        }
    }

    // Lower initialization section if present
    if (unit.initSection)
    {
        std::string initName = unit.name + "_init";
        currentFunc_ = &builder_->startFunction(initName, Type(Type::Kind::Void), {});
        size_t entryIdx = createBlock("entry");
        setBlock(entryIdx);
        lowerBlock(*unit.initSection);
        emitRetVoid();
    }

    // Add extern declarations for used runtime functions
    for (const auto &externName : usedExterns_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(std::string(desc->name), desc->signature.retType,
                                desc->signature.paramTypes);
        }
    }

    return std::move(*module_);
}

void Lowerer::mergeModule(Module &target, Module &source)
{
    // Merge functions
    for (auto &func : source.functions)
    {
        target.functions.push_back(std::move(func));
    }

    // Merge externs (avoid duplicates)
    for (const auto &ext : source.externs)
    {
        bool found = false;
        for (const auto &existing : target.externs)
        {
            if (existing.name == ext.name)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            target.externs.push_back(ext);
        }
    }

    // Merge globals
    for (auto &glob : source.globals)
    {
        target.globals.push_back(std::move(glob));
    }
}

//===----------------------------------------------------------------------===//
// Block and Name Management
//===----------------------------------------------------------------------===//

size_t Lowerer::createBlock(const std::string &base)
{
    std::ostringstream oss;
    oss << base << "_" << blockCounter_++;
    builder_->createBlock(*currentFunc_, oss.str());
    return currentFunc_->blocks.size() - 1;
}

void Lowerer::setBlock(size_t blockIdx)
{
    currentBlockIdx_ = blockIdx;
    builder_->setInsertPoint(currentFunc_->blocks[blockIdx]);
}

std::string Lowerer::getStringGlobal(const std::string &value)
{
    // Delegate to the common StringTable which handles interning and deduplication
    return stringTable_.intern(value);
}

//===----------------------------------------------------------------------===//
// Type Mapping
//===----------------------------------------------------------------------===//

Lowerer::Type Lowerer::mapType(const PasType &pasType)
{
    switch (pasType.kind)
    {
    case PasTypeKind::Void:
        return Type(Type::Kind::Void);
    case PasTypeKind::Integer:
    case PasTypeKind::Enum:
        // Enums are represented as integers (ordinal values)
        return Type(Type::Kind::I64);
    case PasTypeKind::Real:
        return Type(Type::Kind::F64);
    case PasTypeKind::Boolean:
        return Type(Type::Kind::I1);
    case PasTypeKind::String:
        return Type(Type::Kind::Str);
    case PasTypeKind::Pointer:
    case PasTypeKind::Class:
    case PasTypeKind::Interface:
    case PasTypeKind::Array:
        return Type(Type::Kind::Ptr);
    case PasTypeKind::Optional:
        // For reference-type optionals (String?, Class?, etc.), use Ptr (null = nil)
        // For value-type optionals (Integer?, Real?, Boolean?), we use a struct
        // representation but map to Ptr for alloca purposes (the struct is in memory)
        if (pasType.innerType && pasType.innerType->isReference())
            return Type(Type::Kind::Ptr);
        // Value-type optionals use (hasValue: i64, value: T) in memory
        // Return Ptr since we access them via alloca
        return Type(Type::Kind::Ptr);
    case PasTypeKind::Nil:
        return Type(Type::Kind::Ptr);
    default:
        return Type(Type::Kind::I64);
    }
}

int64_t Lowerer::sizeOf(const PasType &pasType)
{
    switch (pasType.kind)
    {
    case PasTypeKind::Integer:
    case PasTypeKind::Enum:
        return 8; // Enums stored as 64-bit integers
    case PasTypeKind::Real:
        return 8;
    case PasTypeKind::Boolean:
        return 1;
    case PasTypeKind::String:
    case PasTypeKind::Pointer:
    case PasTypeKind::Class:
    case PasTypeKind::Interface:
    case PasTypeKind::Array:
        return 8; // Pointer size
    case PasTypeKind::Optional:
        // For value types, need space for flag + value
        if (pasType.innerType)
            return 8 + sizeOf(*pasType.innerType);
        return 16;
    default:
        return 8;
    }
}

//===----------------------------------------------------------------------===//
// Declaration Lowering
//===----------------------------------------------------------------------===//

void Lowerer::lowerDeclarations(Program &prog)
{
    allocateLocals(prog.decls);
}

void Lowerer::allocateLocals(const std::vector<std::unique_ptr<Decl>> &decls)
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
            auto &constDecl = static_cast<const ConstDecl &>(*decl);
            if (constDecl.value)
            {
                std::string key = toLower(constDecl.name);
                LowerResult result = lowerExpr(*constDecl.value);
                constants_[key] = result.value;
            }
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
    case PasTypeKind::String: {
        // Initialize to empty string
        std::string globalName = getStringGlobal("");
        Value strVal = emitConstStr(globalName);
        emitStore(ilType, slot, strVal);
        break;
    }
    case PasTypeKind::Pointer:
    case PasTypeKind::Class:
    case PasTypeKind::Interface:
    case PasTypeKind::Array:
    case PasTypeKind::Optional:
        // Initialize to nil
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
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

    // Build parameter list
    std::vector<il::core::Param> params;

    // For methods, add implicit Self parameter as first parameter
    if (decl.isMethod())
    {
        il::core::Param selfParam;
        selfParam.name = "Self";
        selfParam.type = Type(Type::Kind::Ptr);  // Classes are always pointers
        params.push_back(std::move(selfParam));
    }

    for (const auto &param : decl.params)
    {
        il::core::Param ilParam;
        ilParam.name = param.name;
        if (param.type)
        {
            // Get type from semantic analyzer by looking up the variable
            auto varType = sema_->lookupVariable(toLower(param.name));
            ilParam.type = varType ? mapType(*varType) : Type(Type::Kind::I64);
        }
        else
        {
            ilParam.type = Type(Type::Kind::I64);
        }
        params.push_back(std::move(ilParam));
    }

    // Determine return type
    Type returnType = Type(Type::Kind::I64);
    if (decl.returnType)
    {
        // Look up the function signature to get return type
        auto sig = sema_->lookupFunction(toLower(decl.name));
        if (sig)
            returnType = mapType(sig->returnType);
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
    for (size_t i = 0; i < decl.params.size() && (i + paramOffset) < currentFunc_->params.size(); ++i)
    {
        const auto &param = decl.params[i];
        std::string key = toLower(param.name);

        unsigned paramId = currentFunc_->params[i + paramOffset].id;
        Value paramVal = Value::temp(paramId);

        // Allocate slot and store parameter
        Value slot = emitAlloca(8);
        locals_[key] = slot;
        emitStore(currentFunc_->params[i + paramOffset].type, slot, paramVal);
    }

    // Allocate result variable for the function
    std::string resultKey = toLower(decl.name);
    Value resultSlot = emitAlloca(8);
    locals_[resultKey] = resultSlot;

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

    // Build parameter list
    std::vector<il::core::Param> params;

    // For methods, add implicit Self parameter as first parameter
    if (decl.isMethod())
    {
        il::core::Param selfParam;
        selfParam.name = "Self";
        selfParam.type = Type(Type::Kind::Ptr);  // Classes are always pointers
        params.push_back(std::move(selfParam));
    }

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
    currentFuncName_.clear();  // Procedures don't have Result
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
    for (size_t i = 0; i < decl.params.size() && (i + paramOffset) < currentFunc_->params.size(); ++i)
    {
        const auto &param = decl.params[i];
        std::string key = toLower(param.name);

        unsigned paramId = currentFunc_->params[i + paramOffset].id;
        Value paramVal = Value::temp(paramId);

        Value slot = emitAlloca(8);
        locals_[key] = slot;
        emitStore(currentFunc_->params[i + paramOffset].type, slot, paramVal);

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
    selfParam.type = Type(Type::Kind::Ptr);  // Classes are always pointers
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
    currentFuncName_.clear();  // Constructors don't have Result
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

LowerResult Lowerer::lowerExpr(const Expr &expr)
{
    switch (expr.kind)
    {
    case ExprKind::IntLiteral:
        return lowerIntLiteral(static_cast<const IntLiteralExpr &>(expr));
    case ExprKind::RealLiteral:
        return lowerRealLiteral(static_cast<const RealLiteralExpr &>(expr));
    case ExprKind::StringLiteral:
        return lowerStringLiteral(static_cast<const StringLiteralExpr &>(expr));
    case ExprKind::BoolLiteral:
        return lowerBoolLiteral(static_cast<const BoolLiteralExpr &>(expr));
    case ExprKind::NilLiteral:
        return lowerNilLiteral(static_cast<const NilLiteralExpr &>(expr));
    case ExprKind::Name:
        return lowerName(static_cast<const NameExpr &>(expr));
    case ExprKind::Unary:
        return lowerUnary(static_cast<const UnaryExpr &>(expr));
    case ExprKind::Binary:
        return lowerBinary(static_cast<const BinaryExpr &>(expr));
    case ExprKind::Call:
        return lowerCall(static_cast<const CallExpr &>(expr));
    case ExprKind::Index:
        return lowerIndex(static_cast<const IndexExpr &>(expr));
    case ExprKind::Field:
        return lowerField(static_cast<const FieldExpr &>(expr));
    default:
        // Unsupported expression type - return zero
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerIntLiteral(const IntLiteralExpr &expr)
{
    return {Value::constInt(expr.value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerRealLiteral(const RealLiteralExpr &expr)
{
    return {Value::constFloat(expr.value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(const StringLiteralExpr &expr)
{
    std::string globalName = getStringGlobal(expr.value);
    Value strVal = emitConstStr(globalName);
    return {strVal, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(const BoolLiteralExpr &expr)
{
    return {Value::constBool(expr.value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNilLiteral(const NilLiteralExpr &)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerName(const NameExpr &expr)
{
    std::string key = toLower(expr.name);

    // Check for built-in math constants (Pi and E from Viper.Math)
    if (key == "pi")
    {
        return {Value::constFloat(3.14159265358979323846), Type(Type::Kind::F64)};
    }
    if (key == "e")
    {
        return {Value::constFloat(2.71828182845904523536), Type(Type::Kind::F64)};
    }

    // Check constants first (including enum constants from semantic analyzer)
    auto constIt = constants_.find(key);
    if (constIt != constants_.end())
    {
        return {constIt->second, Type(Type::Kind::I64)}; // Type approximation
    }

    // Check semantic analyzer for enum constants
    if (auto constType = sema_->lookupConstant(key))
    {
        if (constType->kind == PasTypeKind::Enum && constType->enumOrdinal >= 0)
        {
            // Enum constant: emit its ordinal value as an integer
            return {Value::constInt(constType->enumOrdinal), Type(Type::Kind::I64)};
        }
        // Handle Real constants (like Pi, E if they come through lookupConstant)
        if (constType->kind == PasTypeKind::Real)
        {
            // For now, return 0.0 - the actual values are handled above
            return {Value::constFloat(0.0), Type(Type::Kind::F64)};
        }
    }

    // Check locals
    auto localIt = locals_.find(key);
    if (localIt != locals_.end())
    {
        // First check our own localTypes_ map (for procedure locals)
        // then fall back to semantic analyzer (for global variables)
        Type ilType = Type(Type::Kind::I64);
        auto localTypeIt = localTypes_.find(key);
        if (localTypeIt != localTypes_.end())
        {
            ilType = mapType(localTypeIt->second);
        }
        else
        {
            auto varType = sema_->lookupVariable(key);
            if (varType)
                ilType = mapType(*varType);
        }
        Value loaded = emitLoad(ilType, localIt->second);
        return {loaded, ilType};
    }

    // Check for zero-argument builtin functions (Pascal allows calling without parens)
    if (auto builtinOpt = lookupBuiltin(key))
    {
        const auto &desc = getBuiltinDescriptor(*builtinOpt);
        // Only handle if it can be called with 0 args and has non-void return type
        if (desc.minArgs == 0 && desc.result != ResultKind::Void)
        {
            const char *rtSym = getBuiltinRuntimeSymbol(*builtinOpt);

            // Look up the actual runtime signature to get the correct return type
            const auto *rtDesc = il::runtime::findRuntimeDescriptor(rtSym);
            Type rtRetType = Type(Type::Kind::Void);
            if (rtDesc)
            {
                rtRetType = rtDesc->signature.retType;
            }
            else
            {
                // Fallback to Pascal type mapping
                PasType resultPasType = getBuiltinResultType(*builtinOpt);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(*builtinOpt);
            Type pascalRetType = mapType(pascalResultType);

            // Emit call with no arguments
            Value result = emitCallRet(rtRetType, rtSym, {});

            // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
            if (pascalRetType.kind == Type::Kind::I1 &&
                (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
            {
                // Convert to i1: compare != 0
                Value zero = Value::constInt(0);
                result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                return {result, Type(Type::Kind::I1)};
            }

            return {result, rtRetType};
        }
    }

    // Check for zero-argument user-defined functions (Pascal allows calling without parens)
    if (auto sig = sema_->lookupFunction(key))
    {
        // Only handle if it can be called with 0 args and has non-void return type
        if (sig->requiredParams == 0 && sig->returnType.kind != PasTypeKind::Void)
        {
            Type retType = mapType(sig->returnType);
            // Use the original function name from the signature (preserves case)
            Value result = emitCallRet(retType, sig->name, {});
            return {result, retType};
        }
    }

    // Check if we're inside a class method and the name is a field of the current class
    if (!currentClassName_.empty())
    {
        auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
        if (classInfo)
        {
            auto fieldIt = classInfo->fields.find(key);
            if (fieldIt != classInfo->fields.end())
            {
                // Access Self.fieldName
                // First, load Self pointer from locals
                auto selfIt = locals_.find("self");
                if (selfIt != locals_.end())
                {
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                    // Build a PasType with the class fields for getFieldAddress
                    PasType selfType = PasType::classType(currentClassName_);
                    for (const auto &[fname, finfo] : classInfo->fields)
                    {
                        selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                    }
                    auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, expr.name);

                    // Load the field value
                    Value fieldVal = emitLoad(fieldType, fieldAddr);
                    return {fieldVal, fieldType};
                }
            }
        }
    }

    // Unknown - return zero
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerUnary(const UnaryExpr &expr)
{
    LowerResult operand = lowerExpr(*expr.operand);

    switch (expr.op)
    {
    case UnaryExpr::Op::Neg:
        if (operand.type.kind == Type::Kind::F64)
        {
            // Negate float: 0.0 - x
            Value zero = Value::constFloat(0.0);
            Value result = emitBinary(Opcode::FSub, operand.type, zero, operand.value);
            return {result, operand.type};
        }
        else
        {
            // Negate integer: 0 - x (use overflow-checking subtraction)
            Value zero = Value::constInt(0);
            Value result = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), zero, operand.value);
            return {result, Type(Type::Kind::I64)};
        }

    case UnaryExpr::Op::Not:
        // Boolean not
        {
            // If operand is i1, first zext to i64
            Value opVal = operand.value;
            if (operand.type.kind == Type::Kind::I1)
            {
                opVal = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), opVal);
            }
            // xor with 1
            Value one = Value::constInt(1);
            Value result = emitBinary(Opcode::Xor, Type(Type::Kind::I64), opVal, one);
            // Truncate back to i1
            result = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), result);
            return {result, Type(Type::Kind::I1)};
        }

    case UnaryExpr::Op::Plus:
        // Identity
        return operand;
    }

    return operand;
}

LowerResult Lowerer::lowerBinary(const BinaryExpr &expr)
{
    // Handle short-circuit operators specially
    switch (expr.op)
    {
    case BinaryExpr::Op::And:
        return lowerLogicalAnd(expr);
    case BinaryExpr::Op::Or:
        return lowerLogicalOr(expr);
    case BinaryExpr::Op::Coalesce:
        return lowerCoalesce(expr);
    default:
        break;
    }

    // Lower operands
    LowerResult lhs = lowerExpr(*expr.left);
    LowerResult rhs = lowerExpr(*expr.right);

    // Handle string comparisons via runtime call
    bool isString = (lhs.type.kind == Type::Kind::Str || rhs.type.kind == Type::Kind::Str);
    if (isString && (expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne))
    {
        // Call rt_str_eq(str, str) -> i1
        usedExterns_.insert("rt_str_eq");
        Value result = emitCallRet(Type(Type::Kind::I1), "rt_str_eq", {lhs.value, rhs.value});
        if (expr.op == BinaryExpr::Op::Ne)
        {
            // Negate the result for !=
            Value zero = Value::constBool(false);
            result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), result, zero);
        }
        return {result, Type(Type::Kind::I1)};
    }

    // Determine result type
    bool isFloat = (lhs.type.kind == Type::Kind::F64 || rhs.type.kind == Type::Kind::F64);

    // Promote integer to float if needed
    Value lhsVal = lhs.value;
    Value rhsVal = rhs.value;
    if (isFloat)
    {
        if (lhs.type.kind != Type::Kind::F64)
            lhsVal = emitSitofp(lhs.value);
        if (rhs.type.kind != Type::Kind::F64)
            rhsVal = emitSitofp(rhs.value);
    }

    Type resultType = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);

    switch (expr.op)
    {
    // Arithmetic
    // For signed integers (Pascal Integer type is always signed), use the .ovf
    // variants which trap on overflow as required by the IL spec
    case BinaryExpr::Op::Add:
        return {emitBinary(isFloat ? Opcode::FAdd : Opcode::IAddOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Sub:
        return {emitBinary(isFloat ? Opcode::FSub : Opcode::ISubOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Mul:
        return {emitBinary(isFloat ? Opcode::FMul : Opcode::IMulOvf, resultType, lhsVal, rhsVal),
                resultType};

    case BinaryExpr::Op::Div:
        // Real division always returns Real
        if (!isFloat)
        {
            lhsVal = emitSitofp(lhs.value);
            rhsVal = emitSitofp(rhs.value);
        }
        return {emitBinary(Opcode::FDiv, Type(Type::Kind::F64), lhsVal, rhsVal),
                Type(Type::Kind::F64)};

    case BinaryExpr::Op::IntDiv:
        // Integer division (with trap on divide-by-zero)
        return {emitBinary(Opcode::SDivChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                Type(Type::Kind::I64)};

    case BinaryExpr::Op::Mod:
        // Integer remainder (with trap on divide-by-zero)
        return {emitBinary(Opcode::SRemChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                Type(Type::Kind::I64)};

    // Comparisons
    case BinaryExpr::Op::Eq:
        return {emitBinary(isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Ne:
        return {emitBinary(isFloat ? Opcode::FCmpNE : Opcode::ICmpNe, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Lt:
        return {emitBinary(isFloat ? Opcode::FCmpLT : Opcode::SCmpLT, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Le:
        return {emitBinary(isFloat ? Opcode::FCmpLE : Opcode::SCmpLE, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Gt:
        return {emitBinary(isFloat ? Opcode::FCmpGT : Opcode::SCmpGT, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    case BinaryExpr::Op::Ge:
        return {emitBinary(isFloat ? Opcode::FCmpGE : Opcode::SCmpGE, Type(Type::Kind::I1),
                           lhsVal, rhsVal),
                Type(Type::Kind::I1)};

    default:
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerLogicalAnd(const BinaryExpr &expr)
{
    // Short-circuit: if left is false, result is false; else result is right
    size_t evalRhsBlock = createBlock("and_rhs");
    size_t shortCircuitBlock = createBlock("and_short");
    size_t joinBlock = createBlock("and_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, evaluate right; else short-circuit with false
    emitCBr(leftBool, evalRhsBlock, shortCircuitBlock);

    // Short-circuit: left was false, result is false
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(0));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerLogicalOr(const BinaryExpr &expr)
{
    // Short-circuit: if left is true, result is true; else result is right
    size_t shortCircuitBlock = createBlock("or_short");
    size_t evalRhsBlock = createBlock("or_rhs");
    size_t joinBlock = createBlock("or_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, short-circuit with true; else evaluate right
    emitCBr(leftBool, shortCircuitBlock, evalRhsBlock);

    // Short-circuit: left was true, result is true
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(1));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerCoalesce(const BinaryExpr &expr)
{
    // a ?? b: if a is not nil, use a; else evaluate and use b
    // Short-circuits: b is only evaluated if a is nil

    size_t useLeftBlock = createBlock("coalesce_use_lhs");
    size_t evalRhsBlock = createBlock("coalesce_rhs");
    size_t joinBlock = createBlock("coalesce_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(8);

    // Evaluate left operand
    LowerResult left = lowerExpr(*expr.left);

    // For reference-type optionals, null pointer means nil
    // For value-type optionals, we'd check the hasValue flag at offset 0
    // Currently, both are represented as Ptr (null = nil)
    Value isNotNil = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1),
                                left.value, Value::null());

    emitCBr(isNotNil, useLeftBlock, evalRhsBlock);

    // Use left value (not nil) - store to result slot
    setBlock(useLeftBlock);
    emitStore(left.type, resultSlot, left.value);
    emitBr(joinBlock);

    // Evaluate right operand (left was nil) - store to result slot
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(right.type, resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load from result slot
    setBlock(joinBlock);
    Value result = emitLoad(right.type, resultSlot);

    return {result, right.type};
}

LowerResult Lowerer::lowerCall(const CallExpr &expr)
{
    // Check for constructor call (marked by semantic analyzer)
    if (expr.isConstructorCall && !expr.constructorClassName.empty())
    {
        // Constructor call: ClassName.Create(args)
        // 1. Allocate memory for the object
        // 2. Call the constructor with Self as first argument
        // 3. Return the object pointer

        std::string className = expr.constructorClassName;

        // Get class info to determine object size
        auto *classInfo = sema_->lookupClass(toLower(className));
        if (!classInfo)
        {
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }

        // Calculate object size (sum of field sizes)
        int64_t objectSize = 0;
        for (const auto &[fname, finfo] : classInfo->fields)
        {
            objectSize += sizeOf(finfo.type);
        }
        if (objectSize == 0)
            objectSize = 8; // Minimum object size

        // Allocate object using rt_alloc
        Value sizeVal = Value::constInt(objectSize);
        Value objPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {sizeVal});

        // Get constructor name from the field expression
        std::string constructorName = "Create";
        if (expr.callee && expr.callee->kind == ExprKind::Field)
        {
            const auto &fieldExpr = static_cast<const FieldExpr &>(*expr.callee);
            constructorName = fieldExpr.field;
        }

        // Build constructor call arguments (Self first, then user args)
        std::vector<Value> ctorArgs;
        ctorArgs.push_back(objPtr);  // Self

        for (const auto &arg : expr.args)
        {
            LowerResult argResult = lowerExpr(*arg);
            ctorArgs.push_back(argResult.value);
        }

        // Call the constructor
        std::string ctorFunc = className + "." + constructorName;
        emitCall(ctorFunc, ctorArgs);

        // Return the object pointer
        return {objPtr, Type(Type::Kind::Ptr)};
    }

    // Get callee name for regular calls
    if (expr.callee->kind != ExprKind::Name)
    {
        // Method call (FieldExpr) - not yet fully supported
        // For now, just return a default value
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto &nameExpr = static_cast<const NameExpr &>(*expr.callee);
    std::string callee = nameExpr.name;

    // Lower arguments and track their types
    std::vector<Value> args;
    std::vector<PasType> argTypes;
    for (const auto &arg : expr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
        // Map IL type back to PasType for dispatch
        PasType pasType;
        switch (argResult.type.kind)
        {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I1:
            pasType.kind = PasTypeKind::Integer;
            break;
        case Type::Kind::F64:
            pasType.kind = PasTypeKind::Real;
            break;
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            pasType.kind = PasTypeKind::String;
            break;
        default:
            pasType.kind = PasTypeKind::Unknown;
            break;
        }
        argTypes.push_back(pasType);
    }

    // Check for builtin functions
    std::string lowerCallee = toLower(callee);
    auto builtinOpt = lookupBuiltin(lowerCallee);

    if (builtinOpt)
    {
        PascalBuiltin builtin = *builtinOpt;
        const BuiltinDescriptor &desc = getBuiltinDescriptor(builtin);

        // Determine first arg type for dispatch
        PasTypeKind firstArgType =
            argTypes.empty() ? PasTypeKind::Unknown : argTypes[0].kind;

        // Handle Write/WriteLn specially (variadic with type dispatch)
        if (builtin == PascalBuiltin::Write || builtin == PascalBuiltin::WriteLn)
        {
            // Print each argument using type-appropriate runtime call
            for (size_t i = 0; i < args.size(); ++i)
            {
                const char *rtSym = getBuiltinRuntimeSymbol(PascalBuiltin::Write, argTypes[i].kind);
                if (rtSym)
                {
                    emitCall(rtSym, {args[i]});
                }
                else
                {
                    // Default to i64
                    emitCall("rt_print_i64", {args[i]});
                }
            }
            if (builtin == PascalBuiltin::WriteLn)
            {
                std::string nlGlobal = getStringGlobal("\n");
                Value nlStr = emitConstStr(nlGlobal);
                emitCall("rt_print_str", {nlStr});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle ReadLn
        if (builtin == PascalBuiltin::ReadLn)
        {
            // For now, just call rt_input_line and discard result
            emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle inline builtins
        if (builtin == PascalBuiltin::Ord)
        {
            // Ord just returns the integer value (identity for integers)
            if (!args.empty())
                return {args[0], Type(Type::Kind::I64)};
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Pred)
        {
            // Pred(x) = x - 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Succ)
        {
            // Succ(x) = x + 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Sqr)
        {
            // Sqr(x) = x * x (use overflow-checking multiplication for integers)
            if (!args.empty())
            {
                Opcode mulOp = (firstArgType == PasTypeKind::Real) ? Opcode::FMul : Opcode::IMulOvf;
                Type ty = (firstArgType == PasTypeKind::Real) ? Type(Type::Kind::F64)
                                                              : Type(Type::Kind::I64);
                Value result = emitBinary(mulOp, ty, args[0], args[0]);
                return {result, ty};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Randomize)
        {
            // Randomize([seed]) - if no seed provided, use 0 as default
            usedExterns_.insert("rt_randomize_i64");
            Value seed = args.empty() ? Value::constInt(0) : args[0];
            emitCall("rt_randomize_i64", {seed});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle builtins with runtime symbols
        const char *rtSym = getBuiltinRuntimeSymbol(builtin, firstArgType);
        if (rtSym)
        {
            // Look up the actual runtime signature to get the correct return type
            const auto *rtDesc = il::runtime::findRuntimeDescriptor(rtSym);
            Type rtRetType = Type(Type::Kind::Void);
            if (rtDesc)
            {
                rtRetType = rtDesc->signature.retType;
            }
            else
            {
                // Fallback to Pascal type mapping
                PasType resultPasType = getBuiltinResultType(builtin, firstArgType);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(builtin, firstArgType);
            Type pascalRetType = mapType(pascalResultType);

            if (rtRetType.kind == Type::Kind::Void)
            {
                emitCall(rtSym, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallRet(rtRetType, rtSym, args);

                // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
                if (pascalRetType.kind == Type::Kind::I1 &&
                    (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
                {
                    // Convert to i1: compare != 0
                    Value zero = Value::constInt(0);
                    result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                    return {result, Type(Type::Kind::I1)};
                }

                return {result, rtRetType};
            }
        }
    }

    // Regular function call
    const FuncSignature *sig = sema_->lookupFunction(callee);
    Type retType = sig ? mapType(sig->returnType) : Type(Type::Kind::I64);

    if (retType.kind == Type::Kind::Void)
    {
        emitCall(callee, args);
        return {Value::constInt(0), retType};
    }
    else
    {
        Value result = emitCallRet(retType, callee, args);
        return {result, retType};
    }
}

LowerResult Lowerer::lowerIndex(const IndexExpr &expr)
{
    // Get base type
    PasType baseType = sema_->typeOf(*expr.base);

    if (baseType.kind == PasTypeKind::Array && !expr.indices.empty())
    {
        // Get base address (the array variable's alloca slot)
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;

                // Get element type and size
                Type elemType = Type(Type::Kind::I64); // Default
                int64_t elemSize = 8;
                if (baseType.elementType)
                {
                    elemType = mapType(*baseType.elementType);
                    elemSize = sizeOf(*baseType.elementType);
                }

                // Calculate offset: index * elemSize
                LowerResult index = lowerExpr(*expr.indices[0]);
                Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64),
                                          index.value, Value::constInt(elemSize));

                // GEP to get element address
                Value elemAddr = emitGep(baseAddr, offset);

                // Load the element
                Value result = emitLoad(elemType, elemAddr);
                return {result, elemType};
            }
        }
    }

    // Fallback for other cases
    LowerResult base = lowerExpr(*expr.base);
    (void)base;
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

std::pair<Lowerer::Value, Lowerer::Type> Lowerer::getFieldAddress(
    Value baseAddr, const PasType &baseType, const std::string &fieldName)
{
    std::string fieldKey = toLower(fieldName);

    // Calculate field offset by iterating through fields in order
    // Fields are stored in a map, but we need consistent ordering
    // For simplicity, use alphabetical order (same as iteration order in std::map)
    int64_t offset = 0;
    Type fieldType = Type(Type::Kind::I64);

    for (const auto &[name, typePtr] : baseType.fields)
    {
        if (name == fieldKey)
        {
            if (typePtr)
            {
                fieldType = mapType(*typePtr);
            }
            break;
        }
        // Add size of this field to offset
        if (typePtr)
        {
            offset += sizeOf(*typePtr);
        }
        else
        {
            offset += 8; // Default size
        }
    }

    // GEP to get field address
    Value fieldAddr = emitGep(baseAddr, Value::constInt(offset));
    return {fieldAddr, fieldType};
}

LowerResult Lowerer::lowerField(const FieldExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    // Get base type
    PasType baseType = sema_->typeOf(*expr.base);

    if (baseType.kind == PasTypeKind::Record)
    {
        // Records are stored inline in the variable's slot
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;
                auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, expr.field);

                // Load the field value
                Value result = emitLoad(fieldType, fieldAddr);
                return {result, fieldType};
            }
        }
        // Handle nested field access (e.g., a.b.c)
        else if (expr.base->kind == ExprKind::Field)
        {
            // Recursively get the base field's address
            // For now, fall through to default handling
        }
    }
    else if (baseType.kind == PasTypeKind::Class)
    {
        // Classes are reference types - the variable's slot contains a pointer to the object
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                // Load the object pointer from the variable's slot
                Value objPtr = emitLoad(Type(Type::Kind::Ptr), it->second);

                // Get class info to build a proper type with fields
                PasType classTypeWithFields = baseType;
                auto *classInfo = sema_->lookupClass(toLower(baseType.name));
                if (classInfo)
                {
                    for (const auto &[fname, finfo] : classInfo->fields)
                    {
                        classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                    }
                }

                auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, expr.field);

                // Load the field value
                Value result = emitLoad(fieldType, fieldAddr);
                return {result, fieldType};
            }
        }
    }

    // Fallback
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

//===----------------------------------------------------------------------===//
// Statement Lowering
//===----------------------------------------------------------------------===//

void Lowerer::lowerStmt(const Stmt &stmt)
{
    switch (stmt.kind)
    {
    case StmtKind::Assign:
        lowerAssign(static_cast<const AssignStmt &>(stmt));
        break;
    case StmtKind::Call:
        lowerCallStmt(static_cast<const CallStmt &>(stmt));
        break;
    case StmtKind::Block:
        lowerBlock(static_cast<const BlockStmt &>(stmt));
        break;
    case StmtKind::If:
        lowerIf(static_cast<const IfStmt &>(stmt));
        break;
    case StmtKind::Case:
        lowerCase(static_cast<const CaseStmt &>(stmt));
        break;
    case StmtKind::For:
        lowerFor(static_cast<const ForStmt &>(stmt));
        break;
    case StmtKind::ForIn:
        lowerForIn(static_cast<const ForInStmt &>(stmt));
        break;
    case StmtKind::While:
        lowerWhile(static_cast<const WhileStmt &>(stmt));
        break;
    case StmtKind::Repeat:
        lowerRepeat(static_cast<const RepeatStmt &>(stmt));
        break;
    case StmtKind::Break:
        lowerBreak(static_cast<const BreakStmt &>(stmt));
        break;
    case StmtKind::Continue:
        lowerContinue(static_cast<const ContinueStmt &>(stmt));
        break;
    case StmtKind::Empty:
        // No-op
        break;
    case StmtKind::Raise:
        lowerRaise(static_cast<const RaiseStmt &>(stmt));
        break;
    case StmtKind::Exit:
        lowerExit(static_cast<const ExitStmt &>(stmt));
        break;
    case StmtKind::TryExcept:
        lowerTryExcept(static_cast<const TryExceptStmt &>(stmt));
        break;
    case StmtKind::TryFinally:
        lowerTryFinally(static_cast<const TryFinallyStmt &>(stmt));
        break;
    default:
        // Other statements not yet implemented
        break;
    }
}

void Lowerer::lowerAssign(const AssignStmt &stmt)
{
    if (!stmt.target || !stmt.value)
        return;

    // Get target slot
    if (stmt.target->kind == ExprKind::Name)
    {
        auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        std::string key = toLower(nameExpr.name);

        // Map "Result" to the current function's return slot
        if (key == "result" && !currentFuncName_.empty())
        {
            key = currentFuncName_;
        }

        auto it = locals_.find(key);
        if (it != locals_.end())
        {
            Value slot = it->second;

            // Lower value
            LowerResult value = lowerExpr(*stmt.value);

            // Get target type
            auto varType = sema_->lookupVariable(key);
            Type ilType = varType ? mapType(*varType) : value.type;

            emitStore(ilType, slot, value.value);
            return;
        }

        // Check if this is a class field assignment (inside a method)
        if (!currentClassName_.empty())
        {
            auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
            if (classInfo)
            {
                auto fieldIt = classInfo->fields.find(key);
                if (fieldIt != classInfo->fields.end())
                {
                    // Assign to Self.fieldName
                    auto selfIt = locals_.find("self");
                    if (selfIt != locals_.end())
                    {
                        Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                        // Build a PasType with the class fields for getFieldAddress
                        PasType selfType = PasType::classType(currentClassName_);
                        for (const auto &[fname, finfo] : classInfo->fields)
                        {
                            selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                        }
                        auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, nameExpr.name);

                        // Lower value
                        LowerResult value = lowerExpr(*stmt.value);

                        // Store to field
                        emitStore(fieldType, fieldAddr, value.value);
                        return;
                    }
                }
            }
        }
        // Unknown target - just return
        return;
    }
    else if (stmt.target->kind == ExprKind::Field)
    {
        // Field assignment: rec.field := value
        auto &fieldExpr = static_cast<const FieldExpr &>(*stmt.target);
        if (!fieldExpr.base)
            return;

        // Get base type
        PasType baseType = sema_->typeOf(*fieldExpr.base);

        if ((baseType.kind == PasTypeKind::Record || baseType.kind == PasTypeKind::Class) &&
            fieldExpr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*fieldExpr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;
                auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, fieldExpr.field);

                // Lower value
                LowerResult value = lowerExpr(*stmt.value);

                // Store to field
                emitStore(fieldType, fieldAddr, value.value);
            }
        }
    }
    else if (stmt.target->kind == ExprKind::Index)
    {
        // Array index assignment: arr[i] := value
        auto &indexExpr = static_cast<const IndexExpr &>(*stmt.target);
        if (!indexExpr.base || indexExpr.indices.empty())
            return;

        PasType baseType = sema_->typeOf(*indexExpr.base);

        if (baseType.kind == PasTypeKind::Array && indexExpr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*indexExpr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;

                // Get element type and size
                Type elemType = Type(Type::Kind::I64);
                int64_t elemSize = 8;
                if (baseType.elementType)
                {
                    elemType = mapType(*baseType.elementType);
                    elemSize = sizeOf(*baseType.elementType);
                }

                // Calculate offset: index * elemSize
                LowerResult index = lowerExpr(*indexExpr.indices[0]);
                Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64),
                                          index.value, Value::constInt(elemSize));

                // GEP to get element address
                Value elemAddr = emitGep(baseAddr, offset);

                // Lower value
                LowerResult value = lowerExpr(*stmt.value);

                // Store to element
                emitStore(elemType, elemAddr, value.value);
            }
        }
    }
}

void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (stmt.call && stmt.call->kind == ExprKind::Call)
    {
        lowerCall(static_cast<const CallExpr &>(*stmt.call));
    }
}

void Lowerer::lowerBlock(const BlockStmt &stmt)
{
    for (const auto &s : stmt.stmts)
    {
        if (s)
            lowerStmt(*s);
    }
}

void Lowerer::lowerIf(const IfStmt &stmt)
{
    size_t thenBlock = createBlock("if_then");
    size_t endBlock = createBlock("if_end");
    size_t elseBlock = stmt.elseBranch ? createBlock("if_else") : endBlock;

    // Evaluate condition
    LowerResult cond = lowerExpr(*stmt.condition);
    emitCBr(cond.value, thenBlock, elseBlock);

    // Then branch
    setBlock(thenBlock);
    if (stmt.thenBranch)
        lowerStmt(*stmt.thenBranch);
    emitBr(endBlock);

    // Else branch
    if (stmt.elseBranch)
    {
        setBlock(elseBlock);
        lowerStmt(*stmt.elseBranch);
        emitBr(endBlock);
    }

    setBlock(endBlock);
}

void Lowerer::lowerCase(const CaseStmt &stmt)
{
    // Lower case as if-else cascade
    LowerResult scrutinee = lowerExpr(*stmt.expr);
    size_t endBlock = createBlock("case_end");

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        const CaseArm &arm = stmt.arms[i];
        size_t armBlock = createBlock("case_arm");
        size_t nextBlock;
        if (i + 1 < stmt.arms.size())
        {
            nextBlock = createBlock("case_next");
        }
        else if (stmt.elseBody)
        {
            nextBlock = createBlock("case_else");
        }
        else
        {
            nextBlock = endBlock;
        }

        // Check each label - create test blocks for each label
        // Pattern: test_0 -> (match? arm : test_1) -> (match? arm : test_2) -> ... -> nextBlock
        for (size_t j = 0; j < arm.labels.size(); ++j)
        {
            LowerResult labelVal = lowerExpr(*arm.labels[j]);
            Value match = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1),
                                     scrutinee.value, labelVal.value);

            // If this is not the last label, create another test block
            size_t falseBlock = (j + 1 < arm.labels.size()) ? createBlock("case_test") : nextBlock;
            emitCBr(match, armBlock, falseBlock);

            // Move to the next test block for the next label check
            if (j + 1 < arm.labels.size())
            {
                setBlock(falseBlock);
            }
        }

        // Arm body
        setBlock(armBlock);
        if (arm.body)
            lowerStmt(*arm.body);
        emitBr(endBlock);

        if (nextBlock != endBlock)
            setBlock(nextBlock);
    }

    // Else body
    if (stmt.elseBody)
    {
        lowerStmt(*stmt.elseBody);
        emitBr(endBlock);
    }

    setBlock(endBlock);
}

void Lowerer::lowerFor(const ForStmt &stmt)
{
    size_t headerBlock = createBlock("for_header");
    size_t bodyBlock = createBlock("for_body");
    size_t afterBlock = createBlock("for_after");
    size_t exitBlock = createBlock("for_exit");

    // Allocate loop variable if not already
    std::string key = toLower(stmt.loopVar);
    Value loopSlot;
    auto it = locals_.find(key);
    if (it == locals_.end())
    {
        loopSlot = emitAlloca(8);
        locals_[key] = loopSlot;
    }
    else
    {
        loopSlot = it->second;
    }

    // Initialize loop variable
    LowerResult startVal = lowerExpr(*stmt.start);
    emitStore(Type(Type::Kind::I64), loopSlot, startVal.value);

    // Evaluate bound once
    LowerResult boundVal = lowerExpr(*stmt.bound);
    Value bound = boundVal.value;

    emitBr(headerBlock);

    // Header: check condition
    setBlock(headerBlock);
    Value loopVal = emitLoad(Type(Type::Kind::I64), loopSlot);
    Value cond;
    if (stmt.direction == ForDirection::To)
    {
        cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), loopVal, bound);
    }
    else
    {
        cond = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), loopVal, bound);
    }
    emitCBr(cond, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, afterBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(afterBlock);
    loopStack_.pop();

    // After: increment/decrement (use overflow-checking variants for signed integers)
    setBlock(afterBlock);
    Value currentVal = emitLoad(Type(Type::Kind::I64), loopSlot);
    Value one = Value::constInt(1);
    Value newVal;
    if (stmt.direction == ForDirection::To)
    {
        newVal = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), currentVal, one);
    }
    else
    {
        newVal = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), currentVal, one);
    }
    emitStore(Type(Type::Kind::I64), loopSlot, newVal);
    emitBr(headerBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerForIn(const ForInStmt &stmt)
{
    // Desugar to index-based loop:
    // for item in arr do body  =>  for i := 0 to Length(arr)-1 do begin item := arr[i]; body end
    // for ch in s do body      =>  for i := 0 to Length(s)-1 do begin ch := s[i]; body end

    size_t headerBlock = createBlock("forin_header");
    size_t bodyBlock = createBlock("forin_body");
    size_t afterBlock = createBlock("forin_after");
    size_t exitBlock = createBlock("forin_exit");

    // Get collection type from semantic analyzer
    PasType collType = sema_->typeOf(*stmt.collection);
    bool isString = (collType.kind == PasTypeKind::String);
    bool isArray = (collType.kind == PasTypeKind::Array);

    // Allocate index variable
    Value indexSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::I64), indexSlot, Value::constInt(0));

    // Get collection value
    LowerResult collection = lowerExpr(*stmt.collection);

    // Get length based on collection type
    Value length;
    if (isString)
    {
        // Call rt_len for strings
        length = emitCallRet(Type(Type::Kind::I64), "rt_len", {collection.value});
    }
    else if (isArray)
    {
        // Call appropriate rt_arr_*_len based on element type
        // For now, use generic i64 array length as placeholder
        length = emitCallRet(Type(Type::Kind::I64), "rt_arr_i64_len", {collection.value});
    }
    else
    {
        // Fallback for unsupported types
        length = Value::constInt(0);
    }

    emitBr(headerBlock);

    // Header: check i < length
    setBlock(headerBlock);
    Value indexVal = emitLoad(Type(Type::Kind::I64), indexSlot);
    Value cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), indexVal, length);
    emitCBr(cond, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, afterBlock);
    setBlock(bodyBlock);

    // Allocate loop variable slot if not already present
    std::string key = toLower(stmt.loopVar);
    Value varSlot;
    if (locals_.find(key) == locals_.end())
    {
        varSlot = emitAlloca(8);
        locals_[key] = varSlot;
    }
    else
    {
        varSlot = locals_[key];
    }

    // Get element at current index and store in loop variable
    Value currentIdx = emitLoad(Type(Type::Kind::I64), indexSlot);
    if (isString)
    {
        // Get single character as a string: rt_substr(s, i, 1)
        Value elem = emitCallRet(Type(Type::Kind::Str), "rt_substr",
                                 {collection.value, currentIdx, Value::constInt(1)});
        emitStore(Type(Type::Kind::Str), varSlot, elem);
    }
    else if (isArray)
    {
        // Get array element: rt_arr_i64_get(arr, i)
        Value elem = emitCallRet(Type(Type::Kind::I64), "rt_arr_i64_get",
                                 {collection.value, currentIdx});
        emitStore(Type(Type::Kind::I64), varSlot, elem);
    }

    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(afterBlock);
    loopStack_.pop();

    // After: increment index (use overflow-checking addition)
    setBlock(afterBlock);
    Value idxAfter = emitLoad(Type(Type::Kind::I64), indexSlot);
    Value newIdx = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxAfter, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), indexSlot, newIdx);
    emitBr(headerBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    size_t headerBlock = createBlock("while_header");
    size_t bodyBlock = createBlock("while_body");
    size_t exitBlock = createBlock("while_exit");

    emitBr(headerBlock);

    // Header: evaluate condition
    setBlock(headerBlock);
    LowerResult cond = lowerExpr(*stmt.condition);
    emitCBr(cond.value, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, headerBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(headerBlock);
    loopStack_.pop();

    setBlock(exitBlock);
}

void Lowerer::lowerRepeat(const RepeatStmt &stmt)
{
    size_t bodyBlock = createBlock("repeat_body");
    size_t headerBlock = createBlock("repeat_header");
    size_t exitBlock = createBlock("repeat_exit");

    emitBr(bodyBlock);

    // Body (executes first)
    loopStack_.push(exitBlock, headerBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(headerBlock);
    loopStack_.pop();

    // Header: evaluate condition (until condition is true)
    setBlock(headerBlock);
    LowerResult cond = lowerExpr(*stmt.condition);
    // Repeat until: loop while condition is false
    emitCBr(cond.value, exitBlock, bodyBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerBreak(const BreakStmt &)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.breakTarget());
        // Create a dead block for any following code
        size_t deadBlock = createBlock("after_break");
        setBlock(deadBlock);
    }
}

void Lowerer::lowerContinue(const ContinueStmt &)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.continueTarget());
        // Create a dead block for any following code
        size_t deadBlock = createBlock("after_continue");
        setBlock(deadBlock);
    }
}

//===----------------------------------------------------------------------===//
// Instruction Emission Helpers
//===----------------------------------------------------------------------===//

Lowerer::Value Lowerer::emitAlloca(int64_t size)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Alloca;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands.push_back(Value::constInt(size));
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitLoad(Type ty, Value addr)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Load;
    instr.type = ty;
    instr.operands.push_back(addr);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    il::core::Instr instr;
    instr.op = Opcode::Store;
    instr.type = ty;
    instr.operands.push_back(addr);
    instr.operands.push_back(val);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

Lowerer::Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands.push_back(lhs);
    instr.operands.push_back(rhs);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitUnary(Opcode op, Type ty, Value operand)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands.push_back(operand);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitCallRet(Type retTy, const std::string &callee,
                                     const std::vector<Value> &args)
{
    // Track runtime externs for later declaration
    if (callee.substr(0, 3) == "rt_")
    {
        usedExterns_.insert(callee);
    }

    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Call;
    instr.type = retTy;
    instr.callee = callee;
    instr.operands = args;
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    // Track runtime externs for later declaration
    if (callee.substr(0, 3) == "rt_")
    {
        usedExterns_.insert(callee);
    }

    il::core::Instr instr;
    instr.op = Opcode::Call;
    instr.type = Type(Type::Kind::Void);
    instr.callee = callee;
    instr.operands = args;
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitBr(size_t targetIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
    instr.brArgs.push_back({});
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitCBr(Value cond, size_t trueIdx, size_t falseIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
    instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitRet(Value val)
{
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(val);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitRetVoid()
{
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

Lowerer::Value Lowerer::emitConstStr(const std::string &globalName)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global(globalName));
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitSitofp(Value intVal)
{
    return emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), intVal);
}

Lowerer::Value Lowerer::emitFptosi(Value floatVal)
{
    return emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), floatVal);
}

Lowerer::Value Lowerer::emitZext1(Value boolVal)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), boolVal);
}

Lowerer::Value Lowerer::emitTrunc1(Value intVal)
{
    return emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), intVal);
}

Lowerer::Value Lowerer::emitGep(Value base, Value offset)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::GEP;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands = {base, offset};
    currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

unsigned Lowerer::nextTempId()
{
    return builder_->reserveTempId();
}

//===----------------------------------------------------------------------===//
// Exception Handling Lowering
//===----------------------------------------------------------------------===//

size_t Lowerer::createHandlerBlock(const std::string &base)
{
    std::ostringstream oss;
    oss << base << "_" << blockCounter_++;
    builder_->createBlock(*currentFunc_, oss.str());
    size_t idx = currentFunc_->blocks.size() - 1;

    // Add handler parameters: %err : Error, %tok : ResumeTok
    BasicBlock &blk = currentFunc_->blocks[idx];
    unsigned errId = nextTempId();
    unsigned tokId = nextTempId();

    il::core::Param errParam;
    errParam.name = "err";
    errParam.type = Type(Type::Kind::Error);
    errParam.id = errId;
    blk.params.push_back(std::move(errParam));

    il::core::Param tokParam;
    tokParam.name = "tok";
    tokParam.type = Type(Type::Kind::ResumeTok);
    tokParam.id = tokId;
    blk.params.push_back(std::move(tokParam));

    return idx;
}

void Lowerer::emitEhPush(size_t handlerBlockIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::EhPush;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[handlerBlockIdx].label);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitEhPop()
{
    il::core::Instr instr;
    instr.op = Opcode::EhPop;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitResumeSame(Value resumeTok)
{
    il::core::Instr instr;
    instr.op = Opcode::ResumeSame;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(resumeTok);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitResumeLabel(Value resumeTok, size_t targetBlockIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::ResumeLabel;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(resumeTok);
    instr.labels.push_back(currentFunc_->blocks[targetBlockIdx].label);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::lowerRaise(const RaiseStmt &stmt)
{
    if (stmt.exception)
    {
        // raise Expr; - evaluate expression and call rt_throw
        LowerResult excVal = lowerExpr(*stmt.exception);
        emitCall("rt_throw", {excVal.value});
        // rt_throw does not return, but we still need to mark block as unterminated
        // for subsequent code (which is dead but may exist)
    }
    else
    {
        // raise; (re-raise) - only valid inside except handler
        // Use ResumeSame with the current handler's resume token
        if (inExceptHandler_)
        {
            emitResumeSame(currentResumeTok_);
        }
        // If not in handler, semantic analysis should have caught this error
    }
}

void Lowerer::lowerExit(const ExitStmt &stmt)
{
    if (stmt.value)
    {
        // Exit(value) - store value in Result slot and return
        LowerResult value = lowerExpr(*stmt.value);

        // Store to the function's result variable (named after function)
        if (!currentFuncName_.empty())
        {
            auto it = locals_.find(currentFuncName_);
            if (it != locals_.end())
            {
                // Get return type
                auto retType = sema_->lookupVariable(currentFuncName_);
                Type ilType = retType ? mapType(*retType) : value.type;
                emitStore(ilType, it->second, value.value);
            }
        }
    }

    // Emit return
    if (!currentFuncName_.empty())
    {
        // Function - load and return Result value
        auto it = locals_.find(currentFuncName_);
        if (it != locals_.end())
        {
            auto retType = sema_->lookupVariable(currentFuncName_);
            Type ilType = retType ? mapType(*retType) : Type(Type::Kind::I64);
            Value retVal = emitLoad(ilType, it->second);
            emitRet(retVal);
        }
        else
        {
            // Fallback for void return
            emitRetVoid();
        }
    }
    else
    {
        // Procedure or fallback - void return
        emitRetVoid();
    }

    // Create a dead block for any following code (like Break/Continue)
    size_t deadBlock = createBlock("after_exit");
    setBlock(deadBlock);
}

void Lowerer::lowerTryExcept(const TryExceptStmt &stmt)
{
    // Create blocks:
    // - handler: receives exception, dispatches to matching handler or propagates
    // - after: continuation after try-except
    size_t handlerIdx = createHandlerBlock("except_handler");
    size_t afterIdx = createBlock("except_after");

    // Get handler params for later use
    BasicBlock &handlerBlk = currentFunc_->blocks[handlerIdx];
    Value errParam = Value::temp(handlerBlk.params[0].id);
    Value tokParam = Value::temp(handlerBlk.params[1].id);

    // In current block: EhPush, then branch to try body
    size_t tryBodyIdx = createBlock("try_body");
    emitEhPush(handlerIdx);
    emitBr(tryBodyIdx);

    // Lower try body
    setBlock(tryBodyIdx);
    if (stmt.tryBody)
    {
        lowerBlock(*stmt.tryBody);
    }

    // Normal exit: EhPop and branch to after
    if (!currentBlock()->terminated)
    {
        emitEhPop();
        emitBr(afterIdx);
    }

    // Lower handler block: dispatch to matching handlers
    setBlock(handlerIdx);

    // Save previous handler state and set current
    bool prevInHandler = inExceptHandler_;
    Value prevResumeTok = currentResumeTok_;
    inExceptHandler_ = true;
    currentResumeTok_ = tokParam;

    // For each handler: check type and dispatch
    // Pascal "on E: Type do" checks if exception is of Type
    // For simplicity, we'll use a cascade of type checks

    std::vector<size_t> handlerBodyIdxs;
    for (size_t i = 0; i < stmt.handlers.size(); ++i)
    {
        handlerBodyIdxs.push_back(createBlock("handler_body"));
    }
    size_t elseIdx = stmt.elseBody ? createBlock("except_else") : afterIdx;

    // Build type-check cascade
    for (size_t i = 0; i < stmt.handlers.size(); ++i)
    {
        const ExceptHandler &h = stmt.handlers[i];
        size_t nextCheck = (i + 1 < stmt.handlers.size()) ? createBlock("handler_check")
                                                          : elseIdx;

        // Check if exception type matches handler type
        // For now, use a simplified check: call rt_exc_is_type(err, "TypeName")
        std::string typeGlobal = getStringGlobal(h.typeName);
        Value typeStr = emitConstStr(typeGlobal);
        Value isMatch = emitCallRet(Type(Type::Kind::I1), "rt_exc_is_type",
                                    {errParam, typeStr});
        emitCBr(isMatch, handlerBodyIdxs[i], nextCheck);

        if (i + 1 < stmt.handlers.size())
        {
            setBlock(nextCheck);
        }
    }

    // Lower each handler body
    for (size_t i = 0; i < stmt.handlers.size(); ++i)
    {
        const ExceptHandler &h = stmt.handlers[i];
        setBlock(handlerBodyIdxs[i]);

        // Bind exception variable if named
        if (!h.varName.empty())
        {
            std::string key = toLower(h.varName);
            Value slot = emitAlloca(8);
            locals_[key] = slot;
            emitStore(Type(Type::Kind::Ptr), slot, errParam);
        }

        // Lower handler body
        if (h.body)
        {
            lowerStmt(*h.body);
        }

        // Exit handler: ResumeLabel to after
        if (!currentBlock()->terminated)
        {
            emitResumeLabel(tokParam, afterIdx);
        }
    }

    // Lower else body if present
    if (stmt.elseBody)
    {
        setBlock(elseIdx);
        lowerStmt(*stmt.elseBody);
        if (!currentBlock()->terminated)
        {
            emitResumeLabel(tokParam, afterIdx);
        }
    }

    // If no handler matched and no else, propagate exception
    if (!stmt.elseBody && stmt.handlers.empty())
    {
        setBlock(handlerIdx);
        emitResumeSame(tokParam);
    }
    else if (!stmt.elseBody)
    {
        // After checking all handlers, if none matched, propagate
        setBlock(elseIdx);
        emitResumeSame(tokParam);
    }

    // Restore handler state
    inExceptHandler_ = prevInHandler;
    currentResumeTok_ = prevResumeTok;

    // Continue at after block
    setBlock(afterIdx);
}

void Lowerer::lowerTryFinally(const TryFinallyStmt &stmt)
{
    // Create blocks:
    // - handler: receives exception, runs finally, propagates
    // - finally_normal: runs finally on normal path
    // - after: continuation after try-finally
    size_t handlerIdx = createHandlerBlock("finally_handler");
    size_t finallyNormalIdx = createBlock("finally_normal");
    size_t afterIdx = createBlock("finally_after");

    // Get handler params
    BasicBlock &handlerBlk = currentFunc_->blocks[handlerIdx];
    Value tokParam = Value::temp(handlerBlk.params[1].id);

    // In current block: EhPush, then branch to try body
    size_t tryBodyIdx = createBlock("try_body");
    emitEhPush(handlerIdx);
    emitBr(tryBodyIdx);

    // Lower try body
    setBlock(tryBodyIdx);
    if (stmt.tryBody)
    {
        lowerBlock(*stmt.tryBody);
    }

    // Normal exit: EhPop and branch to finally_normal
    if (!currentBlock()->terminated)
    {
        emitEhPop();
        emitBr(finallyNormalIdx);
    }

    // finally_normal: run finally body, then branch to after
    setBlock(finallyNormalIdx);
    if (stmt.finallyBody)
    {
        lowerBlock(*stmt.finallyBody);
    }
    if (!currentBlock()->terminated)
    {
        emitBr(afterIdx);
    }

    // Handler: run finally body, then propagate exception
    setBlock(handlerIdx);
    if (stmt.finallyBody)
    {
        lowerBlock(*stmt.finallyBody);
    }
    if (!currentBlock()->terminated)
    {
        emitResumeSame(tokParam);
    }

    // Continue at after block
    setBlock(afterIdx);
}

} // namespace il::frontends::pascal
