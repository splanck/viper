//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/zanna/vm/debug/DebugClassLayout.hpp
// Purpose: Plain-data class-layout sidecar the host installs on the debug
//          controller so the VM can expand user class instances field-by-field
//          at a stop (ADR 0138).
// Key invariants:
//   - Pure data: no VM, runtime, or frontend dependencies. The host (tool)
//     converts the frontend's export into this shape; the VM only reads it.
//   - Field offsets are byte offsets from the object base pointer (the value
//     rt_obj_new_i64 returned), exactly as the frontend's GEPs use them.
//   - Table keys are the compiler-assigned runtime class ids (positive for
//     user classes; builtin runtime ids are negative and never appear here).
// Ownership/Lifetime:
//   - Owned by DebugCtrl for the duration of a debug run; stop-time variable
//     stores borrow a const pointer that never outlives the run.
// Links: include/zanna/vm/debug/DebugFrontend.hpp, src/vm/debug/VMDebug.cpp,
//        docs/adr/0138-debug-class-layout-sidecar.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::vm {

/// @brief How the debugger reads a field's storage at (object base + offset).
enum class DebugFieldStorage : uint8_t {
    I64,     ///< 8-byte signed integer.
    I32,     ///< 4-byte signed integer (e.g. Byte fields).
    I16,     ///< 2-byte signed integer.
    I1,      ///< 1-byte boolean.
    F64,     ///< 8-byte IEEE double.
    Str,     ///< 8-byte runtime string handle (may be null).
    Managed, ///< 8-byte managed pointer (object/list/seq/map/box; may be null).
    Weak,    ///< 8-byte weak-reference slot; resolve via rt_weak_load.
    Raw,     ///< 8-byte raw pointer; shown as an opaque leaf, never dereferenced.
    Opaque,  ///< Inline aggregate or unknown storage; shown as a typed leaf.
};

/// @brief One instance field of a user class, as the debugger reads it.
struct DebugFieldLayout {
    std::string name;     ///< Source-level field name.
    std::string typeName; ///< Semantic type label (e.g. "Integer", "List[Str]").
    uint64_t offset = 0;  ///< Byte offset from the object base pointer.
    DebugFieldStorage storage = DebugFieldStorage::Opaque; ///< Read strategy.
    bool boolDisplay = false; ///< Format integer storage as true/false.
};

/// @brief Field layout of one user class (inherited fields first, then own).
struct DebugClassLayout {
    std::string qname;                    ///< Fully-qualified class name.
    std::vector<DebugFieldLayout> fields; ///< Instance fields in layout order.
};

/// @brief Class id -> layout, for every instantiated class of the debugged
///        module. Ids match what rt_obj_type_id returns for live instances.
using DebugClassLayoutTable = std::unordered_map<int64_t, DebugClassLayout>;

} // namespace il::vm
