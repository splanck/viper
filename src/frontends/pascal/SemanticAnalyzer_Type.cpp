//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Type.cpp
// Purpose: Type resolution and constant evaluation.
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
// Type Resolution
//===----------------------------------------------------------------------===//

PasType SemanticAnalyzer::resolveType(TypeNode &typeNode)
{
    if (auto it = typeCache_.find(&typeNode); it != typeCache_.end())
        return it->second;

    auto remember = [this, &typeNode](PasType t)
    {
        typeCache_[&typeNode] = t;
        return t;
    };

    switch (typeNode.kind)
    {
        case TypeKind::Named:
        {
            auto &named = static_cast<NamedTypeNode &>(typeNode);
            std::string key = toLower(named.name);

            // Check built-in types first
            if (key == "integer")
                return remember(PasType::integer());
            if (key == "real" || key == "double")
                return remember(PasType::real());
            if (key == "boolean")
                return remember(PasType::boolean());
            if (key == "string")
                return remember(PasType::string());

            // Look up user-defined type
            if (auto type = lookupType(key))
            {
                return remember(*type);
            }

            error(typeNode.loc, "undefined type '" + named.name + "'");
            return remember(PasType::unknown());
        }
        case TypeKind::Optional:
        {
            auto &opt = static_cast<OptionalTypeNode &>(typeNode);
            if (!opt.inner)
                return remember(PasType::unknown());
            PasType inner = resolveType(*opt.inner);

            // Check for double optional (T??) - this is a compile error
            if (inner.isOptional())
            {
                error(typeNode.loc,
                      "double optional type (T?"
                      "?) is not allowed");
                // Return single optional for error recovery
                return remember(inner);
            }

            return remember(PasType::optional(inner));
        }
        case TypeKind::Array:
        {
            auto &arr = static_cast<ArrayTypeNode &>(typeNode);
            if (!arr.elementType)
                return remember(PasType::unknown());

            // Collect dimension sizes for fixed arrays
            std::vector<int64_t> dimSizes;

            // Validate dimension sizes for fixed arrays
            for (auto &dim : arr.dimensions)
            {
                if (dim.size)
                {
                    // Dimension size must be a compile-time constant integer
                    if (!isConstantExpr(*dim.size))
                    {
                        error(*dim.size, "array dimension must be a compile-time constant");
                        dimSizes.push_back(0); // Use 0 as error placeholder
                        continue;
                    }

                    // Type-check the dimension expression
                    PasType dimType = const_cast<SemanticAnalyzer *>(this)->typeOf(*dim.size);
                    if (dimType.kind != PasTypeKind::Integer &&
                        dimType.kind != PasTypeKind::Unknown)
                    {
                        error(*dim.size, "array dimension must be an integer");
                        dimSizes.push_back(0);
                        continue;
                    }

                    // Evaluate and check the value is positive
                    int64_t dimValue = evaluateConstantInt(*dim.size);
                    if (dimValue <= 0)
                    {
                        error(*dim.size, "array dimension must be positive");
                        dimSizes.push_back(0);
                    }
                    else
                    {
                        dimSizes.push_back(dimValue);
                    }
                }
                else
                {
                    // Dynamic dimension - size is 0
                    dimSizes.push_back(0);
                }
            }

            PasType elem = resolveType(*arr.elementType);
            return remember(PasType::array(elem, arr.dimensions.size(), std::move(dimSizes)));
        }
        case TypeKind::Record:
        {
            auto &rec = static_cast<RecordTypeNode &>(typeNode);
            PasType result;
            result.kind = PasTypeKind::Record;
            for (auto &field : rec.fields)
            {
                if (field.type)
                {
                    result.fields[toLower(field.name)] =
                        std::make_shared<PasType>(resolveType(*field.type));
                }
            }
            return remember(result);
        }
        case TypeKind::Pointer:
        {
            // v0.1: Pointer types are not supported
            error(typeNode.loc,
                  "pointer types (^T) are not supported in Viper Pascal v0.1; use classes instead");
            return remember(PasType::unknown());
        }
        case TypeKind::Enum:
        {
            auto &en = static_cast<EnumTypeNode &>(typeNode);
            return remember(PasType::enumType(en.values));
        }
        case TypeKind::Set:
        {
            // v0.1: Set types are not supported
            error(typeNode.loc, "set types are not supported in Viper Pascal v0.1");
            return remember(PasType::unknown());
        }
        case TypeKind::Procedure:
        {
            PasType result;
            result.kind = PasTypeKind::Procedure;
            return remember(result);
        }
        case TypeKind::Function:
        {
            auto &func = static_cast<FunctionTypeNode &>(typeNode);
            PasType result;
            result.kind = PasTypeKind::Function;
            if (func.returnType)
            {
                result.returnType = std::make_shared<PasType>(resolveType(*func.returnType));
            }
            return remember(result);
        }
        case TypeKind::Range:
        {
            // Subrange - treat as the base type for now
            PasType result;
            result.kind = PasTypeKind::Range;
            return remember(result);
        }
    }
    return remember(PasType::unknown());
}

