//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_irbuilder_assertions.cpp
// Purpose: Verify IRBuilder debug assertions enforce invariants correctly.
// Key invariants: Tests verify that valid usage patterns succeed and do not
//                 trigger assertions. Misuse scenarios are documented but
//                 cannot be tested directly since assertions abort.
// Ownership/Lifetime: Test constructs modules and inspects results.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <iostream>

using namespace il::core;
using namespace il::build;

/// Test: Valid function and block creation with unique names.
static void test_valid_function_and_block_creation()
{
    Module m;
    IRBuilder b(m);

    // Create a function with valid parameters
    auto &fn = b.startFunction(
        "test_func",
        Type(Type::Kind::I64),
        {Param{"x", Type(Type::Kind::I64), 0}, Param{"y", Type(Type::Kind::I32), 1}});
    assert(fn.name == "test_func");
    assert(fn.params.size() == 2);

    // Create blocks with unique labels
    // Note: Store indices or re-fetch references after each createBlock
    // since vector reallocation can invalidate references
    b.createBlock(fn, "entry", {});
    b.createBlock(fn, "loop", {Param{"i", Type(Type::Kind::I64), 0}});
    b.createBlock(fn, "exit", {});

    assert(fn.blocks.size() == 3);
    assert(fn.blocks[0].label == "entry");
    assert(fn.blocks[1].label == "loop");
    assert(fn.blocks[2].label == "exit");
    assert(fn.blocks[1].params.size() == 1);

    std::cout << "  test_valid_function_and_block_creation: PASSED\n";
}

/// Test: Valid extern creation with unique name.
static void test_valid_extern_creation()
{
    Module m;
    IRBuilder b(m);

    // Add unique externs
    b.addExtern("rt_print", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    b.addExtern("rt_str_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});

    assert(m.externs.size() == 2);
    assert(m.externs[0].name == "rt_print");
    assert(m.externs[1].name == "rt_str_len");

    std::cout << "  test_valid_extern_creation: PASSED\n";
}

/// Test: Valid global creation.
static void test_valid_global_creation()
{
    Module m;
    IRBuilder b(m);

    b.addGlobal("counter", Type(Type::Kind::I64), "");
    b.addGlobalStr("greeting", "Hello, World!");

    assert(m.globals.size() == 2);
    assert(m.globals[0].name == "counter");
    assert(m.globals[1].name == "greeting");

    std::cout << "  test_valid_global_creation: PASSED\n";
}

/// Test: Valid branch with matching argument counts.
static void test_valid_branch_arguments()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("test", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry", {});
    b.createBlock(fn, "target", {Param{"val", Type(Type::Kind::I64), 0}});

    // Re-fetch references after all blocks created
    auto &entry = fn.blocks[0];
    auto &target = fn.blocks[1];

    b.setInsertPoint(entry);
    // Branch with correct number of arguments
    b.br(target, {Value::constInt(42)});

    assert(entry.terminated);
    assert(entry.instructions.size() == 1);
    assert(entry.instructions[0].op == Opcode::Br);

    std::cout << "  test_valid_branch_arguments: PASSED\n";
}

/// Test: Valid conditional branch with matching argument counts.
static void test_valid_cbr_arguments()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("test", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry", {});
    b.createBlock(fn, "then", {});
    b.createBlock(fn, "else", {Param{"x", Type(Type::Kind::I64), 0}});

    // Re-fetch references after all blocks created
    auto &entry = fn.blocks[0];
    auto &then_bb = fn.blocks[1];
    auto &else_bb = fn.blocks[2];

    b.setInsertPoint(entry);
    unsigned condId = b.reserveTempId();
    Instr cmp;
    cmp.result = condId;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::constInt(1));
    cmp.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(std::move(cmp));

    // CBr with correct argument counts for each target
    b.cbr(Value::temp(condId), then_bb, {}, else_bb, {Value::constInt(10)});

    assert(entry.terminated);

    std::cout << "  test_valid_cbr_arguments: PASSED\n";
}

/// Test: Valid call emission with known callee.
static void test_valid_call_emission()
{
    Module m;
    IRBuilder b(m);

    b.addExtern("helper", Type(Type::Kind::I64), {Type(Type::Kind::I64)});

    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    Value dst = Value::temp(b.reserveTempId());
    b.emitCall("helper", {Value::constInt(5)}, dst, {});
    b.emitRet(dst, {});

    assert(entry.instructions.size() == 2);
    assert(entry.instructions[0].op == Opcode::Call);
    assert(entry.instructions[1].op == Opcode::Ret);

    std::cout << "  test_valid_call_emission: PASSED\n";
}

/// Test: Valid return emission.
static void test_valid_return_emission()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    b.emitRet(Value::constInt(42), {});

    assert(entry.terminated);
    assert(entry.instructions.size() == 1);
    assert(entry.instructions[0].op == Opcode::Ret);

    std::cout << "  test_valid_return_emission: PASSED\n";
}

/// Test: Block parameter access returns correct SSA values.
static void test_block_param_access()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("test", Type(Type::Kind::Void), {});
    b.createBlock(
        fn,
        "loop",
        {Param{"counter", Type(Type::Kind::I64), 0}, Param{"sum", Type(Type::Kind::I64), 1}});

    auto &bb = fn.blocks[0];
    Value p0 = b.blockParam(bb, 0);
    Value p1 = b.blockParam(bb, 1);

    assert(p0.kind == Value::Kind::Temp);
    assert(p1.kind == Value::Kind::Temp);
    assert(p0.id != p1.id); // Different temporaries

    std::cout << "  test_block_param_access: PASSED\n";
}

