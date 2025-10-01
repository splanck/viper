// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements AST scanning to compute expression types and runtime requirements.
// Key invariants: Scanning only mutates bookkeeping flags; no IR emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

namespace
{

Lowerer::ExprType exprTypeFromAstType(::il::frontends::basic::Type ty)
{
    using AstType = ::il::frontends::basic::Type;
    switch (ty)
    {
        case AstType::Str:
            return Lowerer::ExprType::Str;
        case AstType::F64:
            return Lowerer::ExprType::F64;
        case AstType::Bool:
            return Lowerer::ExprType::Bool;
        case AstType::I64:
        default:
            return Lowerer::ExprType::I64;
    }
}
} // namespace

class ScanWalker final : public BasicAstWalker<ScanWalker>
{
  public:
    explicit ScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    using ExprType = Lowerer::ExprType;

    ExprType evaluateExpr(const Expr &expr)
    {
        StackScope scope(*this);
        expr.accept(*this);
        return pop();
    }

    void evaluateStmt(const Stmt &stmt)
    {
        StackScope scope(*this);
        stmt.accept(*this);
    }

    void evaluateProgram(const Program &prog)
    {
        StackScope scope(*this);
        for (const auto &decl : prog.procs)
            if (decl)
                decl->accept(*this);
        for (const auto &stmt : prog.main)
            if (stmt)
                stmt->accept(*this);
    }

    // Expression hooks --------------------------------------------------

    void after(const IntExpr &)
    {
        push(ExprType::I64);
    }

    void after(const FloatExpr &)
    {
        push(ExprType::F64);
    }

    void after(const StringExpr &)
    {
        push(ExprType::Str);
    }

    void after(const BoolExpr &)
    {
        push(ExprType::I64);
    }

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

