//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Expr.cpp
// Purpose: Expression type checking.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive comparison
using common::char_utils::toLowercase;

// Alias for compatibility with existing code
inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Expression Type Checking
//===----------------------------------------------------------------------===//

PasType SemanticAnalyzer::typeOf(Expr &expr)
{
    switch (expr.kind)
    {
        case ExprKind::IntLiteral:
            return typeOfIntLiteral(static_cast<IntLiteralExpr &>(expr));
        case ExprKind::RealLiteral:
            return typeOfRealLiteral(static_cast<RealLiteralExpr &>(expr));
        case ExprKind::StringLiteral:
            return typeOfStringLiteral(static_cast<StringLiteralExpr &>(expr));
        case ExprKind::BoolLiteral:
            return typeOfBoolLiteral(static_cast<BoolLiteralExpr &>(expr));
        case ExprKind::NilLiteral:
            return typeOfNil(static_cast<NilLiteralExpr &>(expr));
        case ExprKind::Name:
            return typeOfName(static_cast<NameExpr &>(expr));
        case ExprKind::Unary:
            return typeOfUnary(static_cast<UnaryExpr &>(expr));
        case ExprKind::Binary:
            return typeOfBinary(static_cast<BinaryExpr &>(expr));
        case ExprKind::Call:
            return typeOfCall(static_cast<CallExpr &>(expr));
        case ExprKind::Index:
            return typeOfIndex(static_cast<IndexExpr &>(expr));
        case ExprKind::Field:
            return typeOfField(static_cast<FieldExpr &>(expr));
        case ExprKind::TypeCast:
            return typeOfTypeCast(static_cast<TypeCastExpr &>(expr));
        case ExprKind::Is:
            return typeOfIs(static_cast<IsExpr &>(expr));
        case ExprKind::SetConstructor:
            return typeOfSetConstructor(static_cast<SetConstructorExpr &>(expr));
        case ExprKind::AddressOf:
            return typeOfAddressOf(static_cast<AddressOfExpr &>(expr));
        case ExprKind::Dereference:
            return typeOfDereference(static_cast<DereferenceExpr &>(expr));
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfIntLiteral(IntLiteralExpr & /*expr*/)
{
    return PasType::integer();
}

PasType SemanticAnalyzer::typeOfRealLiteral(RealLiteralExpr & /*expr*/)
{
    return PasType::real();
}

PasType SemanticAnalyzer::typeOfStringLiteral(StringLiteralExpr & /*expr*/)
{
    return PasType::string();
}

PasType SemanticAnalyzer::typeOfBoolLiteral(BoolLiteralExpr & /*expr*/)
{
    return PasType::boolean();
}

PasType SemanticAnalyzer::typeOfNil(NilLiteralExpr & /*expr*/)
{
    return PasType::nil();
}

PasType SemanticAnalyzer::typeOfName(NameExpr &expr)
{
    std::string key = toLower(expr.name);

    // Check if the variable is undefined (e.g., for loop variable after loop ends)
    if (undefinedVars_.count(key))
    {
        error(expr, "loop variable '" + expr.name + "' is undefined after loop terminates");
        return PasType::unknown();
    }

    // Check definite assignment for non-nullable reference locals
    if (uninitializedNonNullableVars_.count(key))
    {
        error(expr, "variable '" + expr.name + "' may not have been initialized");
        return PasType::unknown();
    }

    // Check variables first, using effective type (respects narrowing)
    if (auto type = lookupEffectiveType(key))
    {
        return *type;
    }

    // Special-case 'self' in method bodies: treat as current class type
    if (key == "self")
    {
        if (!currentClassName_.empty())
            return PasType::classType(currentClassName_);
        // Fallback: if Self was registered explicitly, use its type
        if (auto selfTy = lookupVariable("self"))
            return *selfTy;
    }

    // Check 'with' contexts (innermost first, so search from back)
    // Name lookup order: locals > with contexts > class members > globals
    for (auto it = withContexts_.rbegin(); it != withContexts_.rend(); ++it)
    {
        const WithContext &ctx = *it;
        if (ctx.type.kind == PasTypeKind::Class)
        {
            auto *classInfo = lookupClass(toLower(ctx.type.name));
            if (classInfo)
            {
                // Check fields
                auto fieldIt = classInfo->fields.find(key);
                if (fieldIt != classInfo->fields.end())
                {
                    return fieldIt->second.type;
                }
                // Check properties
                auto propIt = classInfo->properties.find(key);
                if (propIt != classInfo->properties.end())
                {
                    return propIt->second.type;
                }
            }
        }
        else if (ctx.type.kind == PasTypeKind::Record)
        {
            auto fieldIt = ctx.type.fields.find(key);
            if (fieldIt != ctx.type.fields.end() && fieldIt->second)
            {
                return *fieldIt->second;
            }
        }
    }

    // Inside a method, check fields and properties of the current class (and base classes) next
    // Name lookup order inside methods:
    // 1) Locals/params (handled above)
    // 2) 'with' contexts (handled above)
    // 3) Fields of current class and its base classes
    // 4) Properties of current class and its base classes
    // 5) Outer scopes/globals/builtins
    if (!currentClassName_.empty())
    {
        // Walk up the inheritance chain to find a matching field
        std::string cur = toLower(currentClassName_);
        while (!cur.empty())
        {
            auto *classInfo = lookupClass(cur);
            if (!classInfo)
                break;
            auto fieldIt = classInfo->fields.find(key);
            if (fieldIt != classInfo->fields.end())
            {
                return fieldIt->second.type;
            }
            // Move to base class (if any)
            if (classInfo->baseClass.empty())
                break;
            cur = toLower(classInfo->baseClass);
        }

        // Walk up the inheritance chain to find a matching property
        cur = toLower(currentClassName_);
        while (!cur.empty())
        {
            auto *classInfo = lookupClass(cur);
            if (!classInfo)
                break;
            auto propIt = classInfo->properties.find(key);
            if (propIt != classInfo->properties.end())
            {
                return propIt->second.type;
            }
            // Move to base class (if any)
            if (classInfo->baseClass.empty())
                break;
            cur = toLower(classInfo->baseClass);
        }
    }
    else
    {
        // Fallback: if a 'self' variable exists, use its class type to resolve fields and
        // properties
        if (auto selfTy = lookupVariable("self"))
        {
            if (selfTy->kind == PasTypeKind::Class && !selfTy->name.empty())
            {
                std::string cur = toLower(selfTy->name);
                while (!cur.empty())
                {
                    auto *classInfo = lookupClass(cur);
                    if (!classInfo)
                        break;
                    auto fieldIt = classInfo->fields.find(key);
                    if (fieldIt != classInfo->fields.end())
                    {
                        return fieldIt->second.type;
                    }
                    if (classInfo->baseClass.empty())
                        break;
                    cur = toLower(classInfo->baseClass);
                }
                // Try properties
                cur = toLower(selfTy->name);
                while (!cur.empty())
                {
                    auto *classInfo = lookupClass(cur);
                    if (!classInfo)
                        break;
                    auto propIt = classInfo->properties.find(key);
                    if (propIt != classInfo->properties.end())
                    {
                        return propIt->second.type;
                    }
                    if (classInfo->baseClass.empty())
                        break;
                    cur = toLower(classInfo->baseClass);
                }
            }
        }
    }

    // Check constants
    if (auto type = lookupConstant(key))
    {
        return *type;
    }

    // Check if it's a type name (for type references)
    if (auto type = lookupType(key))
    {
        return *type;
    }

    // Check for zero-argument builtin functions (Pascal allows calling without parens)
    if (auto builtinOpt = lookupBuiltin(key))
    {
        const auto &desc = getBuiltinDescriptor(*builtinOpt);
        // Only allow if it can be called with 0 args and has non-void return type
        if (desc.minArgs == 0 && desc.result != ResultKind::Void)
        {
            return getBuiltinResultType(*builtinOpt);
        }
    }

    // Check for zero-argument user-defined functions (Pascal allows calling without parens)
    if (auto sig = lookupFunction(key))
    {
        // Only allow if it can be called with 0 args and has non-void return type
        if (sig->requiredParams == 0 && sig->returnType.kind != PasTypeKind::Void)
        {
            return sig->returnType;
        }
    }

    error(expr, "undefined identifier '" + expr.name + "'");
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfUnary(UnaryExpr &expr)
{
    if (!expr.operand)
        return PasType::unknown();

    PasType operandType = typeOf(*expr.operand);
    PasType result = unaryResultType(expr.op, operandType);
    if (result.isError() && !operandType.isError())
    {
        switch (expr.op)
        {
            case UnaryExpr::Op::Not:
                error(expr, "operand must be Boolean for 'not'");
                break;
            case UnaryExpr::Op::Neg:
            case UnaryExpr::Op::Plus:
                error(expr, "operand must be numeric for unary +/-");
                break;
        }
    }
    return result;
}

PasType SemanticAnalyzer::typeOfBinary(BinaryExpr &expr)
{
    if (!expr.left || !expr.right)
        return PasType::unknown();

    PasType leftType = typeOf(*expr.left);
    PasType rightType = typeOf(*expr.right);

    PasType result = binaryResultType(expr.op, leftType, rightType);
    if (result.isError() && !leftType.isError() && !rightType.isError())
    {
        // Report the specific error based on operator
        switch (expr.op)
        {
            case BinaryExpr::Op::And:
            case BinaryExpr::Op::Or:
                error(expr, "operands must be Boolean for 'and'/'or'");
                break;
            case BinaryExpr::Op::IntDiv:
            case BinaryExpr::Op::Mod:
                error(expr, "operands must be Integer for 'div'/'mod'");
                break;
            case BinaryExpr::Op::Eq:
            case BinaryExpr::Op::Ne:
                // Check if this was a nil comparison with non-optional reference type
                if ((leftType.kind == PasTypeKind::Nil &&
                     (rightType.kind == PasTypeKind::Class ||
                      rightType.kind == PasTypeKind::Interface)) ||
                    (rightType.kind == PasTypeKind::Nil &&
                     (leftType.kind == PasTypeKind::Class ||
                      leftType.kind == PasTypeKind::Interface)))
                {
                    error(expr, "non-optional class cannot be compared to nil");
                }
                else
                {
                    error(expr, "type mismatch in comparison");
                }
                break;
            case BinaryExpr::Op::Coalesce:
                error(expr, "type mismatch in nil coalescing expression");
                break;
            default:
                error(expr, "type mismatch in binary expression");
                break;
        }
    }
    return result;
}

PasType SemanticAnalyzer::typeOfCall(CallExpr &expr)
{
    if (!expr.callee)
        return PasType::unknown();

    // Get callee name and signature
    std::string calleeName;
    const FuncSignature *sig = nullptr;
    bool isMethodCall = false;
    std::string className;

    if (expr.callee->kind == ExprKind::Name)
    {
        calleeName = static_cast<NameExpr &>(*expr.callee).name;

        // Type-cast form: TClass(expr)
        // If the callee name is a type and that type is a class/interface, treat this as a cast
        std::string calleeKey = toLower(calleeName);
        if (auto typeOpt = lookupType(calleeKey))
        {
            if (typeOpt->kind == PasTypeKind::Class || typeOpt->kind == PasTypeKind::Interface)
            {
                // Expect exactly 1 argument
                if (expr.args.size() != 1)
                {
                    error(expr, "type cast requires exactly one argument");
                    // Still return the target type for error recovery
                    return *typeOpt;
                }

                // Type-check the operand (allow nil)
                PasType argType = typeOf(*expr.args[0]);
                // For class/interface casts, operand should be a class or interface or nil
                if (!(argType.kind == PasTypeKind::Class ||
                      argType.kind == PasTypeKind::Interface || argType.kind == PasTypeKind::Nil ||
                      argType.kind == PasTypeKind::Unknown))
                {
                    error(*expr.args[0], "invalid cast: expected class or interface instance");
                }

                // Result type is the target type
                return *typeOpt;
            }
        }

        // Implicit method call on Self inside a method: MethodName(args)
        if (!currentClassName_.empty())
        {
            auto *classInfo = lookupClass(toLower(currentClassName_));
            if (classInfo)
            {
                std::string mkey = toLower(calleeName);
                auto mit = classInfo->methods.find(mkey);
                if (mit != classInfo->methods.end())
                {
                    const MethodInfo &minfo = mit->second;
                    // Reject abstract methods
                    if (minfo.isAbstract)
                    {
                        error(expr, "cannot call abstract method '" + calleeName + "'");
                        return PasType::unknown();
                    }

                    // Check argument counts
                    size_t totalParams = minfo.params.size();
                    size_t requiredParams = minfo.requiredParams;
                    size_t actual = expr.args.size();
                    if (actual < requiredParams)
                    {
                        error(expr,
                              "too few arguments: expected at least " +
                                  std::to_string(requiredParams) + ", got " +
                                  std::to_string(actual));
                    }
                    else if (actual > totalParams)
                    {
                        error(expr,
                              "too many arguments: expected at most " +
                                  std::to_string(totalParams) + ", got " + std::to_string(actual));
                    }

                    // Type-check arguments
                    for (size_t i = 0; i < expr.args.size() && i < minfo.params.size(); ++i)
                    {
                        if (expr.args[i])
                        {
                            PasType argType = typeOf(*expr.args[i]);
                            const PasType &paramType = minfo.params[i].second;
                            if (!paramType.isError() && !isAssignableFrom(paramType, argType) &&
                                !argType.isError())
                            {
                                error(*expr.args[i],
                                      "argument " + std::to_string(i + 1) +
                                          " type mismatch: expected " + paramType.toString() +
                                          ", got " + argType.toString());
                            }
                        }
                    }
                    return minfo.returnType;
                }
            }
        }

        // Check 'with' contexts for method calls (innermost first)
        for (auto it = withContexts_.rbegin(); it != withContexts_.rend(); ++it)
        {
            const WithContext &ctx = *it;
            if (ctx.type.kind == PasTypeKind::Class)
            {
                auto *classInfo = lookupClass(toLower(ctx.type.name));
                if (classInfo)
                {
                    std::string mkey = toLower(calleeName);
                    auto mit = classInfo->methods.find(mkey);
                    if (mit != classInfo->methods.end())
                    {
                        const MethodInfo &minfo = mit->second;
                        // Reject abstract methods
                        if (minfo.isAbstract)
                        {
                            error(expr, "cannot call abstract method '" + calleeName + "'");
                            return PasType::unknown();
                        }

                        // Mark the expression for the lowerer - it's a method call through with
                        expr.isWithMethodCall = true;
                        expr.withClassName = ctx.type.name;

                        // Check argument counts
                        size_t totalParams = minfo.params.size();
                        size_t requiredParams = minfo.requiredParams;
                        size_t actual = expr.args.size();
                        if (actual < requiredParams)
                        {
                            error(expr,
                                  "too few arguments: expected at least " +
                                      std::to_string(requiredParams) + ", got " +
                                      std::to_string(actual));
                        }
                        else if (actual > totalParams)
                        {
                            error(expr,
                                  "too many arguments: expected at most " +
                                      std::to_string(totalParams) + ", got " +
                                      std::to_string(actual));
                        }

                        // Type-check arguments
                        for (size_t i = 0; i < expr.args.size() && i < minfo.params.size(); ++i)
                        {
                            if (expr.args[i])
                            {
                                PasType argType = typeOf(*expr.args[i]);
                                const PasType &paramType = minfo.params[i].second;
                                if (!paramType.isError() && !isAssignableFrom(paramType, argType) &&
                                    !argType.isError())
                                {
                                    error(*expr.args[i],
                                          "argument " + std::to_string(i + 1) +
                                              " type mismatch: expected " + paramType.toString() +
                                              ", got " + argType.toString());
                                }
                            }
                        }
                        return minfo.returnType;
                    }
                }
            }
        }
    }
    else if (expr.callee->kind == ExprKind::Field)
    {
        // Method call or constructor call - get the method name and receiver type
        auto &fieldExpr = static_cast<FieldExpr &>(*expr.callee);
        calleeName = fieldExpr.field;
        isMethodCall = true;

        // Check if the base is a type reference (for constructor calls like TClassName.Create)
        bool isConstructorCall = false;
        if (fieldExpr.base && fieldExpr.base->kind == ExprKind::Name)
        {
            const auto &baseName = static_cast<const NameExpr &>(*fieldExpr.base);
            std::string baseKey = toLower(baseName.name);

            // Check if this is a type name (not a variable)
            if (!lookupVariable(baseKey) && !lookupConstant(baseKey))
            {
                if (auto typeOpt = lookupType(baseKey))
                {
                    if (typeOpt->kind == PasTypeKind::Class)
                    {
                        // This is a constructor call: ClassName.Create()
                        isConstructorCall = true;
                        className = typeOpt->name;

                        // Mark the expression for the lowerer
                        expr.isConstructorCall = true;
                        expr.constructorClassName = className;

                        // Reject instantiation of abstract classes
                        if (isAbstractClass(className))
                        {
                            error(expr, "cannot instantiate abstract class '" + className + "'");
                            return PasType::unknown();
                        }

                        // Look up the constructor in the class
                        auto *classInfo = lookupClass(baseKey);
                        if (classInfo)
                        {
                            std::string methodKey = toLower(calleeName);
                            auto methodIt = classInfo->methods.find(methodKey);
                            if (methodIt != classInfo->methods.end())
                            {
                                if (methodIt->second.isAbstract)
                                {
                                    error(expr, "cannot call abstract method '" + calleeName + "'");
                                    return PasType::unknown();
                                }
                                // Constructor found - return the class type (new instance)
                                // Type-check arguments
                                for (auto &arg : expr.args)
                                {
                                    if (arg)
                                        typeOf(*arg);
                                }
                                return PasType::classType(className);
                            }
                            else
                            {
                                error(expr,
                                      "class '" + className + "' has no constructor named '" +
                                          calleeName + "'");
                                return PasType::unknown();
                            }
                        }
                    }
                }
            }
        }

        // Regular method call on an instance
        if (!isConstructorCall)
        {
            // Type-check the receiver
            PasType receiverType = typeOf(*fieldExpr.base);

            // Look up the method in the class
            if (receiverType.kind == PasTypeKind::Class)
            {
                className = receiverType.name;
                // Methods are stored with qualified keys
                std::string qualifiedKey = toLower(className + "." + calleeName);
                sig = lookupFunction(qualifiedKey);

                // If not found directly, check the class info for inherited methods
                if (!sig)
                {
                    auto *classInfo = lookupClass(toLower(className));
                    if (classInfo)
                    {
                        std::string methodKey = toLower(calleeName);
                        auto methodIt = classInfo->methods.find(methodKey);
                        if (methodIt != classInfo->methods.end())
                        {
                            if (methodIt->second.isAbstract)
                            {
                                error(expr, "cannot call abstract method '" + calleeName + "'");
                                return PasType::unknown();
                            }
                            // Create a temporary signature from method info
                            // Note: This is a simplification - methods are properly stored
                            // in functions_ with qualified names
                            // For now, just allow the call if the method exists
                            // Type checking for args happens below with the signature
                        }
                    }
                }
            }
            else if (receiverType.kind == PasTypeKind::Interface)
            {
                // Interface method call
                std::string ifaceName = receiverType.name;
                auto *ifaceInfo = lookupInterface(toLower(ifaceName));
                if (ifaceInfo)
                {
                    std::string methodKey = toLower(calleeName);
                    auto methodIt = ifaceInfo->methods.find(methodKey);
                    if (methodIt != ifaceInfo->methods.end())
                    {
                        // Found the interface method - type-check arguments
                        const MethodInfo &methodInfo = methodIt->second;

                        // Check argument count
                        size_t totalParams = methodInfo.params.size();
                        size_t requiredParams = methodInfo.requiredParams;
                        size_t actual = expr.args.size();

                        if (actual < requiredParams)
                        {
                            error(expr,
                                  "too few arguments: expected at least " +
                                      std::to_string(requiredParams) + ", got " +
                                      std::to_string(actual));
                        }
                        else if (actual > totalParams)
                        {
                            error(expr,
                                  "too many arguments: expected at most " +
                                      std::to_string(totalParams) + ", got " +
                                      std::to_string(actual));
                        }

                        // Type-check arguments
                        for (size_t i = 0; i < expr.args.size() && i < methodInfo.params.size();
                             ++i)
                        {
                            if (expr.args[i])
                            {
                                PasType argType = typeOf(*expr.args[i]);
                                const PasType &paramType = methodInfo.params[i].second;
                                if (!paramType.isError() && !isAssignableFrom(paramType, argType) &&
                                    !argType.isError())
                                {
                                    error(*expr.args[i],
                                          "argument " + std::to_string(i + 1) +
                                              " type mismatch: expected " + paramType.toString() +
                                              ", got " + argType.toString());
                                }
                            }
                        }

                        // Mark this as an interface method call for lowering
                        expr.isInterfaceCall = true;
                        expr.interfaceName = ifaceName;

                        return methodInfo.returnType;
                    }
                    else
                    {
                        error(expr,
                              "undefined method '" + calleeName + "' in interface '" + ifaceName +
                                  "'");
                        return PasType::unknown();
                    }
                }
            }
        }
    }
    else
    {
        // Complex callee expression
        typeOf(*expr.callee);
        return PasType::unknown();
    }

    // For non-method calls, look up in global functions
    if (!isMethodCall)
    {
        std::string key = toLower(calleeName);
        sig = lookupFunction(key);

        if (!sig)
        {
            // Check if it's a variable/constant - give a better error message
            if (lookupVariable(key) || lookupConstant(key))
            {
                error(expr,
                      "'" + calleeName +
                          "' is not a procedure or function; "
                          "only calls are allowed as statements");
            }
            else
            {
                error(expr, "undefined procedure or function '" + calleeName + "'");
            }
            return PasType::unknown();
        }
    }
    else if (!sig)
    {
        // Method call - look up with qualified name
        std::string qualifiedKey = toLower(className + "." + calleeName);
        sig = lookupFunction(qualifiedKey);

        if (!sig)
        {
            // Check in class methods directly
            auto *classInfo = lookupClass(toLower(className));
            if (classInfo)
            {
                std::string methodKey = toLower(calleeName);
                auto methodIt = classInfo->methods.find(methodKey);
                if (methodIt != classInfo->methods.end())
                {
                    // Reject direct calls to abstract methods
                    if (methodIt->second.isAbstract)
                    {
                        error(expr, "cannot call abstract method '" + calleeName + "'");
                        return PasType::unknown();
                    }
                    // For methods declared in class, use the method info
                    // Type-check args against the method's parameters
                    const MethodInfo &methodInfo = methodIt->second;

                    // Check argument count
                    size_t totalParams = methodInfo.params.size();
                    size_t requiredParams = methodInfo.requiredParams;
                    size_t actual = expr.args.size();

                    if (actual < requiredParams)
                    {
                        error(expr,
                              "too few arguments: expected at least " +
                                  std::to_string(requiredParams) + ", got " +
                                  std::to_string(actual));
                    }
                    else if (actual > totalParams)
                    {
                        error(expr,
                              "too many arguments: expected at most " +
                                  std::to_string(totalParams) + ", got " + std::to_string(actual));
                    }

                    // Type-check arguments
                    for (size_t i = 0; i < expr.args.size() && i < methodInfo.params.size(); ++i)
                    {
                        if (expr.args[i])
                        {
                            PasType argType = typeOf(*expr.args[i]);
                            const PasType &paramType = methodInfo.params[i].second;
                            if (!paramType.isError() && !isAssignableFrom(paramType, argType) &&
                                !argType.isError())
                            {
                                error(*expr.args[i],
                                      "argument " + std::to_string(i + 1) +
                                          " type mismatch: expected " + paramType.toString() +
                                          ", got " + argType.toString());
                            }
                        }
                    }

                    return methodInfo.returnType;
                }
            }

            error(expr, "undefined method '" + calleeName + "' in class '" + className + "'");
            return PasType::unknown();
        }
    }

    // Check argument count (skip for variadic builtins with 0 declared params)
    size_t totalParams = sig->params.size();
    size_t requiredParams = sig->requiredParams;
    size_t actual = expr.args.size();
    bool isVariadic =
        (totalParams == 0); // Treat 0-param functions as variadic (WriteLn, ReadLn, etc.)

    if (!isVariadic)
    {
        if (actual < requiredParams)
        {
            error(expr,
                  "too few arguments: expected at least " + std::to_string(requiredParams) +
                      ", got " + std::to_string(actual));
        }
        else if (actual > totalParams)
        {
            error(expr,
                  "too many arguments: expected at most " + std::to_string(totalParams) + ", got " +
                      std::to_string(actual));
        }
    }

    // Type-check arguments (for variadic functions, just type-check all args)
    if (isVariadic)
    {
        for (auto &arg : expr.args)
        {
            if (arg)
                typeOf(*arg);
        }
    }
    else
    {
        for (size_t i = 0; i < expr.args.size() && i < sig->params.size(); ++i)
        {
            if (expr.args[i])
            {
                PasType argType = typeOf(*expr.args[i]);
                const PasType &paramType = sig->params[i].second;
                // Skip type check if param is Unknown (used for multi-type builtins like Length)
                if (!paramType.isError() && !isAssignableFrom(paramType, argType) &&
                    !argType.isError())
                {
                    error(*expr.args[i],
                          "argument " + std::to_string(i + 1) + " type mismatch: expected " +
                              paramType.toString() + ", got " + argType.toString());
                }
            }
        }
    }

    // Special validation for SetLength: first argument must be a dynamic array or string
    std::string calleeKey = toLower(calleeName);
    if (calleeKey == "setlength" && !expr.args.empty() && expr.args[0])
    {
        PasType firstArgType = typeOf(*expr.args[0]);
        if (firstArgType.kind == PasTypeKind::Array && firstArgType.dimensions > 0)
        {
            error(*expr.args[0], "SetLength cannot be used on fixed-size arrays");
        }
    }

    return sig->returnType;
}

PasType SemanticAnalyzer::typeOfIndex(IndexExpr &expr)
{
    if (!expr.base)
        return PasType::unknown();

    PasType baseType = typeOf(*expr.base);

    // Check indices
    for (auto &idx : expr.indices)
    {
        if (idx)
        {
            PasType idxType = typeOf(*idx);
            if (!idxType.isOrdinal() && !idxType.isError())
            {
                error(*idx, "array index must be ordinal type");
            }
        }
    }

    // For arrays, return element type
    if (baseType.kind == PasTypeKind::Array && baseType.elementType)
    {
        return *baseType.elementType;
    }

    // For strings, indexing returns a character (also String in Pascal)
    if (baseType.kind == PasTypeKind::String)
    {
        return PasType::string();
    }

    if (!baseType.isError())
    {
        error(expr, "cannot index into " + baseType.toString());
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfField(FieldExpr &expr)
{
    if (!expr.base)
        return PasType::unknown();

    PasType baseType = typeOf(*expr.base);

    // For records/classes, look up field or property
    if (baseType.kind == PasTypeKind::Record || baseType.kind == PasTypeKind::Class)
    {
        std::string fieldKey = toLower(expr.field);

        // For class types, look up fields from class info (baseType.fields may be empty)
        if (baseType.kind == PasTypeKind::Class)
        {
            // Walk up the inheritance chain to find a matching field
            std::string cur = toLower(baseType.name);
            while (!cur.empty())
            {
                auto *classInfo = lookupClass(cur);
                if (!classInfo)
                    break;
                auto fieldIt = classInfo->fields.find(fieldKey);
                if (fieldIt != classInfo->fields.end())
                {
                    return fieldIt->second.type;
                }
                if (classInfo->baseClass.empty())
                    break;
                cur = toLower(classInfo->baseClass);
            }
            // Not a field; could be a constructor call without parentheses (e.g., TClass.Create)
            auto *ci = lookupClass(toLower(baseType.name));
            if (ci)
            {
                // If the member is 'Create', treat as constructor call; enforce abstract rules
                if (fieldKey == toLower(std::string("Create")))
                {
                    if (isAbstractClass(baseType.name))
                    {
                        error(expr, "cannot instantiate abstract class '" + baseType.name + "'");
                        return PasType::unknown();
                    }
                    return PasType::classType(baseType.name);
                }
                auto mit = ci->methods.find(fieldKey);
                if (mit != ci->methods.end())
                {
                    // Disallow abstract class instantiation
                    if (isAbstractClass(baseType.name))
                    {
                        error(expr, "cannot instantiate abstract class '" + baseType.name + "'");
                        return PasType::unknown();
                    }
                    // Treat as zero-arg constructor call; return class type
                    return PasType::classType(baseType.name);
                }
            }
            // Unknown member
            return PasType::unknown();
        }

        // For record types, use baseType.fields
        auto it = baseType.fields.find(fieldKey);
        if (it != baseType.fields.end() && it->second)
        {
            return *it->second;
        }
        // Field not found - might be a method, allow for now
        return PasType::unknown();
    }

    // For interface types, look up method (interfaces have no fields)
    if (baseType.kind == PasTypeKind::Interface)
    {
        std::string methodKey = toLower(expr.field);
        auto *ifaceInfo = lookupInterface(toLower(baseType.name));
        if (ifaceInfo)
        {
            auto methodIt = ifaceInfo->methods.find(methodKey);
            if (methodIt != ifaceInfo->methods.end())
            {
                // Return the method's return type (for parameterless function calls)
                return methodIt->second.returnType;
            }
        }
        // Unknown method
        return PasType::unknown();
    }

    if (!baseType.isError())
    {
        error(expr, "cannot access field on " + baseType.toString());
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfTypeCast(TypeCastExpr &expr)
{
    if (!expr.targetType)
        return PasType::unknown();

    // Type-check the operand
    if (expr.operand)
        typeOf(*expr.operand);

    return resolveType(*expr.targetType);
}

PasType SemanticAnalyzer::typeOfIs(IsExpr &expr)
{
    // Left side: expression type (allow class/interface/optional/nil)
    PasType leftType = PasType::unknown();
    if (expr.operand)
        leftType = typeOf(*expr.operand);

    // Right side: resolve target type
    if (!expr.targetType)
        return PasType::boolean();
    PasType target = resolveType(*expr.targetType);

    // Validate right-hand is a class/interface type
    if (!(target.kind == PasTypeKind::Class || target.kind == PasTypeKind::Interface))
    {
        error(expr, "right-hand side of 'is' must be a class or interface type");
        return PasType::boolean();
    }

    // Validate left-hand side is reference-compatible (class/interface/optional/nil)
    bool lhsOk = (leftType.kind == PasTypeKind::Class || leftType.kind == PasTypeKind::Interface ||
                  leftType.kind == PasTypeKind::Optional || leftType.kind == PasTypeKind::Nil ||
                  leftType.kind == PasTypeKind::Unknown);
    if (!lhsOk)
    {
        error(expr, "left-hand side of 'is' must be a class or interface expression");
    }

    // Result type is Boolean
    return PasType::boolean();
}

PasType SemanticAnalyzer::typeOfSetConstructor(SetConstructorExpr &expr)
{
    PasType elemType = PasType::unknown();

    for (auto &elem : expr.elements)
    {
        if (elem.start)
        {
            PasType t = typeOf(*elem.start);
            if (elemType.isError())
                elemType = t;
        }
        if (elem.end)
        {
            typeOf(*elem.end);
        }
    }

    PasType result;
    result.kind = PasTypeKind::Set;
    if (!elemType.isError())
    {
        result.elementType = std::make_shared<PasType>(elemType);
    }
    return result;
}

PasType SemanticAnalyzer::typeOfAddressOf(AddressOfExpr &expr)
{
    // v0.1: Address-of operator is not supported
    error(expr,
          "address-of operator (@) is not supported in Viper Pascal v0.1; use classes instead");
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfDereference(DereferenceExpr &expr)
{
    // v0.1: Pointer dereference is not supported
    error(expr,
          "pointer dereference (^) is not supported in Viper Pascal v0.1; use classes instead");
    return PasType::unknown();
}

} // namespace il::frontends::pascal
