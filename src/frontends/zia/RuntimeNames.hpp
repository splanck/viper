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
/// This header imports the canonical runtime names from the generated
/// RuntimeNames.hpp and provides Zia-specific aliases for backwards
/// compatibility and convenience. It also defines Zia-specific configuration
/// constants for import limits and object layout.
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

// Import all generated names into this namespace
using namespace il::runtime::names;

//=============================================================================
// Name Aliases (where Zia conventions differ from generated names)
//=============================================================================

// String aliases
inline constexpr const char *kStringContains = kStringHas;
inline constexpr const char *kStringFromInt = kCoreConvertToStringInt;
inline constexpr const char *kStringFromNum = kCoreConvertToStringDouble;

// Core.Object aliases
inline constexpr const char *kObjectToString = kCoreObjectToString;

// Core.Box short aliases
inline constexpr const char *kBoxI64 = kCoreBoxI64;
inline constexpr const char *kBoxF64 = kCoreBoxF64;
inline constexpr const char *kBoxI1 = kCoreBoxI1;
inline constexpr const char *kBoxStr = kCoreBoxStr;
inline constexpr const char *kBoxValueType = kCoreBoxValueType;

// Core.Convert short aliases
inline constexpr const char *kConvertToDouble = kCoreConvertToDouble;
inline constexpr const char *kConvertToInt = kCoreConvertToInt;

// Core.Parse short aliases
inline constexpr const char *kParseDouble = kCoreParseDouble;
inline constexpr const char *kParseInt64 = kCoreParseInt64;

// Collections - short names for convenience
inline constexpr const char *kListNew = kCollectionsListNew;
inline constexpr const char *kListAdd = kCollectionsListPush;
inline constexpr const char *kListGet = kCollectionsListGet;
inline constexpr const char *kListSet = kCollectionsListSet;
inline constexpr const char *kListCount = kCollectionsListGetLen;
inline constexpr const char *kListClear = kCollectionsListClear;
inline constexpr const char *kListRemoveAt = kCollectionsListRemoveAt;
inline constexpr const char *kListContains = kCollectionsListHas;
inline constexpr const char *kListRemove = kCollectionsListRemove;
inline constexpr const char *kListInsert = kCollectionsListInsert;
inline constexpr const char *kListFind = kCollectionsListFind;

inline constexpr const char *kSetNew = kCollectionsSetNew;
inline constexpr const char *kSetCount = kCollectionsSetGetLen;
inline constexpr const char *kSetHas = kCollectionsSetHas;
inline constexpr const char *kSetPut = kCollectionsSetAdd;
inline constexpr const char *kSetDrop = kCollectionsSetRemove;
inline constexpr const char *kSetClear = kCollectionsSetClear;

inline constexpr const char *kMapNew = kCollectionsMapNew;
inline constexpr const char *kMapSet = kCollectionsMapSet;
inline constexpr const char *kMapGet = kCollectionsMapGet;
inline constexpr const char *kMapGetOr = kCollectionsMapGetOr;
inline constexpr const char *kMapContainsKey = kCollectionsMapHas;
inline constexpr const char *kMapCount = kCollectionsMapGetLen;
inline constexpr const char *kMapRemove = kCollectionsMapRemove;
inline constexpr const char *kMapSetIfMissing = kCollectionsMapSetIfMissing;
inline constexpr const char *kMapClear = kCollectionsMapClear;
inline constexpr const char *kMapKeys = kCollectionsMapKeys;
inline constexpr const char *kMapValues = kCollectionsMapValues;

inline constexpr const char *kSeqLen = kCollectionsSeqGetLen;
inline constexpr const char *kSeqGet = kCollectionsSeqGet;

// Boxing - unbox aliases
inline constexpr const char *kUnboxI64 = kCoreBoxToI64;
inline constexpr const char *kUnboxF64 = kCoreBoxToF64;
inline constexpr const char *kUnboxI1 = kCoreBoxToI1;
inline constexpr const char *kUnboxStr = kCoreBoxToStr;

// Math - random aliases (Viper.Math.Random.*)
inline constexpr const char *kMathRandom = kMathRandomNext;

// System - aliases to Time functions
inline constexpr const char *kSystemSleep = kTimeSleepMs;

// Thread - aliases to Threads.Thread functions
inline constexpr const char *kThreadSpawn = kThreadsThreadStart;
inline constexpr const char *kThreadJoin = kThreadsThreadJoin;
inline constexpr const char *kThreadSleep = kThreadsThreadSleep;

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