    void after(const ArrayExpr &expr)
    {
        discardIf(expr.index != nullptr);
        if (lvalueDepth_ > 0)
        {
            push(ExprType::I64);
            return;
        }
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
        lowerer_.requireArrayI32Len();
        lowerer_.requireArrayI32Get();
        lowerer_.requireArrayOobPanic();
        push(ExprType::I64);
    }

    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        push(ExprType::I64);
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        lowerer_.requireArrayI32Len();
        push(ExprType::I64);
    }

    void after(const UnaryExpr &)
    {
        ExprType operand = pop();
        push(operand);
    }

    void after(const BinaryExpr &expr)
    {
        ExprType rhs = pop();
        ExprType lhs = pop();
        push(combineBinary(expr, lhs, rhs));
    }

    bool shouldVisitChildren(const BuiltinCallExpr &) { return false; }

    void after(const BuiltinCallExpr &expr)
    {
        push(lowerer_.scanBuiltinCallExpr(expr));
    }

    bool shouldVisitChildren(const CallExpr &) { return false; }

    void after(const CallExpr &expr)
    {
        for (const auto &arg : expr.args)
            if (arg)
                consumeExpr(*arg);
        push(ExprType::I64);
    }

    // Statement hooks ---------------------------------------------------

    void after(const PrintStmt &stmt)
    {
        for (auto it = stmt.items.rbegin(); it != stmt.items.rend(); ++it)
        {
            if (it->kind == PrintItem::Kind::Expr && it->expr)
                pop();
        }
    }

    void before(const PrintChStmt &)
    {
        lowerer_.requirePrintlnChErr();
    }

    void after(const PrintChStmt &stmt)
    {
        for (auto it = stmt.args.rbegin(); it != stmt.args.rend(); ++it)
        {
            if (!*it)
                continue;
            ExprType ty = pop();
            handlePrintChArg(**it, ty);
        }
        if (stmt.channelExpr)
            pop();
    }

    void after(const LetStmt &stmt)
    {
        if (stmt.expr)
            pop();

        if (!stmt.target)
            return;

        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
        {
            if (!var->name.empty())
            {
                const auto *info = lowerer_.findSymbol(var->name);
                if (!info || !info->hasType)
                    lowerer_.setSymbolType(var->name, inferAstTypeFromName(var->name));
                if (const auto *arrayInfo = lowerer_.findSymbol(var->name);
                    arrayInfo && arrayInfo->isArray)
                {
                    lowerer_.requireArrayI32Retain();
                    lowerer_.requireArrayI32Release();
                }
            }
        }
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
        {
            if (!arr->name.empty())
            {
                const auto *info = lowerer_.findSymbol(arr->name);
                if (!info || !info->hasType)
                    lowerer_.setSymbolType(arr->name, inferAstTypeFromName(arr->name));
                lowerer_.markSymbolReferenced(arr->name);
                lowerer_.markArray(arr->name);
            }
            lowerer_.requireArrayI32Len();
            lowerer_.requireArrayI32Set();
            lowerer_.requireArrayOobPanic();
        }

        pop();
    }

    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
        {
            lowerer_.markArray(stmt.name);
            lowerer_.requireArrayI32New();
            lowerer_.requireArrayI32Retain();
            lowerer_.requireArrayI32Release();
        }
    }

    void after(const DimStmt &stmt)
    {
        discardIf(stmt.size != nullptr);
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
        lowerer_.requireArrayI32Resize();
        lowerer_.requireArrayI32Retain();
        lowerer_.requireArrayI32Release();
    }

    void after(const ReDimStmt &stmt)
    {
        discardIf(stmt.size != nullptr);
    }

    void before(const RandomizeStmt &)
    {
        lowerer_.trackRuntime(Lowerer::RuntimeFeature::RandomizeI64);
    }

    void after(const RandomizeStmt &stmt)
    {
        discardIf(stmt.seed != nullptr);
    }

    void after(const IfStmt &stmt)
    {
        for (auto it = stmt.elseifs.rbegin(); it != stmt.elseifs.rend(); ++it)
            if (it->cond)
                pop();
        if (stmt.cond)
            pop();
    }

    void after(const WhileStmt &stmt)
    {
        discardIf(stmt.cond != nullptr);
    }

    void after(const DoStmt &stmt)
    {
        discardIf(stmt.cond != nullptr);
    }

    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
    }

    void after(const ForStmt &stmt)
    {
        if (stmt.step)
            pop();
        if (stmt.end)
            pop();
        if (stmt.start)
            pop();
    }

    void before(const OpenStmt &)
    {
        lowerer_.requireOpenErrVstr();
    }

    void after(const OpenStmt &stmt)
    {
        if (stmt.channelExpr)
            pop();
        if (stmt.pathExpr)
            pop();
    }

    void before(const CloseStmt &)
    {
        lowerer_.requireCloseErr();
    }

    void after(const CloseStmt &stmt)
    {
        discardIf(stmt.channelExpr != nullptr);
    }

    void before(const InputStmt &stmt)
    {
        lowerer_.requestHelper(Lowerer::RuntimeFeature::InputLine);
        inputVarName_ = stmt.var;
    }

    void after(const InputStmt &stmt)
    {
        discardIf(stmt.prompt != nullptr);
        if (inputVarName_.empty() || inputVarName_.back() != '$')
            lowerer_.requestHelper(Lowerer::RuntimeFeature::ToInt);
        if (!inputVarName_.empty())
        {
            const auto *info = lowerer_.findSymbol(inputVarName_);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(inputVarName_, inferAstTypeFromName(inputVarName_));
        }
        inputVarName_.clear();
    }

    void before(const LineInputChStmt &)
    {
        lowerer_.requireLineInputChErr();
    }

    void after(const LineInputChStmt &stmt)
    {
        if (stmt.targetVar)
            pop();
        if (stmt.channelExpr)
            pop();
    }

    void after(const ReturnStmt &stmt)
    {
        discardIf(stmt.value != nullptr);
    }

    void after(const FunctionDecl &)
    {
        // No expression stack adjustments required.
    }

    void after(const SubDecl &)
    {
        // No expression stack adjustments required.
    }

    void after(const StmtList &)
    {
        // No expression stack adjustments required.
    }

  private:
    struct StackScope
    {
        ScanWalker &walker;
        std::size_t depth;
        explicit StackScope(ScanWalker &w) : walker(w), depth(w.exprStack_.size()) {}
        ~StackScope()
        {
            assert(walker.exprStack_.size() == depth && "expression stack imbalance");
        }
    };

    void push(ExprType ty)
    {
        exprStack_.push_back(ty);
    }

    ExprType pop()
    {
        assert(!exprStack_.empty());
        ExprType ty = exprStack_.back();
        exprStack_.pop_back();
        return ty;
    }

    void discardIf(bool condition)
    {
        if (condition)
            (void)pop();
    }

    ExprType consumeExpr(const Expr &expr)
    {
        expr.accept(*this);
        return pop();
    }

    ExprType combineBinary(const BinaryExpr &expr, ExprType lhs, ExprType rhs)
    {
        using Op = BinaryExpr::Op;
        if (expr.op == Op::Pow)
        {
            lowerer_.trackRuntime(Lowerer::RuntimeFeature::Pow);
            return ExprType::F64;
        }
        if (expr.op == Op::Add && lhs == ExprType::Str && rhs == ExprType::Str)
        {
            lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
            return ExprType::Str;
        }
        if (expr.op == Op::Eq || expr.op == Op::Ne)
        {
            if (lhs == ExprType::Str || rhs == ExprType::Str)
                lowerer_.requestHelper(Lowerer::RuntimeFeature::StrEq);
            return ExprType::Bool;
        }
        if (expr.op == Op::LogicalAndShort || expr.op == Op::LogicalOrShort || expr.op == Op::LogicalAnd ||
            expr.op == Op::LogicalOr)
            return ExprType::Bool;
        if (lhs == ExprType::F64 || rhs == ExprType::F64)
            return ExprType::F64;
        return ExprType::I64;
    }

    void handlePrintChArg(const Expr &expr, ExprType ty)
    {
        if (ty == ExprType::Str)
            return;
        TypeRules::NumericType numericType = lowerer_.classifyNumericType(expr);
        using Feature = Lowerer::RuntimeFeature;
        switch (numericType)
        {
            case TypeRules::NumericType::Integer:
                lowerer_.requestHelper(Feature::StrFromI16);
                break;
            case TypeRules::NumericType::Long:
                lowerer_.requestHelper(Feature::StrFromI32);
                break;
            case TypeRules::NumericType::Single:
                lowerer_.requestHelper(Feature::StrFromSingle);
                break;
            case TypeRules::NumericType::Double:
            default:
                lowerer_.requestHelper(Feature::StrFromDouble);
                break;
        }
    }

    void beforeChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            ++lvalueDepth_;
    }

    void afterChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            --lvalueDepth_;
    }

    void beforeChild(const LineInputChStmt &stmt, const Expr &child)
    {
        if (stmt.targetVar && stmt.targetVar.get() == &child)
        {
            if (auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get()))
            {
                if (!var->name.empty())
                    lowerer_.setSymbolType(var->name, Type::Str);
            }
        }
    }

    Lowerer &lowerer_;
    std::vector<ExprType> exprStack_;
    std::string inputVarName_{};
    int lvalueDepth_{0};
};

