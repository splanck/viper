//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHelpers.cpp
// Purpose: Implement shared trap helpers declared in OpHelpers.hpp.
// Invariants: Diagnostics always provide function and block context when
//             available and never throw exceptions across the VM boundary.
// Ownership: Operates entirely on VM-managed state without persisting data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "viper/vm/internal/OpHelpers.hpp"

#include "il/core/Function.hpp"

#include <string>

namespace il::vm::internal::detail
{
void trapWithMessage(TrapKind kind,
                     const char *message,
                     const il::core::Instr &instr,
                     Frame &frame,
                     const il::core::BasicBlock *block)
{
    const std::string functionName = frame.func ? frame.func->name : std::string();
    const std::string blockLabel = block ? block->label : std::string();
    RuntimeBridge::trap(kind, message, instr.loc, functionName, blockLabel);
}
} // namespace il::vm::internal::detail
