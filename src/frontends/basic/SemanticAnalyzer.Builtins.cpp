// File: src/frontends/basic/SemanticAnalyzer.Builtins.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements BASIC builtin function analysis, validating argument
//          counts and types for SemanticAnalyzer.
// Key invariants: Builtin usage diagnostics rely on centralized helper
//                 functions for arity/type checking.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes owned
//                     externally.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <sstream>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

using semantic_analyzer_detail::builtinName;

namespace
{

BuiltinTypeSet maskForType(SemanticAnalyzer::Type type)
{
    using Type = SemanticAnalyzer::Type;
    switch (type)
    {
        case Type::Int:
            return BuiltinTypeSet::Int;
        case Type::Float:
            return BuiltinTypeSet::Float;
        case Type::String:
            return BuiltinTypeSet::String;
        case Type::Bool:
            return BuiltinTypeSet::Bool;
        case Type::Unknown:
            return BuiltinTypeSet::None;
    }
    return BuiltinTypeSet::None;
}

const char *describeAllowed(BuiltinTypeSet allowed)
{
    const bool allowString = contains(allowed, BuiltinTypeSet::String);
    const bool allowNumber = contains(allowed, BuiltinTypeSet::Int) ||
                             contains(allowed, BuiltinTypeSet::Float);
    const bool allowBool = contains(allowed, BuiltinTypeSet::Bool);
    if (allowString && allowNumber)
        return "value";
    if (allowString && !allowNumber && !allowBool)
        return "string";
    if (allowBool && !allowNumber && !allowString)
        return "boolean";
    if (allowNumber && !allowString && !allowBool)
        return "number";
    return "value";
}

const BuiltinSemantics::Signature *findSignature(const BuiltinSemantics &semantics,
                                                 std::size_t argCount)
{
    for (const auto &sig : semantics.signatures)
        if (sig.args.size() == argCount)
            return &sig;
    return nullptr;
}

SemanticAnalyzer::Type kindToSemanticType(BuiltinValueKind kind)
{
    using Type = SemanticAnalyzer::Type;
    switch (kind)
    {
        case BuiltinValueKind::Int:
            return Type::Int;
        case BuiltinValueKind::Float:
            return Type::Float;
        case BuiltinValueKind::String:
            return Type::String;
        case BuiltinValueKind::Bool:
            return Type::Bool;
    }
    return Type::Unknown;
}

} // namespace

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBuiltinCall(const BuiltinCallExpr &c)
{
    std::vector<Type> argTys;
    argTys.reserve(c.args.size());
    for (auto &a : c.args)
        argTys.push_back(a ? visitExpr(*a) : Type::Unknown);

    const auto &info = getBuiltinInfo(c.builtin);
    if (!info.semantics)
        return Type::Unknown;

    const BuiltinSemantics &semantics = *info.semantics;
    const bool countOk = checkArgCount(c, argTys, semantics);

    if (const auto *sig = findSignature(semantics, argTys.size()); countOk && sig)
    {
        for (std::size_t i = 0; i < sig->args.size(); ++i)
            checkArgType(c, i, argTys[i], sig->args[i]);
    }

    return computeBuiltinResult(semantics, argTys);
}

bool SemanticAnalyzer::checkArgCount(const BuiltinCallExpr &c,
                                     const std::vector<Type> &args,
                                     const BuiltinSemantics &semantics)
{
    if (args.size() >= semantics.minArgs && args.size() <= semantics.maxArgs)
        return true;

    std::ostringstream oss;
    oss << builtinName(c.builtin) << ": expected ";
    if (semantics.minArgs == semantics.maxArgs)
        oss << semantics.minArgs << " arg" << (semantics.minArgs == 1 ? "" : "s");
    else
        oss << semantics.minArgs << '-' << semantics.maxArgs << " args";
    oss << " (got " << args.size() << ')';
    de.emit(il::support::Severity::Error, "B2001", c.loc, 1, oss.str());
    return false;
}

bool SemanticAnalyzer::checkArgType(const BuiltinCallExpr &c,
                                    size_t idx,
                                    Type argTy,
                                    BuiltinTypeSet allowed)
{
    if (argTy == Type::Unknown)
        return true;

    BuiltinTypeSet actual = maskForType(argTy);
    if (actual == BuiltinTypeSet::None || contains(allowed, actual))
        return true;

    il::support::SourceLoc loc = (idx < c.args.size() && c.args[idx]) ? c.args[idx]->loc : c.loc;
    const char *need = describeAllowed(allowed);
    const char *got = "unknown";
    switch (argTy)
    {
        case Type::String:
            got = "string";
            break;
        case Type::Int:
        case Type::Float:
            got = "number";
            break;
        case Type::Bool:
            got = "boolean";
            break;
        case Type::Unknown:
            got = "unknown";
            break;
    }
    std::ostringstream oss;
    oss << builtinName(c.builtin) << ": arg " << (idx + 1) << " must be " << need << " (got "
        << got << ')';
    de.emit(il::support::Severity::Error, "B2001", loc, 1, oss.str());
    return false;
}

SemanticAnalyzer::Type SemanticAnalyzer::computeBuiltinResult(const BuiltinSemantics &semantics,
                                                              const std::vector<Type> &args)
{
    const auto &spec = semantics.result;
    switch (spec.kind)
    {
        case BuiltinSemantics::ResultSpec::Kind::Fixed:
            return kindToSemanticType(spec.type);
        case BuiltinSemantics::ResultSpec::Kind::FromArg:
        {
            if (spec.argIndex < args.size())
            {
                Type argTy = args[spec.argIndex];
                if (argTy != Type::Unknown)
                    return argTy;
            }
            return kindToSemanticType(spec.type);
        }
    }
    return Type::Unknown;
}

} // namespace il::frontends::basic
