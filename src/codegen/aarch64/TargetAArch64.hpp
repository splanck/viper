// src/codegen/aarch64/TargetAArch64.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Define physical registers, register classes, and target metadata for the
//          AArch64 (arm64) ABI backend used by Viper's native code generator on macOS.
// Invariants: Data remains immutable once constructed; accessors return references to
//             shared singleton state describing the ABI contract.
// Ownership: No heap ownership beyond the singleton target info; containers live for the
//            program lifetime.

#pragma once

#include <array>
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

inline constexpr int kSlotSizeBytes = 8;
inline constexpr int kStackAlignment = 16;
inline constexpr std::size_t kMaxGPRArgs = 8; // x0..x7
inline constexpr std::size_t kMaxFPRArgs = 8; // v0..v7

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

[[nodiscard]] TargetInfo &darwinTarget() noexcept;
[[nodiscard]] bool isGPR(PhysReg reg) noexcept;
[[nodiscard]] bool isFPR(PhysReg reg) noexcept;
[[nodiscard]] const char *regName(PhysReg reg) noexcept;

} // namespace viper::codegen::aarch64
