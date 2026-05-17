//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/TargetAArch64.cpp
// Purpose: AArch64 target description — register classification, calling
//          convention tables, and register name lookup for all three OS targets
//          (Darwin, Linux ELF, Windows PE/COFF).
// Key invariants:
//   - All three target singletons are initialized at static-init time; they
//     are never mutated after construction.
//   - Register convention tables follow AAPCS64; Darwin adds BTI/PAC flags.
//   - regName() returns static string literals; callers must not free them.
// Ownership/Lifetime:
//   - darwinTarget(), linuxTarget(), windowsTarget() return references to
//     file-static objects that live for the duration of the program.
// Links: codegen/aarch64/TargetAArch64.hpp
//
//===----------------------------------------------------------------------===//

#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64 {
namespace {

/// @brief Build the Darwin/macOS AArch64 TargetInfo with AAPCS64 register tables,
///        BTI (branch target identification) and PAC (return-address signing) enabled.
TargetInfo makeDarwinTarget() {
    TargetInfo info{};
    // Caller-saved GPRs (AArch64 AAPCS64 / macOS): x0-x17 are call-clobbered; x18 is reserved;
    // x19-x28 callee-saved.
    info.callerSavedGPR = {
        PhysReg::X0,
        PhysReg::X1,
        PhysReg::X2,
        PhysReg::X3,
        PhysReg::X4,
        PhysReg::X5,
        PhysReg::X6,
        PhysReg::X7,
        PhysReg::X8,
        PhysReg::X9,
        PhysReg::X10,
        PhysReg::X11,
        PhysReg::X12,
        PhysReg::X13,
        PhysReg::X14,
        PhysReg::X15,
        PhysReg::X16,
        PhysReg::X17,
    };
    info.calleeSavedGPR = {
        PhysReg::X19,
        PhysReg::X20,
        PhysReg::X21,
        PhysReg::X22,
        PhysReg::X23,
        PhysReg::X24,
        PhysReg::X25,
        PhysReg::X26,
        PhysReg::X27,
        PhysReg::X28,
        PhysReg::X29, // FP
        // X30 (LR) not generally callee-saved as it's saved/restored via prologue/epilogue.
    };
    info.callerSavedFPR = {
        // v0-v7 used for args/returns (caller-saved); v8-v15 are callee-saved per AAPCS64;
        // v16-v31 are caller-saved (call-clobbered).
        PhysReg::V0,  PhysReg::V1,  PhysReg::V2,  PhysReg::V3,  PhysReg::V4,  PhysReg::V5,
        PhysReg::V6,  PhysReg::V7,  PhysReg::V16, PhysReg::V17, PhysReg::V18, PhysReg::V19,
        PhysReg::V20, PhysReg::V21, PhysReg::V22, PhysReg::V23, PhysReg::V24, PhysReg::V25,
        PhysReg::V26, PhysReg::V27, PhysReg::V28, PhysReg::V29, PhysReg::V30, PhysReg::V31,
    };
    info.calleeSavedFPR = {
        // AArch64 Darwin preserves d8-d15 across calls; model as V8..V15.
        PhysReg::V8,
        PhysReg::V9,
        PhysReg::V10,
        PhysReg::V11,
        PhysReg::V12,
        PhysReg::V13,
        PhysReg::V14,
        PhysReg::V15,
    };
    info.intArgOrder = {PhysReg::X0,
                        PhysReg::X1,
                        PhysReg::X2,
                        PhysReg::X3,
                        PhysReg::X4,
                        PhysReg::X5,
                        PhysReg::X6,
                        PhysReg::X7};
    info.f64ArgOrder = {PhysReg::V0,
                        PhysReg::V1,
                        PhysReg::V2,
                        PhysReg::V3,
                        PhysReg::V4,
                        PhysReg::V5,
                        PhysReg::V6,
                        PhysReg::V7};
    info.intReturnReg = PhysReg::X0;
    info.f64ReturnReg = PhysReg::V0;
    info.stackAlignment = 16U;
    return info;
}

/// @brief Build the Linux AArch64 target.
/// Same AAPCS64 register convention as Darwin; only the assembly output
/// format differs (no underscore prefix, ELF .type/.size directives).
static TargetInfo makeLinuxTarget() {
    TargetInfo info = makeDarwinTarget();
    info.abiFormat = ABIFormat::Linux;
    info.emitBranchTargetIdentification = false;
    info.emitReturnAddressSigning = false;
    info.variadicTailOnStack = false;
    return info;
}

/// @brief Build the Windows ARM64 target.
/// Identical AAPCS64 register convention to Linux; PE/COFF assembly format
/// (no underscore symbol prefix, no ELF .type/.size directives).
static TargetInfo makeWindowsTarget() {
    TargetInfo info = makeDarwinTarget();
    info.abiFormat = ABIFormat::Windows;
    info.emitBranchTargetIdentification = false;
    info.emitReturnAddressSigning = false;
    info.variadicTailOnStack = false;
    return info;
}

TargetInfo darwinTargetInstance = makeDarwinTarget();
TargetInfo linuxTargetInstance = makeLinuxTarget();
TargetInfo windowsTargetInstance = makeWindowsTarget();

} // namespace

