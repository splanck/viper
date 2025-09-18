// File: src/frontends/basic/LowerRuntime.cpp
// Purpose: Implements runtime tracking and declaration utilities for BASIC lowering.
// Key invariants: Runtime declarations are emitted once in deterministic order.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cassert>
#include <string>
#include <string_view>

using namespace il::core;

namespace il::frontends::basic
{

namespace
{
void declareRuntimeExtern(build::IRBuilder &b, std::string_view name)
{
    const auto *sig = il::runtime::findRuntimeSignature(name);
    assert(sig && "runtime signature missing from registry");
    b.addExtern(std::string(name), sig->retType, sig->paramTypes);
}
} // namespace

// Purpose: declare required runtime.
// Parameters: build::IRBuilder &b.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::declareRequiredRuntime(build::IRBuilder &b)
{
    declareRuntimeExtern(b, "rt_print_str");
    declareRuntimeExtern(b, "rt_print_i64");
    declareRuntimeExtern(b, "rt_print_f64");
    declareRuntimeExtern(b, "rt_len");
    declareRuntimeExtern(b, "rt_substr");
    if (needRtConcat)
        declareRuntimeExtern(b, "rt_concat");
    if (boundsChecks)
        declareRuntimeExtern(b, "rt_trap");
    if (needInputLine)
        declareRuntimeExtern(b, "rt_input_line");
    if (needRtToInt)
        declareRuntimeExtern(b, "rt_to_int");
    if (needRtIntToStr)
        declareRuntimeExtern(b, "rt_int_to_str");
    if (needRtF64ToStr)
        declareRuntimeExtern(b, "rt_f64_to_str");
    if (needAlloc)
        declareRuntimeExtern(b, "rt_alloc");
    if (needRtLeft)
        declareRuntimeExtern(b, "rt_left");
    if (needRtRight)
        declareRuntimeExtern(b, "rt_right");
    if (needRtMid2)
        declareRuntimeExtern(b, "rt_mid2");
    if (needRtMid3)
        declareRuntimeExtern(b, "rt_mid3");
    if (needRtInstr2)
        declareRuntimeExtern(b, "rt_instr2");
    if (needRtInstr3)
        declareRuntimeExtern(b, "rt_instr3");
    if (needRtLtrim)
        declareRuntimeExtern(b, "rt_ltrim");
    if (needRtRtrim)
        declareRuntimeExtern(b, "rt_rtrim");
    if (needRtTrim)
        declareRuntimeExtern(b, "rt_trim");
    if (needRtUcase)
        declareRuntimeExtern(b, "rt_ucase");
    if (needRtLcase)
        declareRuntimeExtern(b, "rt_lcase");
    if (needRtChr)
        declareRuntimeExtern(b, "rt_chr");
    if (needRtAsc)
        declareRuntimeExtern(b, "rt_asc");

    for (RuntimeFn fn : runtimeOrder)
    {
        switch (fn)
        {
            case RuntimeFn::Sqrt:
                declareRuntimeExtern(b, "rt_sqrt");
                break;
            case RuntimeFn::AbsI64:
                declareRuntimeExtern(b, "rt_abs_i64");
                break;
            case RuntimeFn::AbsF64:
                declareRuntimeExtern(b, "rt_abs_f64");
                break;
            case RuntimeFn::Floor:
                declareRuntimeExtern(b, "rt_floor");
                break;
            case RuntimeFn::Ceil:
                declareRuntimeExtern(b, "rt_ceil");
                break;
            case RuntimeFn::Sin:
                declareRuntimeExtern(b, "rt_sin");
                break;
            case RuntimeFn::Cos:
                declareRuntimeExtern(b, "rt_cos");
                break;
            case RuntimeFn::Pow:
                declareRuntimeExtern(b, "rt_pow");
                break;
            case RuntimeFn::RandomizeI64:
                declareRuntimeExtern(b, "rt_randomize_i64");
                break;
            case RuntimeFn::Rnd:
                declareRuntimeExtern(b, "rt_rnd");
                break;
        }
    }

    if (needRtStrEq)
        declareRuntimeExtern(b, "rt_str_eq");
}

// Purpose: track runtime.
// Parameters: RuntimeFn fn.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::trackRuntime(RuntimeFn fn)
{
    if (runtimeSet.insert(fn).second)
        runtimeOrder.push_back(fn);
}

} // namespace il::frontends::basic
