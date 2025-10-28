//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/EmitCommon.cpp
// Purpose: Define shared IR emission helpers for BASIC lowering to eliminate
//          repetitive instruction construction.
// Key invariants: Helpers update the Lowerer source location before emitting
//                 instructions, preserving diagnostic fidelity.
// Ownership/Lifetime: Emit borrows the Lowerer and never owns IR objects.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/EmitCommon.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace il::frontends::basic
{


Emit::Emit(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

Emit &Emit::at(il::support::SourceLoc loc) noexcept
{
    loc_ = loc;
    return *this;
}

Emit::Value Emit::to_iN(Value value, int bits, int fromBits, Signedness signedness) const
{
    if (bits == fromBits)
    {
        return value;
    }
    if (bits > fromBits)
    {
        return widen_to(value, fromBits, bits, signedness);
    }
    return narrow_to(value, fromBits, bits);
}

Emit::Value Emit::widen_to(Value value, int fromBits, int toBits, Signedness signedness) const
{
    assert(toBits == 64 && "widen_to currently supports widening to i64");
    if (fromBits == toBits)
    {
        return value;
    }
    if (fromBits == 1)
    {
        return emitUnary(il::core::Opcode::Zext1, intType(toBits), value);
    }
    if (fromBits == 16 || fromBits == 32)
    {
        const std::int64_t mask = (fromBits == 16) ? 0xFFFFll : 0xFFFFFFFFll;
        if (signedness == Signedness::Unsigned)
        {
            return emitBinary(il::core::Opcode::And, intType(toBits), value, il::core::Value::constInt(mask));
        }
        const int shift = (fromBits == 32) ? 32 : 48;
        Value masked = emitBinary(il::core::Opcode::And,
                                  intType(toBits),
                                  value,
                                  il::core::Value::constInt(mask));
        Value shl = emitBinary(il::core::Opcode::Shl,
                               intType(toBits),
                               masked,
                               il::core::Value::constInt(shift));
        return emitBinary(
            il::core::Opcode::AShr, intType(toBits), shl, il::core::Value::constInt(shift));
    }

    std::abort();
    return value;
}

Emit::Value Emit::narrow_to(Value value, int fromBits, int toBits) const
{
    if (fromBits == toBits)
    {
        return value;
    }
    if (toBits == 1)
    {
        return emitUnary(il::core::Opcode::Trunc1, intType(toBits), value);
    }
    return emitUnary(il::core::Opcode::CastSiNarrowChk, intType(toBits), value);
}

Emit::Value Emit::add_checked(Value lhs, Value rhs, OverflowPolicy policy, int bits) const
{
    const il::core::Opcode op =
        (policy == OverflowPolicy::Checked) ? il::core::Opcode::IAddOvf : il::core::Opcode::Add;
    return emitBinary(op, intType(bits), lhs, rhs);
}

Emit::Value Emit::logical_and(Value lhs, Value rhs, int bits) const
{
    return emitBinary(il::core::Opcode::And, intType(bits), lhs, rhs);
}

Emit::Value Emit::logical_or(Value lhs, Value rhs, int bits) const
{
    return emitBinary(il::core::Opcode::Or, intType(bits), lhs, rhs);
}

Emit::Value Emit::logical_xor(Value lhs, Value rhs, int bits) const
{
    return emitBinary(il::core::Opcode::Xor, intType(bits), lhs, rhs);
}

Emit::Type Emit::intType(int bits) const
{
    switch (bits)
    {
        case 1:
            return Type(Type::Kind::I1);
        case 16:
            return Type(Type::Kind::I16);
        case 32:
            return Type(Type::Kind::I32);
        case 64:
            return Type(Type::Kind::I64);
        default:
            break;
    }
    std::abort();
    return Type(Type::Kind::I64);
}

void Emit::applyLoc() const
{
    if (loc_)
    {
        lowerer_->curLoc = *loc_;
    }
}

Emit::Value Emit::emitUnary(Opcode op, Type ty, Value val) const
{
    applyLoc();
    return lowerer_->emitUnary(op, ty, val);
}

Emit::Value Emit::emitBinary(Opcode op, Type ty, Value lhs, Value rhs) const
{
    applyLoc();
    return lowerer_->emitBinary(op, ty, lhs, rhs);
}

} // namespace il::frontends::basic
