//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Expr_Binary.cpp
// Purpose: Centralise BASIC binary expression validation rules, covering
//          operand checks, implicit conversions, and result type inference.
// Key invariants:
//   * Every binary operator is described by a validation/result rule that stays
//     in sync with the language specification.
//   * Diagnostics follow a consistent numbering scheme (B2001/B1011/etc.) so
//     users receive actionable feedback across different operator families.
//   * Rule tables are indexed by the @ref BinaryExpr::Op enumeration; the table
//     size is locked to the enum to prevent divergence when new operators land.
// References: docs/basic-language.md#expressions,
//             docs/codemap/basic.md#semantic-analyzer
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helpers for binary expressions.
/// @details Provides operand validation, result inference, and implicit
///          conversion tracking for BASIC binary operators while unifying
///          diagnostic emission through Check_Common utilities.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/sem/Check_Common.hpp"

#include <array>
#include <sstream>

namespace il::frontends::basic::semantic_analyzer_detail
{
using Type = SemanticAnalyzer::Type;

/// @brief Count the number of binary operator rules described in the table.
///
/// The helper converts the terminal @ref BinaryExpr::Op enumerator into a
/// 0-based index and adds one so the rule array can be statically sized.  Keeping
/// the logic in a single function avoids duplicated arithmetic when building the
/// rule table and makes it trivial to audit whenever new operators are added.
///
/// @return Number of entries required to cover every binary operator.
constexpr std::size_t exprRuleCount() noexcept
{
    return static_cast<std::size_t>(BinaryExpr::Op::LogicalOr) + 1;
}

/// @brief Determine whether an operand type is treated as numeric for checks.
///
/// Numeric operands include integers, floats, and "unknown" placeholders.  The
/// unknown category preserves diagnostics emitted earlier while still allowing
/// later validation passes to proceed without cascading errors.
///
/// @param type Semantic type of the operand expression.
/// @return True when the operand is considered numeric.
constexpr bool isNumericType(Type type) noexcept
{
    return type == Type::Int || type == Type::Float || type == Type::Unknown;
}

/// @brief Determine whether an operand participates in integer-only operations.
///
/// The helper recognises integer and unknown operands, aligning with language
/// rules for operators like MOD and IDIV.  Unknown values avoid duplicate
/// diagnostics when prior analysis already reported an error.
///
/// @param type Semantic type of the operand expression.
/// @return True when the operand is allowed in integer-only contexts.
constexpr bool isIntegerType(Type type) noexcept
{
    return type == Type::Int || type == Type::Unknown;
}

/// @brief Determine whether an operand is acceptable for boolean-only rules.
///
/// Logical operators require strict Boolean types. Unknown placeholders are
/// accepted to allow continued validation after earlier errors. The helper is
/// used by logical operator validators and diagnostic messaging.
///
/// @param type Semantic type of the operand expression.
/// @return True for boolean or unknown operands.
constexpr bool isBooleanType(Type type) noexcept
{
    return type == Type::Bool || type == Type::Unknown;
}

/// @brief Check whether the semantic type maps to a BASIC string value.
///
/// String-only operators (e.g., concatenation) rely on this to ensure both
/// operands participate in text operations.  Unknown values are excluded so the
/// validator can emit diagnostics when operands are missing or invalid.
///
/// @param type Semantic type of the operand expression.
/// @return True when the operand type is string.
constexpr bool isStringType(Type type) noexcept
{
    return type == Type::String;
}

/// @brief Infer the result type for arithmetic operations that accept numerics.
///
/// By delegating to @ref commonNumericType the helper enforces consistent
/// promotion rules (int vs float).  Keeping the wrapper clarifies the intent
/// when wiring the rule table.
///
/// @param lhs Type of the left-hand operand.
/// @param rhs Type of the right-hand operand.
/// @return Resulting numeric type or Unknown if promotion fails.
Type numericResult(Type lhs, Type rhs) noexcept
{
    return commonNumericType(lhs, rhs);
}

/// @brief Determine the result type for division operations.
///
/// Division always returns a float when both operands are numeric.  Non-numeric
/// operands propagate an unknown result so callers can suppress redundant
/// diagnostics after the validator runs.
///
/// @param lhs Type of the dividend operand.
/// @param rhs Type of the divisor operand.
/// @return Float for valid numeric operands; Unknown otherwise.
Type divisionResult(Type lhs, Type rhs) noexcept
{
    if (!isNumericType(lhs) || !isNumericType(rhs))
        return Type::Unknown;
    return Type::Float;
}

/// @brief Determine the result type for addition, including string concatenation.
///
/// BASIC allows `+` to concatenate strings.  When both operands are string the
/// helper returns string; otherwise it follows numeric promotion semantics so the
/// rule behaves consistently with subtraction/multiplication.
///
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @return String when both operands are string; otherwise numeric promotion.
Type addResult(Type lhs, Type rhs) noexcept
{
    // If either operand is a string, BASIC treats '+' as concatenation
    // and the result is a string.
    if (lhs == Type::String || rhs == Type::String)
        return Type::String;
    return commonNumericType(lhs, rhs);
}

/// @brief Compute the result type for exponentiation.
///
/// BASIC exponentiation always yields a floating-point result regardless of
/// operand types.  Diagnostics ensure invalid operands are reported before this
/// function is consulted.
///
/// @return Always @c Type::Float.
Type powResult(Type, Type) noexcept
{
    return Type::Float;
}

/// @brief Compute the result type for integer-only arithmetic.
///
/// Operators such as MOD and IDIV produce integer results when operands are
/// valid.  The validator enforces integer operands prior to calling this helper.
///
/// @return Always @c Type::Int.
Type integerResult(Type, Type) noexcept
{
    return Type::Int;
}

/// @brief Compute the result type for boolean-producing operations.
///
/// Comparisons and logical operations evaluate to BOOLEAN results.
/// The validator component ensures operands meet the requirements.
///
/// @return Always @c Type::Bool.
Type booleanResult(Type, Type) noexcept
{
    return Type::Bool;
}

/// @brief Check whether the RHS of a binary expression is a literal zero value.
///
/// The helper inspects integer and floating-point literal nodes so division and
/// modulus validators can emit divide-by-zero diagnostics early without forcing
/// a full constant-folding pass.  Expressions that are not simple literals return
/// false, leaving runtime checks to later phases.
///
/// @param expr Binary expression under inspection.
/// @return True when the RHS is an integer or float literal equal to zero.
bool rhsIsLiteralZero(const BinaryExpr &expr)
{
    if (const auto *ri = as<const IntExpr>(*expr.rhs); ri != nullptr)
        return ri->value == 0;
    if (const auto *rf = as<const FloatExpr>(*expr.rhs); rf != nullptr)
        return rf->value == 0.0;
    return false;
}

/// @brief Validate that both operands satisfy a numeric constraint.
///
/// The validator is shared by arithmetic operations that expect numeric
/// operands.  When either operand fails the check it emits the provided
/// diagnostic via the analyzer's diagnostic sink.
///
/// @param context Expression checking context providing diagnostics access.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateNumericOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (!isNumericType(lhs) || !isNumericType(rhs))
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

/// @brief Validate operands for addition (numeric or string concatenation).
///
/// Addition accepts either numeric or string pairs.  The validator recognises
/// both cases and emits a mismatch diagnostic when neither set of constraints is
/// satisfied.
///
/// @param context Expression checking context providing diagnostics.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateAddOperands(sem::ExprCheckContext &context,
                         const BinaryExpr &expr,
                         Type lhs,
                         Type rhs,
                         std::string_view diagId)
{
    // Accept numeric pairs or when either operand is string (concatenation)
    const bool numericOk = isNumericType(lhs) && isNumericType(rhs);
    const bool stringOk = isStringType(lhs) || isStringType(rhs);
    if (!numericOk && !stringOk)
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

/// @brief Validate operands for division operations.
///
/// Ensures both operands are numeric and warns when the RHS is a literal zero.
/// The validator emits divide-by-zero diagnostics immediately to mirror the
/// language's runtime trap behaviour.
///
/// @param context Expression checking context providing diagnostics.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateDivisionOperands(sem::ExprCheckContext &context,
                              const BinaryExpr &expr,
                              Type lhs,
                              Type rhs,
                              std::string_view diagId)
{
    validateNumericOperands(context, expr, lhs, rhs, diagId);
    if (rhsIsLiteralZero(expr))
        sem::emitDivideByZero(context.diagnostics(), expr);
}

