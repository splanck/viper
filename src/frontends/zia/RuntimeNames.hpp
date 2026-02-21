//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeNames.hpp
/// @brief Zia-specific runtime name aliases and configuration constants.
///
/// @details This header imports the canonical runtime function names from the
/// auto-generated RuntimeNames.hpp (produced by rtgen from runtime.def) and
/// provides Zia-specific short aliases for backwards compatibility and
/// convenience. For example, `kListAdd` maps to `kCollectionsListPush`, and
/// `kBoxI64` maps to `kCoreBoxI64`.
///
/// The aliases exist because the Zia lowerer was written before the canonical
/// naming convention was finalized. Rather than updating hundreds of references
/// throughout the lowerer, these aliases bridge the gap. New code should prefer
/// the canonical names from `il::runtime::names` directly.
///
/// This file also defines Zia-specific compile-time configuration constants:
///   - Import limits (kMaxImportDepth, kMaxImportedFiles)
///   - Object layout constants (kObjectHeaderSize, kVtablePtrOffset, etc.)
///   - Internal runtime function names (rt_alloc, rt_obj_class_id)
///
/// @invariant All alias constants point to the same string literals as their
///            canonical counterparts in il::runtime::names.
/// @invariant Object layout constants must match the C runtime's struct layout
///            (see rt_heap.h, rt_obj_header.h).
///
/// Ownership/Lifetime: Header-only compile-time constants. No runtime state.
///
/// @see il/runtime/RuntimeNames.hpp — canonical generated names.
/// @see runtime.def — authoritative source for all runtime API definitions.
/// @see Lowerer.hpp — primary consumer of these name constants.
///
/// @note All runtime function names are defined in the generated file at
///       il/runtime/generated/RuntimeNames.hpp. This file only provides
///       aliases and Zia-specific constants.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "il/runtime/RuntimeNames.hpp"

