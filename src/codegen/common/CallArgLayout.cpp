//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/CallArgLayout.cpp
// Purpose: Shared ABI slot planning helpers for native backends.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/CallArgLayout.hpp"

#include <algorithm>

namespace viper::codegen::common {
namespace {

template <typename ClassAt>
CallArgLayout planLayout(std::size_t count,
                         ClassAt classAt,
                         const CallArgLayoutConfig &config) {
    CallArgLayout layout{};
    layout.locations.reserve(count);

    const std::size_t unifiedCapacity = std::min(config.maxGPRArgs, config.maxFPRArgs);
    std::size_t gprUsed = 0;
    std::size_t fprUsed = 0;
    std::size_t unifiedUsed = 0;
    std::size_t stackUsed = 0;

    for (std::size_t argIndex = 0; argIndex < count; ++argIndex) {
        const CallArgClass cls = classAt(argIndex);
        const bool isVariadic = config.variadicTailOnStack && argIndex >= config.numNamedArgs;

        CallArgLocation loc{};
        loc.argIndex = argIndex;
        loc.cls = cls;
        loc.isVariadic = isVariadic;

        if (isVariadic) {
            loc.stackSlotIndex = stackUsed++;
            layout.locations.push_back(loc);
            continue;
        }

        if (config.slotModel == CallSlotModel::UnifiedRegisterPositions) {
            if (unifiedUsed < unifiedCapacity) {
                loc.inRegister = true;
                loc.regIndex = unifiedUsed++;
                if (cls == CallArgClass::FPR)
                    fprUsed = std::max(fprUsed, loc.regIndex + 1);
                else
                    gprUsed = std::max(gprUsed, loc.regIndex + 1);
            } else {
                loc.stackSlotIndex = stackUsed++;
            }
        } else if (cls == CallArgClass::FPR) {
            if (fprUsed < config.maxFPRArgs) {
                loc.inRegister = true;
                loc.regIndex = fprUsed++;
            } else {
                loc.stackSlotIndex = stackUsed++;
            }
        } else {
            if (gprUsed < config.maxGPRArgs) {
                loc.inRegister = true;
                loc.regIndex = gprUsed++;
            } else {
                loc.stackSlotIndex = stackUsed++;
            }
        }

        layout.locations.push_back(loc);
    }

    layout.gprRegsUsed = gprUsed;
    layout.fprRegsUsed = fprUsed;
    layout.registerPositionsUsed = unifiedUsed;
    layout.stackSlotsUsed = stackUsed;
    return layout;
}

} // namespace

CallArgLayout planCallArgs(std::span<const CallArg> args, const CallArgLayoutConfig &config) {
    return planLayout(args.size(), [&](std::size_t index) { return args[index].cls; }, config);
}

CallArgLayout planParamClasses(std::span<const CallArgClass> classes,
                               const CallArgLayoutConfig &config) {
    return planLayout(
        classes.size(), [&](std::size_t index) { return classes[index]; }, config);
}

} // namespace viper::codegen::common
