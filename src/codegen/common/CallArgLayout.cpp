//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include <stdexcept>
#include <vector>

namespace zanna::codegen::common {
namespace {

struct LayoutSlot {
    std::size_t argIndex{0};
    CallArgClass cls{CallArgClass::GPR};
    bool isVariadic{false};
    bool forceStack{false};
    bool isAggregatePart{false};
    std::size_t partIndex{0};
    std::size_t byteOffset{0};
    std::size_t byteSize{8};
};

constexpr std::size_t kSlotBytes = 8;

std::size_t checkedRoundUpToSlots(std::size_t sizeBytes) {
    if (sizeBytes == 0)
        return 0;
    if (sizeBytes > static_cast<std::size_t>(-1) - (kSlotBytes - 1))
        throw std::length_error("call argument layout: aggregate size overflows slot count");
    return (sizeBytes + (kSlotBytes - 1)) / kSlotBytes;
}

template <typename SlotBuilder>
CallArgLayout planLayout(SlotBuilder slotBuilder, const CallArgLayoutConfig &config) {
    std::vector<LayoutSlot> slots;
    slotBuilder(slots);

    CallArgLayout layout{};
    layout.locations.reserve(slots.size());

    const std::size_t unifiedCapacity = std::min(config.maxGPRArgs, config.maxFPRArgs);
    std::size_t gprUsed = 0;
    std::size_t fprUsed = 0;
    std::size_t unifiedUsed = 0;
    std::size_t stackUsed = 0;

    for (std::size_t slotIndex = 0; slotIndex < slots.size();) {
        const LayoutSlot &slot = slots[slotIndex];

        if (slot.isAggregatePart && !slot.isVariadic && !slot.forceStack) {
            std::size_t groupEnd = slotIndex;
            while (groupEnd < slots.size() && slots[groupEnd].isAggregatePart &&
                   slots[groupEnd].argIndex == slot.argIndex && !slots[groupEnd].isVariadic &&
                   !slots[groupEnd].forceStack) {
                ++groupEnd;
            }

            const std::size_t groupSlots = groupEnd - slotIndex;
            const bool fitsInRegisters =
                config.slotModel == CallSlotModel::UnifiedRegisterPositions
                    ? unifiedUsed + groupSlots <= unifiedCapacity
                    : gprUsed + groupSlots <= config.maxGPRArgs;

            for (std::size_t i = slotIndex; i < groupEnd; ++i) {
                const LayoutSlot &part = slots[i];
                CallArgLocation loc{};
                loc.argIndex = part.argIndex;
                loc.cls = part.cls;
                loc.isVariadic = part.isVariadic;
                loc.isAggregatePart = part.isAggregatePart;
                loc.partIndex = part.partIndex;
                loc.byteOffset = part.byteOffset;
                loc.byteSize = part.byteSize;

                if (fitsInRegisters) {
                    loc.inRegister = true;
                    if (config.slotModel == CallSlotModel::UnifiedRegisterPositions) {
                        loc.regIndex = unifiedUsed++;
                        gprUsed = std::max(gprUsed, loc.regIndex + 1);
                    } else {
                        loc.regIndex = gprUsed++;
                    }
                } else {
                    loc.stackSlotIndex = stackUsed++;
                }
                layout.locations.push_back(loc);
            }

            slotIndex = groupEnd;
            continue;
        }

        CallArgLocation loc{};
        loc.argIndex = slot.argIndex;
        loc.cls = slot.cls;
        loc.isVariadic = slot.isVariadic;
        loc.isAggregatePart = slot.isAggregatePart;
        loc.partIndex = slot.partIndex;
        loc.byteOffset = slot.byteOffset;
        loc.byteSize = slot.byteSize;

        if (slot.isVariadic || slot.forceStack) {
            loc.stackSlotIndex = stackUsed++;
            layout.locations.push_back(loc);
            ++slotIndex;
            continue;
        }

        if (config.slotModel == CallSlotModel::UnifiedRegisterPositions) {
            if (unifiedUsed < unifiedCapacity) {
                loc.inRegister = true;
                loc.regIndex = unifiedUsed++;
                if (slot.cls == CallArgClass::FPR)
                    fprUsed = std::max(fprUsed, loc.regIndex + 1);
                else
                    gprUsed = std::max(gprUsed, loc.regIndex + 1);
            } else {
                loc.stackSlotIndex = stackUsed++;
            }
        } else if (slot.cls == CallArgClass::FPR) {
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
        ++slotIndex;
    }

    layout.gprRegsUsed = gprUsed;
    layout.fprRegsUsed = fprUsed;
    layout.registerPositionsUsed = unifiedUsed;
    layout.stackSlotsUsed = stackUsed;
    return layout;
}

void appendScalarSlots(std::vector<LayoutSlot> &slots,
                       std::span<const CallArgClass> classes,
                       const CallArgLayoutConfig &config) {
    slots.reserve(classes.size());
    for (std::size_t argIndex = 0; argIndex < classes.size(); ++argIndex) {
        const bool isVariadic = config.variadicTailOnStack && argIndex >= config.numNamedArgs;
        slots.push_back(LayoutSlot{.argIndex = argIndex,
                                   .cls = classes[argIndex],
                                   .isVariadic = isVariadic,
                                   .forceStack = false,
                                   .isAggregatePart = false,
                                   .partIndex = 0,
                                   .byteOffset = 0,
                                   .byteSize = kSlotBytes});
    }
}

void appendCallArgSlots(std::vector<LayoutSlot> &slots,
                        std::span<const CallArg> args,
                        const CallArgLayoutConfig &config) {
    for (std::size_t argIndex = 0; argIndex < args.size(); ++argIndex) {
        const CallArg &arg = args[argIndex];
        const bool isVariadic = config.variadicTailOnStack && argIndex >= config.numNamedArgs;

        if (arg.kind != CallArgKind::AggregateMemory ||
            arg.aggregatePass == AggregatePassKind::Indirect) {
            slots.push_back(LayoutSlot{.argIndex = argIndex,
                                       .cls = arg.kind == CallArgKind::AggregateMemory
                                                  ? CallArgClass::GPR
                                                  : arg.cls,
                                       .isVariadic = isVariadic,
                                       .forceStack = false,
                                       .isAggregatePart = false,
                                       .partIndex = 0,
                                       .byteOffset = 0,
                                       .byteSize = kSlotBytes});
            continue;
        }

        const std::size_t slotCount = checkedRoundUpToSlots(arg.sizeBytes);
        const bool forceStack =
            arg.sizeBytes > config.maxDirectAggregateRegisterBytes || arg.alignBytes > kSlotBytes;
        for (std::size_t partIndex = 0; partIndex < slotCount; ++partIndex) {
            const std::size_t offset = partIndex * kSlotBytes;
            const std::size_t remaining = arg.sizeBytes > offset ? arg.sizeBytes - offset : 0;
            slots.push_back(LayoutSlot{.argIndex = argIndex,
                                       .cls = CallArgClass::GPR,
                                       .isVariadic = isVariadic,
                                       .forceStack = forceStack,
                                       .isAggregatePart = true,
                                       .partIndex = partIndex,
                                       .byteOffset = offset,
                                       .byteSize = std::min(kSlotBytes, remaining)});
        }
    }
}

} // namespace

CallArgLayout planCallArgs(std::span<const CallArg> args, const CallArgLayoutConfig &config) {
    return planLayout([&](std::vector<LayoutSlot> &slots) {
                          appendCallArgSlots(slots, args, config);
                      },
                      config);
}

CallArgLayout planParamClasses(std::span<const CallArgClass> classes,
                               const CallArgLayoutConfig &config) {
    return planLayout([&](std::vector<LayoutSlot> &slots) {
                          appendScalarSlots(slots, classes, config);
                      },
                      config);
}

} // namespace zanna::codegen::common
