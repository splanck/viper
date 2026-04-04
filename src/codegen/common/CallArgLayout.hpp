//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/CallArgLayout.hpp
// Purpose: Shared ABI slot planning helpers for native backends.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/CallLoweringPlan.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace viper::codegen::common {

/// @brief How an ABI consumes register argument slots.
enum class CallSlotModel : uint8_t {
    IndependentRegisterBanks, ///< Separate integer and FP register banks.
    UnifiedRegisterPositions, ///< One positional register window shared by both classes.
};

/// @brief Shared configuration for argument-slot assignment.
struct CallArgLayoutConfig {
    std::size_t maxGPRArgs{0}; ///< Maximum GPR arguments passed in registers.
    std::size_t maxFPRArgs{0}; ///< Maximum FP arguments passed in registers.
    CallSlotModel slotModel{CallSlotModel::IndependentRegisterBanks};
    bool variadicTailOnStack{false}; ///< True when variadic args must spill to the stack.
    std::size_t numNamedArgs{0};     ///< Number of non-variadic arguments.
};

/// @brief Placement for one source-order argument after ABI assignment.
struct CallArgLocation {
    std::size_t argIndex{0};        ///< Source-order argument index.
    CallArgClass cls{CallArgClass::GPR};
    bool isVariadic{false};         ///< True when this argument is part of a variadic tail.
    bool inRegister{false};         ///< True when the argument is passed in a register.
    std::size_t regIndex{0};        ///< Class-specific or positional register slot index.
    std::size_t stackSlotIndex{0};  ///< 0-based stack slot index among spilled arguments.
};

/// @brief Aggregate ABI placement summary for a call or function entry.
struct CallArgLayout {
    std::vector<CallArgLocation> locations{};
    std::size_t gprRegsUsed{0};
    std::size_t fprRegsUsed{0};
    std::size_t registerPositionsUsed{0};
    std::size_t stackSlotsUsed{0};
};

/// @brief Plan outgoing call-argument locations for the given abstract call.
CallArgLayout planCallArgs(std::span<const CallArg> args, const CallArgLayoutConfig &config);

/// @brief Plan incoming parameter locations for the given parameter classes.
CallArgLayout planParamClasses(std::span<const CallArgClass> classes,
                               const CallArgLayoutConfig &config);

} // namespace viper::codegen::common
