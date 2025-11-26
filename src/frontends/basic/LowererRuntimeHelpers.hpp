//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowererRuntimeHelpers.hpp
// Purpose: Runtime helper tracking for BASIC lowering.
// Key invariants: Helper requests are idempotent; tracking state resets
//                 between program lowering invocations.
// Ownership/Lifetime: Included transitively via Lowerer.hpp.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstddef>

namespace il::frontends::basic
{

/// @brief Manual runtime helpers that require explicit tracking.
/// @details These are helpers not covered by the RuntimeFeature enum but
///          still need declaration in the IL module when used.
enum class ManualRuntimeHelper : std::size_t
{
    Trap = 0,
    ArrayI32New,
    ArrayI32Resize,
    ArrayI32Len,
    ArrayI32Get,
    ArrayI32Set,
    ArrayI32Retain,
    ArrayI32Release,
    ArrayStrAlloc,
    ArrayStrRelease,
    ArrayStrGet,
    ArrayStrPut,
    ArrayStrLen,
    // Object arrays (ptr elements)
    ArrayObjNew,
    ArrayObjLen,
    ArrayObjGet,
    ArrayObjPut,
    ArrayObjResize,
    ArrayObjRelease,
    ArrayOobPanic,
    OpenErrVstr,
    CloseErr,
    SeekChErr,
    WriteChErr,
    PrintlnChErr,
    LineInputChErr,
    EofCh,
    LofCh,
    LocCh,
    StrRetainMaybe,
    StrReleaseMaybe,
    SleepMs,
    TimerMs,
    // Module-level variable address helpers
    ModvarAddrI64,
    ModvarAddrF64,
    ModvarAddrI1,
    ModvarAddrPtr,
    ModvarAddrStr,
    Count
};

/// @brief Total number of manual runtime helpers.
inline constexpr std::size_t kManualRuntimeHelperCount =
    static_cast<std::size_t>(ManualRuntimeHelper::Count);

/// @brief Convert a ManualRuntimeHelper to its array index.
inline constexpr std::size_t manualRuntimeHelperIndex(ManualRuntimeHelper helper) noexcept
{
    return static_cast<std::size_t>(helper);
}

/// @brief Tracking array type for manual runtime helper requirements.
using ManualHelperRequirements = std::array<bool, kManualRuntimeHelperCount>;

} // namespace il::frontends::basic