bool SemanticAnalyzer::isAssignableFrom(const PasType &target, const PasType &source)
{
    // Unknown types are always assignable (error recovery)
    if (target.isError() || source.isError())
        return true;

    // Nil is assignable to nil-assignable types (check BEFORE optional unwrap)
    if (source.kind == PasTypeKind::Nil && target.isNilAssignable())
        return true;

    // Same kind - check compatibility
    if (target.kind == source.kind)
    {
        // For optional types, also check inner type compatibility
        if (target.kind == PasTypeKind::Optional && target.innerType && source.innerType)
        {
            return isAssignableFrom(*target.innerType, *source.innerType);
        }
        // For classes, check inheritance
        if (target.kind == PasTypeKind::Class)
        {
            return classInheritsFrom(source.name, target.name);
        }
        // For interfaces, check inheritance
        if (target.kind == PasTypeKind::Interface)
        {
            return interfaceExtendsInterface(source.name, target.name);
        }
        // For arrays, check element type compatibility
        if (target.kind == PasTypeKind::Array && target.elementType && source.elementType)
        {
            // Element types must be exactly compatible (not just assignable)
            // Fixed arrays also must have same dimensions
            if (target.dimensions != source.dimensions)
                return false;
            return isAssignableFrom(*target.elementType, *source.elementType);
        }
        return true;
    }

    // T implicitly converts to T? (non-optional to optional)
    if (target.isOptional() && target.innerType)
    {
        return isAssignableFrom(*target.innerType, source);
    }

    // T? does NOT implicitly convert to T (this is the key rule)
    // This is enforced by not having a rule for it

    // Integer can be promoted to Real
    if (target.kind == PasTypeKind::Real && source.kind == PasTypeKind::Integer)
        return true;

    // Integer can be promoted to Real? (optional Real)
    if (target.isOptional() && target.innerType && target.innerType->kind == PasTypeKind::Real &&
        source.kind == PasTypeKind::Integer)
        return true;

    // Enum/Range can be assigned to Integer
    if (target.kind == PasTypeKind::Integer &&
        (source.kind == PasTypeKind::Enum || source.kind == PasTypeKind::Range))
        return true;

    // Class can be assigned to interface if the class implements the interface
    if (target.kind == PasTypeKind::Interface && source.kind == PasTypeKind::Class)
    {
        return classImplementsInterface(source.name, target.name);
    }

    return false;
}

bool SemanticAnalyzer::areSignaturesCompatible(const MethodInfo &classMethod,
                                               const MethodInfo &ifaceMethod)
{
    // Check parameter count
    if (classMethod.params.size() != ifaceMethod.params.size())
        return false;

    // Check each parameter type and var/out modifier
    for (size_t i = 0; i < classMethod.params.size(); ++i)
    {
        // Check type compatibility (must be exact match for interface implementation)
        const PasType &classParamType = classMethod.params[i].second;
        const PasType &ifaceParamType = ifaceMethod.params[i].second;

        if (!isAssignableFrom(classParamType, ifaceParamType) ||
            !isAssignableFrom(ifaceParamType, classParamType))
        {
            return false;
        }

        // Check var/out parameter match
        if (classMethod.isVarParam[i] != ifaceMethod.isVarParam[i])
            return false;
    }

    // Check return type compatibility
    const PasType &classRetType = classMethod.returnType;
    const PasType &ifaceRetType = ifaceMethod.returnType;

    // Return types must be compatible (class method can return subtype)
    return isAssignableFrom(ifaceRetType, classRetType);
}

