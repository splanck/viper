//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_dce_pure_call.cpp
// Purpose: Verify DCE pass correctly eliminates pure calls with unused results
//          and preserves impure calls even when their results are unused.
// Key invariants: Pure helpers should be removed; impure helpers must be kept.
// Ownership/Lifetime: Test uses in-memory IL modules.
// Links: src/il/transform/DCE.cpp, src/il/transform/CallEffects.hpp
//
//===----------------------------------------------------------------------===//

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/DCE.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace il::core;

namespace
{

/// @brief Build a test module with a call instruction.
/// @param callee The callee name for the call.
/// @param useResult Whether the call result is used by ret.
/// @return Module with a single function containing the call.
Module buildTestModule(const std::string &callee, bool useResult)
{
    Module m;
    m.version = "0.1.0";

    // Add extern declaration
    Extern ext;
    ext.name = callee;
    ext.params = {Type(Type::Kind::I64)};
    ext.retType = Type(Type::Kind::I64);
    m.externs.push_back(ext);

    // Build function
    Function f;
    f.name = "test";
    f.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    // Call instruction: %0 = call @callee(42)
    Instr callInstr;
    callInstr.op = Opcode::Call;
    callInstr.callee = callee;
    callInstr.type = Type(Type::Kind::I64);
    callInstr.result = 0;
    callInstr.operands = {Value::constInt(42)};
    entry.instructions.push_back(callInstr);

    // Return instruction
    Instr retInstr;
    retInstr.op = Opcode::Ret;
    if (useResult)
    {
        // ret %0 - uses the call result
        retInstr.operands = {Value::temp(0)};
    }
    else
    {
        // ret 0 - does NOT use the call result
        retInstr.operands = {Value::constInt(0)};
    }
    entry.instructions.push_back(retInstr);

    f.blocks.push_back(entry);
    m.functions.push_back(f);

    return m;
}

/// @brief Check if a module contains a call to the specified callee.
bool hasCallTo(const Module &m, const std::string &callee)
{
    for (const auto &f : m.functions)
        for (const auto &b : f.blocks)
            for (const auto &i : b.instructions)
                if (i.op == Opcode::Call && i.callee == callee)
                    return true;
    return false;
}

/// @brief Test: Pure call with unused result should be eliminated.
void testPureCallEliminated()
{
    // rt_abs_i64 is marked as pure in HelperEffects
    Module m = buildTestModule("rt_abs_i64", false);
    assert(hasCallTo(m, "rt_abs_i64") && "Precondition: call should exist before DCE");

    il::transform::dce(m);

    assert(!hasCallTo(m, "rt_abs_i64") && "Pure call with unused result should be eliminated");
}

/// @brief Test: Pure call with used result should be preserved.
void testPureCallPreservedWhenUsed()
{
    // rt_abs_i64 is pure, but result is used
    Module m = buildTestModule("rt_abs_i64", true);
    assert(hasCallTo(m, "rt_abs_i64") && "Precondition: call should exist before DCE");

    il::transform::dce(m);

    assert(hasCallTo(m, "rt_abs_i64") && "Pure call with used result should be preserved");
}

/// @brief Test: Impure call with unused result should be preserved.
void testImpureCallPreserved()
{
    // rt_print_i64 is impure (has I/O side effects)
    Module m = buildTestModule("rt_print_i64", false);
    assert(hasCallTo(m, "rt_print_i64") && "Precondition: call should exist before DCE");

    il::transform::dce(m);

    assert(hasCallTo(m, "rt_print_i64") && "Impure call should be preserved even if unused");
}

/// @brief Test: Unknown callee should be conservatively preserved.
void testUnknownCalleePreserved()
{
    // unknown_function is not in the registry, should be kept
    Module m = buildTestModule("unknown_function", false);
    assert(hasCallTo(m, "unknown_function") && "Precondition: call should exist before DCE");

    il::transform::dce(m);

    assert(hasCallTo(m, "unknown_function") && "Unknown callee should be preserved (conservative)");
}

/// @brief Test: Readonly call (reads memory) should be preserved.
void testReadonlyCallPreserved()
{
    // rt_len is readonly (reads string memory) but not pure
    Module m = buildTestModule("rt_len", false);
    assert(hasCallTo(m, "rt_len") && "Precondition: call should exist before DCE");

    il::transform::dce(m);

    // Readonly calls may still have observable effects (memory reads)
    // They should NOT be eliminated by DCE since they might observe state
    assert(hasCallTo(m, "rt_len") && "Readonly call should be preserved (not pure)");
}

/// @brief Test: Multiple pure math functions are eliminated.
void testMultiplePureMathEliminated()
{
    const std::vector<std::string> pureHelpers = {
        "rt_abs_f64", "rt_floor", "rt_ceil", "rt_sin", "rt_cos", "rt_sqrt", "rt_sgn_i64"};

    for (const auto &helper : pureHelpers)
    {
        Module m = buildTestModule(helper, false);
        il::transform::dce(m);
        assert(!hasCallTo(m, helper) &&
               ("Pure helper " + helper + " should be eliminated").c_str());
    }
}

} // namespace

int main()
{
    testPureCallEliminated();
    testPureCallPreservedWhenUsed();
    testImpureCallPreserved();
    testUnknownCalleePreserved();
    testReadonlyCallPreserved();
    testMultiplePureMathEliminated();

    return 0;
}
