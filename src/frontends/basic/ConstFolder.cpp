//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the table-driven constant folder used by the BASIC frontend.  The
// folder interprets literal expressions, applies numeric promotion rules, and
// materialises folded AST nodes while preserving the language's 64-bit wrapping
// semantics and string concatenation behaviour.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Provides constant folding utilities for BASIC AST nodes.
/// @details The helpers in this translation unit evaluate expression trees built
///          from literal operands.  They promote operands according to BASIC's
///          suffix rules, consult rule tables for arithmetic and comparison
///          folding, and replace AST nodes with canonical literals when
///          evaluation succeeds.  Out-of-line definitions keep the header light
///          while exposing rich documentation for each folding primitive.

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/ConstFoldHelpers.hpp"
#include "frontends/basic/ConstFold_Arith.hpp"
#include "frontends/basic/ConstFold_Logic.hpp"
#include "frontends/basic/ConstFold_String.hpp"

extern "C"
{
#include "runtime/rt_format.h"
}
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace il::frontends::basic
{

namespace detail
{
/// @brief Interpret expression @p e as a numeric literal.
/// @param e Expression to inspect.
/// @return Numeric wrapper if @p e is an IntExpr or FloatExpr; std::nullopt otherwise.
/// @invariant Does not evaluate non-literal expressions.
std::optional<Numeric> asNumeric(const Expr &e)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&e))
        return Numeric{false, static_cast<double>(i->value), static_cast<long long>(i->value)};
    if (auto *f = dynamic_cast<const FloatExpr *>(&e))
        return Numeric{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

/// @brief Promote @p a to floating-point if either operand is float.
/// @param a First numeric operand.
/// @param b Second numeric operand.
/// @return @p a converted to float when necessary; otherwise @p a unchanged.
/// @invariant Integer value @p a.i remains intact after promotion.
Numeric promote(const Numeric &a, const Numeric &b)
{
    if (a.isFloat || b.isFloat)
        return Numeric{true, a.isFloat ? a.f : static_cast<double>(a.i), a.i};
    return a;
}

enum class LiteralType
{
    Int,
    Float,
    Bool,
    String,
    Numeric, ///< Wildcard used for numeric promotion lookups.
};

struct Constant
{
    LiteralType type = LiteralType::Int;
    Numeric numeric{};
    bool boolValue = false;
    std::string stringValue;
};

/// @brief Construct a constant descriptor representing an integer literal.
/// @param value Integer payload to store in the constant wrapper.
/// @return Constant tagged as @c LiteralType::Int with numeric fields initialised.
Constant makeIntConstant(long long value)
{
    Constant c;
    c.type = LiteralType::Int;
    c.numeric = Numeric{false, static_cast<double>(value), value};
    return c;
}

/// @brief Construct a constant descriptor representing a floating literal.
/// @param value Floating-point payload to store in the constant wrapper.
/// @return Constant tagged as @c LiteralType::Float.
Constant makeFloatConstant(double value)
{
    Constant c;
    c.type = LiteralType::Float;
    c.numeric = Numeric{true, value, static_cast<long long>(value)};
    return c;
}

/// @brief Construct a constant descriptor representing a boolean literal.
/// @param value Boolean payload captured by the descriptor.
/// @return Constant tagged as @c LiteralType::Bool.
Constant makeBoolConstant(bool value)
{
    Constant c;
    c.type = LiteralType::Bool;
    c.boolValue = value;
    return c;
}

/// @brief Construct a constant descriptor representing a string literal.
/// @param value String payload moved into the descriptor.
/// @return Constant tagged as @c LiteralType::String taking ownership of @p value.
Constant makeStringConstant(std::string value)
{
    Constant c;
    c.type = LiteralType::String;
    c.stringValue = std::move(value);
    return c;
}

/// @brief Promote a numeric wrapper into either an int or float constant descriptor.
/// @param numeric Promoted numeric payload.
/// @return Constant using @c LiteralType::Float when @p numeric represents a float.
Constant makeNumericConstant(const Numeric &numeric)
{
    return numeric.isFloat ? makeFloatConstant(numeric.f) : makeIntConstant(numeric.i);
}

/// @brief Determine whether a literal category stores numeric information.
/// @param type Literal tag to classify.
/// @return @c true when @p type denotes @c Int or @c Float.
bool isNumeric(LiteralType type)
{
    return type == LiteralType::Int || type == LiteralType::Float;
}

/// @brief Check whether a literal type satisfies a rule entry.
/// @param want Rule expectation; may use @c LiteralType::Numeric wildcard.
/// @param have Actual literal type of an operand.
/// @return @c true when the operand type is compatible with the rule.
bool matchesType(LiteralType want, LiteralType have)
{
    if (want == LiteralType::Numeric)
        return isNumeric(have);
    return want == have;
}

/// @brief Read a literal AST node into a @c Constant descriptor.
/// @param expr Expression to inspect.
/// @return Populated constant when @p expr is a literal; empty optional otherwise.
std::optional<Constant> extractConstant(const Expr &expr)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
        return makeIntConstant(i->value);
    if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
        return makeFloatConstant(f->value);
    if (auto *b = dynamic_cast<const BoolExpr *>(&expr))
        return makeBoolConstant(b->value);
    if (auto *s = dynamic_cast<const StringExpr *>(&expr))
        return makeStringConstant(s->value);
    return std::nullopt;
}

