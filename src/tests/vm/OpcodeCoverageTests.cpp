//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/OpcodeCoverageTests.cpp
// Purpose: Ensure every opcode declared in Opcode.def has an executable VM handler.
// Key invariants: Handler table entries are non-null for all non-whitelisted opcodes.
// Ownership/Lifetime: Test inspects static opcode metadata and dispatch table.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/OpcodeInfo.hpp"
#include "vm/VM.hpp"

#include <algorithm>
#include <array>
#include <cassert>

using il::core::Opcode;

namespace
{
/// @brief Opcodes intentionally lacking VM handlers.
/// @details Empty by default; populate when the VM is expected not to execute
/// specific opcodes (e.g., pseudo ops used only during lowering).
constexpr std::array<Opcode, 0> kWhitelistedOpcodes{};

bool isWhitelisted(Opcode opcode)
{
    return std::find(kWhitelistedOpcodes.begin(), kWhitelistedOpcodes.end(), opcode) !=
           kWhitelistedOpcodes.end();
}
} // namespace

int main()
{
    const auto &handlers = il::vm::VM::getOpcodeHandlers();
    const auto opcodes = il::core::all_opcodes();

    for (const auto opcode : opcodes)
    {
        if (isWhitelisted(opcode))
            continue;

        const auto index = static_cast<size_t>(opcode);
        assert(handlers[index] != nullptr && "opcode missing VM handler");
    }

    return 0;
}
