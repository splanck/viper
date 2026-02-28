//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/TargetAArch64.hpp
// Purpose: Define physical registers, register classes, and target metadata for AArch64.
// Key invariants: Register enums map 1:1 to hardware registers; TargetInfo singleton
//   is immutable after initialization; AAPCS64 calling convention rules are encoded.
// Ownership/Lifetime: No heap ownership; darwinTarget() returns a reference to a
//   static singleton that lives for the duration of the program.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/TargetInfoBase.hpp"

#include <optional>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/// @brief AArch64 target-specific definitions for the Viper code generator.
///
/// This namespace contains all AArch64-specific constants, types, and functions
/// needed for code generation, including:
/// - Physical register definitions (GPR and FPR)
/// - ABI constants (stack alignment, argument passing limits)
/// - Calling convention abstraction
/// - Immediate value validation helpers
///
/// The definitions follow the AAPCS64 (ARM 64-bit Procedure Call Standard)
/// and target macOS/Darwin, though most definitions are platform-independent.
namespace viper::codegen::aarch64
{

/// @brief Physical register identifiers for AArch64.
///
/// Enumerates all 64-bit general purpose registers (X0-X30, SP) and floating-point/
/// SIMD registers (V0-V31) used by the AArch64 instruction set. These correspond
/// directly to the hardware register file.
///
/// ## General Purpose Registers
///
/// | Register | AAPCS64 Usage                                          |
/// |----------|--------------------------------------------------------|
/// | X0-X7    | Argument/result registers (caller-saved)               |
/// | X8       | Indirect result location register (caller-saved)       |
/// | X9-X15   | Temporary registers (caller-saved)                     |
/// | X16-X17  | Intra-procedure-call scratch registers (IP0/IP1)       |
/// | X18      | Platform register (reserved on some OSes, don't use)   |
/// | X19-X28  | Callee-saved registers                                 |
/// | X29      | Frame pointer (FP)                                     |
/// | X30      | Link register (LR, holds return address)               |
/// | SP       | Stack pointer (must be 16-byte aligned)                |
///
/// ## Floating-Point/SIMD Registers
///
/// | Register | AAPCS64 Usage                                          |
/// |----------|--------------------------------------------------------|
/// | V0-V7    | Argument/result registers (caller-saved)               |
/// | V8-V15   | Callee-saved (only lower 64 bits / D-register)         |
/// | V16-V31  | Temporary registers (caller-saved)                     |
///
/// @note We model V registers as their 64-bit D-register aliases since Viper
///       currently only supports scalar floating-point (f64) values.
///
/// @see RegClass for distinguishing GPR vs FPR
/// @see isGPR(), isFPR() for register class queries
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

/// @brief Register class discriminator for AArch64.
///
/// Distinguishes between general-purpose registers (GPR) and floating-point/
/// SIMD registers (FPR). This is essential for register allocation since the
/// two classes have separate register files and different allocation pools.
///
/// @note AArch64 has a clean separation between GPR and FPR - there are no
///       instructions that use both classes in a single operand position.
enum class RegClass
{
    GPR, ///< General-purpose registers (X0-X30, SP)
    FPR  ///< Floating-point/SIMD registers (V0-V31 / D0-D31)
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

/// @brief Describes the target platform's ABI and register conventions.
///
/// TargetInfo encapsulates all platform-specific details needed for code generation:
/// - Which registers are caller-saved vs. callee-saved
/// - The order of registers used for argument passing
/// - The registers used for return values
/// - Stack alignment requirements
///
/// ## Usage
///
/// Use darwinTarget() to get the singleton instance for macOS/Darwin. The TargetInfo
/// is typically accessed through CallingConvention for a cleaner API.
///
/// @see CallingConvention for a higher-level interface to calling convention rules
/// @see darwinTarget() for the macOS/Darwin target instance
/// @see linuxTarget()  for the Linux ELF target instance

/// @brief Identifies the target OS ABI for assembly emission.
/// Controls symbol mangling (underscore prefix) and format-specific directives.
///
///  - Darwin:  '_' prefix on symbols; Mach-O format; no ELF directives.
///  - Linux:   No prefix; ELF format; .type/.size directives required.
///  - Windows: No prefix; PE/COFF format; no ELF .type/.size directives.
///             Register conventions are identical to Linux AAPCS64.
enum class ABIFormat
{
    Darwin,  ///< macOS/iOS; symbols prefixed with '_', Mach-O format.
    Linux,   ///< Linux ELF; no symbol prefix, .type/.size required.
    Windows, ///< Windows ARM64; no symbol prefix, PE/COFF format (no .type/.size).
};

struct TargetInfo : viper::codegen::common::TargetInfoBase<PhysReg, kMaxGPRArgs, kMaxFPRArgs>
{
    ABIFormat abiFormat = ABIFormat::Darwin;

    /// @brief Returns true when emitting Linux ELF assembly.
    [[nodiscard]] bool isLinux() const noexcept
    {
        return abiFormat == ABIFormat::Linux;
    }

