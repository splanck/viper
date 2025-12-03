//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/TargetAArch64.hpp
// Purpose: Define physical registers, register classes, and target metadata for the
// Key invariants: To be documented.
// Ownership/Lifetime: No heap ownership beyond the singleton target info; containers live for the
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <optional>
#include <vector>

namespace viper::codegen::aarch64
{

// 64-bit general purpose and SIMD registers used by AArch64 ABI.
enum class PhysReg
{
    X0,
    X1,
    X2,
    X3,
    X4,
    X5,
    X6,
    X7,
    X8, // Indirect result / frame pointer in some ABIs; treated as caller-saved here
    X9,
    X10,
    X11,
    X12,
    X13,
    X14,
    X15,
    X16,
    X17, // intra-proc call registers
    X18, // platform reserved on some OSes; do not allocate
    X19,
    X20,
    X21,
    X22,
    X23,
    X24,
    X25,
    X26,
    X27,
    X28,
    X29, // FP
    X30, // LR
    SP,
    // Floating-point / SIMD 64-bit lanes (we model D-registers)
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8,
    V9,
    V10,
    V11,
    V12,
    V13,
    V14,
    V15,
    V16,
    V17,
    V18,
    V19,
    V20,
    V21,
    V22,
    V23,
    V24,
    V25,
    V26,
    V27,
    V28,
    V29,
    V30,
    V31,
};

enum class RegClass
{
    GPR,
    FPR
};

// =============================================================================
// AArch64 ABI Constants
// =============================================================================

/// @brief Size of a stack slot in bytes (8 bytes for 64-bit values).
inline constexpr int kSlotSizeBytes = 8;

/// @brief Required stack alignment in bytes (16-byte alignment per AAPCS64).
inline constexpr int kStackAlignment = 16;

/// @brief Maximum number of GPR arguments passed in registers (x0-x7).
inline constexpr std::size_t kMaxGPRArgs = 8;

/// @brief Maximum number of FPR arguments passed in registers (v0-v7).
inline constexpr std::size_t kMaxFPRArgs = 8;

/// @brief Scratch register for codegen (not used for allocation, safe to clobber).
inline constexpr PhysReg kScratchGPR = PhysReg::X9;

/// @brief Scratch FPR register for codegen (not used for allocation).
inline constexpr PhysReg kScratchFPR = PhysReg::V16;

// =============================================================================
// Target Information
// =============================================================================

struct TargetInfo
{
    std::vector<PhysReg> callerSavedGPR{};
    std::vector<PhysReg> calleeSavedGPR{};
    std::vector<PhysReg> callerSavedFPR{};
    std::vector<PhysReg> calleeSavedFPR{};
    std::array<PhysReg, kMaxGPRArgs> intArgOrder{};
    std::array<PhysReg, kMaxFPRArgs> f64ArgOrder{};
    PhysReg intReturnReg{PhysReg::X0};
    PhysReg f64ReturnReg{PhysReg::V0};
    unsigned stackAlignment{16U};
};

// =============================================================================
// Calling Convention Abstraction
// =============================================================================

/// @brief Provides a clean interface to the AAPCS64 calling convention.
///
/// This class encapsulates the rules for passing arguments and returning values
/// according to the ARM Architecture Procedure Call Standard for 64-bit (AAPCS64).
///
/// @invariant The TargetInfo reference must remain valid for the lifetime of this object.
class CallingConvention
{
  public:
    explicit CallingConvention(const TargetInfo &ti) noexcept : ti_(ti) {}

    // =========================================================================
    // Argument Passing
    // =========================================================================

    /// @brief Get the register for an integer argument at the given index.
    /// @param index Zero-based argument index.
    /// @return The physical register, or std::nullopt if passed on stack.
    [[nodiscard]] std::optional<PhysReg> getIntArgReg(std::size_t index) const noexcept
    {
        if (index < ti_.intArgOrder.size())
            return ti_.intArgOrder[index];
        return std::nullopt;
    }

    /// @brief Get the register for a floating-point argument at the given index.
    /// @param index Zero-based argument index.
    /// @return The physical register, or std::nullopt if passed on stack.
    [[nodiscard]] std::optional<PhysReg> getFPArgReg(std::size_t index) const noexcept
    {
        if (index < ti_.f64ArgOrder.size())
            return ti_.f64ArgOrder[index];
        return std::nullopt;
    }

