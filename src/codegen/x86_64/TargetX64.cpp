//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/TargetX64.cpp
// Purpose: Materialise the SysV AMD64 target description used by the Viper
//          Machine IR pipeline and surface helper queries about register
//          classes and names.
// Key invariants: The singleton target descriptor is initialised once with
//                 ABI-compliant register sets and thereafter treated as
//                 immutable global data.
// Ownership/Lifetime: All resources live in static storage, meaning callers do
//                     not own or free any structures retrieved from this
//                     translation unit.
// Links: docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "TargetX64.hpp"

namespace viper::codegen::x64
{

namespace
{

/// @brief Construct the SysV AMD64 target description for the backend.
///
/// @details Populates the @ref TargetInfo structure with register save
///          conventions, argument passing order, return registers, and stack
///          alignment information according to the System V ABI.  The helper is
///          evaluated exactly once to initialise the translation unit's static
///          singleton and the resulting object is treated as read-only.
///
/// @return Fully initialised SysV target descriptor.
TargetInfo makeSysVTarget()
{
    TargetInfo info{};
    info.callerSavedGPR = {
        PhysReg::RAX,
        PhysReg::RDI,
        PhysReg::RSI,
        PhysReg::RDX,
        PhysReg::RCX,
        PhysReg::R8,
        PhysReg::R9,
        PhysReg::R10,
        PhysReg::R11,
    };
    info.calleeSavedGPR = {
        PhysReg::RBX,
        PhysReg::R12,
        PhysReg::R13,
        PhysReg::R14,
        PhysReg::R15,
        PhysReg::RBP,
    };
    info.callerSavedXMM = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
        PhysReg::XMM8,
        PhysReg::XMM9,
        PhysReg::XMM10,
        PhysReg::XMM11,
        PhysReg::XMM12,
        PhysReg::XMM13,
        PhysReg::XMM14,
        PhysReg::XMM15,
    };
    info.calleeSavedXMM = {};
    info.intArgOrder = {
        PhysReg::RDI,
        PhysReg::RSI,
        PhysReg::RDX,
        PhysReg::RCX,
        PhysReg::R8,
        PhysReg::R9,
    };
    info.f64ArgOrder = {
        PhysReg::XMM0,
        PhysReg::XMM1,
        PhysReg::XMM2,
        PhysReg::XMM3,
        PhysReg::XMM4,
        PhysReg::XMM5,
        PhysReg::XMM6,
        PhysReg::XMM7,
    };
    info.intReturnReg = PhysReg::RAX;
    info.f64ReturnReg = PhysReg::XMM0;
    info.stackAlignment = 16U;
    info.hasRedZone = true; // Phase A: do not rely on red zone.
    return info;
}

TargetInfo sysvTargetInstance = makeSysVTarget();

} // namespace

/// @brief Retrieve the canonical SysV AMD64 target description.
///
/// @details Returns a reference to the statically initialised singleton created
///          by @ref makeSysVTarget().  The non-const reference allows callers to
///          pass the descriptor to APIs expecting mutable references while still
///          conceptually treating the object as immutable configuration data.
///
/// @return Reference to the global SysV target descriptor.
TargetInfo &sysvTarget() noexcept
{
    return sysvTargetInstance;
}

/// @brief Test whether a physical register is part of the general-purpose set.
///
/// @details Implements a switch over the enumerators recognised by the x86-64
///          backend.  The helper is used by register allocation and frame
///          lowering to discriminate between GPRs and other register classes.
///
/// @param reg Physical register to classify.
/// @return @c true when @p reg identifies a GPR.
bool isGPR(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::RAX:
        case PhysReg::RBX:
        case PhysReg::RCX:
        case PhysReg::RDX:
        case PhysReg::RSI:
        case PhysReg::RDI:
        case PhysReg::R8:
        case PhysReg::R9:
        case PhysReg::R10:
        case PhysReg::R11:
        case PhysReg::R12:
        case PhysReg::R13:
        case PhysReg::R14:
        case PhysReg::R15:
        case PhysReg::RBP:
        case PhysReg::RSP:
            return true;
        default:
            return false;
    }
}

/// @brief Determine whether a physical register belongs to the XMM class.
///
/// @details Mirrors @ref isGPR but enumerates the SIMD XMM registers recognised
///          by the backend.  Used in spill slot planning and instruction
///          selection when choosing encodings for floating-point operands.
///
/// @param reg Physical register to classify.
/// @return @c true when @p reg is an XMM register.
bool isXMM(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::XMM0:
        case PhysReg::XMM1:
        case PhysReg::XMM2:
        case PhysReg::XMM3:
        case PhysReg::XMM4:
        case PhysReg::XMM5:
        case PhysReg::XMM6:
        case PhysReg::XMM7:
        case PhysReg::XMM8:
        case PhysReg::XMM9:
        case PhysReg::XMM10:
        case PhysReg::XMM11:
        case PhysReg::XMM12:
        case PhysReg::XMM13:
        case PhysReg::XMM14:
        case PhysReg::XMM15:
            return true;
        default:
            return false;
    }
}

/// @brief Map a physical register to its textual assembly representation.
///
/// @details The backend prints registers in AT&T syntax.  This helper covers
///          every register enumerator currently used by the backend and
///          provides a stable string literal suitable for emission into
///          assembly listings or diagnostics.  Unknown registers fall through to
///          the @c default case and return a sentinel string.
///
/// @param reg Physical register whose name is requested.
/// @return Null-terminated string containing the register mnemonic.
const char *regName(PhysReg reg) noexcept
{
    switch (reg)
    {
        case PhysReg::RAX:
            return "%rax";
        case PhysReg::RBX:
            return "%rbx";
        case PhysReg::RCX:
            return "%rcx";
        case PhysReg::RDX:
            return "%rdx";
        case PhysReg::RSI:
            return "%rsi";
        case PhysReg::RDI:
            return "%rdi";
        case PhysReg::R8:
            return "%r8";
        case PhysReg::R9:
            return "%r9";
        case PhysReg::R10:
            return "%r10";
        case PhysReg::R11:
            return "%r11";
        case PhysReg::R12:
            return "%r12";
        case PhysReg::R13:
            return "%r13";
        case PhysReg::R14:
            return "%r14";
        case PhysReg::R15:
            return "%r15";
        case PhysReg::RBP:
            return "%rbp";
        case PhysReg::RSP:
            return "%rsp";
        case PhysReg::XMM0:
            return "%xmm0";
        case PhysReg::XMM1:
            return "%xmm1";
        case PhysReg::XMM2:
            return "%xmm2";
        case PhysReg::XMM3:
            return "%xmm3";
        case PhysReg::XMM4:
            return "%xmm4";
        case PhysReg::XMM5:
            return "%xmm5";
        case PhysReg::XMM6:
            return "%xmm6";
        case PhysReg::XMM7:
            return "%xmm7";
        case PhysReg::XMM8:
            return "%xmm8";
        case PhysReg::XMM9:
            return "%xmm9";
        case PhysReg::XMM10:
            return "%xmm10";
        case PhysReg::XMM11:
            return "%xmm11";
        case PhysReg::XMM12:
            return "%xmm12";
        case PhysReg::XMM13:
            return "%xmm13";
        case PhysReg::XMM14:
            return "%xmm14";
        case PhysReg::XMM15:
            return "%xmm15";
        default:
            return "%unknown";
    }
}

} // namespace viper::codegen::x64
