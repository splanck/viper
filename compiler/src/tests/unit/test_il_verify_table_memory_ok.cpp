//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_table_memory_ok.cpp
// Purpose: Ensure verifier accepts basic stack memory operations.
// Key invariants: Memory instructions with matching pointer arithmetic and types pass verification.
// Ownership/Lifetime: Constructs module locally for verification.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <sstream>

int main()
{
    using namespace il::core;

    Module module;

    Function fn;
    fn.name = "mem_ok";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr allocaInstr;
    allocaInstr.result = 0;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands.push_back(Value::constInt(4));

    Instr gepInstr;
    gepInstr.result = 1;
    gepInstr.op = Opcode::GEP;
    gepInstr.type = Type(Type::Kind::Ptr);
    gepInstr.operands.push_back(Value::temp(0));
    gepInstr.operands.push_back(Value::constInt(0));

    Instr loadInstr;
    loadInstr.result = 2;
    loadInstr.op = Opcode::Load;
    loadInstr.type = Type(Type::Kind::I32);
    loadInstr.operands.push_back(Value::temp(1));

    Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = Type(Type::Kind::I32);
    storeInstr.operands.push_back(Value::temp(1));
    storeInstr.operands.push_back(Value::temp(2));

    Instr retInstr;
    retInstr.op = Opcode::Ret;
    retInstr.type = Type(Type::Kind::Void);

    entry.instructions.push_back(allocaInstr);
    entry.instructions.push_back(gepInstr);
    entry.instructions.push_back(loadInstr);
    entry.instructions.push_back(storeInstr);
    entry.instructions.push_back(retInstr);
    entry.terminated = true;

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);

    std::ostringstream diag;
    auto result = il::verify::Verifier::verify(module);
    if (!result)
    {
        il::support::printDiag(result.error(), diag);
    }

    assert(result);
    assert(diag.str().empty());

    return 0;
}
