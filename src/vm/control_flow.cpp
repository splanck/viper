//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/control_flow.cpp
// Purpose: Provide a stable translation unit for legacy control-flow handlers.
// Key invariants: All control-flow opcode implementations now live in vm/ops
//                 yet must continue to link through this historical entry point.
// Ownership/Lifetime: No runtime state; exists solely to preserve linkage until
//                     dependants are updated.
// Links: docs/runtime-vm.md#vm-dispatch
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Control.hpp"

namespace il::vm::detail::control
{
// Intentionally empty: see vm/ops/Op_*.cpp for implementations.
}

