//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_dispatch_coverage.cpp
// Purpose: Verify that all opcodes have handlers in all dispatch strategies.
// Key invariants: Every opcode in Opcode.def must have a corresponding handler
//                 or explicit stub in the function table.
// Ownership/Lifetime: Test uses static tables, no dynamic allocation.
// Links: vm/DispatchMacros.hpp
//
//===----------------------------------------------------------------------===//

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "vm/DispatchMacros.hpp"
#include "vm/ops/generated/HandlerTable.hpp"

#include <cassert>
#include <cstdio>

/// @brief Test that all opcodes have handlers in the function table.
static void testHandlerTableCoverage()
{
    const auto &handlers = il::vm::generated::opcodeHandlers();

    // Static assertion is in HandlerTable.hpp, this is a runtime verification
    assert(handlers.size() == il::core::kNumOpcodes &&
           "Handler table size does not match opcode count");

    size_t nullHandlers = 0;
    for (size_t i = 0; i < il::core::kNumOpcodes; ++i)
    {
        if (handlers[i] == nullptr)
        {
            const char *name = il::core::toString(static_cast<il::core::Opcode>(i));
            std::fprintf(stderr, "WARNING: null handler for opcode %zu (%s)\n", i, name);
            ++nullHandlers;
        }
    }

    // Currently all opcodes should have handlers (no explicit stubs needed)
    assert(nullHandlers == 0 && "Some opcodes have null handlers");
}

/// @brief Test that dispatch metadata matches opcode definitions.
static void testDispatchMetadataConsistency()
{
    // The VMDispatch enum should have the same count as opcodes
    static_assert(il::vm::dispatch::kDispatchCount == il::core::kNumOpcodes,
                  "VMDispatch enum count mismatch with Opcode enum");

    // Verify each opcode has valid dispatch metadata
    for (size_t i = 0; i < il::core::kNumOpcodes; ++i)
    {
        const auto &info = il::core::kOpcodeTable[i];
        // Verify dispatch kind is valid
        assert(static_cast<size_t>(info.vmDispatch) <= static_cast<size_t>(il::core::VMDispatch::EhEntry) &&
               "Invalid VMDispatch value in opcode table");
    }
}

/// @brief Test that handlers can be looked up by opcode.
static void testHandlerLookupByOpcode()
{
    const auto &handlers = il::vm::generated::opcodeHandlers();

    // Test a few representative opcodes
    auto checkOpcode = [&](il::core::Opcode op, const char *name)
    {
        size_t index = static_cast<size_t>(op);
        assert(index < handlers.size() && "Opcode index out of bounds");
        assert(handlers[index] != nullptr && "Handler is null");
        (void)name; // Used for debugging if assert fails
    };

    checkOpcode(il::core::Opcode::Add, "Add");
    checkOpcode(il::core::Opcode::Sub, "Sub");
    checkOpcode(il::core::Opcode::Mul, "Mul");
    checkOpcode(il::core::Opcode::Load, "Load");
    checkOpcode(il::core::Opcode::Store, "Store");
    checkOpcode(il::core::Opcode::Br, "Br");
    checkOpcode(il::core::Opcode::CBr, "CBr");
    checkOpcode(il::core::Opcode::Ret, "Ret");
    checkOpcode(il::core::Opcode::Call, "Call");
    checkOpcode(il::core::Opcode::Trap, "Trap");
}

/// @brief Test the helper functions from DispatchMacros.hpp
static void testDispatchMacroHelpers()
{
    const auto &handlers = il::vm::generated::opcodeHandlers();

    // Test hasHandler
    assert(il::vm::dispatch::hasHandler(il::core::Opcode::Add, handlers) && "Add should have handler");
    assert(il::vm::dispatch::hasHandler(il::core::Opcode::Ret, handlers) && "Ret should have handler");

    // Test verifyAllHandlers
    assert(il::vm::dispatch::verifyAllHandlers(handlers) && "All handlers should be present");
}

int main()
{
    testHandlerTableCoverage();
    testDispatchMetadataConsistency();
    testHandlerLookupByOpcode();
    testDispatchMacroHelpers();

    std::printf("All dispatch coverage tests passed.\n");
    return 0;
}