/// @brief Get the singleton TargetInfo instance for Darwin/macOS AArch64.
/// @return Reference to the pre-configured Darwin target information.
const TargetInfo &darwinTarget() noexcept {
    return darwinTargetInstance;
}

/// @brief Get the singleton TargetInfo instance for Linux AArch64 (ELF).
/// @return Reference to the pre-configured Linux target information.
const TargetInfo &linuxTarget() noexcept {
    return linuxTargetInstance;
}

/// @brief Get the singleton TargetInfo instance for Windows ARM64 (PE/COFF).
/// Identical AAPCS64 register convention to Linux; only assembly syntax differs.
/// @return Reference to the pre-configured Windows ARM64 target information.
const TargetInfo &windowsTarget() noexcept {
    return windowsTargetInstance;
}

/// @brief Check if a physical register is a general-purpose register (GPR).
/// @param reg The physical register to check.
/// @return True if the register is X0-X30 or SP, false otherwise.
bool isGPR(PhysReg reg) noexcept {
    switch (reg) {
        case PhysReg::X0:
        case PhysReg::X1:
        case PhysReg::X2:
        case PhysReg::X3:
        case PhysReg::X4:
        case PhysReg::X5:
        case PhysReg::X6:
        case PhysReg::X7:
        case PhysReg::X8:
        case PhysReg::X9:
        case PhysReg::X10:
        case PhysReg::X11:
        case PhysReg::X12:
        case PhysReg::X13:
        case PhysReg::X14:
        case PhysReg::X15:
        case PhysReg::X16:
        case PhysReg::X17:
        case PhysReg::X18:
        case PhysReg::X19:
        case PhysReg::X20:
        case PhysReg::X21:
        case PhysReg::X22:
        case PhysReg::X23:
        case PhysReg::X24:
        case PhysReg::X25:
        case PhysReg::X26:
        case PhysReg::X27:
        case PhysReg::X28:
        case PhysReg::X29:
        case PhysReg::X30:
        case PhysReg::SP:
            return true;
        default:
            return false;
    }
}

/// @brief Check if a physical register is a floating-point/SIMD register (FPR).
/// @param reg The physical register to check.
/// @return True if the register is V0-V31, false otherwise.
bool isFPR(PhysReg reg) noexcept {
    switch (reg) {
        case PhysReg::V0:
        case PhysReg::V1:
        case PhysReg::V2:
        case PhysReg::V3:
        case PhysReg::V4:
        case PhysReg::V5:
        case PhysReg::V6:
        case PhysReg::V7:
        case PhysReg::V8:
        case PhysReg::V9:
        case PhysReg::V10:
        case PhysReg::V11:
        case PhysReg::V12:
        case PhysReg::V13:
        case PhysReg::V14:
        case PhysReg::V15:
        case PhysReg::V16:
        case PhysReg::V17:
        case PhysReg::V18:
        case PhysReg::V19:
        case PhysReg::V20:
        case PhysReg::V21:
        case PhysReg::V22:
        case PhysReg::V23:
        case PhysReg::V24:
        case PhysReg::V25:
        case PhysReg::V26:
        case PhysReg::V27:
        case PhysReg::V28:
        case PhysReg::V29:
        case PhysReg::V30:
        case PhysReg::V31:
            return true;
        default:
            return false;
    }
}

