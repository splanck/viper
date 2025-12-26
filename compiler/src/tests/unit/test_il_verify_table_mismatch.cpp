//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_table_mismatch.cpp
// Purpose: Ensure verifier reports clear diagnostics for table-driven mismatches.
// Key invariants: Operand and constant typing errors produce stable substrings.
// Ownership/Lifetime: Constructs verifier modules locally within each test case.
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

#include <cassert>
#include <string>

namespace
{

using namespace il::core;

std::string verifyAndCaptureMessage(Module &module)
{
    auto result = il::verify::Verifier::verify(module);
    assert(!result && "verification should fail for negative cases");
    return result.error().message;
}

} // namespace

int main()
{
    {
        Module module;

        Function fn;
        fn.name = "missing_result";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr add;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::constInt(1));
        add.operands.push_back(Value::constInt(2));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);

        entry.instructions.push_back(add);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("missing result") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "load_bad";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr buildInt;
        buildInt.result = 0;
        buildInt.op = Opcode::IAddOvf;
        buildInt.type = Type(Type::Kind::I64);
        buildInt.operands.push_back(Value::constInt(1));
        buildInt.operands.push_back(Value::constInt(2));

        Instr loadBad;
        loadBad.result = 1;
        loadBad.op = Opcode::Load;
        loadBad.type = Type(Type::Kind::I32);
        loadBad.operands.push_back(Value::temp(0));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);

        entry.instructions.push_back(buildInt);
        entry.instructions.push_back(loadBad);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        const bool mentionsPointer = message.find("pointer") != std::string::npos;
        const bool mentionsMismatch = message.find("mismatch") != std::string::npos;
        assert(mentionsPointer && mentionsMismatch);
    }

    {
        Module module;

        Function fn;
        fn.name = "store_range";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr allocPtr;
        allocPtr.result = 0;
        allocPtr.op = Opcode::Alloca;
        allocPtr.type = Type(Type::Kind::Ptr);
        allocPtr.operands.push_back(Value::constInt(8));

        Instr storeBad;
        storeBad.op = Opcode::Store;
        storeBad.type = Type(Type::Kind::I16);
        storeBad.operands.push_back(Value::temp(0));
        storeBad.operands.push_back(Value::constInt(70000));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);

        entry.instructions.push_back(allocPtr);
        entry.instructions.push_back(storeBad);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        const bool rangeMessage =
            message.find("value out of range for store type") != std::string::npos ||
            message.find("operand 1 constant out of range for i16") != std::string::npos;
        assert(rangeMessage);
    }

    {
        Module module;

        Function fn;
        fn.name = "gep_index";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";

        Instr allocPtr;
        allocPtr.result = 0;
        allocPtr.op = Opcode::Alloca;
        allocPtr.type = Type(Type::Kind::Ptr);
        allocPtr.operands.push_back(Value::constInt(8));

        Instr narrowIdx;
        narrowIdx.result = 1;
        narrowIdx.op = Opcode::CastSiNarrowChk;
        narrowIdx.type = Type(Type::Kind::I32);
        narrowIdx.operands.push_back(Value::constInt(0));

        Instr gepBad;
        gepBad.result = 2;
        gepBad.op = Opcode::GEP;
        gepBad.type = Type(Type::Kind::Ptr);
        gepBad.operands.push_back(Value::temp(0));
        gepBad.operands.push_back(Value::temp(1));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);

        entry.instructions.push_back(allocPtr);
        entry.instructions.push_back(narrowIdx);
        entry.instructions.push_back(gepBad);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("operand 1 must be i64") != std::string::npos);
    }

    return 0;
}