/// @brief Convert a constant descriptor back into an owning AST node.
/// @param value Constant information describing the literal to create.
/// @return Newly allocated AST node representing @p value.
ExprPtr materializeConstant(const Constant &value)
{
    switch (value.type)
    {
        case LiteralType::Int:
        {
            auto out = std::make_unique<IntExpr>();
            out->value = value.numeric.i;
            return out;
        }
        case LiteralType::Float:
        {
            auto out = std::make_unique<FloatExpr>();
            double fv =
                value.numeric.isFloat ? value.numeric.f : static_cast<double>(value.numeric.i);
            out->value = fv;
            return out;
        }
        case LiteralType::Bool:
        {
            auto out = std::make_unique<BoolExpr>();
            out->value = value.boolValue;
            return out;
        }
        case LiteralType::String:
        {
            auto out = std::make_unique<StringExpr>();
            out->value = value.stringValue;
            return out;
        }
        case LiteralType::Numeric:
            break;
    }
    return nullptr;
}

/// @brief Attempt to evaluate a numeric binary operation for two constants.
/// @param op Binary opcode describing the arithmetic or logical operator.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded constant or empty optional if operands are incompatible.
std::optional<Constant> evalNumericBinary(BinaryExpr::Op op,
                                          const Constant &lhs,
                                          const Constant &rhs)
{
    if (!isNumeric(lhs.type) || !isNumeric(rhs.type))
        return std::nullopt;

    if (auto folded = tryFoldBinaryArith(lhs.numeric, op, rhs.numeric))
        return makeNumericConstant(*folded);

    return std::nullopt;
}

/// @brief Fold logical AND operations expressed on numeric operands.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded integer constant or empty optional when folding is invalid.
std::optional<Constant> evalNumericAnd(const Constant &lhs, const Constant &rhs)
{
    if (!isNumeric(lhs.type) || !isNumeric(rhs.type))
        return std::nullopt;

    Numeric left = promote(lhs.numeric, rhs.numeric);
    Numeric right = promote(rhs.numeric, lhs.numeric);
    if (left.isFloat || right.isFloat)
        return std::nullopt;

    bool result = (left.i != 0) && (right.i != 0);
    return makeIntConstant(result ? 1 : 0);
}

/// @brief Fold logical OR operations expressed on numeric operands.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded integer constant or empty optional when folding is invalid.
std::optional<Constant> evalNumericOr(const Constant &lhs, const Constant &rhs)
{
    if (!isNumeric(lhs.type) || !isNumeric(rhs.type))
        return std::nullopt;

    Numeric left = promote(lhs.numeric, rhs.numeric);
    Numeric right = promote(rhs.numeric, lhs.numeric);
    if (left.isFloat || right.isFloat)
        return std::nullopt;

    bool result = (left.i != 0) || (right.i != 0);
    return makeIntConstant(result ? 1 : 0);
}

/// @brief Fold string concatenation for constant operands.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Concatenated string constant or empty optional when either operand is non-string.
std::optional<Constant> evalStringConcat(const Constant &lhs, const Constant &rhs)
{
    if (lhs.type != LiteralType::String || rhs.type != LiteralType::String)
        return std::nullopt;

    return makeStringConstant(lhs.stringValue + rhs.stringValue);
}

/// @brief Dispatch helper for folding addition constants via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalAdd(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::Add, lhs, rhs);
}

/// @brief Dispatch helper for folding subtraction constants via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalSub(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::Sub, lhs, rhs);
}

/// @brief Dispatch helper for folding multiplication constants via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalMul(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::Mul, lhs, rhs);
}

/// @brief Dispatch helper for folding floating division via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalDiv(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::Div, lhs, rhs);
}

/// @brief Dispatch helper for folding integer division via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalIDiv(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::IDiv, lhs, rhs);
}

/// @brief Dispatch helper for folding modulo operations via @ref evalNumericBinary.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded numeric constant or empty optional on failure.
std::optional<Constant> evalMod(const Constant &lhs, const Constant &rhs)
{
    return evalNumericBinary(BinaryExpr::Op::Mod, lhs, rhs);
}

using ConstantEvalFn = std::optional<Constant> (*)(const Constant &, const Constant &);

struct ArithmeticRule
{
    BinaryExpr::Op op;
    LiteralType lhs;
    LiteralType rhs;
    bool commutative;
    ConstantEvalFn eval;
};

