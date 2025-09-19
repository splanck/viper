// File: src/frontends/basic/LowerRuntime.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
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
/// @brief Declare a runtime extern using the canonical signature database.
/// @details Consults il::runtime::RuntimeSignatures via findRuntimeSignature to
///          obtain the return and parameter types associated with @p name, then
///          records the extern on the provided IR builder.
/// @param b IR builder that will receive the extern declaration.
/// @param name Runtime registry key that identifies the helper to declare.
/// @pre The runtime signature registry contains an entry for @p name; missing
///      signatures trigger the assertion guarding the lookup.
/// @note This helper mutates the builder but does not touch runtimeOrder or
///       runtimeSet tracking.
void declareRuntimeExtern(build::IRBuilder &b, std::string_view name)
{
    const auto *sig = il::runtime::findRuntimeSignature(name);
    assert(sig && "runtime signature missing from registry");
    b.addExtern(std::string(name), sig->retType, sig->paramTypes);
}
} // namespace

/// @brief Declare every runtime helper required by the current lowering run.
/// @details Emits baseline helpers unconditionally and consults lowering
///          toggles such as needRtConcat, boundsChecks, and similar feature
///          flags to decide which additional helpers are necessary. Runtime
///          math helpers requested dynamically are iterated in the order
///          recorded by trackRuntime so declaration emission remains
///          deterministic. Each declaration delegates to declareRuntimeExtern,
///          which looks up the canonical signature via RuntimeSignatures.
/// @param b IR builder used to register extern declarations.
/// @note Assumes runtimeOrder has been populated through trackRuntime calls;
///       missing signatures are enforced by the assertion in
///       declareRuntimeExtern.
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

/// @brief Record that a runtime helper is needed for the current program.
/// @details Inserts @p fn into runtimeSet, using it as a visited filter, and
///          appends @p fn to runtimeOrder the first time it appears. This
///          preserves deterministic declaration order when
///          declareRequiredRuntime later walks runtimeOrder.
/// @param fn Identifier for the runtime helper that lowering just requested.
/// @post runtimeSet always contains @p fn; runtimeOrder grows only when @p fn
///       was not previously tracked.
void Lowerer::trackRuntime(RuntimeFn fn)
{
    if (runtimeSet.insert(fn).second)
        runtimeOrder.push_back(fn);
}

} // namespace il::frontends::basic
