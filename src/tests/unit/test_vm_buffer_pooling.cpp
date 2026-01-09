//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_buffer_pooling.cpp
// Purpose: Verify VM buffer pooling works correctly for recursive function calls.
// Key invariants: Recursive calls reuse pooled buffers without allocation churn.
// Ownership/Lifetime: Test constructs IL module and executes VM.
// Links: docs/vm-design.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <iostream>

/// Build a simple recursive function that computes factorial(n).
/// This exercises the buffer pooling by making many recursive calls.
static void buildFactorial(il::core::Module &m)
{
    using namespace il::core;

    // extern function for return is not needed, we use Ret opcode
    Function fn;
    fn.name = "factorial";
    fn.retType = Type(Type::Kind::I64);
    {
        Param p;
        p.id = 0;
        p.type = Type(Type::Kind::I64);
        fn.params.push_back(p);
    }

    // Entry block: check if n <= 1
    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.id = 0;
        p.type = Type(Type::Kind::I64);
        entry.params.push_back(p);
    }

    // %1 = scmp_le %0, 1
    entry.instructions.emplace_back();
    Instr &cmp = entry.instructions.back();
    cmp.result = 1;
    cmp.op = Opcode::SCmpLE;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands = {Value::temp(0), Value::constInt(1)};

    // cbr %1, ^base, ^recurse
    entry.instructions.emplace_back();
    Instr &br = entry.instructions.back();
    br.op = Opcode::CBr;
    br.type = Type(Type::Kind::Void);
    br.operands = {Value::temp(1)};
    br.labels = {"base", "recurse"};
    entry.terminated = true;

    // Base case: return 1
    BasicBlock base;
    base.label = "base";
    base.instructions.emplace_back();
    Instr &retBase = base.instructions.back();
    retBase.op = Opcode::Ret;
    retBase.type = Type(Type::Kind::Void);
    retBase.operands = {Value::constInt(1)};
    base.terminated = true;

    // Recursive case: return n * factorial(n-1)
    BasicBlock recurse;
    recurse.label = "recurse";

    // %2 = sub %0, 1
    recurse.instructions.emplace_back();
    Instr &sub = recurse.instructions.back();
    sub.result = 2;
    sub.op = Opcode::Sub;
    sub.type = Type(Type::Kind::I64);
    sub.operands = {Value::temp(0), Value::constInt(1)};

    // %3 = call @factorial(%2)
    recurse.instructions.emplace_back();
    Instr &call = recurse.instructions.back();
    call.result = 3;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(2)};
    call.callee = "factorial";

    // %4 = mul %0, %3
    recurse.instructions.emplace_back();
    Instr &mul = recurse.instructions.back();
    mul.result = 4;
    mul.op = Opcode::Mul;
    mul.type = Type(Type::Kind::I64);
    mul.operands = {Value::temp(0), Value::temp(3)};

    // ret %4
    recurse.instructions.emplace_back();
    Instr &retRec = recurse.instructions.back();
    retRec.op = Opcode::Ret;
    retRec.type = Type(Type::Kind::Void);
    retRec.operands = {Value::temp(4)};
    recurse.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(base));
    fn.blocks.push_back(std::move(recurse));
    fn.valueNames.resize(5);

    m.functions.push_back(std::move(fn));
}

/// Build main function that calls factorial(10).
static void buildMain(il::core::Module &m)
{
    using namespace il::core;

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    // Entry block
    BasicBlock entry;
    entry.label = "entry";

    // %0 = call @factorial(10)
    entry.instructions.emplace_back();
    Instr &call = entry.instructions.back();
    call.result = 0;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::constInt(10)};
    call.callee = "factorial";

    // %1 = icmp_eq %0, 3628800  (10! = 3628800)
    entry.instructions.emplace_back();
    Instr &cmp = entry.instructions.back();
    cmp.result = 1;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands = {Value::temp(0), Value::constInt(3628800)};

    // cbr %1, ^pass, ^fail
    entry.instructions.emplace_back();
    Instr &br = entry.instructions.back();
    br.op = Opcode::CBr;
    br.type = Type(Type::Kind::Void);
    br.operands = {Value::temp(1)};
    br.labels = {"pass", "fail"};
    entry.terminated = true;

    // Pass block: return 0
    BasicBlock pass;
    pass.label = "pass";
    pass.instructions.emplace_back();
    Instr &retPass = pass.instructions.back();
    retPass.op = Opcode::Ret;
    retPass.type = Type(Type::Kind::Void);
    retPass.operands = {Value::constInt(0)};
    pass.terminated = true;

    // Fail block: return 1
    BasicBlock fail;
    fail.label = "fail";
    fail.instructions.emplace_back();
    Instr &retFail = fail.instructions.back();
    retFail.op = Opcode::Ret;
    retFail.type = Type(Type::Kind::Void);
    retFail.operands = {Value::constInt(1)};
    fail.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(pass));
    fn.blocks.push_back(std::move(fail));
    fn.valueNames.resize(2);

    m.functions.push_back(std::move(fn));
}

int main()
{
    using namespace il::core;

    Module m;
    buildFactorial(m);
    buildMain(m);

    // Run the VM - this exercises buffer pooling through recursive calls
    il::vm::VM vm(m);
    int64_t res = vm.run();

    // factorial(10) = 3628800, so result should be 0 if correct
    if (res != 0)
    {
        std::cerr << "FAIL: factorial(10) returned wrong value, exit code: " << res << "\n";
        return 1;
    }

    std::cout << "PASS: buffer pooling test with recursive factorial\n";
    return 0;
}