PasType SemanticAnalyzer::binaryResultType(BinaryExpr::Op op,
                                           const PasType &left,
                                           const PasType &right)
{
    // Error recovery
    if (left.isError() || right.isError())
        return PasType::unknown();

    switch (op)
    {
        // Arithmetic operators
        case BinaryExpr::Op::Add:
        case BinaryExpr::Op::Sub:
        case BinaryExpr::Op::Mul:
            // String concatenation
            if (left.kind == PasTypeKind::String && right.kind == PasTypeKind::String)
                return PasType::string();
            // Numeric operations
            if (left.isNumeric() && right.isNumeric())
            {
                // Promote to Real if either operand is Real
                if (left.kind == PasTypeKind::Real || right.kind == PasTypeKind::Real)
                    return PasType::real();
                return PasType::integer();
            }
            hasError_ = true;
            return PasType::unknown();

        case BinaryExpr::Op::Div:
            // Real division always returns Real
            if (left.isNumeric() && right.isNumeric())
                return PasType::real();
            hasError_ = true;
            return PasType::unknown();

        case BinaryExpr::Op::IntDiv:
        case BinaryExpr::Op::Mod:
            // Integer operations
            if (left.kind == PasTypeKind::Integer && right.kind == PasTypeKind::Integer)
                return PasType::integer();
            hasError_ = true;
            return PasType::unknown();

        // Comparison operators
        case BinaryExpr::Op::Eq:
        case BinaryExpr::Op::Ne:
            // Check for nil comparison with non-optional class/interface
            if ((left.kind == PasTypeKind::Nil &&
                 (right.kind == PasTypeKind::Class || right.kind == PasTypeKind::Interface)) ||
                (right.kind == PasTypeKind::Nil &&
                 (left.kind == PasTypeKind::Class || left.kind == PasTypeKind::Interface)))
            {
                // Non-optional reference types cannot be compared to nil
                hasError_ = true;
                return PasType::unknown();
            }
            // For enum types, both operands must be the same enum type
            if (left.kind == PasTypeKind::Enum || right.kind == PasTypeKind::Enum)
            {
                if (left.kind != right.kind || left.name != right.name)
                {
                    hasError_ = true;
                    return PasType::unknown();
                }
            }
            return PasType::boolean();
        case BinaryExpr::Op::Lt:
        case BinaryExpr::Op::Le:
        case BinaryExpr::Op::Gt:
        case BinaryExpr::Op::Ge:
            // For enum types, both operands must be the same enum type
            if (left.kind == PasTypeKind::Enum || right.kind == PasTypeKind::Enum)
            {
                if (left.kind != right.kind || left.name != right.name)
                {
                    hasError_ = true;
                    return PasType::unknown();
                }
            }
            // Comparisons return Boolean
            return PasType::boolean();

        // Logical operators
        case BinaryExpr::Op::And:
        case BinaryExpr::Op::Or:
            if (left.kind == PasTypeKind::Boolean && right.kind == PasTypeKind::Boolean)
                return PasType::boolean();
            hasError_ = true;
            return PasType::unknown();

        case BinaryExpr::Op::In:
            // Set membership returns Boolean
            return PasType::boolean();

        case BinaryExpr::Op::Coalesce:
            // Nil coalescing (??) operator
            // Rules:
            // - lhs: T?, rhs: T → result: T
            // - lhs: T?, rhs: T? → result: T (unwrap both)
            // - lhs: T, rhs: T → result: T (no-op but valid)
            {
                PasType leftUnwrapped = left.unwrap();
                PasType rightUnwrapped = right.unwrap();

                // Check type compatibility between unwrapped types
                if (!isAssignableFrom(leftUnwrapped, rightUnwrapped) &&
                    !isAssignableFrom(rightUnwrapped, leftUnwrapped))
                {
                    hasError_ = true;
                    return PasType::unknown();
                }

                // Result is always the unwrapped type
                return leftUnwrapped;
            }
    }

    return PasType::unknown();
}

PasType SemanticAnalyzer::unaryResultType(UnaryExpr::Op op, const PasType &operand)
{
    if (operand.isError())
        return PasType::unknown();

    switch (op)
    {
        case UnaryExpr::Op::Neg:
        case UnaryExpr::Op::Plus:
            if (operand.isNumeric())
                return operand;
            hasError_ = true;
            return PasType::unknown();

        case UnaryExpr::Op::Not:
            if (operand.kind == PasTypeKind::Boolean)
                return PasType::boolean();
            hasError_ = true;
            return PasType::unknown();
    }

    return PasType::unknown();
}

