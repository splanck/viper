// File: tests/vm/UnknownOpcodeTests.cpp
// Purpose: Verify the VM traps gracefully when encountering unmapped opcodes.
// Key invariants: Unknown opcode dispatch produces InvalidOperation traps with mnemonic text.
// Ownership/Lifetime: Builds an ephemeral module executed in a forked child to capture stderr.
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include "tests/common/VmFixture.hpp"

#include <cassert>
#include <string>

using namespace il::core;

namespace
{
constexpr int kBogusOpcodeValue = static_cast<int>(Opcode::Count) + 17;
constexpr Opcode kBogusOpcode = static_cast<Opcode>(kBogusOpcodeValue);

} // namespace

int main()
{
    using viper::tests::VmFixture;

    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const il::support::SourceLoc loc{1, 1, 1};

    Instr invalid;
    invalid.result = builder.reserveTempId();
    invalid.op = kBogusOpcode;
    invalid.type = Type(Type::Kind::I64);
    invalid.loc = loc;
    bb.instructions.push_back(invalid);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = loc;
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string diag = fixture.captureTrap(module);
    const bool hasTrapKind = diag.find("Trap @main#0 line 1: InvalidOperation (code=0)") != std::string::npos;
    assert(hasTrapKind && "expected InvalidOperation trap for unmapped opcode");

    const bool hasPrefix = diag.find("unimplemented opcode:") != std::string::npos;
    assert(hasPrefix && "expected diagnostic prefix for unmapped opcode");

    const std::string mnemonic = "opcode#" + std::to_string(kBogusOpcodeValue);
    const bool hasMnemonic = diag.find(mnemonic) != std::string::npos;
    assert(hasMnemonic && "expected diagnostic to mention opcode mnemonic");

    return 0;
}