constexpr std::array<ArithmeticRule, 11> kArithmeticRules = {{
    {BinaryExpr::Op::Add, LiteralType::Numeric, LiteralType::Numeric, true, &evalAdd},
    {BinaryExpr::Op::Sub, LiteralType::Numeric, LiteralType::Numeric, false, &evalSub},
    {BinaryExpr::Op::Mul, LiteralType::Numeric, LiteralType::Numeric, true, &evalMul},
    {BinaryExpr::Op::Div, LiteralType::Numeric, LiteralType::Numeric, false, &evalDiv},
    {BinaryExpr::Op::IDiv, LiteralType::Numeric, LiteralType::Numeric, false, &evalIDiv},
    {BinaryExpr::Op::Mod, LiteralType::Numeric, LiteralType::Numeric, false, &evalMod},
    {BinaryExpr::Op::LogicalAndShort,
     LiteralType::Numeric,
     LiteralType::Numeric,
     true,
     &evalNumericAnd},
    {BinaryExpr::Op::LogicalOrShort,
     LiteralType::Numeric,
     LiteralType::Numeric,
     true,
     &evalNumericOr},
    {BinaryExpr::Op::LogicalAnd, LiteralType::Numeric, LiteralType::Numeric, true, &evalNumericAnd},
    {BinaryExpr::Op::LogicalOr, LiteralType::Numeric, LiteralType::Numeric, true, &evalNumericOr},
    {BinaryExpr::Op::Add, LiteralType::String, LiteralType::String, false, &evalStringConcat},
}};

struct ArithmeticMatch
{
    const ArithmeticRule *rule;
    bool swapped;
};

/// @brief Locate the arithmetic folding rule compatible with two literal types.
/// @param op Binary opcode being considered for folding.
/// @param lhs Literal type classification for the left operand.
/// @param rhs Literal type classification for the right operand.
/// @return Matching rule and whether operands should be swapped, or empty optional if unsupported.
std::optional<ArithmeticMatch> findArithmeticRule(BinaryExpr::Op op,
                                                  LiteralType lhs,
                                                  LiteralType rhs)
{
    for (const auto &rule : kArithmeticRules)
    {
        if (rule.op != op)
            continue;

        if (matchesType(rule.lhs, lhs) && matchesType(rule.rhs, rhs))
            return ArithmeticMatch{&rule, false};

        if (rule.commutative && matchesType(rule.lhs, rhs) && matchesType(rule.rhs, lhs))
            return ArithmeticMatch{&rule, true};
    }
    return std::nullopt;
}

enum class CompareOutcome : std::size_t
{
    Less = 0,
    Equal = 1,
    Greater = 2,
    Unordered = 3,
};

constexpr std::size_t kCompareOutcomeCount = 4;

using ComparatorFn = std::optional<CompareOutcome> (*)(const Constant &, const Constant &);

/// @brief Compare numeric constants using BASIC promotion semantics.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Comparison outcome or empty optional when operands are non-numeric.
std::optional<CompareOutcome> compareNumericConstants(const Constant &lhs, const Constant &rhs)
{
    if (!isNumeric(lhs.type) || !isNumeric(rhs.type))
        return std::nullopt;

    Numeric left = promote(lhs.numeric, rhs.numeric);
    Numeric right = promote(rhs.numeric, lhs.numeric);

    if (left.isFloat || right.isFloat)
    {
        double lv = left.isFloat ? left.f : static_cast<double>(left.i);
        double rv = right.isFloat ? right.f : static_cast<double>(right.i);
        if (std::isnan(lv) || std::isnan(rv))
            return CompareOutcome::Unordered;
        if (lv < rv)
            return CompareOutcome::Less;
        if (lv > rv)
            return CompareOutcome::Greater;
        return CompareOutcome::Equal;
    }

    if (left.i < right.i)
        return CompareOutcome::Less;
    if (left.i > right.i)
        return CompareOutcome::Greater;
    return CompareOutcome::Equal;
}

/// @brief Compare string constants lexicographically.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Comparison outcome or empty optional when operands are non-strings.
std::optional<CompareOutcome> compareStringConstants(const Constant &lhs, const Constant &rhs)
{
    if (lhs.type != LiteralType::String || rhs.type != LiteralType::String)
        return std::nullopt;

    int cmp = lhs.stringValue.compare(rhs.stringValue);
    if (cmp < 0)
        return CompareOutcome::Less;
    if (cmp > 0)
        return CompareOutcome::Greater;
    return CompareOutcome::Equal;
}

struct ComparatorRule
{
    LiteralType lhs;
    LiteralType rhs;
    bool commutative;
    ComparatorFn compare;
};

constexpr std::array<ComparatorRule, 2> kComparatorRules = {{
    {LiteralType::Numeric, LiteralType::Numeric, true, &compareNumericConstants},
    {LiteralType::String, LiteralType::String, true, &compareStringConstants},
}};

