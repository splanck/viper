//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/CallLoweringPlan.hpp
// Purpose: Target-independent data structures describing a function call to be
//          lowered into MIR. Both x86-64 and AArch64 backends can use these
//          types to represent call arguments and metadata before emitting
//          target-specific instruction sequences.
//
// Key invariants:
//   - Arguments are ordered by source position (parameter order).
//   - Register class determines GPR vs FPR argument passing lanes.
//   - isVarArg controls vararg-specific behavior (x86: %al count;
//     AArch64: force anonymous args to stack per AAPCS64).
//   - numNamedArgs specifies the boundary between named and variadic arguments.
//
// Ownership/Lifetime: Value types, no heap allocation.
// Links: codegen/x86_64/CallLowering.hpp, codegen/aarch64/InstrLowering.cpp,
//        plans/audit-01-backend-abstraction.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::codegen::common
{

/// @brief Register class for a call argument.
/// @details Target-independent classification used before register assignment.
///          Both backends map this to their target-specific RegClass enum.
enum class CallArgClass : uint8_t
{
    GPR, ///< General-purpose (integer/pointer) argument.
    FPR, ///< Floating-point argument.
};

/// @brief Describes a single argument in a call lowering plan.
struct CallArg
{
    CallArgClass cls{CallArgClass::GPR}; ///< Register class for this argument.
    uint16_t vreg{0};     ///< Virtual register holding the value (when !isImm).
    bool isImm{false};    ///< True when the argument is a compile-time constant.
    int64_t imm{0};       ///< Immediate value (when isImm == true).
};

/// @brief Target-independent description of a function call to be lowered.
///
/// @details Captures all information needed by a backend to emit the
///          target-specific call sequence: callee name, argument list, return
///          type classification, and variadic metadata.
///
///          x86-64 uses this directly in its CallLowering.cpp.
///          AArch64 can adopt this as its InstrLowering.cpp call handling
///          matures into a standalone module.
struct CallLoweringPlan
{
    std::string callee{};        ///< Symbolic name of the callee.
    std::vector<CallArg> args{}; ///< Ordered list of call arguments.
    bool returnsF64{false};      ///< True when the call returns FP in the FP return register.
    bool isVarArg{false};        ///< True when the callee uses variadic arguments.
    std::size_t numNamedArgs{0}; ///< Number of named (non-variadic) parameters.
                                 ///< Only meaningful when isVarArg is true.
                                 ///< Args beyond this index are variadic.
};

} // namespace viper::codegen::common
