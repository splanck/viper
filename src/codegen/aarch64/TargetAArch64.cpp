//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TargetAArch64.cpp
/// @brief AArch64 target description and register information for code generation.
///
/// This file implements the target description for the AArch64 (ARM64) backend,
/// providing register classification, calling convention details, and register
/// naming utilities. It defines the AAPCS64 (Arm 64-bit Architecture Procedure
/// Call Standard) register usage for macOS/Darwin.
///
/// **Target Description Architecture:**
/// ```
/// ┌───────────────────────────────────────────────────────────────────────────┐
/// │                          TargetInfo Structure                             │
/// ├───────────────────────────────────────────────────────────────────────────┤
/// │  callerSavedGPR[]  │ x0-x17: Registers that callers must save            │
/// │  calleeSavedGPR[]  │ x19-x28, x29: Registers that callees must preserve  │
/// │  callerSavedFPR[]  │ v0-v31: Floating-point registers (caller-saved)     │
/// │  calleeSavedFPR[]  │ v8-v15: FP registers that callees must preserve     │
/// │  intArgOrder[]     │ x0-x7: Integer argument passing registers           │
/// │  f64ArgOrder[]     │ v0-v7: Floating-point argument passing registers    │
/// │  intReturnReg      │ x0: Integer return value register                   │
/// │  f64ReturnReg      │ v0: Floating-point return value register            │
/// │  stackAlignment    │ 16: Required stack alignment in bytes               │
/// └───────────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **AAPCS64 Register Roles:**
/// ```
/// General Purpose Registers (GPR):
/// ┌─────────┬──────────────────────────────────────────────────────────┐
/// │ x0-x7   │ Argument/result passing, caller-saved                    │
/// │ x8      │ Indirect result location register, caller-saved          │
/// │ x9-x15  │ Temporary registers, caller-saved                        │
/// │ x16-x17 │ Intra-procedure-call scratch registers, caller-saved     │
/// │ x18     │ Platform register (reserved on Darwin)                   │
/// │ x19-x28 │ Callee-saved registers                                   │
/// │ x29     │ Frame pointer (FP), callee-saved                         │
/// │ x30     │ Link register (LR), saved/restored by prologue/epilogue  │
/// │ sp      │ Stack pointer (special, not in GPR set)                  │
/// └─────────┴──────────────────────────────────────────────────────────┘
///
/// Floating-Point/SIMD Registers (FPR):
/// ┌─────────┬──────────────────────────────────────────────────────────┐
/// │ v0-v7   │ Argument/result passing, caller-saved                    │
/// │ v8-v15  │ Callee-saved (lower 64 bits only on Darwin)              │
/// │ v16-v31 │ Caller-saved                                             │
/// └─────────┴──────────────────────────────────────────────────────────┘
/// ```
///
/// **Calling Convention Summary:**
/// - Integer arguments: x0, x1, x2, x3, x4, x5, x6, x7 (in order)
/// - Floating-point arguments: v0, v1, v2, v3, v4, v5, v6, v7 (in order)
/// - Integer return value: x0 (or x0:x1 for 128-bit)
/// - Floating-point return value: v0
/// - Stack grows downward, 16-byte aligned
/// - Callee saves x19-x28, x29 (FP), and the lower 64 bits of v8-v15
///
/// **Helper Functions:**
///
/// | Function    | Purpose                                        |
/// |-------------|------------------------------------------------|
/// | isGPR()     | Check if a physical register is a GPR          |
/// | isFPR()     | Check if a physical register is an FPR         |
/// | regName()   | Get the textual name of a physical register    |
/// | darwinTarget() | Get the singleton Darwin target instance    |
///
/// **Usage Example:**
/// ```cpp
/// const TargetInfo& ti = darwinTarget();
///
/// // Get argument registers for a call
/// for (size_t i = 0; i < args.size() && i < ti.intArgOrder.size(); ++i) {
///     emit(MOV, ti.intArgOrder[i], args[i]);
/// }
///
/// // Check if a register needs saving
/// if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), reg)
///     != ti.calleeSavedGPR.end()) {
///     // Save in prologue, restore in epilogue
/// }
/// ```
///
/// @see MachineIR.hpp For PhysReg enumeration and RegClass
/// @see RegAllocLinear.cpp For register allocation using target info
/// @see AsmEmitter.cpp For assembly generation using register names
///
//===----------------------------------------------------------------------===//

#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64
{
namespace
{
TargetInfo makeDarwinTarget()
{
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
static TargetInfo makeLinuxTarget()
{
    TargetInfo info = makeDarwinTarget();
    info.abiFormat  = ABIFormat::Linux;
    return info;
}

TargetInfo darwinTargetInstance = makeDarwinTarget();
TargetInfo linuxTargetInstance  = makeLinuxTarget();

} // namespace

/// @brief Get the singleton TargetInfo instance for Darwin/macOS AArch64.
/// @return Reference to the pre-configured Darwin target information.
const TargetInfo &darwinTarget() noexcept
{
    return darwinTargetInstance;
}

/// @brief Get the singleton TargetInfo instance for Linux AArch64 (ELF).
/// @return Reference to the pre-configured Linux target information.
const TargetInfo &linuxTarget() noexcept
{
    return linuxTargetInstance;
}

/// @brief Check if a physical register is a general-purpose register (GPR).
/// @param reg The physical register to check.
/// @return True if the register is X0-X30 or SP, false otherwise.
bool isGPR(PhysReg reg) noexcept
{
    switch (reg)
    {
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
bool isFPR(PhysReg reg) noexcept
{
    switch (reg)
    {
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
const char *regName(PhysReg reg) noexcept
{
    switch (reg)
    {
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
