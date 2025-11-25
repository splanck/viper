//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_instruction_checker_binary_arity.cpp
// Purpose: Regression test ensuring binary arithmetic instructions enforce operand arity.
// Key invariants: iadd.ovf instructions reject operand counts other than two and surface
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/InstructionChecker.hpp"

#include <cassert>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

int main()
{
    using namespace il::core;
    using il::verify::TypeInference;
    using il::verify::verifyInstruction;

    Function fn;
    fn.name = "arith";

    BasicBlock entry;
    entry.label = "entry";

    std::unordered_map<unsigned, Type> temps{
        {1u, Type(Type::Kind::I64)}, {2u, Type(Type::Kind::I64)}, {3u, Type(Type::Kind::I64)}};
    std::unordered_set<unsigned> defined{1u, 2u, 3u};
    TypeInference types(temps, defined);

    Instr add;
    add.result = 4u;
    add.op = Opcode::IAddOvf;
    add.operands.push_back(Value::temp(1u));
    add.operands.push_back(Value::temp(2u));
    add.operands.push_back(Value::temp(3u));

    std::unordered_map<std::string, const Function *> funcs;
    std::unordered_map<std::string, const Extern *> externs;

    std::ostringstream err;
    const bool ok = verifyInstruction(fn, entry, add, externs, funcs, types, err);
    assert(!ok);
    const std::string diag = err.str();
    assert(diag.find("invalid operand count") != std::string::npos);

    return 0;
}
