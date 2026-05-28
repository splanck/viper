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

namespace {

using namespace il::core;

std::string verifyAndCaptureMessage(Module &module) {
    auto result = il::verify::Verifier::verify(module);
    assert(!result && "verification should fail for negative cases");
    return result.error().message;
}

void verifySucceeds(Module &module) {
    auto result = il::verify::Verifier::verify(module);
    assert(result && "verification should succeed");
}

} // namespace

int main() {
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
        fn.name = "duplicate_function_param_id";
        fn.retType = Type(Type::Kind::Void);
        fn.params.push_back(Param{"a", Type(Type::Kind::I64), 0});
        fn.params.push_back(Param{"b", Type(Type::Kind::I64), 0});
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("duplicate param id") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "duplicate_block_param_id";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        entry.params.push_back(Param{"a", Type(Type::Kind::I64), 0});
        entry.params.push_back(Param{"b", Type(Type::Kind::I64), 0});
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("duplicate temp %0") != std::string::npos ||
               message.find("duplicate param id %0") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "entry_param_alias_redefined";
        fn.retType = Type(Type::Kind::I64);
        fn.params.push_back(Param{"x", Type(Type::Kind::I64), 0});

        BasicBlock entry;
        entry.label = "entry";
        entry.params.push_back(Param{"x", Type(Type::Kind::I64), 0});
        Instr redefine;
        redefine.result = 0;
        redefine.op = Opcode::IAddOvf;
        redefine.type = Type(Type::Kind::I64);
        redefine.operands = {Value::constInt(1), Value::constInt(2)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(0)};
        entry.instructions = {redefine, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("duplicate temp %0") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "duplicate_instruction_result";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr first;
        first.result = 0;
        first.op = Opcode::IAddOvf;
        first.type = Type(Type::Kind::I64);
        first.operands = {Value::constInt(1), Value::constInt(2)};
        Instr second = first;
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {first, second, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("duplicate temp %0") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "checked_add_ptr_type";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr allocaPtr;
        allocaPtr.result = 0;
        allocaPtr.op = Opcode::Alloca;
        allocaPtr.type = Type(Type::Kind::Ptr);
        allocaPtr.operands = {Value::constInt(8)};
        Instr badAdd;
        badAdd.result = 1;
        badAdd.op = Opcode::IAddOvf;
        badAdd.type = Type(Type::Kind::Ptr);
        badAdd.operands = {Value::temp(0), Value::temp(0)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {allocaPtr, badAdd, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("integer binary result must be i16, i32, or i64") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "fixed_result_mismatch";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr cmp;
        cmp.result = 0;
        cmp.op = Opcode::ICmpEq;
        cmp.type = Type(Type::Kind::I64);
        cmp.operands = {Value::constInt(1), Value::constInt(1)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {cmp, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("result type mismatch") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "unknown_branch_arg";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"next"};
        br.brArgs = {{Value::temp(99)}};
        entry.instructions.push_back(br);
        entry.terminated = true;

        BasicBlock next;
        next.label = "next";
        next.params.push_back(Param{"x", Type(Type::Kind::I64), 0});
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        next.instructions.push_back(ret);
        next.terminated = true;

        fn.blocks = {entry, next};
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("unknown temp %99") != std::string::npos ||
               message.find("unknown branch arg") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "dominance_error";
        fn.retType = Type(Type::Kind::I64);

        BasicBlock entry;
        entry.label = "entry";
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::constBool(true)};
        cbr.labels = {"left", "right"};
        cbr.brArgs = {{}, {}};
        entry.instructions.push_back(cbr);
        entry.terminated = true;

        BasicBlock left;
        left.label = "left";
        Instr retLeft;
        retLeft.op = Opcode::Ret;
        retLeft.type = Type(Type::Kind::Void);
        retLeft.operands = {Value::temp(0)};
        left.instructions.push_back(retLeft);
        left.terminated = true;

        BasicBlock right;
        right.label = "right";
        Instr def;
        def.result = 0;
        def.op = Opcode::IAddOvf;
        def.type = Type(Type::Kind::I64);
        def.operands = {Value::constInt(1), Value::constInt(2)};
        Instr retRight;
        retRight.op = Opcode::Ret;
        retRight.type = Type(Type::Kind::Void);
        retRight.operands = {Value::temp(0)};
        right.instructions = {def, retRight};
        right.terminated = true;

        fn.blocks = {entry, left, right};
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("not dominated") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "unreachable_roots_merge";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr entryRet;
        entryRet.op = Opcode::Ret;
        entryRet.type = Type(Type::Kind::Void);
        entry.instructions = {entryRet};
        entry.terminated = true;

        BasicBlock deadA;
        deadA.label = "dead_a";
        Instr brA;
        brA.op = Opcode::Br;
        brA.type = Type(Type::Kind::Void);
        brA.labels = {"join"};
        brA.brArgs = {{Value::constInt(1)}};
        deadA.instructions = {brA};
        deadA.terminated = true;

        BasicBlock deadB;
        deadB.label = "dead_b";
        Instr brB;
        brB.op = Opcode::Br;
        brB.type = Type(Type::Kind::Void);
        brB.labels = {"join"};
        brB.brArgs = {{Value::constInt(2)}};
        deadB.instructions = {brB};
        deadB.terminated = true;

        BasicBlock join;
        join.label = "join";
        join.params.push_back(Param{"x", Type(Type::Kind::I64), 0});
        Instr joinRet;
        joinRet.op = Opcode::Ret;
        joinRet.type = Type(Type::Kind::Void);
        join.instructions = {joinRet};
        join.terminated = true;

        fn.blocks = {entry, deadA, deadB, join};
        module.functions.push_back(fn);

        verifySucceeds(module);
    }

    {
        Module module;

        Function fn;
        fn.name = "return_gep_alloca";
        fn.retType = Type(Type::Kind::Ptr);

        BasicBlock entry;
        entry.label = "entry";
        Instr allocaPtr;
        allocaPtr.result = 0;
        allocaPtr.op = Opcode::Alloca;
        allocaPtr.type = Type(Type::Kind::Ptr);
        allocaPtr.operands = {Value::constInt(8)};
        Instr gep;
        gep.result = 1;
        gep.op = Opcode::GEP;
        gep.type = Type(Type::Kind::Ptr);
        gep.operands = {Value::temp(0), Value::constInt(0)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions = {allocaPtr, gep, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("returning alloca-derived pointer") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "bad_indirect_callee";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr intValue;
        intValue.result = 0;
        intValue.op = Opcode::IAddOvf;
        intValue.type = Type(Type::Kind::I64);
        intValue.operands = {Value::constInt(1), Value::constInt(2)};
        Instr call;
        call.op = Opcode::CallIndirect;
        call.type = Type(Type::Kind::Void);
        call.operands = {Value::temp(0)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {intValue, call, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("call.indirect callee must be ptr") != std::string::npos);
    }

    {
        Module module;
        module.externs.push_back({"same", Type(Type::Kind::Void), {}});

        Function fn;
        fn.name = "same";
        fn.retType = Type(Type::Kind::Void);
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions.push_back(ret);
        entry.terminated = true;
        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("collides with extern") != std::string::npos);
    }

    {
        Module module;
        module.externs.push_back({"rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)}});

        Function fn;
        fn.name = "bad_call_attr";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr call;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::Void);
        call.callee = "rt_print_i64";
        call.operands = {Value::constInt(1)};
        call.CallAttr.pure = true;
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {call, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("pure attribute contradicts") != std::string::npos);
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

        Param idxParam;
        idxParam.name = "idx";
        idxParam.type = Type(Type::Kind::I32);
        idxParam.id = 1;
        fn.params.push_back(idxParam);

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
        entry.instructions.push_back(gepBad);
        entry.instructions.push_back(ret);
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("operand 1 must be i64") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "partial_branch_args";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr cond;
        cond.result = 0;
        cond.op = Opcode::ICmpEq;
        cond.type = Type(Type::Kind::I1);
        cond.operands = {Value::constInt(0), Value::constInt(0)};
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(0)};
        cbr.labels = {"left", "right"};
        cbr.brArgs = {{}};
        entry.instructions = {cond, cbr};
        entry.terminated = true;

        BasicBlock left;
        left.label = "left";
        Instr leftRet;
        leftRet.op = Opcode::Ret;
        leftRet.type = Type(Type::Kind::Void);
        left.instructions = {leftRet};
        left.terminated = true;

        BasicBlock right = left;
        right.label = "right";
        right.params.push_back(Param{"x", Type(Type::Kind::I64), 1});

        fn.blocks = {entry, left, right};
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("branch arg count mismatch") != std::string::npos);
    }

    {
        Module module;

        Function fn;
        fn.name = "branch_i32_literal_arg";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"target"};
        br.brArgs = {{Value::constInt(1)}};
        entry.instructions = {br};
        entry.terminated = true;

        BasicBlock target;
        target.label = "target";
        target.params.push_back(Param{"x", Type(Type::Kind::I32), 0});
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        target.instructions = {ret};
        target.terminated = true;

        fn.blocks = {entry, target};
        module.functions.push_back(fn);

        verifySucceeds(module);
    }

    {
        Module module;

        Function fn;
        fn.name = "ret_i32_literal";
        fn.retType = Type(Type::Kind::I32);

        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::constInt(7)};
        entry.instructions = {ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        verifySucceeds(module);
    }

    {
        Module module;

        Function fn;
        fn.name = "pure_store";
        fn.retType = Type(Type::Kind::Void);
        fn.attrs().pure = true;

        BasicBlock entry;
        entry.label = "entry";
        Instr allocPtr;
        allocPtr.result = 0;
        allocPtr.op = Opcode::Alloca;
        allocPtr.type = Type(Type::Kind::Ptr);
        allocPtr.operands.push_back(Value::constInt(8));
        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands = {Value::temp(0), Value::constInt(1)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {allocPtr, store, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        verifySucceeds(module);
    }

    {
        Module module;

        Function fn;
        fn.name = "nothrow_trap";
        fn.retType = Type(Type::Kind::Void);
        fn.attrs().nothrow = true;

        BasicBlock entry;
        entry.label = "entry";
        Instr trap;
        trap.op = Opcode::Trap;
        trap.type = Type(Type::Kind::Void);
        entry.instructions = {trap};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("nothrow function contains trapping instruction") != std::string::npos);
    }

    {
        Module module;
        module.externs.push_back({"sink", Type(Type::Kind::Void), {Type(Type::Kind::Ptr)}});

        Function fn;
        fn.name = "alloca_direct_call_borrow";
        fn.retType = Type(Type::Kind::Void);

        BasicBlock entry;
        entry.label = "entry";
        Instr allocPtr;
        allocPtr.result = 0;
        allocPtr.op = Opcode::Alloca;
        allocPtr.type = Type(Type::Kind::Ptr);
        allocPtr.operands.push_back(Value::constInt(8));
        Instr call;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::Void);
        call.callee = "sink";
        call.operands = {Value::temp(0)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {allocPtr, call, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        verifySucceeds(module);
    }

    {
        Module module;

        Function fn;
        fn.name = "alloca_indirect_call_escape";
        fn.retType = Type(Type::Kind::Void);
        fn.params.push_back(Param{"fnptr", Type(Type::Kind::Ptr), 10});

        BasicBlock entry;
        entry.label = "entry";
        entry.params.push_back(Param{"fnptr", Type(Type::Kind::Ptr), 10});
        Instr allocPtr;
        allocPtr.result = 0;
        allocPtr.op = Opcode::Alloca;
        allocPtr.type = Type(Type::Kind::Ptr);
        allocPtr.operands.push_back(Value::constInt(8));
        Instr call;
        call.op = Opcode::CallIndirect;
        call.type = Type(Type::Kind::Void);
        call.hasIndirectSignature = true;
        call.indirectRetType = Type(Type::Kind::Void);
        call.indirectParamTypes = {Type(Type::Kind::Ptr)};
        call.operands = {Value::temp(10), Value::temp(0)};
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        entry.instructions = {allocPtr, call, ret};
        entry.terminated = true;

        fn.blocks.push_back(entry);
        module.functions.push_back(fn);

        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("passing alloca-derived pointer") != std::string::npos);
    }

    return 0;
}
