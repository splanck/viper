//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/RuntimeHelperRequests.hpp
// Purpose: Runtime helper request interface for BASIC lowering.
//
// This header defines the interface for requesting runtime helpers during
// lowering. It extracts the runtime requirement tracking from Lowerer to
// reduce header size and improve modularity.
//
// The RuntimeHelperRequester interface allows lowering code to declare
// dependencies on runtime functions without directly including Lowerer.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::basic
{

/// @brief Interface for requesting runtime helpers during lowering.
///
/// This interface abstracts the runtime requirement tracking mechanism,
/// allowing lowering helpers to declare their runtime dependencies without
/// tight coupling to the Lowerer class.
class RuntimeHelperRequester
{
  public:
    virtual ~RuntimeHelperRequester() = default;

    using RuntimeFeature = il::runtime::RuntimeFeature;

    /// @brief Request a runtime helper by feature enum.
    virtual void requestHelper(RuntimeFeature feature) = 0;

    /// @brief Check if a runtime helper is needed.
    [[nodiscard]] virtual bool isHelperNeeded(RuntimeFeature feature) const = 0;

    // =========================================================================
    // Convenience request methods for common runtime helpers
    // =========================================================================

    /// @brief Request the trap helper.
    virtual void requireTrap() = 0;

    // Array I32 operations
    virtual void requireArrayI32New() = 0;
    virtual void requireArrayI32Resize() = 0;
    virtual void requireArrayI32Len() = 0;
    virtual void requireArrayI32Get() = 0;
    virtual void requireArrayI32Set() = 0;
    virtual void requireArrayI32Retain() = 0;
    virtual void requireArrayI32Release() = 0;
    virtual void requireArrayOobPanic() = 0;

    // Array I64 operations (for LONG arrays)
    virtual void requireArrayI64New() = 0;
    virtual void requireArrayI64Resize() = 0;
    virtual void requireArrayI64Len() = 0;
    virtual void requireArrayI64Get() = 0;
    virtual void requireArrayI64Set() = 0;
    virtual void requireArrayI64Retain() = 0;
    virtual void requireArrayI64Release() = 0;

    // Array F64 operations (for SINGLE/DOUBLE arrays)
    virtual void requireArrayF64New() = 0;
    virtual void requireArrayF64Resize() = 0;
    virtual void requireArrayF64Len() = 0;
    virtual void requireArrayF64Get() = 0;
    virtual void requireArrayF64Set() = 0;
    virtual void requireArrayF64Retain() = 0;
    virtual void requireArrayF64Release() = 0;

    // Array String operations
    virtual void requireArrayStrAlloc() = 0;
    virtual void requireArrayStrRelease() = 0;
    virtual void requireArrayStrGet() = 0;
    virtual void requireArrayStrPut() = 0;
    virtual void requireArrayStrLen() = 0;

    // Array Object operations
    virtual void requireArrayObjNew() = 0;
    virtual void requireArrayObjLen() = 0;
    virtual void requireArrayObjGet() = 0;
    virtual void requireArrayObjPut() = 0;
    virtual void requireArrayObjResize() = 0;
    virtual void requireArrayObjRelease() = 0;

    // File I/O helpers
    virtual void requireOpenErrVstr() = 0;
    virtual void requireCloseErr() = 0;
    virtual void requireSeekChErr() = 0;
    virtual void requireWriteChErr() = 0;
    virtual void requirePrintlnChErr() = 0;
    virtual void requireLineInputChErr() = 0;
    virtual void requireEofCh() = 0;
    virtual void requireLofCh() = 0;
    virtual void requireLocCh() = 0;

    // Module-level variable address helpers
    virtual void requireModvarAddrI64() = 0;
    virtual void requireModvarAddrF64() = 0;
    virtual void requireModvarAddrI1() = 0;
    virtual void requireModvarAddrPtr() = 0;
    virtual void requireModvarAddrStr() = 0;

    // String lifetime helpers
    virtual void requireStrRetainMaybe() = 0;
    virtual void requireStrReleaseMaybe() = 0;

    // Miscellaneous helpers
    virtual void requireSleepMs() = 0;
    virtual void requireTimerMs() = 0;
};

} // namespace il::frontends::basic
