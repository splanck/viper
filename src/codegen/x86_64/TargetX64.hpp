//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/TargetX64.hpp
// Purpose: Define physical registers, register classes, and SysV ABI metadata.
// Key invariants: sysvTarget() returns a singleton initialised once on first call;
//                 caller/callee-saved sets and argument order are immutable after
//                 initialisation; kStackAlignment is always 16 bytes.
// Ownership/Lifetime: No heap ownership beyond the singleton target info; containers live for the
//                     duration of the process.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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

// -----------------------------------------------------------------------------
// Architecture constants
// -----------------------------------------------------------------------------

/// \brief Size of a single spill slot or stack slot in bytes.
/// \details All stack-based values (spills, outgoing arguments, etc.) are
///          allocated in 8-byte increments to match the pointer size and
///          maintain natural alignment for scalar values.
inline constexpr int kSlotSizeBytes = 8;

/// \brief Required stack alignment at function call boundaries (bytes).
/// \details The SysV AMD64 ABI mandates 16-byte stack alignment immediately
///          before a CALL instruction executes. This constant is used by
///          frame lowering and call lowering to enforce alignment.
inline constexpr int kStackAlignment = 16;

/// \brief Page size for stack probing (bytes).
/// \details When allocating stack frames larger than this threshold, the code
///          generator emits stack probing code to ensure the guard page is
///          touched and stack overflow is detected properly.
inline constexpr int kPageSize = 4096;

/// \brief Maximum number of integer/pointer arguments passed in registers (SysV).
/// \details The SysV AMD64 ABI allows up to 6 integer arguments in registers
///          (RDI, RSI, RDX, RCX, R8, R9). Additional arguments go on the stack.
inline constexpr std::size_t kMaxGPRArgsSysV = 6;

/// \brief Maximum number of floating-point arguments passed in registers (SysV).
/// \details The SysV AMD64 ABI allows up to 8 floating-point arguments in
///          XMM registers (XMM0-XMM7). Additional arguments go on the stack.
inline constexpr std::size_t kMaxXMMArgsSysV = 8;

/// \brief Maximum number of integer/pointer arguments passed in registers (Windows).
/// \details The Windows x64 ABI allows up to 4 integer arguments in registers
///          (RCX, RDX, R8, R9). Additional arguments go on the stack.
inline constexpr std::size_t kMaxGPRArgsWin64 = 4;

/// \brief Maximum number of floating-point arguments passed in registers (Windows).
/// \details The Windows x64 ABI allows up to 4 floating-point arguments in
///          XMM registers (XMM0-XMM3). Additional arguments go on the stack.
inline constexpr std::size_t kMaxXMMArgsWin64 = 4;

/// \brief Platform-appropriate max GPR args.
#if defined(_WIN32)
inline constexpr std::size_t kMaxGPRArgs = kMaxGPRArgsWin64;
inline constexpr std::size_t kMaxXMMArgs = kMaxXMMArgsWin64;
#else
inline constexpr std::size_t kMaxGPRArgs = kMaxGPRArgsSysV;
inline constexpr std::size_t kMaxXMMArgs = kMaxXMMArgsSysV;
#endif

/// \brief Captures the architectural contract for an x86-64 ABI.
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
    /// \brief Maximum integer arguments in registers.
    std::size_t maxGPRArgs{6};
    /// \brief Maximum floating-point arguments in registers.
    std::size_t maxXMMArgs{8};
    /// \brief Shadow space required before call (Windows only).
    std::size_t shadowSpace{0};
};

/// \brief Returns the singleton SysV target description.
[[nodiscard]] TargetInfo &sysvTarget() noexcept;

/// \brief Returns the singleton Windows x64 target description.
[[nodiscard]] TargetInfo &win64Target() noexcept;

/// \brief Returns the platform-appropriate target description.
/// \details On Windows returns win64Target(), otherwise returns sysvTarget().
[[nodiscard]] TargetInfo &hostTarget() noexcept;

/// \brief Determines if a physical register belongs to the general-purpose class.
[[nodiscard]] bool isGPR(PhysReg reg) noexcept;

/// \brief Determines if a physical register belongs to the XMM class.
[[nodiscard]] bool isXMM(PhysReg reg) noexcept;

/// \brief Provides the canonical AT&T name of a physical register.
[[nodiscard]] const char *regName(PhysReg reg) noexcept;

} // namespace viper::codegen::x64