/// @brief Validate operands for integer-only arithmetic (IDIV/MOD).
///
/// Confirms both operands are integers and reports divide-by-zero when the RHS
/// literal is zero.  Unknown operands trigger diagnostics via the helper so the
/// analyzer records precise error locations.
///
/// @param context Expression checking context providing diagnostics.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateIntegerOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (!isIntegerType(lhs) || !isIntegerType(rhs))
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
    if (rhsIsLiteralZero(expr))
        sem::emitDivideByZero(context.diagnostics(), expr);
}

/// @brief Validate operands for comparison operators.
///
/// Numeric comparisons accept numeric operands; equality/inequality also allow
/// string comparisons.  The validator enforces these rules and reports the
/// appropriate mismatch diagnostic through the shared sink.
///
/// @param context Expression checking context providing diagnostics.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateComparisonOperands(sem::ExprCheckContext &context,
                                const BinaryExpr &expr,
                                Type lhs,
                                Type rhs,
                                std::string_view diagId)
{
    // All comparison operators support strings for lexicographic comparison
    const bool numericOk = isNumericType(lhs) && isNumericType(rhs);
    const bool stringOk = isStringType(lhs) && isStringType(rhs);
    if (!numericOk && !stringOk)
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

/// @brief Validate operands for logical (boolean) operators.
///
/// Logical operators require boolean operands.  When the operands are invalid
/// the helper formats a descriptive message and emits diagnostic
/// `DiagNonBooleanLogicalOperand`.
///
/// @param context Expression checking context providing diagnostics.
/// @param expr Binary expression being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @param diagId Diagnostic identifier to emit on mismatch.
void validateLogicalOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (isBooleanType(lhs) && isBooleanType(rhs))
        return;

    sem::emitTypeMismatch(context.diagnostics(),
                          std::string(diagId),
                          expr.loc,
                          1,
                          formatLogicalOperandMessage(expr.op, lhs, rhs));
}

