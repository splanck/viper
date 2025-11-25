//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/UnknownOpcodeTests.cpp
// Purpose: Verify the VM traps gracefully when encountering unmapped opcodes.
// Key invariants: Unknown opcode dispatch produces InvalidOperation traps with mnemonic text.
// Ownership/Lifetime: Builds an ephemeral module executed in a forked child to capture stderr.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "common/TestIRBuilder.hpp"

#include <cassert>
#include <string>

using namespace il::core;

namespace
{
constexpr int kBogusOpcodeValue = static_cast<int>(Opcode::Count) + 17;
constexpr Opcode kBogusOpcode = static_cast<Opcode>(kBogusOpcodeValue);

} // namespace

TEST_WITH_IL(il, {
    const il::support::SourceLoc loc = il.loc();

    il::core::Instr invalid;
    invalid.result = il.reserveTemp();
    invalid.op = kBogusOpcode;
    invalid.type = Type(Type::Kind::I64);
    invalid.loc = loc;
    il.block().instructions.push_back(invalid);

    il.retVoid(loc);

    const std::string diag = il.captureTrap();
    const bool hasTrapKind =
        diag.find("Trap @main#0 line 1: InvalidOperation (code=0)") != std::string::npos;
    assert(hasTrapKind && "expected InvalidOperation trap for unmapped opcode");

    const bool hasPrefix = diag.find("unimplemented opcode:") != std::string::npos;
    assert(hasPrefix && "expected diagnostic prefix for unmapped opcode");

    const std::string mnemonic = "opcode#" + std::to_string(kBogusOpcodeValue);
    const bool hasMnemonic = diag.find(mnemonic) != std::string::npos;
    assert(hasMnemonic && "expected diagnostic to mention opcode mnemonic");

    const bool hasBlock = diag.find("(block entry)") != std::string::npos;
    assert(hasBlock && "expected diagnostic to mention source block label");
});
