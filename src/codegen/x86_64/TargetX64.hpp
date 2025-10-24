// src/codegen/x86_64/TargetX64.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Define physical registers, register classes, and target metadata for the
//          System V AMD64 ABI backend used by Viper's x86-64 code generator.
// Invariants: Data remains immutable once constructed; accessors return references to
//             shared singleton state describing the ABI contract.
// Ownership: No heap ownership beyond the singleton target info; containers live for the
//            program lifetime.
// Notes: Standalone header — depends only on the C++ standard library.

#pragma once

#include <array>
#include <vector>

namespace viper::codegen::x64
{

/// \brief Enumerates the physical registers recognised by the SysV AMD64 ABI.
/// \details Covers the 64-bit general-purpose registers and the XMM vector register file.
enum class PhysReg
{
    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    RBP,
    RSP,
    XMM0,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15
};

/// \brief Register classification used by the allocator and instruction selector.
enum class RegClass
{
    GPR,
    XMM
};

/// \brief Captures the architectural contract for the SysV AMD64 ABI.
/// \invariant Vectors are populated once during singleton creation and remain constant.
struct TargetInfo
{
    /// \brief Caller-saved general purpose registers.
    std::vector<PhysReg> callerSavedGPR{};
    /// \brief Callee-saved general purpose registers.
    std::vector<PhysReg> calleeSavedGPR{};
    /// \brief Caller-saved XMM registers.
    std::vector<PhysReg> callerSavedXMM{};
    /// \brief Callee-saved XMM registers.
    std::vector<PhysReg> calleeSavedXMM{};
    /// \brief ABI argument order for integer and pointer values.
    std::array<PhysReg, 6> intArgOrder{};
    /// \brief ABI argument order for 64-bit floating-point values.
    std::array<PhysReg, 8> f64ArgOrder{};
    /// \brief Register used to return integer and pointer values.
    PhysReg intReturnReg{PhysReg::RAX};
    /// \brief Register used to return 64-bit floating-point values.
    PhysReg f64ReturnReg{PhysReg::XMM0};
    /// \brief Required stack alignment at call boundaries (bytes).
    unsigned stackAlignment{16U};
    /// \brief Whether the ABI specifies a red zone.
    /// Phase A: do not rely on red zone.
    bool hasRedZone{true};
};

/// \brief Returns the singleton SysV target description.
[[nodiscard]] TargetInfo &sysvTarget() noexcept;

/// \brief Determines if a physical register belongs to the general-purpose class.
[[nodiscard]] bool isGPR(PhysReg reg) noexcept;

/// \brief Determines if a physical register belongs to the XMM class.
[[nodiscard]] bool isXMM(PhysReg reg) noexcept;

/// \brief Provides the canonical AT&T name of a physical register.
[[nodiscard]] const char *regName(PhysReg reg) noexcept;

} // namespace viper::codegen::x64