/// @brief Calculate the common numeric type used for implicit promotions.
///
/// When either operand is floating-point the result promotes to float; otherwise
/// it stays integer.  Unknown operands bypass promotion so subsequent rules can
/// continue emitting diagnostics without guessing.
///
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @return Resulting numeric type after promotion.
SemanticAnalyzer::Type commonNumericType(Type lhs, Type rhs) noexcept
{
    return (lhs == Type::Float || rhs == Type::Float) ? Type::Float : Type::Int;
}

/// @brief Format a diagnostic message for logical operand mismatches.
///
/// The helper builds a human-readable string describing the invalid operand
/// types, making diagnostics actionable while keeping message formatting out of
/// the validation logic.
///
/// @param op Logical operator being validated.
/// @param lhs Type of the left operand.
/// @param rhs Type of the right operand.
/// @return Fully formatted diagnostic message string.
std::string formatLogicalOperandMessage(BinaryExpr::Op op, Type lhs, Type rhs)
{
    std::ostringstream oss;
    oss << "Logical operator " << logicalOpName(op) << " requires BOOLEAN operands, got "
        << semanticTypeName(lhs) << " and " << semanticTypeName(rhs) << '.';
    return oss.str();
}

/// @brief Look up the validation rule for a specific binary operator.
///
/// The function materialises a static array of @ref ExprRule entries sized using
/// @ref exprRuleCount.  Each rule stores the validator function, result type
/// resolver, and diagnostic identifier.  Access is performed through
/// `std::array::at` so out-of-range enumerators trigger a debug-time failure.
///
/// @param op Binary operator whose rule is requested.
/// @return Reference to the rule describing validation and result semantics.
const ExprRule &exprRule(BinaryExpr::Op op)
{
    static constexpr std::array<ExprRule, exprRuleCount()> rules = {{
        {BinaryExpr::Op::Add, &validateAddOperands, &addResult, "B2001"},
        {BinaryExpr::Op::Sub, &validateNumericOperands, &numericResult, "B2001"},
        {BinaryExpr::Op::Mul, &validateNumericOperands, &numericResult, "B2001"},
        {BinaryExpr::Op::Div, &validateDivisionOperands, &divisionResult, "B2001"},
        {BinaryExpr::Op::Pow, &validateNumericOperands, &powResult, "B2001"},
        {BinaryExpr::Op::IDiv, &validateIntegerOperands, &integerResult, "B2001"},
        {BinaryExpr::Op::Mod, &validateIntegerOperands, &integerResult, "B2001"},
        {BinaryExpr::Op::Eq, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Ne, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Lt, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Le, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Gt, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Ge, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::LogicalAndShort,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalOrShort,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalAnd,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalOr,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
    }};

    // Belt-and-suspenders: enforce that comparison operators always resolve to
    // boolean semantics. The validator already ensures operand correctness, so
    // this guards the rule table from accidental drift.
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Eq)].result == &booleanResult,
                  "Equality comparison must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Ne)].result == &booleanResult,
                  "Inequality comparison must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Lt)].result == &booleanResult,
                  "Less-than comparison must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Le)].result == &booleanResult,
                  "Less-equal comparison must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Gt)].result == &booleanResult,
                  "Greater-than comparison must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::Ge)].result == &booleanResult,
                  "Greater-equal comparison must return BOOL");

    // Logical operators demand boolean inputs/outputs; ensure the result stays
    // pinned to the boolean helper to match operand validation rules.
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::LogicalAndShort)].result ==
                      &booleanResult,
                  "Short-circuit AND must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::LogicalOrShort)].result ==
                      &booleanResult,
                  "Short-circuit OR must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::LogicalAnd)].result ==
                      &booleanResult,
                  "Logical AND must return BOOL");
    static_assert(rules[static_cast<std::size_t>(BinaryExpr::Op::LogicalOr)].result ==
                      &booleanResult,
                  "Logical OR must return BOOL");

    return rules.at(static_cast<std::size_t>(op));
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic::sem
{

/// @brief Analyse a BASIC binary expression, validating operands and inferring the result.
///
/// The function coordinates evaluation of both operands via
/// @ref ExprCheckContext so implicit conversions and diagnostic state remain in
/// sync with the rest of semantic analysis.  After collecting operand types it
/// performs two duties:
///
/// 1. For arithmetic operations, request implicit numeric promotions when the
///    operands differ (e.g., INT + FLOAT) so later lowering can inject casts.
/// 2. Dispatch to the rule table provided by
///    @ref semantic_analyzer_detail::exprRule to validate operands and produce
///    the resulting semantic type.  Validators emit diagnostics as needed, while
///    the rule's result callback determines the expression's final type.
///
/// If no rule is available or validation fails the function returns
/// `Type::Unknown`, signalling that diagnostics were issued elsewhere.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param expr Binary expression AST node being analysed.
/// @return Resulting semantic type classification or Unknown when validation
///         fails.
SemanticAnalyzer::Type analyzeBinaryExpr(SemanticAnalyzer &analyzer, const BinaryExpr &expr)
{
    ExprCheckContext context(analyzer);
    using Type = SemanticAnalyzer::Type;

    Type lhs = Type::Unknown;
    Type rhs = Type::Unknown;
    if (expr.lhs)
        lhs = context.evaluate(*expr.lhs);
    if (expr.rhs)
        rhs = context.evaluate(*expr.rhs);

    if (expr.op == BinaryExpr::Op::Add || expr.op == BinaryExpr::Op::Sub ||
        expr.op == BinaryExpr::Op::Mul)
    {
        if (semantic_analyzer_detail::isNumericType(lhs) &&
            semantic_analyzer_detail::isNumericType(rhs))
        {
            const Type target = semantic_analyzer_detail::commonNumericType(lhs, rhs);
            if (target == Type::Float)
            {
                // Promote INT operands to FLOAT when mixing numeric types and
                // record the implicit conversion so later passes can insert the
                // cast explicitly during lowering/codegen.
                if (expr.lhs && lhs != Type::Float && lhs != Type::Unknown)
                    context.markImplicitConversion(*expr.lhs, target);
                if (expr.rhs && rhs != Type::Float && rhs != Type::Unknown)
                    context.markImplicitConversion(*expr.rhs, target);
            }
        }
    }

    // Implicit STR$ coercion for '+' when one operand is string.
    if (expr.op == BinaryExpr::Op::Add)
    {
        if (lhs == Type::String && rhs != Type::String && rhs != Type::Unknown && expr.rhs)
            context.markImplicitConversion(*expr.rhs, Type::String);
        else if (rhs == Type::String && lhs != Type::String && lhs != Type::Unknown && expr.lhs)
            context.markImplicitConversion(*expr.lhs, Type::String);
    }

    const auto &rule = semantic_analyzer_detail::exprRule(expr.op);
    if (rule.validator)
        rule.validator(context, expr, lhs, rhs, rule.mismatchDiag);
    if (rule.result)
        return rule.result(lhs, rhs);
    return Type::Unknown;
}

} // namespace il::frontends::basic::sem