struct ComparatorMatch
{
    const ComparatorRule *rule;
    bool swapped;
};

/// @brief Determine which comparator rule applies to a pair of literal types.
/// @param lhs Literal type of the left operand.
/// @param rhs Literal type of the right operand.
/// @return Comparator rule metadata with swap flag or empty optional if unsupported.
std::optional<ComparatorMatch> findComparatorRule(LiteralType lhs, LiteralType rhs)
{
    for (const auto &rule : kComparatorRules)
    {
        if (matchesType(rule.lhs, lhs) && matchesType(rule.rhs, rhs))
            return ComparatorMatch{&rule, false};
        if (rule.commutative && matchesType(rule.lhs, rhs) && matchesType(rule.rhs, lhs))
            return ComparatorMatch{&rule, true};
    }
    return std::nullopt;
}

struct ComparisonTruth
{
    BinaryExpr::Op op;
    std::array<std::optional<long long>, kCompareOutcomeCount> truth;
};

constexpr std::array<ComparisonTruth, 6> kComparisonTruthTable = {{
    {BinaryExpr::Op::Eq, {0LL, 1LL, 0LL, 0LL}},
    {BinaryExpr::Op::Ne, {1LL, 0LL, 1LL, 1LL}},
    {BinaryExpr::Op::Lt, {1LL, 0LL, 0LL, 0LL}},
    {BinaryExpr::Op::Le, {1LL, 1LL, 0LL, 0LL}},
    {BinaryExpr::Op::Gt, {0LL, 0LL, 1LL, 0LL}},
    {BinaryExpr::Op::Ge, {0LL, 1LL, 1LL, 0LL}},
}};

/// @brief Lookup the comparison truth table entry for a given opcode.
/// @param op Binary comparison opcode.
/// @return Pointer to truth table row describing expected outcomes; nullptr if absent.
const ComparisonTruth *findComparisonTruth(BinaryExpr::Op op)
{
    for (const auto &entry : kComparisonTruthTable)
    {
        if (entry.op == op)
            return &entry;
    }
    return nullptr;
}

/// @brief Evaluate a comparison between two constant values.
/// @param op Comparison opcode to evaluate.
/// @param lhs Left operand constant.
/// @param rhs Right operand constant.
/// @return Folded integer constant (0/1) or empty optional when unsupported.
std::optional<Constant> evalComparison(BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if (lhs.type == LiteralType::String && rhs.type == LiteralType::String &&
        !(op == BinaryExpr::Op::Eq || op == BinaryExpr::Op::Ne))
        return std::nullopt;

    auto *truth = findComparisonTruth(op);
    if (!truth)
        return std::nullopt;

    auto comparator = findComparatorRule(lhs.type, rhs.type);
    if (!comparator)
        return std::nullopt;

    const Constant &first = comparator->swapped ? rhs : lhs;
    const Constant &second = comparator->swapped ? lhs : rhs;

    auto outcome = comparator->rule->compare(first, second);
    if (!outcome)
        return std::nullopt;

    auto value = truth->truth[static_cast<std::size_t>(*outcome)];
    if (!value)
        return std::nullopt;

    return makeIntConstant(*value);
}

/// @brief Attempt to fold a binary expression when both operands are literals.
/// @param op Binary opcode of the expression.
/// @param lhsExpr Left-hand operand expression.
/// @param rhsExpr Right-hand operand expression.
/// @return Newly materialised literal node or nullptr if folding fails.
ExprPtr foldBinaryLiteral(BinaryExpr::Op op, const Expr &lhsExpr, const Expr &rhsExpr)
{
    auto lhsConst = extractConstant(lhsExpr);
    auto rhsConst = extractConstant(rhsExpr);
    if (!lhsConst || !rhsConst)
        return nullptr;

    auto arithmetic = findArithmeticRule(op, lhsConst->type, rhsConst->type);
    if (arithmetic)
    {
        const Constant &left = arithmetic->swapped ? *rhsConst : *lhsConst;
        const Constant &right = arithmetic->swapped ? *lhsConst : *rhsConst;
        if (auto result = arithmetic->rule->eval(left, right))
            return materializeConstant(*result);
    }

    if (auto result = evalComparison(op, *lhsConst, *rhsConst))
        return materializeConstant(*result);

    return nullptr;
}
} // namespace detail

namespace
{

/// @brief AST visitor that performs in-place constant folding for BASIC.
/// @details Traverses expressions and statements, eagerly rewriting literal
///          subtrees into canonical nodes.  The pass threads context via member
///          pointers so nested visits can replace the current expression or
///          statement without returning large structures.
class ConstFolderPass : public MutExprVisitor, public MutStmtVisitor
{
  public:
    /// @brief Fold all procedures and top-level statements in a program.
    /// @param prog Program whose AST will be mutated in place.
    void run(Program &prog)
    {
        for (auto &decl : prog.procs)
            foldStmt(decl);
        for (auto &stmt : prog.main)
            foldStmt(stmt);
    }