    /// @brief Returns true when emitting Windows ARM64 PE/COFF assembly.
    [[nodiscard]] bool isWindows() const noexcept
    {
        return abiFormat == ABIFormat::Windows;
    }
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

/// @brief Returns the singleton TargetInfo for macOS/Darwin on AArch64.
///
/// Provides the platform-specific register conventions and ABI details for
/// macOS running on Apple Silicon. The returned reference is to a static
/// object that persists for the lifetime of the program.
///
/// @return Reference to the Darwin target information singleton.
[[nodiscard]] const TargetInfo &darwinTarget() noexcept;

/// @brief Return the singleton TargetInfo for Linux AArch64 (ELF / AAPCS64).
/// Same register convention as Darwin; differs only in assembly syntax
/// (no underscore prefix, emits .type/.size ELF directives).
[[nodiscard]] const TargetInfo &linuxTarget() noexcept;

/// @brief Return the singleton TargetInfo for Windows ARM64 (PE/COFF / AAPCS64).
/// Identical register convention to Linux; differs in assembly syntax:
/// no underscore prefix, no ELF .type/.size directives (PE/COFF format).
[[nodiscard]] const TargetInfo &windowsTarget() noexcept;

/// @brief Tests whether a physical register is a general-purpose register.
/// @param reg The register to test.
/// @return True if reg is X0-X30 or SP, false if it's a FPR.
[[nodiscard]] bool isGPR(PhysReg reg) noexcept;

/// @brief Tests whether a physical register is a floating-point register.
/// @param reg The register to test.
/// @return True if reg is V0-V31, false if it's a GPR.
[[nodiscard]] bool isFPR(PhysReg reg) noexcept;

/// @brief Returns the assembly syntax name for a physical register.
///
/// Returns strings like "x0", "x29", "sp", "d0", etc. for use in
/// assembly output. For FPRs, returns the D-register name since Viper
/// uses 64-bit scalar floating-point.
///
/// @param reg The register to name.
/// @return The register's assembly name (static storage, do not free).
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

/// @brief Check if a 64-bit immediate is encodable as an AArch64 logical immediate.
///
/// AArch64 logical immediates (AND/ORR/EOR) encode values that consist of a
/// replicated bit-pattern across a 64-bit word.  A valid pattern is a run of
/// 1-bits (possibly rotated) repeated to fill an element of size 2, 4, 8, 16,
/// 32, or 64 bits.  Values of 0 and ~0 are excluded (not representable).
///
/// Algorithm (see ARM DDI 0487 §C5.1.3 DecodeBitMasks):
///   1. For each element size (64, 32, 16, 8, 4, 2 bits) check whether the
///      value is a consistent replication of an element-sized chunk.
///   2. Within the element, check that the set bits form a contiguous run
///      (possibly rotated — i.e. the bits wrap from MSB to LSB).
///
/// @param imm The immediate value to test (interpreted as unsigned 64-bit).
/// @return true if @p imm can be used directly as a logical immediate operand.
[[nodiscard]] inline bool isLogicalImmediate(uint64_t imm) noexcept
{
    // 0 and ~0 are never valid logical immediates.
    if (imm == 0 || imm == ~uint64_t(0))
        return false;

    // Try each element size from 2 up to 64 bits.
    // For each size N: check whether imm is a consistent replication of an
    // N-bit chunk, and whether that chunk is a contiguous run of 1-bits
    // (possibly rotated to wrap from MSB to LSB).
    for (int N : {2, 4, 8, 16, 32, 64})
    {
        const uint64_t mask = (N == 64) ? ~uint64_t(0) : ((uint64_t(1) << N) - 1);
        const uint64_t elem = imm & mask;

        // Skip all-zero elements.
        if (elem == 0)
            continue;

        // Verify all N-bit chunks of imm are identical to elem.
        if (N < 64)
        {
            uint64_t replicated = elem;
            for (int shift = N; shift < 64; shift += N)
                replicated |= (elem << shift);
            if (replicated != imm)
                continue;
        }

        // elem is the representative N-bit pattern.
        // Check that its bits form a single contiguous run (possibly rotated).
        //
        // Method: rotate elem left by 1 within N bits, XOR with original.
        // A contiguous run has exactly 2 transitions (one 0→1, one 1→0)
        // in the circular bit sequence, so popcount(elem XOR rotate(elem, 1)) == 2.
        //
        // Special case: if elem == mask (all ones in N bits), skip — it would
        // replicate to ~0 which is excluded, but for safety skip anyway.
        if (elem == mask)
            continue;

        const uint64_t rotLeft1 = ((elem << 1) | (elem >> (N - 1))) & mask;
        const uint64_t transitions = elem ^ rotLeft1;
#ifdef _MSC_VER
        if (__popcnt64(transitions) == 2)
#else
        if (__builtin_popcountll(transitions) == 2)
#endif
            return true;
    }
    return false;
}

} // namespace viper::codegen::aarch64
