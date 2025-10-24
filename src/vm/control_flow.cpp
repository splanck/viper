//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the legacy control-flow dispatch entry point retained for backward
// compatibility.  Historically the VM linked control-flow handlers through this
// translation unit; modern handlers now live in `vm/ops`, but some build systems
// and tests still expect this TU to participate in the link.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Compatibility shim that keeps the historical control-flow dispatcher
///        translation unit alive.
/// @details The real implementations for branch, call, and return opcodes now
///          reside in the `vm/ops` directory.  Keeping this file (and the
///          associated namespace) ensures older link scripts continue to resolve
///          the symbols without modification until all callers migrate to the
///          new layout.  No functions are defined here because the namespace only
///          exists as a forwarding placeholder.

#include "vm/OpHandlers_Control.hpp"

namespace il::vm::detail::control
{
/// @brief Sentinel namespace used solely to preserve linkage expectations.
/// @details Control-flow opcode handlers previously lived in this namespace.
///          They were moved into dedicated files under `vm/ops`, but the empty
///          namespace remains so translation units that reference it still link
///          successfully while the codebase transitions to the new structure.
/// @note Intentionally empty: all logic lives in `vm/ops/Op_*.cpp`.
} // namespace il::vm::detail::control
