//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Scan_ExprTypes.cpp
// Purpose: Implements expression-type inference for BASIC scan passes.
// Key invariants: Produces expression classifications without mutating runtime
//                 flags or emitting IL.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/core/Type.hpp"
#include <cassert>
#include <optional>
#include <vector>

namespace il::frontends::basic::lower
{
/// @brief Determine the resulting type for a builtin call without lowering.
///
/// @param lowerer Lowerer supplying symbol/type caches.
/// @param expr Builtin call expression to analyse.
/// @return Classification describing the builtin's result type.
Lowerer::ExprType scanBuiltinExprTypes(Lowerer &lowerer, const BuiltinCallExpr &expr);

namespace detail
{

/// @brief Translate AST-level type annotations to Lowerer expression kinds.
///
/// @param ty AST type enumerator recorded on declarations.
/// @return Lowerer expression classification matching @p ty.
Lowerer::ExprType exprTypeFromAstType(Type ty)
{
    switch (ty)
    {
        case Type::Str: return Lowerer::ExprType::Str;
        case Type::F64: return Lowerer::ExprType::F64;
        case Type::Bool: return Lowerer::ExprType::Bool;
        case Type::I64:
        default: return Lowerer::ExprType::I64;
    }
}

/// @brief AST walker that infers expression types during the scan phase.
///
/// @details The scanner pushes inferred @ref Lowerer::ExprType values onto a
///          private stack while traversing the AST.  It cooperates with the
///          lowerer to resolve symbols without mutating IR generation state.
class ExprTypeScanner final : public BasicAstWalker<ExprTypeScanner>
{
  public:
    /// @brief Construct a scanner bound to the owning lowering context.
    ///
    /// @param lowerer Reference to the BASIC lowerer providing symbol data.
    explicit ExprTypeScanner(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    using ExprType = Lowerer::ExprType;

    /// @brief Evaluate @p expr and return its inferred type classification.
    ///
    /// @param expr Expression to analyse.
    /// @return Expression category such as integer, floating, or string.
    ExprType evaluateExpr(const Expr &expr)
    {
        StackScope scope(*this);
        expr.accept(*this);
        return pop();
    }

    /// @brief Skip builtin arguments because specialised rules handle them.
    ///
    /// @param expr Builtin call under inspection.
    /// @return False to prevent recursive descent.
    bool shouldVisitChildren(const BuiltinCallExpr &) { return false; }

    /// @brief Skip procedure call children to avoid double counting side effects.
    ///
    /// @param expr Procedure call expression.
    /// @return False so the walker avoids visiting argument subtrees automatically.
    bool shouldVisitChildren(const CallExpr &) { return false; }

    /// @brief Skip constructor arguments because custom logic consumes them.
    ///
    /// @param expr Constructor call expression.
    /// @return False to defer traversal to helper routines.
    bool shouldVisitChildren(const NewExpr &) { return false; }

    /// @brief Skip member access children; the base is handled manually.
    ///
    /// @param expr Member access node.
    /// @return False to avoid automatic recursion.
    bool shouldVisitChildren(const MemberAccessExpr &) { return false; }

    /// @brief Skip method call arguments because explicit handling is required.
    ///
    /// @param expr Method call expression.
    /// @return False to maintain manual traversal order.
    bool shouldVisitChildren(const MethodCallExpr &) { return false; }

    /// @brief Classify integer literals as 64-bit integers.
    ///
    /// @param expr Integer literal (value unused).
    void after(const IntExpr &)
    {
        push(ExprType::I64);
    }

    /// @brief Classify floating literals as 64-bit floats.
    ///
    /// @param expr Floating literal.
    void after(const FloatExpr &)
    {
        push(ExprType::F64);
    }

    /// @brief Classify string literals as strings.
    ///
    /// @param expr String literal node.
    void after(const StringExpr &)
    {
        push(ExprType::Str);
    }

    /// @brief Treat boolean literals as integer flags (historical BASIC rule).
    ///
    /// @param expr Boolean literal node.
    void after(const BoolExpr &)
    {
        push(ExprType::I64);
    }

    /// @brief Resolve variable references using known symbol metadata.
    ///
    /// @param expr Variable reference to analyse.
    void after(const VarExpr &expr)
    {
        ExprType result = ExprType::I64;
        if (const auto *info = lowerer_.findSymbol(expr.name))
        {
            if (info->hasType)
                result = exprTypeFromAstType(info->type);
            else
                result = exprTypeFromAstType(inferAstTypeFromName(expr.name));
        }
        else
        {
            result = exprTypeFromAstType(inferAstTypeFromName(expr.name));
        }
        push(result);
    }

    /// @brief Infer array element access and consume optional index types.
    ///
    /// @param expr Array expression possibly containing an index.
    void after(const ArrayExpr &expr)
    {
        discardIf(expr.index != nullptr);
        push(ExprType::I64);
    }

    /// @brief Treat LBOUND queries as integer expressions.
    void after(const LBoundExpr &)
    {
        push(ExprType::I64);
    }

    /// @brief Treat UBOUND queries as integer expressions.
    void after(const UBoundExpr &)
    {
        push(ExprType::I64);
    }

    /// @brief Propagate operand classification through unary operators.
    void after(const UnaryExpr &)
    {
        ExprType operand = pop();
        push(operand);
    }

    /// @brief Combine operand types for binary operations.
    ///
    /// @param expr Binary expression describing the operator.
    void after(const BinaryExpr &expr)
    {
        ExprType rhs = pop();
        ExprType lhs = pop();
        push(combineBinary(expr, lhs, rhs));
    }

    /// @brief Delegate builtin classification to the shared helper.
    ///
    /// @param expr Builtin call expression.
    void after(const BuiltinCallExpr &expr)
    {
        push(::il::frontends::basic::lower::scanBuiltinExprTypes(lowerer_, expr));
    }

    /// @brief Use stored procedure signatures to classify call expressions.
    ///
    /// @param expr Procedure call expression.
    void after(const CallExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        if (const auto *sig = lowerer_.findProcSignature(expr.callee))
        {
            using K = il::core::Type::Kind;
            switch (sig->retType.kind)
            {
                case K::F64: push(ExprType::F64); break;
                case K::Ptr:
                case K::I1:
                case K::I16:
                case K::I32:
                case K::I64: push(ExprType::I64); break;
                case K::Str: push(ExprType::Str); break;
                case K::Void:
                default: push(ExprType::I64); break;
            }
        }
        else
        {
            push(ExprType::I64);
        }
    }

    /// @brief Classify object construction expressions as integer handles.
    ///
    /// @param expr New expression containing constructor arguments.
    void after(const NewExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        push(ExprType::I64);
    }

    /// @brief Treat ME references as integer handles to the current object.
    void after(const MeExpr &)
    {
        push(ExprType::I64);
    }

    /// @brief Resolve member access result types from cached class layouts.
    ///
    /// @param expr Member access expression being evaluated.
    void after(const MemberAccessExpr &expr)
    {
        ExprType result = ExprType::I64;
        if (expr.base)
        {
            consumeExpr(*expr.base);
            std::string className = lowerer_.resolveObjectClass(*expr.base);
            auto layoutIt = lowerer_.classLayouts_.find(className);
            if (layoutIt != lowerer_.classLayouts_.end())
            {
                if (const auto *field = layoutIt->second.findField(expr.member))
                    result = exprTypeFromAstType(field->type);
            }
        }
        push(result);
    }

    /// @brief Treat method calls as integer-returning after consuming arguments.
    ///
    /// @param expr Method call expression encountered during scanning.
    void after(const MethodCallExpr &expr)
    {
        if (expr.base)
            consumeExpr(*expr.base);
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        push(ExprType::I64);
    }

  private:
    struct StackScope
    {
        /// @brief Reference to the owning walker used for validation.
        ExprTypeScanner &walker;
        /// @brief Depth snapshot captured on scope entry.
        std::size_t depth;

        /// @brief Record current stack depth to enforce balance at scope exit.
        explicit StackScope(ExprTypeScanner &w) : walker(w), depth(w.exprStack_.size()) {}

        /// @brief Assert that the stack depth matches the entry snapshot.
        ~StackScope()
        {
            assert(walker.exprStack_.size() == depth && "expression stack imbalance");
        }
    };

    /// @brief Push a classification onto the evaluation stack.
    ///
    /// @param ty Expression classification to record.
    void push(ExprType ty)
    {
        exprStack_.push_back(ty);
    }

    /// @brief Pop and return the most recent classification.
    ///
    /// @return Last computed classification.
    ExprType pop()
    {
        assert(!exprStack_.empty());
        ExprType ty = exprStack_.back();
        exprStack_.pop_back();
        return ty;
    }

    /// @brief Conditionally discard the most recent classification.
    ///
    /// @param condition When true the top of the stack is removed.
    void discardIf(bool condition)
    {
        if (condition)
            (void)pop();
    }

    /// @brief Evaluate a child expression and return its classification.
    ///
    /// @param expr Expression subtree to consume.
    /// @return Computed classification of @p expr.
    ExprType consumeExpr(const Expr &expr)
    {
        expr.accept(*this);
        return pop();
    }

    /// @brief Combine operand classifications following BASIC promotion rules.
    ///
    /// @param expr Binary expression providing operator context.
    /// @param lhs Left-hand operand classification.
    /// @param rhs Right-hand operand classification.
    /// @return Resulting classification after promotions.
    ExprType combineBinary(const BinaryExpr &expr, ExprType lhs, ExprType rhs)
    {
        using Op = BinaryExpr::Op;
        if (expr.op == Op::Pow)
            return ExprType::F64;
        if (expr.op == Op::Add && lhs == ExprType::Str && rhs == ExprType::Str)
            return ExprType::Str;
        if (expr.op == Op::Eq || expr.op == Op::Ne)
            return ExprType::Bool;
        if (expr.op == Op::LogicalAndShort || expr.op == Op::LogicalOrShort || expr.op == Op::LogicalAnd ||
            expr.op == Op::LogicalOr)
            return ExprType::Bool;
        if (lhs == ExprType::F64 || rhs == ExprType::F64)
            return ExprType::F64;
        return ExprType::I64;
    }

    Lowerer &lowerer_;
    std::vector<ExprType> exprStack_{};
};

} // namespace detail

/// @brief Classify a standalone expression using the scan-time inference walker.
///
/// @param lowerer Lowerer providing symbol tables and inference caches.
/// @param expr Expression to classify.
/// @return Expression classification recorded by the scan.
Lowerer::ExprType scanExprTypes(Lowerer &lowerer, const Expr &expr)
{
    detail::ExprTypeScanner scanner(lowerer);
    return scanner.evaluateExpr(expr);
}

/// @brief Determine builtin expression result types by consulting scan rules.
///
/// @param lowerer Lowerer providing rule lookup and expression scanner access.
/// @param expr Builtin call to analyse.
/// @return Resulting expression classification.
Lowerer::ExprType scanBuiltinExprTypes(Lowerer &lowerer, const BuiltinCallExpr &expr)
{
    const auto &rule = getBuiltinScanRule(expr.builtin);
    std::vector<std::optional<Lowerer::ExprType>> argTypes(expr.args.size());

    auto scanArg = [&](std::size_t idx) {
        if (idx >= expr.args.size())
            return;
        const auto &arg = expr.args[idx];
        if (!arg)
            return;
        detail::ExprTypeScanner scanner(lowerer);
        argTypes[idx] = scanner.evaluateExpr(*arg);
    };

    if (rule.traversal == BuiltinScanRule::ArgTraversal::All)
    {
        for (std::size_t i = 0; i < expr.args.size(); ++i)
            scanArg(i);
    }
    else
    {
        for (std::size_t idx : rule.explicitArgs)
            scanArg(idx);
    }

    auto argType = [&](std::size_t idx) -> std::optional<Lowerer::ExprType> {
        if (idx >= argTypes.size())
            return std::nullopt;
        return argTypes[idx];
    };

    Lowerer::ExprType result = rule.result.type;
    if (rule.result.kind == BuiltinScanRule::ResultSpec::Kind::FromArg)
    {
        if (auto ty = argType(rule.result.argIndex))
            result = *ty;
    }
    return result;
}

} // namespace il::frontends::basic::lower
