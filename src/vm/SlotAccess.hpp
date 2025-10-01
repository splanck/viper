// File: src/vm/SlotAccess.hpp
// Purpose: Declare shared helpers for mapping IL types to VM slot storage.
// Key invariants: Accessors return valid pointers only for supported type kinds.
// Ownership/Lifetime: Callers manage slot and buffer lifetimes; helpers never allocate.
// Links: docs/architecture.md#cpp-overview
#pragma once

#include "il/core/Type.hpp"
#include "rt.hpp"

#include <cstdint>

namespace il::vm
{

union Slot; ///< Forward declaration from VM.hpp

namespace slot_access
{

/// @brief Temporary storage used when marshalling runtime call results.
struct ResultBuffers
{
    int64_t i64 = 0;   ///< Integer and boolean results.
    double f64 = 0.0;  ///< Floating-point results.
    rt_string str = nullptr; ///< Runtime string results.
    void *ptr = nullptr;     ///< Pointer results.
};

/// @brief Obtain a pointer to the active member of @p slot for @p kind.
/// @param slot Slot containing the value.
/// @param kind IL type describing the slot contents.
/// @return Pointer to the member backing @p kind or nullptr when unsupported.
void *slotPointer(Slot &slot, il::core::Type::Kind kind);

/// @brief Obtain a pointer to the temporary buffer for a runtime result of @p kind.
/// @param kind Result type reported by the runtime helper.
/// @param buffers Aggregate storing marshalled values.
/// @return Pointer suitable for passing to the runtime helper or nullptr when unsupported.
void *resultBuffer(il::core::Type::Kind kind, ResultBuffers &buffers);

/// @brief Assign a runtime result stored in @p buffers back into @p slot according to @p kind.
/// @param slot Destination slot to receive the value.
/// @param kind Type describing the marshalled result.
/// @param buffers Temporary storage populated by the runtime helper.
void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers);

/// @brief Decode raw memory referenced by @p ptr into @p out according to @p kind.
/// @param kind IL type describing the pointed-to value.
/// @param ptr Raw memory pointer with sufficient storage for the type.
/// @param out Destination slot receiving the loaded value.
void loadFromPointer(il::core::Type::Kind kind, const void *ptr, Slot &out);

/// @brief Encode @p value into raw memory pointed to by @p ptr according to @p kind.
/// @param kind IL type describing the pointed-to value.
/// @param ptr Raw memory pointer with sufficient storage for the type.
/// @param value Source slot supplying the value to write.
void storeToPointer(il::core::Type::Kind kind, void *ptr, const Slot &value);

} // namespace slot_access

} // namespace il::vm