  private:
    /// @brief Recursively fold an expression tree and update the current slot.
    /// @param expr Expression pointer reference that may be replaced with a literal.
    void foldExpr(ExprPtr &expr)
    {
        if (!expr)
            return;
        ExprPtr *prev = currentExpr_;
        currentExpr_ = &expr;
        expr->accept(*this);
        currentExpr_ = prev;
    }

    /// @brief Recursively fold a statement subtree.
    /// @param stmt Statement pointer reference that may be rewritten.
    void foldStmt(StmtPtr &stmt)
    {
        if (!stmt)
            return;
        StmtPtr *prev = currentStmt_;
        currentStmt_ = &stmt;
        stmt->accept(*this);
        currentStmt_ = prev;
    }

    /// @brief Access the expression slot currently being rewritten.
    /// @return Reference to the pointer tracked by the visitor.
    ExprPtr &exprSlot()
    {
        return *currentExpr_;
    }

    /// @brief Replace the active expression with an integer literal node.
    /// @param v Integer value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithInt(long long v, il::support::SourceLoc loc)
    {
        auto ni = std::make_unique<IntExpr>();
        ni->loc = loc;
        ni->value = v;
        exprSlot() = std::move(ni);
    }

    /// @brief Replace the active expression with a boolean literal node.
    /// @param v Boolean value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithBool(bool v, il::support::SourceLoc loc)
    {
        auto nb = std::make_unique<BoolExpr>();
        nb->loc = loc;
        nb->value = v;
        exprSlot() = std::move(nb);
    }

    /// @brief Replace the active expression with a string literal node.
    /// @param s String payload moved into the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithStr(std::string s, il::support::SourceLoc loc)
    {
        auto ns = std::make_unique<StringExpr>();
        ns->loc = loc;
        ns->value = std::move(s);
        exprSlot() = std::move(ns);
    }

    /// @brief Replace the active expression with a floating-point literal node.
    /// @param v Floating-point value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithFloat(double v, il::support::SourceLoc loc)
    {
        auto nf = std::make_unique<FloatExpr>();
        nf->loc = loc;
        nf->value = v;
        exprSlot() = std::move(nf);
    }

    /// @brief Replace the active expression with an arbitrary expression node.
    /// @param replacement Newly constructed expression that becomes current.
    void replaceWithExpr(ExprPtr replacement)
    {
        exprSlot() = std::move(replacement);
    }

    /// @brief Extract a finite numeric value from an expression if possible.
    /// @param expr Expression pointer to inspect.
    /// @return Finite double value or empty optional when non-numeric.
    std::optional<double> getFiniteDouble(const ExprPtr &expr) const
    {
        if (!expr)
            return std::nullopt;
        auto numeric = detail::asNumeric(*expr);
        if (!numeric)
            return std::nullopt;
        double value = numeric->isFloat ? numeric->f : static_cast<double>(numeric->i);
        if (!std::isfinite(value))
            return std::nullopt;
        return value;
    }

    /// @brief Interpret an expression as an integer number of digits.
    /// @param expr Expression to inspect.
    /// @return Rounded digit count within 32-bit range or empty optional when invalid.
    std::optional<int> getRoundedDigits(const ExprPtr &expr) const
    {
        auto value = getFiniteDouble(expr);
        if (!value)
            return std::nullopt;
        double rounded = std::nearbyint(*value);
        if (!std::isfinite(rounded))
            return std::nullopt;
        if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
            rounded > static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::nullopt;
        return static_cast<int>(rounded);
    }

    /// @brief Round a floating value to the specified decimal digits.
    /// @param value Floating-point value to round.
    /// @param digits Positive for fractional digits, negative for integral multiples.
    /// @return Rounded result or empty optional when intermediate computations overflow.
    std::optional<double> roundToDigits(double value, int digits) const
    {
        if (!std::isfinite(value))
            return std::nullopt;

        if (digits == 0)
        {
            double rounded = std::nearbyint(value);
            if (!std::isfinite(rounded))
                return std::nullopt;
            return rounded;
        }

        double scaleExponent = static_cast<double>(std::abs(digits));
        double scale = std::pow(10.0, scaleExponent);
        if (!std::isfinite(scale) || scale == 0.0)
            return std::nullopt;

        double scaled = digits > 0 ? value * scale : value / scale;
        if (!std::isfinite(scaled))
            return std::nullopt;

        double rounded = std::nearbyint(scaled);
        if (!std::isfinite(rounded))
            return std::nullopt;

        double result = digits > 0 ? rounded / scale : rounded * scale;
        if (!std::isfinite(result))
            return std::nullopt;
        return result;
    }