bool SemanticAnalyzer::isConstantExpr(const Expr &expr) const
{
    switch (expr.kind)
    {
        case ExprKind::IntLiteral:
        case ExprKind::RealLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::NilLiteral:
            return true;

        case ExprKind::Name:
        {
            // Check if it's a constant identifier
            const auto &nameExpr = static_cast<const NameExpr &>(expr);
            std::string key = toLower(nameExpr.name);
            return constants_.count(key) > 0;
        }

        case ExprKind::Unary:
        {
            // Unary on a constant is still constant
            const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
            return unaryExpr.operand && isConstantExpr(*unaryExpr.operand);
        }

        case ExprKind::Binary:
        {
            // Binary on constants is still constant (for compile-time evaluable ops)
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            return binExpr.left && binExpr.right && isConstantExpr(*binExpr.left) &&
                   isConstantExpr(*binExpr.right);
        }

        default:
            return false;
    }
}

bool SemanticAnalyzer::checkConstantDivZero(const Expr &expr)
{
    switch (expr.kind)
    {
        case ExprKind::Unary:
        {
            const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
            if (unaryExpr.operand)
                return checkConstantDivZero(*unaryExpr.operand);
            return false;
        }

        case ExprKind::Binary:
        {
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            if (!binExpr.left || !binExpr.right)
                return false;

            // Check left operand first
            if (checkConstantDivZero(*binExpr.left))
                return true;

            // Check for division by zero
            if (binExpr.op == BinaryExpr::Op::IntDiv || binExpr.op == BinaryExpr::Op::Mod ||
                binExpr.op == BinaryExpr::Op::Div)
            {
                PasType rightType = typeOf(*binExpr.right);
                if (rightType.kind == PasTypeKind::Integer)
                {
                    int64_t divisor = evaluateConstantInt(*binExpr.right);
                    if (divisor == 0)
                    {
                        error(*binExpr.right, "division by zero in constant expression");
                        return true;
                    }
                }
                else if (rightType.kind == PasTypeKind::Real)
                {
                    double divisor = evaluateConstantReal(*binExpr.right);
                    if (divisor == 0.0)
                    {
                        error(*binExpr.right, "division by zero in constant expression");
                        return true;
                    }
                }
            }

            // Check right operand
            return checkConstantDivZero(*binExpr.right);
        }

        default:
            return false;
    }
}

int64_t SemanticAnalyzer::evaluateConstantInt(const Expr &expr) const
{
    switch (expr.kind)
    {
        case ExprKind::IntLiteral:
            return static_cast<const IntLiteralExpr &>(expr).value;

        case ExprKind::Name:
        {
            // Look up constant value
            const auto &nameExpr = static_cast<const NameExpr &>(expr);
            std::string key = toLower(nameExpr.name);

            // Check for stored integer constant value
            auto valIt = constantValues_.find(key);
            if (valIt != constantValues_.end())
            {
                return valIt->second;
            }

            // Check for enum constant with ordinal
            auto it = constants_.find(key);
            if (it != constants_.end())
            {
                if (it->second.kind == PasTypeKind::Enum && it->second.enumOrdinal >= 0)
                {
                    return it->second.enumOrdinal;
                }
            }
            return 0;
        }

        case ExprKind::Unary:
        {
            const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
            if (!unaryExpr.operand)
                return 0;
            int64_t operand = evaluateConstantInt(*unaryExpr.operand);
            switch (unaryExpr.op)
            {
                case UnaryExpr::Op::Neg:
                    return -operand;
                case UnaryExpr::Op::Plus:
                    return operand;
                case UnaryExpr::Op::Not:
                    return operand ? 0 : 1;
            }
            return 0;
        }

        case ExprKind::Binary:
        {
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            if (!binExpr.left || !binExpr.right)
                return 0;
            int64_t left = evaluateConstantInt(*binExpr.left);
            int64_t right = evaluateConstantInt(*binExpr.right);
            switch (binExpr.op)
            {
                case BinaryExpr::Op::Add:
                    return left + right;
                case BinaryExpr::Op::Sub:
                    return left - right;
                case BinaryExpr::Op::Mul:
                    return left * right;
                case BinaryExpr::Op::IntDiv:
                    return right != 0 ? left / right : 0;
                case BinaryExpr::Op::Mod:
                    return right != 0 ? left % right : 0;
                default:
                    return 0;
            }
        }

        default:
            return 0;
    }
}

