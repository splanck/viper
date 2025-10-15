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

#include <array>
#include <span>
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

    const auto &signature = builtinSignature(c.builtin);
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.analyze)
        return (this->*(info.analyze))(c, argTys, signature);
    return analyzeBuiltinWithSignature(c, argTys, signature);
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
                                    std::span<const Type> allowed)
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
namespace
{
using SemanticType = SemanticAnalyzer::Type;
using BuiltinArgSpec = SemanticAnalyzer::BuiltinArgSpec;
using BuiltinSignature = SemanticAnalyzer::BuiltinSignature;

static constexpr std::array<SemanticType, 1> kStringType{{SemanticType::String}};
static constexpr std::array<SemanticType, 2> kNumericTypes{{SemanticType::Int,
                                                            SemanticType::Float}};

static constexpr std::array<SemanticType, 1> kIntType{{SemanticType::Int}};

static constexpr std::array<BuiltinArgSpec, 1> kSingleStringArg{{
    BuiltinArgSpec{false, kStringType.data(), kStringType.size()},
}};

static constexpr std::array<BuiltinArgSpec, 1> kSingleNumericArg{{
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
}};

static constexpr std::array<BuiltinArgSpec, 1> kSingleIntArg{{
    BuiltinArgSpec{false, kIntType.data(), kIntType.size()},
}};

static constexpr std::array<BuiltinArgSpec, 2> kStringNumericArgs{{
    BuiltinArgSpec{false, kStringType.data(), kStringType.size()},
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
}};

static constexpr std::array<BuiltinArgSpec, 2> kNumericNumericArgs{{
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
}};

static constexpr std::array<BuiltinArgSpec, 3> kMidArgs{{
    BuiltinArgSpec{false, kStringType.data(), kStringType.size()},
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
    BuiltinArgSpec{true, kNumericTypes.data(), kNumericTypes.size()},
}};

static constexpr std::array<BuiltinArgSpec, 2> kRoundArgs{{
    BuiltinArgSpec{false, kNumericTypes.data(), kNumericTypes.size()},
    BuiltinArgSpec{true, kNumericTypes.data(), kNumericTypes.size()},
}};

static constexpr std::array<BuiltinArgSpec, 3> kInstrArgs{{
    BuiltinArgSpec{true, kNumericTypes.data(), kNumericTypes.size()},
    BuiltinArgSpec{false, kStringType.data(), kStringType.size()},
    BuiltinArgSpec{false, kStringType.data(), kStringType.size()},
}};

static constexpr std::array<BuiltinSignature, 34> kBuiltinSignatures{{
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::Int},
    BuiltinSignature{2, 1, kMidArgs.data(), kMidArgs.size(), SemanticType::String},
    BuiltinSignature{2, 0, kStringNumericArgs.data(), kStringNumericArgs.size(),
                     SemanticType::String},
    BuiltinSignature{2, 0, kStringNumericArgs.data(), kStringNumericArgs.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Int},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Int},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 1, kRoundArgs.data(), kRoundArgs.size(), SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Int},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::Float},
    BuiltinSignature{2, 0, kNumericNumericArgs.data(), kNumericNumericArgs.size(),
                     SemanticType::Float},
    BuiltinSignature{0, 0, nullptr, 0, SemanticType::Float},
    BuiltinSignature{2, 1, kInstrArgs.data(), kInstrArgs.size(), SemanticType::Int},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleNumericArg.data(), kSingleNumericArg.size(),
                     SemanticType::String},
    BuiltinSignature{1, 0, kSingleStringArg.data(), kSingleStringArg.size(),
                     SemanticType::Int},
    BuiltinSignature{0, 0, nullptr, 0, SemanticType::String},
    BuiltinSignature{0, 0, nullptr, 0, SemanticType::String},
    BuiltinSignature{1, 0, kSingleIntArg.data(), kSingleIntArg.size(),
                     SemanticType::Int},
    BuiltinSignature{1, 0, kSingleIntArg.data(), kSingleIntArg.size(),
                     SemanticType::Int},
    BuiltinSignature{1, 0, kSingleIntArg.data(), kSingleIntArg.size(),
                     SemanticType::Int},
}};

} // namespace

const SemanticAnalyzer::BuiltinSignature &
SemanticAnalyzer::builtinSignature(BuiltinCallExpr::Builtin builtin)
{
    return kBuiltinSignatures[static_cast<size_t>(builtin)];
}

bool SemanticAnalyzer::validateBuiltinArgs(const BuiltinCallExpr &c,
                                           const std::vector<Type> &args,
                                           const BuiltinSignature &signature)
{
    const std::size_t minArgs = signature.requiredArgs;
    const std::size_t maxArgs = signature.requiredArgs + signature.optionalArgs;
    if (!checkArgCount(c, args, minArgs, maxArgs))
        return false;

    if (signature.argumentCount == 0 || signature.arguments == nullptr)
        return true;

    std::size_t missing =
        signature.argumentCount > args.size() ? signature.argumentCount - args.size() : 0;
    std::size_t argIndex = 0;
    for (std::size_t specIndex = 0; specIndex < signature.argumentCount &&
                                    argIndex < args.size(); ++specIndex)
    {
        const BuiltinArgSpec &spec = signature.arguments[specIndex];
        if (spec.optional && missing > 0)
        {
            const std::size_t remainingSpecs = signature.argumentCount - specIndex - 1;
            const std::size_t remainingArgs = args.size() - argIndex;
            if (remainingSpecs >= remainingArgs)
            {
                --missing;
                continue;
            }
        }

        if (spec.allowed != nullptr && spec.allowedCount > 0)
        {
            std::span<const Type> allowed(spec.allowed, spec.allowedCount);
            checkArgType(c, argIndex, args[argIndex], allowed);
        }
        ++argIndex;
    }

    return true;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBuiltinWithSignature(
    const BuiltinCallExpr &c,
    const std::vector<Type> &args,
    const BuiltinSignature &signature)
{
    validateBuiltinArgs(c, args, signature);
    return signature.result;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeAbs(const BuiltinCallExpr &c,
                                                    const std::vector<Type> &args,
                                                    const BuiltinSignature &signature)
{
    const bool countOk = validateBuiltinArgs(c, args, signature);
    if (!countOk || args.empty())
        return Type::Int;

    if (args[0] == Type::Float)
        return Type::Float;
    if (args[0] == Type::Int || args[0] == Type::Unknown)
        return Type::Int;

    // Type mismatch already diagnosed; fall back to integer type to match legacy behaviour.
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeInstr(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args,
                                                      const BuiltinSignature &signature)
{
    validateBuiltinArgs(c, args, signature);
    return signature.result;
}

} // namespace il::frontends::basic
