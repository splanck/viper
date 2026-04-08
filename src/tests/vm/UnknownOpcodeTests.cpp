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

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <string>

using namespace il::core;

namespace {
constexpr int kBogusOpcodeValue = static_cast<int>(Opcode::Count) + 17;
constexpr Opcode kBogusOpcode = static_cast<Opcode>(kBogusOpcodeValue);

void runUnknownOpcodeChild() {
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr invalid;
    invalid.result = builder.reserveTempId();
    invalid.op = kBogusOpcode;
    invalid.type = Type(Type::Kind::I64);
    invalid.loc = {1, 1, 1};
    bb.instructions.push_back(invalid);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    vm.run();
}

} // namespace

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(runUnknownOpcodeChild);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    const auto result = viper::tests::runIsolated(runUnknownOpcodeChild);
    assert(result.trapped() && "expected invalid opcode to trap");

    const std::string &diag = result.stderrText;
    // Format: "Trap @function:block#ip line N: Kind (code=C)"
    const bool hasTrapKind =
        diag.find("Trap @main:entry#0 line 1: InvalidOperation (code=0)") != std::string::npos;
    assert(hasTrapKind && "expected InvalidOperation trap for unmapped opcode");

    const bool hasPrefix = diag.find("unimplemented opcode:") != std::string::npos;
    assert(hasPrefix && "expected diagnostic prefix for unmapped opcode");

    const std::string mnemonic = "opcode#" + std::to_string(kBogusOpcodeValue);
    const bool hasMnemonic = diag.find(mnemonic) != std::string::npos;
    assert(hasMnemonic && "expected diagnostic to mention opcode mnemonic");

    const bool hasBlock = diag.find("(block entry)") != std::string::npos;
    assert(hasBlock && "expected diagnostic to mention source block label");
    return 0;
}
