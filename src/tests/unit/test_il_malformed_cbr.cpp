// File: tests/unit/test_il_malformed_cbr.cpp
// Purpose: Ensure serializer handles conditional branches with missing labels.
// Key invariants: Serializer should not crash on malformed cbr instructions.
// Ownership/Lifetime: Test constructs modules on stack.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "viper/il/IO.hpp"
#include <cassert>
#include <string>

int main()
{
    using namespace il::core;
    Module m;
    Function f;
    f.name = "f";
    f.retType = Type(Type::Kind::Void);

    BasicBlock bb;
    bb.label = "entry";

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::constInt(1));
    cbr.labels.push_back("L1"); // Missing second label
    bb.instructions.push_back(std::move(cbr));

    f.blocks.push_back(std::move(bb));
    m.functions.push_back(std::move(f));

    std::string out = il::io::Serializer::toString(m);
    assert(out.find("missing label") != std::string::npos);
    return 0;
}
