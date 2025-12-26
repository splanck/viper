//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_dispatch_handlers.cpp
// Purpose: Ensure every opcode annotated with a VM dispatch kind has a handler.
// Key invariants: VM opcode handler table provides non-null entries for all
// Ownership/Lifetime: Test uses static metadata only.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/OpcodeInfo.hpp"
#include "vm/VM.hpp"

#include <cassert>

int main()
{
    const auto &handlers = il::vm::VM::getOpcodeHandlers();

    for (size_t idx = 0; idx < il::core::kNumOpcodes; ++idx)
    {
        const auto &info = il::core::kOpcodeTable[idx];
        if (info.vmDispatch == il::core::VMDispatch::None)
            continue;

        assert(handlers[idx] != nullptr && "dispatchable opcode missing handler");
    }

    return 0;
}