    /// @brief Check if an integer argument at the given index is passed in a register.
    [[nodiscard]] bool isIntArgInReg(std::size_t index) const noexcept
    {
        return index < ti_.intArgOrder.size();
    }

    /// @brief Check if a floating-point argument at the given index is passed in a register.
    [[nodiscard]] bool isFPArgInReg(std::size_t index) const noexcept
    {
        return index < ti_.f64ArgOrder.size();
    }

    /// @brief Get the maximum number of integer arguments passed in registers.
    [[nodiscard]] std::size_t maxIntArgsInRegs() const noexcept
    {
        return ti_.intArgOrder.size();
    }

    /// @brief Get the maximum number of FP arguments passed in registers.
    [[nodiscard]] std::size_t maxFPArgsInRegs() const noexcept
    {
        return ti_.f64ArgOrder.size();
    }

    // =========================================================================
    // Return Values
    // =========================================================================

    /// @brief Get the register used for returning integer values.
    [[nodiscard]] PhysReg getIntReturnReg() const noexcept
    {
        return ti_.intReturnReg;
    }

    /// @brief Get the register used for returning floating-point values.
    [[nodiscard]] PhysReg getFPReturnReg() const noexcept
    {
        return ti_.f64ReturnReg;
    }

    // =========================================================================
    // Stack Layout
    // =========================================================================

    /// @brief Get the required stack alignment in bytes.
    [[nodiscard]] unsigned getStackAlignment() const noexcept
    {
        return ti_.stackAlignment;
    }

    /// @brief Get the size of a stack slot in bytes.
    [[nodiscard]] static constexpr int getSlotSize() noexcept
    {
        return kSlotSizeBytes;
    }

  private:
    const TargetInfo &ti_;
};

[[nodiscard]] TargetInfo &darwinTarget() noexcept;
[[nodiscard]] bool isGPR(PhysReg reg) noexcept;
[[nodiscard]] bool isFPR(PhysReg reg) noexcept;
[[nodiscard]] const char *regName(PhysReg reg) noexcept;

// =============================================================================
// Immediate Value Validation
// =============================================================================

/// @brief Check if an immediate fits in an unsigned 12-bit field (0-4095).
///
/// Used for add/sub immediate instructions without shift.
[[nodiscard]] constexpr bool isUImm12(long long imm) noexcept
{
    return imm >= 0 && imm <= 4095;
}

/// @brief Check if an immediate fits in a signed 9-bit field (-256 to 255).
///
/// Used for unscaled load/store addressing modes.
[[nodiscard]] constexpr bool isSImm9(long long imm) noexcept
{
    return imm >= -256 && imm <= 255;
}

/// @brief Check if an immediate fits in an unsigned 12-bit scaled field.
///
/// For 64-bit loads/stores, immediate must be a multiple of 8 in range [0, 32760].
/// @param imm The immediate offset.
/// @param scale Scale factor (8 for 64-bit, 4 for 32-bit, etc.).
[[nodiscard]] constexpr bool isScaledUImm12(long long imm, int scale) noexcept
{
    if (imm < 0 || imm % scale != 0)
        return false;
    return (imm / scale) <= 4095;
}

/// @brief Check if an immediate is valid for shift instructions (0-63 for 64-bit).
[[nodiscard]] constexpr bool isValidShiftAmount(long long imm) noexcept
{
    return imm >= 0 && imm <= 63;
}

/// @brief Check if an immediate can be represented as a mov immediate.
///
/// AArch64 mov immediate can encode values where at most one 16-bit chunk
/// is non-zero, or the value can be represented as an inverted bitmask.
/// For simplicity, this checks if the value fits in 16 bits or can use movz/movn.
[[nodiscard]] constexpr bool isSimpleMovImm(long long imm) noexcept
{
    // Values that fit in 16 bits with possible shift
    if (imm >= 0 && imm <= 0xFFFF)
        return true;
    if (imm >= 0 && (imm & 0xFFFF) == 0 && ((imm >> 16) & 0xFFFF) <= 0xFFFF && ((imm >> 32) == 0))
        return true;
    // Negative values that can use movn
    if (imm < 0 && imm >= -0x10000)
        return true;
    return false;
}

/// @brief Check if an immediate requires a multi-instruction sequence.
///
/// Returns true if the immediate cannot be encoded in a single mov instruction.
[[nodiscard]] constexpr bool needsWideImmSequence(long long imm) noexcept
{
    return !isSimpleMovImm(imm);
}

} // namespace viper::codegen::aarch64
