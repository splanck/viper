//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/SemanticAnalyzer.Builtins.cpp
// Purpose: Implement BASIC builtin function analysis, validating argument
//          counts and types for the semantic analyser.
// Key invariants: Builtin usage diagnostics rely on centralised helpers for
//                 arity/type checking so call sites share consistent rules.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes owned
//                     externally.
// Links: docs/codemap.md, docs/basic-language.md#builtins
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <array>
#include <span>
#include <sstream>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

using semantic_analyzer_detail::builtinName;

/// @brief Analyse a builtin call expression and return its resulting type.
///
/// @details Gathers argument types, looks up the builtin signature, and
///          dispatches to any specialised analyser registered in the builtin
///          table.  When no override exists the generic signature-based analysis
///          path is used, ensuring every builtin honours the declarative
///          metadata.
///
/// @param c Builtin call AST node to analyse.
/// @return Resulting semantic type inferred for the call.
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

/// @brief Validate the argument count for a builtin invocation.
///
/// @details Ensures the number of provided arguments falls within the inclusive
///          range [@p min, @p max].  On failure a diagnostic describing the
///          expected range is emitted.
///
/// @param c Builtin call under inspection.
/// @param args Types observed for each argument expression.
/// @param min Minimum accepted argument count.
/// @param max Maximum accepted argument count.
/// @return @c true when the argument count is valid; otherwise @c false.
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

/// @brief Ensure an argument's type matches one of the permitted categories.
///
/// @details Accepts the computed argument type and compares it against the
///          allowed set supplied by the builtin signature.  When the type is not
///          permitted, a diagnostic explains the mismatch and the function
///          returns @c false.
///
/// @param c Builtin call being analysed.
/// @param idx Zero-based argument index.
/// @param argTy Observed type of the argument.
/// @param allowed Span describing the acceptable types.
/// @return @c true when the argument type is acceptable; otherwise @c false.
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

/// @brief Retrieve the declarative signature for a builtin enumerator.
///
/// @param builtin Builtin enumerator to inspect.
/// @return Reference to the static signature describing arity and result type.
const SemanticAnalyzer::BuiltinSignature &
SemanticAnalyzer::builtinSignature(BuiltinCallExpr::Builtin builtin)
{
    return kBuiltinSignatures[static_cast<size_t>(builtin)];
}

/// @brief Validate builtin arguments against a signature definition.
///
/// @details Verifies argument counts, honours optional parameters, and enforces
///          type rules using @ref checkArgType.  Missing optional arguments are
///          handled gracefully so callers can omit trailing parameters.
///
/// @param c Builtin call under validation.
/// @param args Observed argument types.
/// @param signature Declarative specification defining expectations.
/// @return @c true when the invocation satisfies the signature.
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

/// @brief Analyse a builtin using only its declarative signature.
///
/// @details Invokes @ref validateBuiltinArgs to emit diagnostics and returns
///          the signature's result type regardless of validation outcome so
///          downstream analysis can continue.
///
/// @param c Builtin call being analysed.
/// @param args Observed argument types.
/// @param signature Signature describing arity and return type.
/// @return Semantic type produced by the builtin.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeBuiltinWithSignature(
    const BuiltinCallExpr &c,
    const std::vector<Type> &args,
    const BuiltinSignature &signature)
{
    validateBuiltinArgs(c, args, signature);
    return signature.result;
}

/// @brief Special-case analysis for the ABS builtin.
///
/// @details Validates arguments using the generic path and then selects the
///          return type based on the argument: floating-point inputs return
///          floats while integers (or unknown types) yield integers.  This
///          mirrors runtime behaviour and prevents unnecessary coercions.
///
/// @param c Builtin call node.
/// @param args Observed argument types.
/// @param signature Declarative signature for ABS.
/// @return Resulting semantic type for the call.
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

/// @brief Analyse the INSTR builtin.
///
/// @details Currently defers entirely to the signature validation so the result
///          type always matches the declarative metadata.  Hooked separately so
///          more sophisticated diagnostics can be added without altering the
///          registry format.
///
/// @param c Builtin call node.
/// @param args Observed argument types.
/// @param signature Declarative signature for INSTR.
/// @return Resulting semantic type for the call.
SemanticAnalyzer::Type SemanticAnalyzer::analyzeInstr(const BuiltinCallExpr &c,
                                                      const std::vector<Type> &args,
                                                      const BuiltinSignature &signature)
{
    validateBuiltinArgs(c, args, signature);
    return signature.result;
}

} // namespace il::frontends::basic
