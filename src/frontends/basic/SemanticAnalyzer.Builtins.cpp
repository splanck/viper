// File: src/frontends/basic/SemanticAnalyzer.Builtins.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements BASIC builtin function analysis, validating argument
//          counts and types for SemanticAnalyzer.
// Key invariants: Builtin usage diagnostics rely on centralized helper
//                 functions for arity/type checking.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes owned
//                     externally.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <sstream>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

using semantic_analyzer_detail::builtinName;

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBuiltinCall(const BuiltinCallExpr &c)
{
    std::vector<Type> argTys;
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.analyze)
        return (this->*(info.analyze))(c, argTys);
    return Type::Unknown;
}

bool SemanticAnalyzer::checkArgCount(const BuiltinCallExpr &c,
                                     const std::vector<Type> &args,
                                     size_t min,
                                     size_t max)
{
    if (args.size() < min || args.size() > max)
    {
        std::ostringstream oss;
        oss << builtinName(c.builtin) << ": expected ";
        if (min == max)
            oss << min << " arg" << (min == 1 ? "" : "s");
        else
            oss << min << '-' << max << " args";
        oss << " (got " << args.size() << ')';
        de.emit(il::support::Severity::Error, "B2001", c.loc, 1, oss.str());
        return false;
    }
    return true;
}

bool SemanticAnalyzer::checkArgType(const BuiltinCallExpr &c,
                                    size_t idx,
                                    Type argTy,
                                    std::initializer_list<Type> allowed)
{
    if (argTy == Type::Unknown)
        return true;
    for (Type t : allowed)
        if (t == argTy)
            return true;
    il::support::SourceLoc loc = (idx < c.args.size() && c.args[idx]) ? c.args[idx]->loc : c.loc;
    bool wantString = false;
    bool wantNumber = false;
    for (Type t : allowed)
    {
        if (t == Type::String)
            wantString = true;
        if (t == Type::Int || t == Type::Float)
            wantNumber = true;
    }
    const char *need = wantString ? (wantNumber ? "value" : "string") : "number";
    const char *got = "unknown";
    if (argTy == Type::String)
        got = "string";
    else if (argTy == Type::Int || argTy == Type::Float)
        got = "number";
    std::ostringstream oss;
    oss << builtinName(c.builtin) << ": arg " << (idx + 1) << " must be " << need << " (got "
        << got << ')';
    de.emit(il::support::Severity::Error, "B2001", loc, 1, oss.str());
    return false;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRnd(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    checkArgCount(c, args, 0, 0);
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLen(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeMid(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 3))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
        if (args.size() == 3)
            checkArgType(c, 2, args[2], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLeft(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRight(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::String});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeStr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeVal(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeInt(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Float});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeInstr(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 3))
    {
        size_t idx = 0;
        if (args.size() == 3)
        {
            checkArgType(c, idx, args[idx], {Type::Int, Type::Float});
            idx++;
        }
        checkArgType(c, idx, args[idx], {Type::String});
        idx++;
        checkArgType(c, idx, args[idx], {Type::String});
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeRtrim(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeTrim(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLcase(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeChr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::String;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeAsc(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::String});
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeSqr(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeAbs(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1) && !args.empty())
    {
        if (args[0] == Type::Float)
            return Type::Float;
        if (args[0] == Type::Int || args[0] == Type::Unknown)
            return Type::Int;
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeFloor(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCeil(const BuiltinCallExpr &c,
                                                     const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeSin(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeCos(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 1, 1))
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
    return Type::Float;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzePow(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args)
{
    if (checkArgCount(c, args, 2, 2))
    {
        checkArgType(c, 0, args[0], {Type::Int, Type::Float});
        checkArgType(c, 1, args[1], {Type::Int, Type::Float});
    }
    return Type::Float;
}

} // namespace il::frontends::basic
