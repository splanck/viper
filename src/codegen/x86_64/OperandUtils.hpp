// src/codegen/x86_64/OperandUtils.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Provide shared utility functions for Machine IR operand manipulation,
//          reducing code duplication across the x86-64 backend.
// Invariants: All helpers are stateless and safe to call from any context.
//             Functions preserve operand semantics and never mutate global state.
// Ownership: Helpers operate on values or const references; callers retain
//            ownership of all operands.
// Notes: This header consolidates helpers previously duplicated in ISel.cpp,
//        CallLowering.cpp, FrameLowering.cpp, and LowerILToMIR.cpp.

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <variant>

namespace viper::codegen::x64
{

// -----------------------------------------------------------------------------
// Operand cloning and copying
// -----------------------------------------------------------------------------

/// \brief Produce a shallow copy of an operand for reuse in new instructions.
/// \details Machine IR operands are small value types. This helper exists to
///          make clone intent explicit at call sites where code constructs new
///          instructions from existing operands (for example inserting a
///          movzx after a setcc).
/// \param operand Operand to copy.
/// \return A value-equal copy of the operand.
[[nodiscard]] inline Operand cloneOperand(const Operand &operand)
{
    return operand;
}

// -----------------------------------------------------------------------------
// Operand type checking and casting
// -----------------------------------------------------------------------------

/// \brief Determine whether an operand stores an immediate value.
/// \param operand Operand to classify.
/// \return True when the operand holds an OpImm payload.
[[nodiscard]] inline bool isImm(const Operand &operand) noexcept
{
    return std::holds_alternative<OpImm>(operand);
}

/// \brief Determine whether an operand stores a register.
/// \param operand Operand to classify.
/// \return True when the operand holds an OpReg payload.
[[nodiscard]] inline bool isReg(const Operand &operand) noexcept
{
    return std::holds_alternative<OpReg>(operand);
}

/// \brief Determine whether an operand stores a memory reference.
/// \param operand Operand to classify.
/// \return True when the operand holds an OpMem payload.
[[nodiscard]] inline bool isMem(const Operand &operand) noexcept
{
    return std::holds_alternative<OpMem>(operand);
}

/// \brief Determine whether an operand stores a label.
/// \param operand Operand to classify.
/// \return True when the operand holds an OpLabel payload.
[[nodiscard]] inline bool isLabel(const Operand &operand) noexcept
{
    return std::holds_alternative<OpLabel>(operand);
}

/// \brief View an operand as an immediate when possible.
/// \details Wraps std::get_if to centralise the cast and emphasise the
///          nullable nature of the conversion.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpImm payload or nullptr on mismatch.
[[nodiscard]] inline OpImm *asImm(Operand &operand) noexcept
{
    return std::get_if<OpImm>(&operand);
}

/// \brief View a const operand as an immediate when possible.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpImm payload or nullptr on mismatch.
[[nodiscard]] inline const OpImm *asImm(const Operand &operand) noexcept
{
    return std::get_if<OpImm>(&operand);
}

/// \brief View a mutable operand as a register reference.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpReg payload or nullptr when not a register.
[[nodiscard]] inline OpReg *asReg(Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

/// \brief View a read-only operand as a register reference.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpReg payload or nullptr when not a register.
[[nodiscard]] inline const OpReg *asReg(const Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

/// \brief View a mutable operand as a memory reference.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpMem payload or nullptr when not memory.
[[nodiscard]] inline OpMem *asMem(Operand &operand) noexcept
{
    return std::get_if<OpMem>(&operand);
}

/// \brief View a read-only operand as a memory reference.
/// \param operand Operand to reinterpret.
/// \return Pointer to the OpMem payload or nullptr when not memory.
[[nodiscard]] inline const OpMem *asMem(const Operand &operand) noexcept
{
    return std::get_if<OpMem>(&operand);
}

// -----------------------------------------------------------------------------
// Register comparison
// -----------------------------------------------------------------------------

/// \brief Compare two operands for register identity.
/// \details The check covers both physical and virtual registers by comparing
///          the register class, physical flag, and numeric identifier. Used to
///          detect whether two operands refer to the same register so peepholes
///          can avoid duplicating work.
/// \param lhs First operand to compare.
/// \param rhs Second operand to compare.
/// \return True when both operands refer to the same register.
[[nodiscard]] inline bool sameRegister(const Operand &lhs, const Operand &rhs) noexcept
{
    const auto *lhsReg = asReg(lhs);
    const auto *rhsReg = asReg(rhs);
    if (!lhsReg || !rhsReg)
    {
        return false;
    }
    return lhsReg->isPhys == rhsReg->isPhys && lhsReg->cls == rhsReg->cls &&
           lhsReg->idOrPhys == rhsReg->idOrPhys;
}

// -----------------------------------------------------------------------------
// Physical register operand construction helpers
// -----------------------------------------------------------------------------

/// \brief Create an operand referencing a concrete physical register.
/// \details Wraps makePhysRegOperand to make call sites more readable when
///          they need an Operand representing a hardware register. The
///          helper preserves the register class supplied by the caller.
/// \param cls Register class describing the operand kind (GPR/XMM).
/// \param reg Physical register enumerator chosen by the caller.
/// \return Operand that refers to the requested physical register.
[[nodiscard]] inline Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// \brief Build an OpReg operand anchored to a physical GPR base register.
/// \details The backend frequently needs an addressing base for stack-relative
///          memory operands. This helper constructs the canonical OpReg
///          representation referencing reg in the general purpose register class.
/// \param reg Physical register serving as the base of an address expression.
/// \return Register operand pointing at the supplied physical register.
[[nodiscard]] inline OpReg makePhysBase(PhysReg reg)
{
    return makePhysReg(RegClass::GPR, static_cast<uint16_t>(reg));
}

// -----------------------------------------------------------------------------
// Alignment and rounding utilities
// -----------------------------------------------------------------------------

/// \brief Round value up to the nearest multiple of align.
/// \details Used when computing spill areas and outgoing argument space to
///          maintain stack alignment. Alignment is assumed to be positive.
/// \param value Base value to round.
/// \param align Required alignment in bytes (must be > 0).
/// \return Smallest multiple of align that is greater than or equal to value.
[[nodiscard]] inline int roundUp(int value, int align)
{
    assert(align > 0 && "alignment must be positive");
    const int remainder = value % align;
    if (remainder == 0)
    {
        return value;
    }
    return value + (align - remainder);
}

/// \brief Round bytes up to the nearest multiple of align, returning size_t.
/// \details Variant of roundUp for size_t arguments, commonly used for
///          argument slot allocation.
/// \param bytes Requested byte count.
/// \param align Required alignment in bytes (must be > 0).
/// \return Smallest multiple of align >= bytes.
[[nodiscard]] inline std::size_t roundUpSize(std::size_t bytes, std::size_t align)
{
    assert(align > 0 && "alignment must be positive");
    const std::size_t remainder = bytes % align;
    if (remainder == 0)
    {
        return bytes;
    }
    return bytes + (align - remainder);
}

} // namespace viper::codegen::x64