    /// @brief Parse a string literal using BASIC's VAL semantics.
    /// @param expr String literal expression to parse.
    /// @return Parsed double or empty optional when the string is not a valid number.
    std::optional<double> parseValLiteral(const StringExpr &expr) const
    {
        const std::string &s = expr.value;
        const char *raw = s.c_str();
        while (*raw && std::isspace(static_cast<unsigned char>(*raw)))
            ++raw;

        if (*raw == '\0')
            return 0.0;

        auto isDigit = [](char ch) { return ch >= '0' && ch <= '9'; };

        if (*raw == '+' || *raw == '-')
        {
            char next = raw[1];
            if (next == '.')
            {
                if (!isDigit(raw[2]))
                    return 0.0;
            }
            else if (!isDigit(next))
            {
                return 0.0;
            }
        }
        else if (*raw == '.')
        {
            if (!isDigit(raw[1]))
                return 0.0;
        }
        else if (!isDigit(*raw))
        {
            return 0.0;
        }

        char *endp = nullptr;
        double parsed = std::strtod(raw, &endp);
        if (endp == raw)
            return 0.0;
        if (!std::isfinite(parsed))
            return std::nullopt;
        return parsed;
    }

    /// @brief Fold LEN builtin calls when the argument is a literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldLen(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto folded = detail::foldLenLiteral(*expr.args[0]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold MID builtin calls when all operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldMid(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 3 || !expr.args[0] || !expr.args[1] || !expr.args[2])
            return false;
        if (auto folded = detail::foldMidLiteral(*expr.args[0], *expr.args[1], *expr.args[2]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold LEFT builtin calls when both operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldLeft(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = detail::foldLeftLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold RIGHT builtin calls when both operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldRight(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = detail::foldRightLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold VAL builtin calls when the argument is a literal string.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a numeric constant.
    bool tryFoldVal(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto *literal = dynamic_cast<StringExpr *>(expr.args[0].get()))
        {
            auto parsed = parseValLiteral(*literal);
            if (!parsed)
                return false;
            replaceWithFloat(*parsed, expr.loc);
            return true;
        }
        return false;
    }

    /// @brief Fold INT builtin calls for literal numeric arguments.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldInt(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;
        double floored = std::floor(*value);
        if (!std::isfinite(floored))
            return false;
        replaceWithFloat(floored, expr.loc);
        return true;
    }

    /// @brief Fold FIX builtin calls for literal numeric arguments.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldFix(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;
        double truncated = std::trunc(*value);
        if (!std::isfinite(truncated))
            return false;
        replaceWithFloat(truncated, expr.loc);
        return true;
    }

    /// @brief Fold ROUND builtin calls when arguments are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a rounded constant.
    bool tryFoldRound(BuiltinCallExpr &expr)
    {
        if (expr.args.empty() || !expr.args[0])
            return false;

        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;

        int digits = 0;
        if (expr.args.size() >= 2 && expr.args[1])
        {
            auto parsedDigits = getRoundedDigits(expr.args[1]);
            if (!parsedDigits)
                return false;
            digits = *parsedDigits;
        }

        auto result = roundToDigits(*value, digits);
        if (!result)
            return false;
        replaceWithFloat(*result, expr.loc);
        return true;
    }

    /// @brief Fold STR builtin calls when the argument is literal numeric.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a string literal.
    bool tryFoldStr(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto numeric = detail::asNumeric(*expr.args[0]);
        if (!numeric)
            return false;

        char buf[64];
        if (numeric->isFloat)
        {
            rt_format_f64(numeric->f, buf, sizeof(buf));
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lld", numeric->i);
        }
        replaceWithStr(buf, expr.loc);
        return true;
    }

    struct BuiltinDispatchEntry
    {
        BuiltinCallExpr::Builtin builtin;
        bool (ConstFolderPass::*folder)(BuiltinCallExpr &);
    };

    static constexpr std::array<BuiltinDispatchEntry, 9> kBuiltinDispatch{{
        {BuiltinCallExpr::Builtin::Len, &ConstFolderPass::tryFoldLen},
        {BuiltinCallExpr::Builtin::Mid, &ConstFolderPass::tryFoldMid},
        {BuiltinCallExpr::Builtin::Left, &ConstFolderPass::tryFoldLeft},
        {BuiltinCallExpr::Builtin::Right, &ConstFolderPass::tryFoldRight},
        {BuiltinCallExpr::Builtin::Val, &ConstFolderPass::tryFoldVal},
        {BuiltinCallExpr::Builtin::Int, &ConstFolderPass::tryFoldInt},
        {BuiltinCallExpr::Builtin::Fix, &ConstFolderPass::tryFoldFix},
        {BuiltinCallExpr::Builtin::Round, &ConstFolderPass::tryFoldRound},
        {BuiltinCallExpr::Builtin::Str, &ConstFolderPass::tryFoldStr},
    }};

    // MutExprVisitor overrides ----------------------------------------------
    /// @brief Literals are already canonical, so integer nodes are left untouched.
    void visit(IntExpr &) override {}

    /// @brief Floating literals require no rewriting beyond their existing value.
    void visit(FloatExpr &) override {}

    /// @brief String literals are already canonical and therefore skipped.
    void visit(StringExpr &) override {}

    /// @brief Boolean literals are already canonical and therefore skipped.
    void visit(BoolExpr &) override {}

    /// @brief Variable references cannot be folded directly.
    void visit(VarExpr &) override {}

    /// @brief Fold array index expressions before evaluating bounds.
    void visit(ArrayExpr &expr) override
    {
        foldExpr(expr.index);
    }

    /// @brief Lower bound queries are left untouched because they resolve at runtime.
    void visit(LBoundExpr &) override {}

    /// @brief Upper bound queries are left untouched because they resolve at runtime.
    void visit(UBoundExpr &) override {}

    /// @brief Fold unary operations when the operand collapses to a literal.
    void visit(UnaryExpr &expr) override
    {
        foldExpr(expr.expr);
        switch (expr.op)
        {
            case UnaryExpr::Op::LogicalNot:
                if (auto replacement = detail::foldLogicalNot(*expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
            case UnaryExpr::Op::Plus:
            case UnaryExpr::Op::Negate:
                if (auto replacement = detail::foldUnaryArith(expr.op, *expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
        }
    }

    /// @brief Fold binary operations by evaluating literal operands and applying shortcuts.
    void visit(BinaryExpr &expr) override
    {
        foldExpr(expr.lhs);

        if (auto *lhsBool = dynamic_cast<BoolExpr *>(expr.lhs.get()))
        {
            if (auto shortCircuit = detail::tryShortCircuit(expr.op, *lhsBool))
            {
                replaceWithBool(*shortCircuit, expr.loc);
                return;
            }

            if (detail::isShortCircuitOp(expr.op))
            {
                ExprPtr rhs = std::move(expr.rhs);
                foldExpr(rhs);
                if (auto folded = detail::foldLogicalBinary(*lhsBool, expr.op, *rhs))
                {
                    folded->loc = expr.loc;
                    replaceWithExpr(std::move(folded));
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }

        foldExpr(expr.rhs);

        if (auto folded = detail::foldLogicalBinary(*expr.lhs, expr.op, *expr.rhs))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return;
        }

        if (auto folded = detail::foldBinaryLiteral(expr.op, *expr.lhs, *expr.rhs))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
        }
    }

    /// @brief Fold builtin function calls whose arguments are literal values.
    void visit(BuiltinCallExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);

        for (const auto &entry : kBuiltinDispatch)
        {
            if (entry.builtin == expr.builtin)
            {
                if ((this->*entry.folder)(expr))
                    return;
                break;
            }
        }
    }

    /// @brief User-defined calls are not folded because they may have side effects.
    void visit(CallExpr &) override {}

    /// @brief Fold constructor arguments to expose more literal values downstream.
    void visit(NewExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

    /// @brief The ME pseudo-variable cannot be folded.
    void visit(MeExpr &) override {}

    /// @brief Fold the receiver of member accesses before further lowering.
    void visit(MemberAccessExpr &expr) override
    {
        foldExpr(expr.base);
    }

    /// @brief Fold the receiver and arguments of method invocations.
    void visit(MethodCallExpr &expr) override
    {
        foldExpr(expr.base);
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

    // MutStmtVisitor overrides ----------------------------------------------
    /// @brief Labels carry no expressions to fold.
    void visit(LabelStmt &) override {}

    /// @brief Fold expressions embedded in PRINT statement items.
    void visit(PrintStmt &stmt) override
    {
        for (auto &item : stmt.items)
        {
            if (item.kind == PrintItem::Kind::Expr)
                foldExpr(item.expr);
        }
    }

    /// @brief Fold channel and argument expressions for PRINT # statements.
    void visit(PrintChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        for (auto &arg : stmt.args)
            foldExpr(arg);
    }

    /// @brief Fold arguments within CALL statements while leaving target intact.
    void visit(CallStmt &stmt) override
    {
        if (!stmt.call)
            return;
        for (auto &arg : stmt.call->args)
            foldExpr(arg);
    }

    /// @brief CLS has no foldable expressions.
    void visit(ClsStmt &) override {}

    /// @brief Fold the foreground/background expressions for COLOR statements.
    void visit(ColorStmt &stmt) override
    {
        foldExpr(stmt.fg);
        foldExpr(stmt.bg);
    }

    /// @brief Fold cursor position expressions for LOCATE statements.
    void visit(LocateStmt &stmt) override
    {
        foldExpr(stmt.row);
        foldExpr(stmt.col);
    }

    /// @brief Fold both the target and assigned expression in LET statements.
    void visit(LetStmt &stmt) override
    {
        foldExpr(stmt.target);
        foldExpr(stmt.expr);
    }

    /// @brief Fold array size expressions in DIM statements when present.
    void visit(DimStmt &stmt) override
    {
        if (stmt.isArray && stmt.size)
            foldExpr(stmt.size);
    }

    /// @brief Fold new bounds in REDIM statements when present.
    void visit(ReDimStmt &stmt) override
    {
        if (stmt.size)
            foldExpr(stmt.size);
    }

    /// @brief RANDOMIZE statements carry no foldable expressions.
    void visit(RandomizeStmt &) override {}

    /// @brief Fold predicates and branch bodies within IF statements.
    void visit(IfStmt &stmt) override
    {
        foldExpr(stmt.cond);
        foldStmt(stmt.then_branch);
        for (auto &elseif : stmt.elseifs)
        {
            foldExpr(elseif.cond);
            foldStmt(elseif.then_branch);
        }
        foldStmt(stmt.else_branch);
    }

    /// @brief Fold selectors and arms inside SELECT CASE statements.
    void visit(SelectCaseStmt &stmt) override
    {
        foldExpr(stmt.selector);
        for (auto &arm : stmt.arms)
            for (auto &bodyStmt : arm.body)
                foldStmt(bodyStmt);
        for (auto &bodyStmt : stmt.elseBody)
            foldStmt(bodyStmt);
    }

    /// @brief Fold loop predicates and bodies for WHILE statements.
    void visit(WhileStmt &stmt) override
    {
        foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold DO loop conditions (when present) and bodies.
    void visit(DoStmt &stmt) override
    {
        if (stmt.cond)
            foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold range and body expressions for FOR loops.
    void visit(ForStmt &stmt) override
    {
        foldExpr(stmt.start);
        foldExpr(stmt.end);
        if (stmt.step)
            foldExpr(stmt.step);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief NEXT statements have no expressions to fold.
    void visit(NextStmt &) override {}

    /// @brief EXIT statements have no expressions to fold.
    void visit(ExitStmt &) override {}

    /// @brief GOTO statements have no expressions to fold.
    void visit(GotoStmt &) override {}

    /// @brief GOSUB statements have no expressions to fold.
    void visit(GosubStmt &) override {}

    /// @brief Fold OPEN statement operands such as path and channel.
    void visit(OpenStmt &stmt) override
    {
        if (stmt.pathExpr)
            foldExpr(stmt.pathExpr);
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }

    /// @brief Fold channel expressions in CLOSE statements.
    void visit(CloseStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }

    /// @brief Fold channel and offset expressions in SEEK statements.
    void visit(SeekStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
        if (stmt.positionExpr)
            foldExpr(stmt.positionExpr);
    }

    /// @brief ON ERROR GOTO contains no literal operands to fold.
    void visit(OnErrorGoto &) override {}

    /// @brief RESUME statements rely on runtime state and are left untouched.
    void visit(Resume &) override {}

    /// @brief END statements have no expressions to fold.
    void visit(EndStmt &) override {}

    /// @brief Fold prompts within INPUT statements when literal.
    void visit(InputStmt &stmt) override
    {
        if (stmt.prompt)
            foldExpr(stmt.prompt);
    }

    /// @brief INPUT # statements have no additional foldable expressions.
    void visit(InputChStmt &) override {}

    /// @brief Fold channel and destination expressions in LINE INPUT #.
    void visit(LineInputChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        foldExpr(stmt.targetVar);
    }

    /// @brief RETURN statements are control-only and do not fold expressions.
    void visit(ReturnStmt &) override {}

    /// @brief FUNCTION declarations are processed elsewhere; nothing to fold here.
    void visit(FunctionDecl &) override {}

    /// @brief SUB declarations are processed elsewhere; nothing to fold here.
    void visit(SubDecl &) override {}

    /// @brief Recursively fold every statement within a statement list.
    void visit(StmtList &stmt) override
    {
        for (auto &child : stmt.stmts)
            foldStmt(child);
    }

    /// @brief Fold the target expression of DELETE statements.
    void visit(DeleteStmt &stmt) override
    {
        foldExpr(stmt.target);
    }

    /// @brief Fold the body statements of constructors.
    void visit(ConstructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold the body statements of destructors.
    void visit(DestructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold the body statements of method declarations.
    void visit(MethodDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold every member statement within class declarations.
    void visit(ClassDecl &stmt) override
    {
        for (auto &member : stmt.members)
            foldStmt(member);
    }

    /// @brief TYPE declarations define shapes only and do not fold expressions.
    void visit(TypeDecl &) override {}

    ExprPtr *currentExpr_ = nullptr;
    StmtPtr *currentStmt_ = nullptr;
};

} // namespace

/// @brief Perform constant folding across an entire BASIC program.
/// @param prog Program to mutate; expressions are folded in place.
void foldConstants(Program &prog)
{
    ConstFolderPass pass;
    pass.run(prog);
}

} // namespace il::frontends::basic
