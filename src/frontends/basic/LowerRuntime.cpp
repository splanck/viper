// File: src/frontends/basic/LowerRuntime.cpp
// Purpose: Implements runtime tracking and declaration utilities for BASIC lowering.
// Key invariants: Runtime declarations are emitted once in deterministic order.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"

using namespace il::core;

namespace il::frontends::basic
{

// Purpose: declare required runtime.
// Parameters: build::IRBuilder &b.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::declareRequiredRuntime(build::IRBuilder &b)
{
    using Type = il::core::Type;
    b.addExtern("rt_print_str", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    b.addExtern("rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)});
    b.addExtern("rt_print_f64", Type(Type::Kind::Void), {Type(Type::Kind::F64)});
    b.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    b.addExtern("rt_substr",
                Type(Type::Kind::Str),
                {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)});
    if (needRtConcat)
        b.addExtern(
            "rt_concat", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::Str)});
    if (boundsChecks)
        b.addExtern("rt_trap", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    if (needInputLine)
        b.addExtern("rt_input_line", Type(Type::Kind::Str), {});
    if (needRtToInt)
        b.addExtern("rt_to_int", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    if (needRtIntToStr)
        b.addExtern("rt_int_to_str", Type(Type::Kind::Str), {Type(Type::Kind::I64)});
    if (needRtF64ToStr)
        b.addExtern("rt_f64_to_str", Type(Type::Kind::Str), {Type(Type::Kind::F64)});
    if (needAlloc)
        b.addExtern("rt_alloc", Type(Type::Kind::Ptr), {Type(Type::Kind::I64)});
    if (needRtLeft)
        b.addExtern(
            "rt_left", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)});
    if (needRtRight)
        b.addExtern(
            "rt_right", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)});
    if (needRtMid2)
        b.addExtern(
            "rt_mid2", Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)});
    if (needRtMid3)
        b.addExtern("rt_mid3",
                    Type(Type::Kind::Str),
                    {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)});
    if (needRtInstr2)
        b.addExtern(
            "rt_instr2", Type(Type::Kind::I64), {Type(Type::Kind::Str), Type(Type::Kind::Str)});
    if (needRtInstr3)
        b.addExtern("rt_instr3",
                    Type(Type::Kind::I64),
                    {Type(Type::Kind::I64), Type(Type::Kind::Str), Type(Type::Kind::Str)});
    if (needRtLtrim)
        b.addExtern("rt_ltrim", Type(Type::Kind::Str), {Type(Type::Kind::Str)});
    if (needRtRtrim)
        b.addExtern("rt_rtrim", Type(Type::Kind::Str), {Type(Type::Kind::Str)});
    if (needRtTrim)
        b.addExtern("rt_trim", Type(Type::Kind::Str), {Type(Type::Kind::Str)});
    if (needRtUcase)
        b.addExtern("rt_ucase", Type(Type::Kind::Str), {Type(Type::Kind::Str)});
    if (needRtLcase)
        b.addExtern("rt_lcase", Type(Type::Kind::Str), {Type(Type::Kind::Str)});
    if (needRtChr)
        b.addExtern("rt_chr", Type(Type::Kind::Str), {Type(Type::Kind::I64)});
    if (needRtAsc)
        b.addExtern("rt_asc", Type(Type::Kind::I64), {Type(Type::Kind::Str)});

    for (RuntimeFn fn : runtimeOrder)
    {
        switch (fn)
        {
            case RuntimeFn::Sqrt:
                b.addExtern("rt_sqrt", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::AbsI64:
                b.addExtern("rt_abs_i64", Type(Type::Kind::I64), {Type(Type::Kind::I64)});
                break;
            case RuntimeFn::AbsF64:
                b.addExtern("rt_abs_f64", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::Floor:
                b.addExtern("rt_floor", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::Ceil:
                b.addExtern("rt_ceil", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::Sin:
                b.addExtern("rt_sin", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::Cos:
                b.addExtern("rt_cos", Type(Type::Kind::F64), {Type(Type::Kind::F64)});
                break;
            case RuntimeFn::Pow:
                b.addExtern("rt_pow",
                            Type(Type::Kind::F64),
                            {Type(Type::Kind::F64), Type(Type::Kind::F64)});
                break;
            case RuntimeFn::RandomizeI64:
                b.addExtern("rt_randomize_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)});
                break;
            case RuntimeFn::Rnd:
                b.addExtern("rt_rnd", Type(Type::Kind::F64), {});
                break;
        }
    }

    if (needRtStrEq)
        b.addExtern(
            "rt_str_eq", Type(Type::Kind::I1), {Type(Type::Kind::Str), Type(Type::Kind::Str)});
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
