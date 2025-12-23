//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements the BASIC type coercion engine.
/// @details Provides the out-of-line definitions for
///          @ref TypeCoercionEngine. The implementation emits IL conversions
///          that preserve BASIC semantics (logical TRUE = -1, FALSE = 0),
///          performs sign extension for narrow integers, and uses checked
///          floating-point conversions where required.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/TypeCoercionEngine.hpp"

#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

/// @brief Construct a coercion engine bound to a lowering context.
/// @details Stores the lowerer reference used for emission; no state is owned
///          beyond the reference.
/// @param lowerer Lowering engine used to emit conversions.
TypeCoercionEngine::TypeCoercionEngine(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

// =============================================================================
// Primary Coercion Methods
// =============================================================================

/// @brief Coerce a value to a 64-bit signed integer.
/// @details Applies BASIC coercion semantics: booleans are mapped to logical
///          words (-1/0), floats are converted with round-to-even and overflow
///          checks, and narrower integers are sign-extended to i64.
/// @param v Value/type pair to convert.
/// @param loc Source location for emitted conversions.
/// @return Updated r-value guaranteed to have i64 type.
RVal TypeCoercionEngine::toI64(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);

    switch (v.type.kind)
    {
        case Type::Kind::I64:
            // Already i64, no conversion needed
            return v;

        case Type::Kind::I1:
            // Boolean to integer: use BASIC logical conversion (TRUE = -1)
            v.value = emitBoolToLogicalI64(v.value);
            v.type = Type(Type::Kind::I64);
            return v;

        case Type::Kind::F64:
            // Float to integer: round-to-even with overflow check
            v.value = emitUnary(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), v.value);
            v.type = Type(Type::Kind::I64);
            return v;

        case Type::Kind::I16:
        case Type::Kind::I32:
        {
            // Narrow integer to i64: sign-extend
            const int fromBits = (v.type.kind == Type::Kind::I32) ? 32 : 16;
            v.value = widenToI64(v.value, fromBits, loc);
            v.type = Type(Type::Kind::I64);
            return v;
        }

        default:
            // Unsupported type, return unchanged
            return v;
    }
}

/// @brief Coerce a value to a 64-bit floating-point value.
/// @details Converts non-f64 values by first normalizing to i64, then emitting
///          a signed integer-to-float conversion. This preserves BASIC integer
///          semantics before widening to floating-point.
/// @param v Value/type pair to convert.
/// @param loc Source location for emitted conversions.
/// @return Updated r-value guaranteed to have f64 type.
RVal TypeCoercionEngine::toF64(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);

    if (v.type.kind == Type::Kind::F64)
        return v;

    // First convert to i64 if needed, then to f64
    v = toI64(std::move(v), loc);

    if (v.type.kind == Type::Kind::I64)
    {
        v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
        v.type = Type(Type::Kind::F64);
    }

    return v;
}

/// @brief Coerce a value to a boolean (i1).
/// @details Numeric inputs are normalized to i64 before truncating to i1,
///          producing a canonical boolean representation suitable for IL
///          branching and comparisons.
/// @param v Value/type pair to convert.
/// @param loc Source location for emitted conversions.
/// @return Updated r-value guaranteed to have i1 type.
RVal TypeCoercionEngine::toBool(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);

    if (v.type.kind == Type::Kind::I1)
        return v;

    // Convert numeric types to i64 first
    if (v.type.kind == Type::Kind::F64 || v.type.kind == Type::Kind::I16 ||
        v.type.kind == Type::Kind::I32 || v.type.kind == Type::Kind::I64)
    {
        v = toI64(std::move(v), loc);
    }

    // Truncate to boolean
    if (v.type.kind != Type::Kind::I1)
    {
        v.value = emitUnary(Opcode::Trunc1, boolType(), v.value);
        v.type = boolType();
    }

    return v;
}