/// @brief Get the assembly name for a physical register.
/// @param reg The physical register to get the name for.
/// @return The lowercase assembly name string (e.g., "x0", "sp", "v0").
const char *regName(PhysReg reg) noexcept {
    switch (reg) {
        case PhysReg::X0:
            return "x0";
        case PhysReg::X1:
            return "x1";
        case PhysReg::X2:
            return "x2";
        case PhysReg::X3:
            return "x3";
        case PhysReg::X4:
            return "x4";
        case PhysReg::X5:
            return "x5";
        case PhysReg::X6:
            return "x6";
        case PhysReg::X7:
            return "x7";
        case PhysReg::X8:
            return "x8";
        case PhysReg::X9:
            return "x9";
        case PhysReg::X10:
            return "x10";
        case PhysReg::X11:
            return "x11";
        case PhysReg::X12:
            return "x12";
        case PhysReg::X13:
            return "x13";
        case PhysReg::X14:
            return "x14";
        case PhysReg::X15:
            return "x15";
        case PhysReg::X16:
            return "x16";
        case PhysReg::X17:
            return "x17";
        case PhysReg::X18:
            return "x18";
        case PhysReg::X19:
            return "x19";
        case PhysReg::X20:
            return "x20";
        case PhysReg::X21:
            return "x21";
        case PhysReg::X22:
            return "x22";
        case PhysReg::X23:
            return "x23";
        case PhysReg::X24:
            return "x24";
        case PhysReg::X25:
            return "x25";
        case PhysReg::X26:
            return "x26";
        case PhysReg::X27:
            return "x27";
        case PhysReg::X28:
            return "x28";
        case PhysReg::X29:
            return "x29";
        case PhysReg::X30:
            return "x30";
        case PhysReg::SP:
            return "sp";
        case PhysReg::V0:
            return "v0";
        case PhysReg::V1:
            return "v1";
        case PhysReg::V2:
            return "v2";
        case PhysReg::V3:
            return "v3";
        case PhysReg::V4:
            return "v4";
        case PhysReg::V5:
            return "v5";
        case PhysReg::V6:
            return "v6";
        case PhysReg::V7:
            return "v7";
        case PhysReg::V8:
            return "v8";
        case PhysReg::V9:
            return "v9";
        case PhysReg::V10:
            return "v10";
        case PhysReg::V11:
            return "v11";
        case PhysReg::V12:
            return "v12";
        case PhysReg::V13:
            return "v13";
        case PhysReg::V14:
            return "v14";
        case PhysReg::V15:
            return "v15";
        case PhysReg::V16:
            return "v16";
        case PhysReg::V17:
            return "v17";
        case PhysReg::V18:
            return "v18";
        case PhysReg::V19:
            return "v19";
        case PhysReg::V20:
            return "v20";
        case PhysReg::V21:
            return "v21";
        case PhysReg::V22:
            return "v22";
        case PhysReg::V23:
            return "v23";
        case PhysReg::V24:
            return "v24";
        case PhysReg::V25:
            return "v25";
        case PhysReg::V26:
            return "v26";
        case PhysReg::V27:
            return "v27";
        case PhysReg::V28:
            return "v28";
        case PhysReg::V29:
            return "v29";
        case PhysReg::V30:
            return "v30";
        case PhysReg::V31:
            return "v31";
        default:
            return "unknown";
    }
}

} // namespace viper::codegen::aarch64