namespace il::frontends::zia::runtime
{

/// @brief Import all canonical runtime names into the zia::runtime namespace.
using namespace il::runtime::names;

//=============================================================================
/// @name String Aliases
/// @brief Short names for Viper.String runtime functions.
/// @{
//=============================================================================

/// @brief Check if a string contains a substring. Maps to Viper.String.Has.
inline constexpr const char *kStringContains = kStringHas;
/// @brief Convert an i64 integer to its string representation.
inline constexpr const char *kStringFromInt = kCoreConvertToStringInt;
/// @brief Convert an f64 float to its string representation.
inline constexpr const char *kStringFromNum = kCoreConvertToStringDouble;
/// @}

//=============================================================================
/// @name Core.Object Aliases
/// @brief Short names for Viper.Core.Object runtime functions.
/// @{
//=============================================================================

/// @brief Convert any object to its string representation.
inline constexpr const char *kObjectToString = kCoreObjectToString;
/// @}

//=============================================================================
/// @name Boxing Aliases
/// @brief Short names for Viper.Core.Box boxing/unboxing functions.
/// @details Boxing wraps primitive values (i64, f64, bool, str) into heap-
///          allocated Box objects for polymorphic storage in collections.
/// @{
//=============================================================================

/// @brief Box an i64 value into a heap-allocated Box object.
inline constexpr const char *kBoxI64 = kCoreBoxI64;
/// @brief Box an f64 value into a heap-allocated Box object.
inline constexpr const char *kBoxF64 = kCoreBoxF64;
/// @brief Box a boolean (i1) value into a heap-allocated Box object.
inline constexpr const char *kBoxI1 = kCoreBoxI1;
/// @brief Box a string pointer into a heap-allocated Box object.
inline constexpr const char *kBoxStr = kCoreBoxStr;
/// @brief Query the value type tag stored in a Box object.
inline constexpr const char *kBoxValueType = kCoreBoxValueType;
/// @brief Unbox a Box object to extract the i64 value.
inline constexpr const char *kUnboxI64 = kCoreBoxToI64;
/// @brief Unbox a Box object to extract the f64 value.
inline constexpr const char *kUnboxF64 = kCoreBoxToF64;
/// @brief Unbox a Box object to extract the boolean (i1) value.
inline constexpr const char *kUnboxI1 = kCoreBoxToI1;
/// @brief Unbox a Box object to extract the string pointer.
inline constexpr const char *kUnboxStr = kCoreBoxToStr;
/// @}

//=============================================================================
/// @name Core.Convert Aliases
/// @brief Short names for Viper.Core.Convert type conversion functions.
/// @{
//=============================================================================

/// @brief Convert a value to f64 (double-precision float).
inline constexpr const char *kConvertToDouble = kCoreConvertToDouble;
/// @brief Convert a value to i64 (64-bit integer).
inline constexpr const char *kConvertToInt = kCoreConvertToInt;
/// @}

//=============================================================================
/// @name Core.Parse Aliases
/// @brief Short names for Viper.Core.Parse string-to-number parsing functions.
/// @{
//=============================================================================

/// @brief Parse a string to an f64 value.
inline constexpr const char *kParseDouble = kCoreParseDouble;
/// @brief Parse a string to an i64 value.
inline constexpr const char *kParseInt64 = kCoreParseInt64;
/// @}

//=============================================================================
/// @name List Aliases
/// @brief Short names for Viper.Collections.List functions.
/// @details List is a dynamic growable array with O(1) amortized append.
/// @{
//=============================================================================

/// @brief Create a new empty List. Returns a heap-allocated list handle.
inline constexpr const char *kListNew = kCollectionsListNew;
/// @brief Append an element to the end of the list. O(1) amortized.
inline constexpr const char *kListAdd = kCollectionsListPush;
/// @brief Get the element at a given index. O(1).
inline constexpr const char *kListGet = kCollectionsListGet;
/// @brief Set the element at a given index. O(1).
inline constexpr const char *kListSet = kCollectionsListSet;
/// @brief Get the number of elements in the list. O(1).
inline constexpr const char *kListCount = kCollectionsListGetLen;
/// @brief Remove all elements from the list.
inline constexpr const char *kListClear = kCollectionsListClear;
/// @brief Remove the element at a given index, shifting subsequent elements.
inline constexpr const char *kListRemoveAt = kCollectionsListRemoveAt;
/// @brief Check if the list contains a given value. O(n) linear scan.
inline constexpr const char *kListContains = kCollectionsListHas;
/// @brief Remove the first occurrence of a value. O(n).
inline constexpr const char *kListRemove = kCollectionsListRemove;
/// @brief Insert an element at a given index, shifting subsequent elements.
inline constexpr const char *kListInsert = kCollectionsListInsert;
/// @brief Find the index of the first occurrence of a value, or -1 if absent.
inline constexpr const char *kListFind = kCollectionsListFind;
/// @brief Remove and return the last element.
inline constexpr const char *kListPop = kCollectionsListPop;
/// @}

//=============================================================================
/// @name Set Aliases
/// @brief Short names for Viper.Collections.Set functions.
/// @details Set is a hash-based unordered collection of unique values.
/// @{
//=============================================================================

/// @brief Create a new empty Set. Returns a heap-allocated set handle.
inline constexpr const char *kSetNew = kCollectionsSetNew;
/// @brief Get the number of elements in the set. O(1).
inline constexpr const char *kSetCount = kCollectionsSetGetLen;
/// @brief Check if the set contains a given value. O(1) average.
inline constexpr const char *kSetHas = kCollectionsSetHas;
/// @brief Add a value to the set. No effect if already present. O(1) average.
inline constexpr const char *kSetPut = kCollectionsSetAdd;
/// @brief Remove a value from the set. O(1) average.
inline constexpr const char *kSetDrop = kCollectionsSetRemove;
/// @brief Remove all elements from the set.
inline constexpr const char *kSetClear = kCollectionsSetClear;
/// @}

//=============================================================================
/// @name Map Aliases
/// @brief Short names for Viper.Collections.Map functions.
/// @details Map is a hash-based key-value dictionary with O(1) average lookups.
/// @{
//=============================================================================

/// @brief Create a new empty Map. Returns a heap-allocated map handle.
inline constexpr const char *kMapNew = kCollectionsMapNew;
/// @brief Set a key-value pair, overwriting any existing value for the key.
inline constexpr const char *kMapSet = kCollectionsMapSet;
/// @brief Get the value for a given key. Traps if key is absent.
inline constexpr const char *kMapGet = kCollectionsMapGet;
/// @brief Get the value for a given key, or a default if absent.
inline constexpr const char *kMapGetOr = kCollectionsMapGetOr;
/// @brief Check if the map contains a given key. O(1) average.
inline constexpr const char *kMapContainsKey = kCollectionsMapHas;
/// @brief Get the number of key-value pairs in the map. O(1).
inline constexpr const char *kMapCount = kCollectionsMapGetLen;
/// @brief Remove a key-value pair by key.
inline constexpr const char *kMapRemove = kCollectionsMapRemove;
/// @brief Set a key-value pair only if the key is not already present.
inline constexpr const char *kMapSetIfMissing = kCollectionsMapSetIfMissing;
/// @brief Remove all key-value pairs from the map.
inline constexpr const char *kMapClear = kCollectionsMapClear;
/// @brief Get a Seq of all keys in the map.
inline constexpr const char *kMapKeys = kCollectionsMapKeys;
/// @brief Get a Seq of all values in the map.
inline constexpr const char *kMapValues = kCollectionsMapValues;
/// @}

//=============================================================================
/// @name Seq Aliases
/// @brief Short names for Viper.Collections.Seq (immutable sequence) functions.
/// @{
//=============================================================================

/// @brief Get the number of elements in the Seq. O(1).
inline constexpr const char *kSeqLen = kCollectionsSeqGetLen;
/// @brief Get the element at a given index in the Seq. O(1). Returns obj (Ptr).
inline constexpr const char *kSeqGet = kCollectionsSeqGet;
/// @brief Get a string element at a given index in a seq<str> Seq. Returns Str directly.
/// @details seq<str> sequences store raw rt_string pointers (not boxed). This function
///          casts the void* element to rt_string without boxing/unboxing overhead.
inline constexpr const char *kSeqGetStr = "Viper.Collections.Seq.GetStr";
/// @}

//=============================================================================
/// @name Math & System Aliases
/// @brief Short names for miscellaneous runtime functions.
/// @{
//=============================================================================

/// @brief Generate a random number. Maps to Viper.Math.Random.Next.
inline constexpr const char *kMathRandom = kMathRandomNext;
/// @brief Sleep for a given number of milliseconds. Maps to Viper.Time.SleepMs.
inline constexpr const char *kSystemSleep = kTimeSleepMs;
/// @}

//=============================================================================
/// @name Thread Aliases
/// @brief Short names for Viper.Threads.Thread functions.
/// @{
//=============================================================================

/// @brief Start a new thread executing a given function. Returns thread handle.
inline constexpr const char *kThreadSpawn = kThreadsThreadStart;
/// @brief Wait for a thread to complete execution.
inline constexpr const char *kThreadJoin = kThreadsThreadJoin;
/// @brief Suspend the current thread for a given number of milliseconds.
inline constexpr const char *kThreadSleep = kThreadsThreadSleep;
/// @}

//=============================================================================
// Zia-Specific Configuration Constants
//=============================================================================
/// @name Configuration Constants
/// @brief Compile-time constants for compiler behavior and object layout.
/// @{

/// @brief Maximum depth for import recursion to prevent stack overflow.
inline constexpr size_t kMaxImportDepth = 50;

/// @brief Maximum number of imported files to prevent runaway compilation.
inline constexpr size_t kMaxImportedFiles = 100;

/// @brief Object header size for entity types in bytes.
/// All entity instances begin with an 8-byte header containing runtime info.
inline constexpr size_t kObjectHeaderSize = 8;

/// @brief Offset of the vtable pointer within entity objects.
inline constexpr size_t kVtablePtrOffset = 8;

/// @brief Size of the vtable pointer in bytes.
inline constexpr size_t kVtablePtrSize = 8;

/// @brief Offset where entity fields begin (after header and vtable ptr).
inline constexpr size_t kEntityFieldsOffset = kObjectHeaderSize + kVtablePtrSize;

/// @}

//=============================================================================
// Internal Runtime Functions
//=============================================================================
/// @name Internal Runtime Functions
/// @brief Low-level runtime functions not in the Viper.* namespace.
/// @{

/// @brief Allocate memory for a runtime object.
/// @details Signature: rt_alloc(i64 classId, i64 size) -> ptr
inline constexpr const char *kRtAlloc = "rt_alloc";

/// @brief Get the class ID from a runtime object's header.
/// @details Signature: rt_obj_class_id(ptr) -> i64
inline constexpr const char *kRtObjClassId = "rt_obj_class_id";

/// @}

} // namespace il::frontends::zia::runtime
