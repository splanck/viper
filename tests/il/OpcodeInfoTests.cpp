// File: tests/il/OpcodeInfoTests.cpp
// Purpose: Exercise opcode metadata enumeration helpers for stability.
// Key invariants: Enumeration covers every opcode exactly once in declaration order.
// Ownership/Lifetime: Uses read-only metadata from il::core.
// Links: docs/il-guide.md#reference

#include "il/core/OpcodeInfo.hpp"

#include <cassert>

int main()
{
    using namespace il::core;

    const auto ops = all_opcodes();
    assert(!ops.empty());

    const auto again = all_opcodes();
    assert(ops == again);

    for (size_t index = 0; index < ops.size(); ++index)
    {
        const Opcode op = ops[index];
        assert(static_cast<size_t>(op) == index);

        const auto mnemonic = opcode_mnemonic(op);
        assert(!mnemonic.empty());
        assert(mnemonic == getOpcodeInfo(op).name);
    }

    return 0;
}