/// @brief Coerce a value to a specific IL type kind.
/// @details Dispatches to the appropriate coercion routine and leaves the
///          value unchanged for unsupported or identical target kinds.
/// @param v Value/type pair to convert.
/// @param target Target IL type kind.
/// @param loc Source location for emitted conversions.
/// @return Updated r-value with the target type when supported.
RVal TypeCoercionEngine::toType(RVal v, Type::Kind target, il::support::SourceLoc loc)
{
    switch (target)
    {
        case Type::Kind::I64:
            return toI64(std::move(v), loc);
        case Type::Kind::F64:
            return toF64(std::move(v), loc);
        case Type::Kind::I1:
            return toBool(std::move(v), loc);
        default:
            return v;
    }
}

/// @brief Coerce a value to match a BASIC AST type.
/// @details Converts the value based on the BASIC type enum, preserving BASIC
///          integer, floating-point, and boolean semantics.
/// @param v Value/type pair to convert.
/// @param target Target BASIC AST type.
/// @param loc Source location for emitted conversions.
/// @return Updated r-value with the corresponding IL type.
RVal TypeCoercionEngine::toAstType(RVal v, AstType target, il::support::SourceLoc loc)
{
    switch (target)
    {
        case AstType::I64:
            return toI64(std::move(v), loc);
        case AstType::F64:
            return toF64(std::move(v), loc);
        case AstType::Bool:
            return toBool(std::move(v), loc);
        default:
            return v;
    }
}

// =============================================================================
// Type Queries
// =============================================================================

/// @brief Check whether an r-value already holds an i64.
/// @param v Value/type pair to query.
/// @return True when the value's type is i64.
bool TypeCoercionEngine::isI64(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::I64;
}

/// @brief Check whether an r-value already holds an f64.
/// @param v Value/type pair to query.
/// @return True when the value's type is f64.
bool TypeCoercionEngine::isF64(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::F64;
}

/// @brief Check whether an r-value already holds a boolean (i1).
/// @param v Value/type pair to query.
/// @return True when the value's type is i1.
bool TypeCoercionEngine::isBool(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::I1;
}

/// @brief Check whether an r-value is a BASIC string pointer.
/// @details Strings are represented as pointers in IL.
/// @param v Value/type pair to query.
/// @return True when the value's type is a pointer.
bool TypeCoercionEngine::isString(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::Ptr;
}

/// @brief Check whether an r-value is a pointer type.
/// @param v Value/type pair to query.
/// @return True when the value's type is a pointer.
bool TypeCoercionEngine::isPointer(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::Ptr;
}

/// @brief Determine whether an IL type kind is numeric.
/// @details Treats integer and floating-point kinds as numeric and excludes
///          pointer or string types.
/// @param kind IL type kind to query.
/// @return True when the kind is numeric.
bool TypeCoercionEngine::isNumeric(Type::Kind kind) noexcept
{
    switch (kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I16:
        case Type::Kind::I32:
        case Type::Kind::I64:
        case Type::Kind::F64:
            return true;
        default:
            return false;
    }
}

/// @brief Determine whether a BASIC AST type is numeric.
/// @details Treats integer, floating-point, and boolean as numeric for the
///          purposes of coercion and promotion.
/// @param type BASIC AST type to query.
/// @return True when the type is numeric.
bool TypeCoercionEngine::isNumericAst(AstType type) noexcept
{
    switch (type)
    {
        case AstType::I64:
        case AstType::F64:
        case AstType::Bool:
            return true;
        default:
            return false;
    }
}

// =============================================================================
// IL Type Helpers
// =============================================================================

/// @brief Return the IL boolean type (i1).
/// @return IL type representing a boolean.
TypeCoercionEngine::IlType TypeCoercionEngine::boolType() noexcept
{
    return IlType(IlType::Kind::I1);
}

/// @brief Return the IL integer type (i64).
/// @return IL type representing a 64-bit integer.
TypeCoercionEngine::IlType TypeCoercionEngine::intType() noexcept
{
    return IlType(IlType::Kind::I64);
}

/// @brief Return the IL floating-point type (f64).
/// @return IL type representing a 64-bit float.
TypeCoercionEngine::IlType TypeCoercionEngine::floatType() noexcept
{
    return IlType(IlType::Kind::F64);
}

