//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/classes/RuntimeClasses.hpp
// Purpose: Describe runtime classes (initially Viper.String) as C++ POD
//          descriptors consumable by frontends, verifiers, and backends.
// Invariants:
//   - The catalog is immutable after construction and returned as a const ref.
//   - All string fields are static string literals or nullptr where allowed.
//   - Receiver (object) is arg0 in method signatures.
// Ownership/Lifetime:
//   - The returned vector and elements have static storage lifetime.
// Links:
//   - docs/il-guide.md#reference
//   - docs/architecture.md#cpp-overview
//   - src/il/runtime/classes/RuntimeClasses.inc (single source of truth)

#pragma once

#include <cstddef>
#include <vector>

namespace il::runtime
{

/// @brief Stable identifiers for runtime class types.
/// @note Only RTCLS_String is seeded for RC-1; future classes extend this enum.
enum class RuntimeTypeId : std::size_t
{
    RTCLS_String = 0,
    RTCLS_StringBuilder,
    RTCLS_Object,
    RTCLS_File,
    RTCLS_Path,
    RTCLS_Dir,
    RTCLS_List,
    RTCLS_Math,
    RTCLS_Convert,
    RTCLS_Random,
    RTCLS_Environment,
    RTCLS_Exec,
    RTCLS_Fmt,
    RTCLS_Canvas,
    RTCLS_Codec,
    RTCLS_Csv,
    RTCLS_Color,
    RTCLS_DateTime,
    RTCLS_Map,
    RTCLS_Seq,
    RTCLS_Stack,
    RTCLS_TreeMap,
    RTCLS_Queue,
    RTCLS_Heap,
    RTCLS_Ring,
    RTCLS_Bits,
    RTCLS_Bytes,
    RTCLS_Bag,
    RTCLS_BinFile,
    RTCLS_MemStream,
    RTCLS_LineReader,
    RTCLS_LineWriter,
    RTCLS_Watcher,
    RTCLS_Compress,
    RTCLS_Archive,
    RTCLS_Log,
    RTCLS_Machine,
    RTCLS_Terminal,
    RTCLS_Clock,
    RTCLS_Countdown,
    RTCLS_Stopwatch,
    RTCLS_Guid,
    RTCLS_Hash,
    RTCLS_KeyDerive,
    RTCLS_CryptoRand,
    RTCLS_Pattern,
    RTCLS_Template,
    RTCLS_Vec2,
    RTCLS_Vec3,
    RTCLS_Pixels,
    RTCLS_ThreadsMonitor,
    RTCLS_ThreadsSafeI64,
    RTCLS_ThreadsThread,
    RTCLS_ThreadsGate,
    RTCLS_ThreadsBarrier,
    RTCLS_ThreadsRwLock,
    RTCLS_Tcp,
    RTCLS_TcpServer,
    RTCLS_Udp,
    RTCLS_Dns,
    RTCLS_Http,
    RTCLS_HttpReq,
    RTCLS_HttpRes,
    RTCLS_Url,
    RTCLS_Keyboard,
    RTCLS_Mouse,
    RTCLS_Pad,
};

/// @brief Describes a property on a runtime class.
/// @details Properties surface as getters/setters. Setters may be null when
///          the property is read-only.
struct RuntimeProperty
{
    const char *name;   ///< Property name (e.g., "Length").
    const char *type;   ///< IL scalar type (e.g., "i64", "i1").
    const char *getter; ///< Canonical extern target (e.g., "Viper.String.get_Length").
    const char *setter; ///< Canonical extern target or nullptr if read-only.
    bool readonly;      ///< True if setter is nullptr.
};

/// @brief Describes a method on a runtime class.
struct RuntimeMethod
{
    const char *name;      ///< Method name (e.g., "Substring").
    const char *signature; ///< Signature string in compact IL grammar.
    const char *target;    ///< Canonical extern target (e.g., "Viper.String.Substring").
};

/// @brief Describes a runtime class and its members.
struct RuntimeClass
{
    const char *qname;    ///< Fully-qualified name (e.g., "Viper.String").
    const char *layout;   ///< Layout descriptor (opaque until object model defined).
    const char *ctor;     ///< Optional ctor helper extern; may be nullptr.
    RuntimeTypeId typeId; ///< Stable type identifier.
    std::vector<RuntimeProperty> properties; ///< Declared properties.
    std::vector<RuntimeMethod> methods;      ///< Declared methods.
};

/// @brief Expose the immutable runtime class catalog.
/// @returns Const reference to a statically initialized vector of descriptors.
/// @signature-grammar
///   ret(args) where:
///     - ret, args are IL scalars: i64, f64, i1, str, obj, void
///     - Methods implicitly take the receiver as arg0 (not spelled in signature)
///     - Example: "str(i64,i64)" for String.Substring(start,len) -> string
const std::vector<RuntimeClass> &runtimeClassCatalog();

} // namespace il::runtime
