//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr_Access.cpp
// Purpose: Array index and field access expression lowering for Pascal AST to IL.
// Key invariants: Computes correct field offsets using class layout.
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

LowerResult Lowerer::lowerIndex(const IndexExpr &expr)
{
    // Get base type
    PasType baseType = typeOfExpr(*expr.base);

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
                Value offset = emitBinary(
                    Opcode::IMulOvf, Type(Type::Kind::I64), index.value, Value::constInt(elemSize));

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

std::pair<Lowerer::Value, Lowerer::Type> Lowerer::getFieldAddress(Value baseAddr,
                                                                  const PasType &baseType,
                                                                  const std::string &fieldName)
{
    std::string fieldKey = toLower(fieldName);

    // For class types, use the computed class layout which accounts for vptr
    if (baseType.kind == PasTypeKind::Class)
    {
        std::string classKey = toLower(baseType.name);
        auto layoutIt = classLayouts_.find(classKey);
        if (layoutIt != classLayouts_.end())
        {
            const ClassLayout &layout = layoutIt->second;
            for (const auto &field : layout.fields)
            {
                if (toLower(field.name) == fieldKey)
                {
                    Type fieldType = mapType(field.type);
                    Value fieldAddr =
                        emitGep(baseAddr, Value::constInt(static_cast<long long>(field.offset)));
                    return {fieldAddr, fieldType};
                }
            }
        }
    }

    // Fallback for records and other types: calculate field offset by iterating
    // Fields are stored in a map, using alphabetical order
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

    // Check for zero-argument constructor call without parentheses: ClassName.Create
    // This happens in Pascal when calling a parameterless constructor
    if (expr.base->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
        std::string baseName = toLower(nameExpr.name);

        // Check if base is a class type (not a variable)
        if (!locals_.count(baseName))
        {
            auto typeOpt = sema_->lookupType(baseName);
            if (typeOpt && typeOpt->kind == PasTypeKind::Class)
            {
                // This might be a constructor call: ClassName.Create
                auto *classInfo = sema_->lookupClass(baseName);
                if (classInfo)
                {
                    std::string methodKey = toLower(expr.field);
                    auto methodIt = classInfo->methods.find(methodKey);
                    if (methodIt != classInfo->methods.end())
                    {
                        // Found a method - check if it's a constructor
                        // Constructors are typically named "Create" and return void
                        // Treat this as a zero-argument constructor call

                        // Create a synthetic CallExpr for lowering
                        CallExpr syntheticCall(nullptr, {}, expr.loc);
                        syntheticCall.isConstructorCall = true;
                        syntheticCall.constructorClassName = typeOpt->name;

                        // Copy callee from the field expression to get the method name
                        syntheticCall.callee = std::make_unique<FieldExpr>(
                            std::make_unique<NameExpr>(nameExpr.name, nameExpr.loc),
                            expr.field,
                            expr.loc);

                        return lowerConstructorCall(syntheticCall);
                    }
                }
            }
        }
    }

    // Get base type
    PasType baseType = typeOfExpr(*expr.base);

    // Handle interface method call (parameterless function)
    if (baseType.kind == PasTypeKind::Interface)
    {
        // This is an interface method call like "animal.GetName" (for a function)
        // We treat it as a zero-argument call through the interface
        std::string ifaceName = baseType.name;
        std::string methodName = expr.field;

        // Get interface info
        const InterfaceInfo *ifaceInfo = sema_->lookupInterface(toLower(ifaceName));
        if (ifaceInfo)
        {
            auto methodIt = ifaceInfo->methods.find(toLower(methodName));
            if (methodIt != ifaceInfo->methods.end())
            {
                // Create a synthetic CallExpr for the interface method call
                // We don't need to set callee since we already have the FieldExpr
                CallExpr syntheticCall(nullptr, {}, expr.loc);
                syntheticCall.isInterfaceCall = true;
                syntheticCall.interfaceName = ifaceName;

                return lowerInterfaceMethodCall(expr, syntheticCall);
            }
        }
    }

    if (baseType.kind == PasTypeKind::Record)
    {
        // Records are stored inline in the variable's slot
        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);

            // Check local variables first
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;
                auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, expr.field);

                // Load the field value
                Value result = emitLoad(fieldType, fieldAddr);
                return {result, fieldType};
            }

            // Check global variables (BUG-PAS-OOP-003 fix)
            auto globalIt = globalTypes_.find(key);
            if (globalIt != globalTypes_.end())
            {
                Value baseAddr = getGlobalVarAddr(key, globalIt->second);
                auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, expr.field);

                // Load the field value
                Value result = emitLoad(fieldType, fieldAddr);
                return {result, fieldType};
            }
        }
        // Handle nested field access (e.g., a.b.c)
        // For now, fall through to default handling - nested record fields are rare
    }
    else if (baseType.kind == PasTypeKind::Class)
    {
        // Classes are reference types - the variable's slot contains a pointer to the object
        Value objPtr = Value::null(); // Sentinel value
        bool foundObjPtr = false;

        if (expr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                // Load the object pointer from the variable's slot
                objPtr = emitLoad(Type(Type::Kind::Ptr), it->second);
                foundObjPtr = true;
            }
            // Check for global class variable
            if (!foundObjPtr)
            {
                auto globalIt = globalTypes_.find(key);
                if (globalIt != globalTypes_.end())
                {
                    // Load the object pointer from the global variable
                    Value globalAddr = getGlobalVarAddr(key, globalIt->second);
                    objPtr = emitLoad(Type(Type::Kind::Ptr), globalAddr);
                    foundObjPtr = true;
                }
            }
            if (!foundObjPtr && !currentClassName_.empty())
            {
                // Check if it's a class field accessed inside a method
                auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
                if (classInfo)
                {
                    auto fieldIt = classInfo->fields.find(key);
                    if (fieldIt != classInfo->fields.end())
                    {
                        // Access Self.fieldName to get the object pointer
                        auto selfIt = locals_.find("self");
                        if (selfIt != locals_.end())
                        {
                            Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                            // Build type with fields for getFieldAddress
                            PasType selfType = PasType::classType(currentClassName_);
                            for (const auto &[fname, finfo] : classInfo->fields)
                            {
                                selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                            }
                            auto [fieldAddr, fieldType] =
                                getFieldAddress(selfPtr, selfType, nameExpr.name);

                            // Load the field value (which is a pointer to another object)
                            objPtr = emitLoad(Type(Type::Kind::Ptr), fieldAddr);
                            foundObjPtr = true;
                        }
                    }
                }
            }

            if (!foundObjPtr)
            {
                return {Value::constInt(0), Type(Type::Kind::I64)};
            }
        }
        else if (expr.base->kind == ExprKind::Field)
        {
            // Nested field access: a.b.c where a.b is a class-typed field
            // Recursively lower the base to get the object pointer
            LowerResult baseResult = lowerField(static_cast<const FieldExpr &>(*expr.base));
            objPtr = baseResult.value;
            // The result of lowerField should be a pointer to the nested object
        }
        else
        {
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        // Check if this is a property access or a zero-argument method call (Pascal allows calling
        // without parens)
        auto *classInfo = sema_->lookupClass(toLower(baseType.name));
        if (classInfo)
        {
            std::string methodKey = toLower(expr.field);
            // 1) Property read lowering - check current class and base classes
            const PropertyInfo *foundProperty = nullptr;
            std::string definingClassName;
            {
                std::string cur = toLower(baseType.name);
                while (!cur.empty())
                {
                    auto *ci = sema_->lookupClass(cur);
                    if (!ci)
                        break;
                    auto pit = ci->properties.find(methodKey);
                    if (pit != ci->properties.end())
                    {
                        foundProperty = &pit->second;
                        definingClassName = ci->name;
                        break;
                    }
                    if (ci->baseClass.empty())
                        break;
                    cur = toLower(ci->baseClass);
                }
            }
            if (foundProperty)
            {
                const auto &p = *foundProperty;
                // Getter via method
                if (p.getter.kind == PropertyAccessor::Kind::Method)
                {
                    std::string funcName = definingClassName + "." + p.getter.name;
                    Type retType = mapType(p.type);
                    Value result = emitCallRet(retType, funcName, {objPtr});
                    return {result, retType};
                }
                // Getter via field
                if (p.getter.kind == PropertyAccessor::Kind::Field)
                {
                    // Build class type with fields from the defining class
                    auto *defClassInfo = sema_->lookupClass(toLower(definingClassName));
                    PasType classTypeWithFields = PasType::classType(definingClassName);
                    if (defClassInfo)
                    {
                        for (const auto &[fname, finfo] : defClassInfo->fields)
                        {
                            classTypeWithFields.fields[fname] =
                                std::make_shared<PasType>(finfo.type);
                        }
                    }
                    auto [fieldAddr, fieldType] =
                        getFieldAddress(objPtr, classTypeWithFields, p.getter.name);
                    Value result = emitLoad(fieldType, fieldAddr);
                    return {result, fieldType};
                }
            }

            // 2) Zero-arg method sugar
            const MethodInfo *methodInfo = classInfo->findMethod(methodKey);
            if (methodInfo)
            {
                // This is a method - check if it can be called with zero arguments
                if (methodInfo->requiredParams == 0 &&
                    methodInfo->returnType.kind != PasTypeKind::Void)
                {
                    // Call the method with just the Self pointer
                    std::string methodName = baseType.name + "." + expr.field;
                    Type retType = mapType(methodInfo->returnType);
                    Value result = emitCallRet(retType, methodName, {objPtr});
                    return {result, retType};
                }
            }

            // Not a method or method requires arguments - check for field access
            PasType classTypeWithFields = baseType;
            for (const auto &[fname, finfo] : classInfo->fields)
            {
                classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
            }

            auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, expr.field);

            // Load the field value
            Value result = emitLoad(fieldType, fieldAddr);
            return {result, fieldType};
        }

        // No class info - fall through to default field access
        auto [fieldAddr, fieldType] = getFieldAddress(objPtr, baseType, expr.field);
        Value result = emitLoad(fieldType, fieldAddr);
        return {result, fieldType};
    }

    // Fallback
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::pascal