/// @brief Return the IL pointer type.
/// @return IL type representing a pointer.
TypeCoercionEngine::IlType TypeCoercionEngine::ptrType() noexcept
{
    return IlType(IlType::Kind::Ptr);
}

/// @brief Map a BASIC AST type to an IL type.
/// @details Uses the canonical BASIC-to-IL mapping for scalar types, defaulting
///          to i64 for unrecognized or integer-like categories.
/// @param type BASIC AST type to map.
/// @return Corresponding IL type.
TypeCoercionEngine::IlType TypeCoercionEngine::astToIl(AstType type) noexcept
{
    switch (type)
    {
        case AstType::I64:
            return intType();
        case AstType::F64:
            return floatType();
        case AstType::Bool:
            return boolType();
        case AstType::Str:
            return ptrType();
        default:
            return intType(); // Default to integer
    }
}

// =============================================================================
// Widening Helpers
// =============================================================================

/// @brief Sign-extend a narrower integer to i64.
/// @details Emits a widening conversion from the specified bit width to 64 bits
///          using the common emission helper.
/// @param v Value to widen.
/// @param fromBits Original bit width (16 or 32).
/// @param loc Source location for emitted conversions.
/// @return Widened i64 value.
TypeCoercionEngine::Value TypeCoercionEngine::widenToI64(Value v,
                                                         int fromBits,
                                                         il::support::SourceLoc loc)
{
    Emit emit(lowerer_);
    return emit.at(loc).widen_to(v, fromBits, 64);
}

// =============================================================================
// Promotion Rules
// =============================================================================

/// @brief Compute the promoted numeric type for two operands.
/// @details If either operand is floating-point, the common type is f64;
///          otherwise the common type is i64.
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Promoted numeric type kind.
TypeCoercionEngine::Type::Kind TypeCoercionEngine::promoteNumeric(Type::Kind lhs,
                                                                  Type::Kind rhs) noexcept
{
    // If either operand is float, result is float
    if (lhs == Type::Kind::F64 || rhs == Type::Kind::F64)
        return Type::Kind::F64;

    // Otherwise, promote to i64
    return Type::Kind::I64;
}

/// @brief Coerce two operands to a common numeric type.
/// @details Uses @ref promoteNumeric to pick the common type and then applies
///          the required conversions in place.
/// @param lhs Left operand to coerce.
/// @param rhs Right operand to coerce.
/// @param loc Source location for emitted conversions.
void TypeCoercionEngine::promoteOperands(RVal &lhs, RVal &rhs, il::support::SourceLoc loc)
{
    Type::Kind common = promoteNumeric(lhs.type.kind, rhs.type.kind);

    if (common == Type::Kind::F64)
    {
        lhs = toF64(std::move(lhs), loc);
        rhs = toF64(std::move(rhs), loc);
    }
    else
    {
        lhs = toI64(std::move(lhs), loc);
        rhs = toI64(std::move(rhs), loc);
    }
}

// =============================================================================
// Internal Helpers
// =============================================================================

/// @brief Emit a unary IL instruction using the lowerer.
/// @details Thin wrapper around the lowerer's emission API to keep coercion
///          logic centralized and testable.
/// @param op Opcode of the unary instruction.
/// @param resultType Result type for the instruction.
/// @param operand Operand value to convert.
/// @return Emitted IL value.
TypeCoercionEngine::Value TypeCoercionEngine::emitUnary(Opcode op, Type resultType, Value operand)
{
    return lowerer_.emitUnary(op, resultType, operand);
}

/// @brief Convert a boolean to BASIC's logical i64 representation.
/// @details Emits the standard transformation that maps true to -1 and false to 0.
/// @param boolVal Boolean value to convert.
/// @return i64 value representing the BASIC logical word.
TypeCoercionEngine::Value TypeCoercionEngine::emitBoolToLogicalI64(Value boolVal)
{
    return lowerer_.emitBasicLogicalI64(boolVal);
}

} // namespace il::frontends::basic
