// File: src/frontends/basic/LowerRuntime.hpp
// Purpose: Declares runtime tracking and declaration helpers for BASIC lowering.
// Key invariants: Runtime declarations are emitted exactly once per function.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
#pragma once

enum class RuntimeFn
{
    Sqrt,
    AbsI64,
    AbsF64,
    Floor,
    Ceil,
    Sin,
    Cos,
    Pow,
    RandomizeI64,
    Rnd,
};

struct RuntimeFnHash
{
    size_t operator()(RuntimeFn f) const
    {
        return static_cast<size_t>(f);
    }
};

std::vector<RuntimeFn> runtimeOrder;
std::unordered_set<RuntimeFn, RuntimeFnHash> runtimeSet;

void trackRuntime(RuntimeFn fn);
void declareRequiredRuntime(build::IRBuilder &b);
