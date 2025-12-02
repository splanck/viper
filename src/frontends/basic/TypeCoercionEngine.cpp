//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/TypeCoercionEngine.cpp
// Purpose: Implementation of centralized type coercion logic.
//
// Key invariants:
//   - Coercions are idempotent (same-type conversions return input unchanged)
//   - BASIC TRUE = -1, FALSE = 0 semantics are preserved
//   - Narrower integers are sign-extended before further conversion
//
// Ownership/Lifetime: See TypeCoercionEngine.hpp
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/TypeCoercionEngine.hpp"

#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

TypeCoercionEngine::TypeCoercionEngine(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

// =============================================================================
// Primary Coercion Methods
// =============================================================================

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

bool TypeCoercionEngine::isI64(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::I64;
}

bool TypeCoercionEngine::isF64(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::F64;
}

bool TypeCoercionEngine::isBool(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::I1;
}

bool TypeCoercionEngine::isString(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::Ptr;
}

bool TypeCoercionEngine::isPointer(const RVal &v) noexcept
{
    return v.type.kind == Type::Kind::Ptr;
}

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

TypeCoercionEngine::IlType TypeCoercionEngine::boolType() noexcept
{
    return IlType(IlType::Kind::I1);
}

TypeCoercionEngine::IlType TypeCoercionEngine::intType() noexcept
{
    return IlType(IlType::Kind::I64);
}

TypeCoercionEngine::IlType TypeCoercionEngine::floatType() noexcept
{
    return IlType(IlType::Kind::F64);
}

TypeCoercionEngine::IlType TypeCoercionEngine::ptrType() noexcept
{
    return IlType(IlType::Kind::Ptr);
}

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

TypeCoercionEngine::Value TypeCoercionEngine::widenToI64(Value v, int fromBits, il::support::SourceLoc loc)
{
    Emit emit(lowerer_);
    return emit.at(loc).widen_to(v, fromBits, 64);
}

// =============================================================================
// Promotion Rules
// =============================================================================

TypeCoercionEngine::Type::Kind TypeCoercionEngine::promoteNumeric(Type::Kind lhs, Type::Kind rhs) noexcept
{
    // If either operand is float, result is float
    if (lhs == Type::Kind::F64 || rhs == Type::Kind::F64)
        return Type::Kind::F64;

    // Otherwise, promote to i64
    return Type::Kind::I64;
}

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

TypeCoercionEngine::Value TypeCoercionEngine::emitUnary(Opcode op, Type resultType, Value operand)
{
    return lowerer_.emitUnary(op, resultType, operand);
}

TypeCoercionEngine::Value TypeCoercionEngine::emitBoolToLogicalI64(Value boolVal)
{
    return lowerer_.emitBasicLogicalI64(boolVal);
}

} // namespace il::frontends::basic