double SemanticAnalyzer::evaluateConstantReal(const Expr &expr) const
{
    switch (expr.kind)
    {
        case ExprKind::RealLiteral:
            return static_cast<const RealLiteralExpr &>(expr).value;

        case ExprKind::IntLiteral:
            // Integer can be promoted to real
            return static_cast<double>(static_cast<const IntLiteralExpr &>(expr).value);

        case ExprKind::Name:
        {
            const auto &nameExpr = static_cast<const NameExpr &>(expr);
            std::string key = toLower(nameExpr.name);

            // Check for stored real constant value
            auto realIt = constantRealValues_.find(key);
            if (realIt != constantRealValues_.end())
            {
                return realIt->second;
            }

            // Check for stored integer constant value (promote to real)
            auto intIt = constantValues_.find(key);
            if (intIt != constantValues_.end())
            {
                return static_cast<double>(intIt->second);
            }
            return 0.0;
        }

        case ExprKind::Unary:
        {
            const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
            if (!unaryExpr.operand)
                return 0.0;
            double operand = evaluateConstantReal(*unaryExpr.operand);
            switch (unaryExpr.op)
            {
                case UnaryExpr::Op::Neg:
                    return -operand;
                case UnaryExpr::Op::Plus:
                    return operand;
                default:
                    return 0.0;
            }
        }

        case ExprKind::Binary:
        {
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            if (!binExpr.left || !binExpr.right)
                return 0.0;
            double left = evaluateConstantReal(*binExpr.left);
            double right = evaluateConstantReal(*binExpr.right);
            switch (binExpr.op)
            {
                case BinaryExpr::Op::Add:
                    return left + right;
                case BinaryExpr::Op::Sub:
                    return left - right;
                case BinaryExpr::Op::Mul:
                    return left * right;
                case BinaryExpr::Op::Div:
                    return right != 0.0 ? left / right : 0.0;
                default:
                    return 0.0;
            }
        }

        default:
            return 0.0;
    }
}

std::string SemanticAnalyzer::evaluateConstantString(const Expr &expr) const
{
    switch (expr.kind)
    {
        case ExprKind::StringLiteral:
            return static_cast<const StringLiteralExpr &>(expr).value;

        case ExprKind::Name:
        {
            const auto &nameExpr = static_cast<const NameExpr &>(expr);
            std::string key = toLower(nameExpr.name);

            auto strIt = constantStrValues_.find(key);
            if (strIt != constantStrValues_.end())
            {
                return strIt->second;
            }
            return "";
        }

        case ExprKind::Binary:
        {
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            if (!binExpr.left || !binExpr.right)
                return "";
            // Only string concatenation is supported
            if (binExpr.op == BinaryExpr::Op::Add)
            {
                std::string left = evaluateConstantString(*binExpr.left);
                std::string right = evaluateConstantString(*binExpr.right);
                return left + right;
            }
            return "";
        }

        default:
            return "";
    }
}