/// Test: Insert block at specific index.
static void test_insert_block_at_index()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("test", Type(Type::Kind::Void), {});
    b.addBlock(fn, "first");
    b.addBlock(fn, "last");

    // Insert a block in the middle
    b.insertBlock(fn, 1, "middle");

    assert(fn.blocks.size() == 3);
    assert(fn.blocks[0].label == "first");
    assert(fn.blocks[1].label == "middle");
    assert(fn.blocks[2].label == "last");

    std::cout << "  test_insert_block_at_index: PASSED\n";
}

/// Test: Reserve temp IDs correctly increment.
static void test_reserve_temp_increments()
{
    Module m;
    IRBuilder b(m);

    auto &fn =
        b.startFunction("test", Type(Type::Kind::Void), {Param{"x", Type(Type::Kind::I64), 0}});
    (void)fn;

    // After function with 1 param, nextTemp should be 1
    unsigned t1 = b.reserveTempId();
    unsigned t2 = b.reserveTempId();
    unsigned t3 = b.reserveTempId();

    assert(t1 == 1); // Param uses 0
    assert(t2 == 2);
    assert(t3 == 3);

    std::cout << "  test_reserve_temp_increments: PASSED\n";
}

/// Test: setInsertPoint changes active block.
static void test_set_insert_point()
{
    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("test", Type(Type::Kind::I64), {});
    b.addBlock(fn, "bb1");
    b.addBlock(fn, "bb2");

    // Re-fetch stable references
    auto &bb1 = fn.blocks[0];
    auto &bb2 = fn.blocks[1];

    b.setInsertPoint(bb1);
    b.emitRet(Value::constInt(1), {});

    b.setInsertPoint(bb2);
    b.emitRet(Value::constInt(2), {});

    assert(bb1.instructions.size() == 1);
    assert(bb2.instructions.size() == 1);
    assert(bb1.terminated);
    assert(bb2.terminated);

    std::cout << "  test_set_insert_point: PASSED\n";
}

/// Test: Module seeding in constructor picks up existing entries.
static void test_module_seeding()
{
    Module m;
    // Pre-populate the module
    m.functions.push_back({"existing_fn", Type(Type::Kind::Void), {}, {}, {}});
    m.externs.push_back({"existing_ext", Type(Type::Kind::I64), {}});

    IRBuilder b(m);

    // Can call existing function/extern
    auto &fn = b.startFunction("new_fn", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    // Should be able to emit call to pre-existing extern
    unsigned dst = b.reserveTempId();
    b.emitCall("existing_ext", {}, Value::temp(dst), {});
    b.emitRet(Value::temp(dst), {});

    assert(m.functions.size() == 2);

    std::cout << "  test_module_seeding: PASSED\n";
}

/*
 * DOCUMENTED MISUSE SCENARIOS
 * ===========================
 * The following scenarios would trigger debug assertions but cannot be tested
 * directly since assertions abort the program:
 *
 * 1. Empty function name:
 *    b.startFunction("", Type(Type::Kind::Void), {});
 *    -> Assertion: "function name cannot be empty"
 *
 * 2. Empty block label:
 *    b.createBlock(fn, "", {});
 *    -> Assertion: "block label cannot be empty"
 *
 * 3. Empty extern name:
 *    b.addExtern("", Type(Type::Kind::Void), {});
 *    -> Assertion: "extern name cannot be empty"
 *
 * 4. Empty global name:
 *    b.addGlobal("", Type(Type::Kind::I64), "");
 *    -> Assertion: "global name cannot be empty"
 *
 * 5. Duplicate block label:
 *    b.createBlock(fn, "entry", {});
 *    b.createBlock(fn, "entry", {});  // Same label
 *    -> Assertion: "block label already exists in function"
 *
 * 6. Duplicate extern name:
 *    b.addExtern("helper", Type(Type::Kind::Void), {});
 *    b.addExtern("helper", Type(Type::Kind::I64), {});
 *    -> Assertion: "extern name already exists in module"
 *
 * 7. Void parameter type:
 *    b.startFunction("f", Type(Type::Kind::Void), {
 *        Param{"bad", Type(Type::Kind::Void), 0}
 *    });
 *    -> Assertion: "parameter cannot have Void type"
 *
 * 8. Append to terminated block:
 *    b.setInsertPoint(bb);
 *    b.emitRet({}, {});
 *    Instr add{};
 *    add.op = Opcode::Add;
 *    b.append(std::move(add));  // Block already terminated
 *    -> Assertion: "cannot append non-terminator instruction to terminated block"
 *
 * 9. Dangling temp ID in operand:
 *    b.setInsertPoint(bb);
 *    b.emitRet(Value::temp(999), {});  // Temp 999 never allocated
 *    -> Assertion: "operand temp ID exceeds allocated temporaries"
 *
 * 10. Branch argument count mismatch:
 *     b.br(target, {});  // target has 2 params but 0 args provided
 *     -> Assertion: "branch argument count must match block parameter count"
 */

int main()
{
    std::cout << "Running IRBuilder assertion tests...\n";

    test_valid_function_and_block_creation();
    test_valid_extern_creation();
    test_valid_global_creation();
    test_valid_branch_arguments();
    test_valid_cbr_arguments();
    test_valid_call_emission();
    test_valid_return_emission();
    test_block_param_access();
    test_insert_block_at_index();
    test_reserve_temp_increments();
    test_set_insert_point();
    test_module_seeding();

    std::cout << "All IRBuilder assertion tests PASSED!\n";
    return 0;
}