/// @brief Scans a BASIC builtin call using declarative metadata.
/// @param c Builtin call expression to inspect.
/// @return The inferred result type of the builtin invocation.
/// @details Applies the BuiltinScanRule for the builtin to traverse arguments and
/// request any runtime helpers required by the call.
Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &c)
{
    const auto &rule = getBuiltinScanRule(c.builtin);
    std::vector<std::optional<ExprType>> argTypes(c.args.size());

    if (c.builtin == BuiltinCallExpr::Builtin::Str && !c.args.empty() && c.args[0])
    {
        TypeRules::NumericType numericType = classifyNumericType(*c.args[0]);
        switch (numericType)
        {
            case TypeRules::NumericType::Integer:
                requestHelper(RuntimeFeature::StrFromI16);
                break;
            case TypeRules::NumericType::Long:
                requestHelper(RuntimeFeature::StrFromI32);
                break;
            case TypeRules::NumericType::Single:
                requestHelper(RuntimeFeature::StrFromSingle);
                break;
            case TypeRules::NumericType::Double:
            default:
                requestHelper(RuntimeFeature::StrFromDouble);
                break;
        }
    }

    auto scanArg = [&](std::size_t idx) {
        if (idx >= c.args.size())
            return;
        const auto &arg = c.args[idx];
        if (!arg)
            return;
        ScanWalker walker(*this);
        argTypes[idx] = walker.evaluateExpr(*arg);
    };

    if (rule.traversal == BuiltinScanRule::ArgTraversal::All)
    {
        for (std::size_t i = 0; i < c.args.size(); ++i)
            scanArg(i);
    }
    else
    {
        for (std::size_t idx : rule.explicitArgs)
            scanArg(idx);
    }

    auto hasArg = [&](std::size_t idx) {
        return idx < c.args.size() && c.args[idx] != nullptr;
    };

    auto argType = [&](std::size_t idx) -> std::optional<ExprType> {
        if (idx >= argTypes.size())
            return std::nullopt;
        return argTypes[idx];
    };

    using Feature = BuiltinScanRule::Feature;
    for (const auto &feature : rule.features)
    {
        bool apply = false;
        switch (feature.condition)
        {
            case Feature::Condition::Always:
                apply = true;
                break;
            case Feature::Condition::IfArgPresent:
                apply = hasArg(feature.argIndex);
                break;
            case Feature::Condition::IfArgMissing:
                apply = !hasArg(feature.argIndex);
                break;
            case Feature::Condition::IfArgTypeIs:
            {
                auto ty = argType(feature.argIndex);
                apply = ty && *ty == feature.type;
                break;
            }
            case Feature::Condition::IfArgTypeIsNot:
            {
                auto ty = argType(feature.argIndex);
                apply = ty && *ty != feature.type;
                break;
            }
        }

        if (!apply)
            continue;

        switch (feature.action)
        {
            case Feature::Action::Request:
                requestHelper(feature.feature);
                break;
            case Feature::Action::Track:
                trackRuntime(feature.feature);
                break;
        }
    }

    ExprType result = rule.result.type;
    if (rule.result.kind == BuiltinScanRule::ResultSpec::Kind::FromArg)
    {
        if (auto ty = argType(rule.result.argIndex))
            result = *ty;
    }
    return result;
}

Lowerer::ExprType Lowerer::scanExpr(const Expr &e)
{
    ScanWalker walker(*this);
    return walker.evaluateExpr(e);
}

void Lowerer::scanStmt(const Stmt &s)
{
    ScanWalker walker(*this);
    walker.evaluateStmt(s);
}

void Lowerer::scanProgram(const Program &prog)
{
    ScanWalker walker(*this);
    walker.evaluateProgram(prog);
}

} // namespace il::frontends::basic