bool SemanticAnalyzer::evaluateConstantBool(const Expr &expr) const
{
    switch (expr.kind)
    {
        case ExprKind::BoolLiteral:
            return static_cast<const BoolLiteralExpr &>(expr).value;

        case ExprKind::Name:
        {
            const auto &nameExpr = static_cast<const NameExpr &>(expr);
            std::string key = toLower(nameExpr.name);

            // Check for boolean constant - need to look up type and get value
            auto constIt = constants_.find(key);
            if (constIt != constants_.end() && constIt->second.kind == PasTypeKind::Boolean)
            {
                // Boolean constants stored as int (0/1)
                auto intIt = constantValues_.find(key);
                if (intIt != constantValues_.end())
                {
                    return intIt->second != 0;
                }
            }
            return false;
        }

        case ExprKind::Unary:
        {
            const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
            if (!unaryExpr.operand)
                return false;
            if (unaryExpr.op == UnaryExpr::Op::Not)
            {
                return !evaluateConstantBool(*unaryExpr.operand);
            }
            return false;
        }

        case ExprKind::Binary:
        {
            const auto &binExpr = static_cast<const BinaryExpr &>(expr);
            if (!binExpr.left || !binExpr.right)
                return false;

            // Logical operators on booleans
            if (binExpr.op == BinaryExpr::Op::And)
            {
                return evaluateConstantBool(*binExpr.left) && evaluateConstantBool(*binExpr.right);
            }
            if (binExpr.op == BinaryExpr::Op::Or)
            {
                return evaluateConstantBool(*binExpr.left) || evaluateConstantBool(*binExpr.right);
            }

            // Comparison operators - need to determine operand types
            // For now, try integer comparison first
            PasType leftType = const_cast<SemanticAnalyzer *>(this)->typeOf(*binExpr.left);
            PasType rightType = const_cast<SemanticAnalyzer *>(this)->typeOf(*binExpr.right);

            if (leftType.kind == PasTypeKind::Integer || rightType.kind == PasTypeKind::Integer ||
                leftType.kind == PasTypeKind::Real || rightType.kind == PasTypeKind::Real)
            {
                // Numeric comparison - use real for mixed types
                double left = evaluateConstantReal(*binExpr.left);
                double right = evaluateConstantReal(*binExpr.right);
                switch (binExpr.op)
                {
                    case BinaryExpr::Op::Eq:
                        return left == right;
                    case BinaryExpr::Op::Ne:
                        return left != right;
                    case BinaryExpr::Op::Lt:
                        return left < right;
                    case BinaryExpr::Op::Le:
                        return left <= right;
                    case BinaryExpr::Op::Gt:
                        return left > right;
                    case BinaryExpr::Op::Ge:
                        return left >= right;
                    default:
                        return false;
                }
            }

            if (leftType.kind == PasTypeKind::String && rightType.kind == PasTypeKind::String)
            {
                std::string left = evaluateConstantString(*binExpr.left);
                std::string right = evaluateConstantString(*binExpr.right);
                switch (binExpr.op)
                {
                    case BinaryExpr::Op::Eq:
                        return left == right;
                    case BinaryExpr::Op::Ne:
                        return left != right;
                    case BinaryExpr::Op::Lt:
                        return left < right;
                    case BinaryExpr::Op::Le:
                        return left <= right;
                    case BinaryExpr::Op::Gt:
                        return left > right;
                    case BinaryExpr::Op::Ge:
                        return left >= right;
                    default:
                        return false;
                }
            }

            if (leftType.kind == PasTypeKind::Boolean && rightType.kind == PasTypeKind::Boolean)
            {
                bool left = evaluateConstantBool(*binExpr.left);
                bool right = evaluateConstantBool(*binExpr.right);
                switch (binExpr.op)
                {
                    case BinaryExpr::Op::Eq:
                        return left == right;
                    case BinaryExpr::Op::Ne:
                        return left != right;
                    default:
                        return false;
                }
            }

            return false;
        }

        default:
            return false;
    }
}

ConstantValue SemanticAnalyzer::foldConstant(const Expr &expr)
{
    if (!isConstantExpr(expr))
    {
        return ConstantValue{}; // hasValue = false
    }

    // Determine result type
    PasType exprType = typeOf(const_cast<Expr &>(expr));

    switch (exprType.kind)
    {
        case PasTypeKind::Integer:
        {
            int64_t val = evaluateConstantInt(expr);
            return ConstantValue::makeInt(val);
        }

        case PasTypeKind::Real:
        {
            double val = evaluateConstantReal(expr);
            return ConstantValue::makeReal(val);
        }

        case PasTypeKind::String:
        {
            std::string val = evaluateConstantString(expr);
            return ConstantValue::makeString(val);
        }

        case PasTypeKind::Boolean:
        {
            bool val = evaluateConstantBool(expr);
            return ConstantValue::makeBool(val);
        }

        default:
            return ConstantValue{}; // hasValue = false
    }
}

size_t SemanticAnalyzer::validateDefaultParams(const std::vector<ParamDecl> &params,
                                               il::support::SourceLoc loc)
{
    bool seenDefault = false;
    size_t requiredCount = 0;

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &param = params[i];

        if (param.defaultValue)
        {
            seenDefault = true;

            // Check that default value is a compile-time constant
            if (!isConstantExpr(*param.defaultValue))
            {
                error(param.loc, "default parameter value must be a compile-time constant");
            }

            // Type-check the default value
            // (Cast away const for typeOf - it doesn't modify the expr semantically)
            PasType defaultType = const_cast<SemanticAnalyzer *>(this)->typeOf(
                *const_cast<Expr *>(param.defaultValue.get()));
            PasType paramType = param.type ? resolveType(*const_cast<TypeNode *>(param.type.get()))
                                           : PasType::unknown();

            if (!isAssignableFrom(paramType, defaultType) && !defaultType.isError())
            {
                error(param.loc,
                      "default value type " + defaultType.toString() +
                          " is not compatible with parameter type " + paramType.toString());
            }
        }
        else if (seenDefault)
        {
            // Error: non-default parameter after default parameter
            error(param.loc,
                  "parameter '" + param.name +
                      "' must have a default value because it follows a parameter with a default");
        }
        else
        {
            requiredCount++;
        }
    }

    return requiredCount;
}

} // namespace il::frontends::pascal
