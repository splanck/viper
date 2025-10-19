// File: src/frontends/basic/lower/Scan_RuntimeNeeds.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Tracks runtime feature requirements during BASIC scan.
// Key invariants: Scan sets flags only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

namespace scan_detail
{
Lowerer::ExprType scanExprType(Lowerer &, const Expr &);

class RuntimeNeedsWalker final : public BasicAstWalker<RuntimeNeedsWalker>
{
  public:
    explicit RuntimeNeedsWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void evaluateStmt(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    void evaluateProgram(const Program &prog)
    {
        for (const auto &decl : prog.procs)
        {
            if (decl)
                decl->accept(*this);
        }
        for (const auto &stmt : prog.main)
        {
            if (stmt)
                stmt->accept(*this);
        }
    }

    void after(const ArrayExpr &expr)
    {
        if (lvalueDepth_ > 0)
            return;
        if (!expr.name.empty())
        {
            lowerer_.requireArrayI32Len();
            lowerer_.requireArrayI32Get();
            lowerer_.requireArrayOobPanic();
        }
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.requireArrayI32Len();
    }

    void after(const BinaryExpr &expr)
    {
        if (!expr.lhs || !expr.rhs)
            return;
        auto lhsTy = scanExpr(*expr.lhs);
        auto rhsTy = scanExpr(*expr.rhs);
        handleBinaryRuntimeNeeds(expr, lhsTy, rhsTy);
    }

    void after(const BuiltinCallExpr &expr)
    {
        applyBuiltinRuntimeNeeds(expr);
    }

    void before(const PrintChStmt &)
    {
        lowerer_.requirePrintlnChErr();
    }

    void after(const PrintChStmt &stmt)
    {
        if (stmt.mode == PrintChStmt::Mode::Write && stmt.args.size() > 1)
            lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
        for (const auto &arg : stmt.args)
        {
            if (!arg)
                continue;
            auto ty = scanExpr(*arg);
            handlePrintChArg(*arg, ty, stmt.mode);
        }
    }

    void before(const GosubStmt &)
    {
        lowerer_.requireTrap();
    }

    void before(const ReturnStmt &)
    {
        lowerer_.requireTrap();
    }

    void after(const ClsStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermCls);
    }

    void after(const ColorStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermColor);
    }

    void after(const LocateStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermLocate);
    }

    void after(const LetStmt &stmt)
    {
        if (!stmt.target)
            return;

        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
        {
            if (!var->name.empty())
            {
                if (const auto *arrayInfo = lowerer_.findSymbol(var->name); arrayInfo && arrayInfo->isArray)
                {
                    lowerer_.requireArrayI32Retain();
                    lowerer_.requireArrayI32Release();
                }
                else
                {
                    const auto *symInfo = lowerer_.findSymbol(var->name);
                    Type symType = symInfo ? symInfo->type : inferAstTypeFromName(var->name);
                    if (symType == Type::Str)
                    {
                        lowerer_.requireStrRetainMaybe();
                        lowerer_.requireStrReleaseMaybe();
                    }
                }
            }
        }
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
        {
            if (!arr->name.empty())
            {
                lowerer_.requireArrayI32Len();
                lowerer_.requireArrayI32Set();
                lowerer_.requireArrayOobPanic();
            }
        }
    }

    void before(const DimStmt &stmt)
    {
        if (!stmt.isArray)
            return;
        lowerer_.requireArrayI32New();
        lowerer_.requireArrayI32Retain();
        lowerer_.requireArrayI32Release();
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.requireArrayI32Resize();
        lowerer_.requireArrayI32Retain();
        lowerer_.requireArrayI32Release();
    }

    void before(const RandomizeStmt &)
    {
        lowerer_.trackRuntime(Lowerer::RuntimeFeature::RandomizeI64);
    }

    void before(const OpenStmt &)
    {
        lowerer_.requireOpenErrVstr();
    }

    void before(const CloseStmt &)
    {
        lowerer_.requireCloseErr();
    }

    void before(const SeekStmt &)
    {
        lowerer_.requireSeekChErr();
    }

    void before(const InputStmt &stmt)
    {
        lowerer_.requestHelper(Lowerer::RuntimeFeature::InputLine);
        if (stmt.vars.size() > 1)
        {
            lowerer_.requestHelper(Lowerer::RuntimeFeature::SplitFields);
            lowerer_.requireStrReleaseMaybe();
        }
    }

    void after(const InputStmt &stmt)
    {
        for (const auto &name : stmt.vars)
        {
            if (name.empty())
                continue;
            Type astTy = inferAstTypeFromName(name);
            switch (astTy)
            {
                case Type::Str:
                    break;
                case Type::F64:
                    lowerer_.requestHelper(Lowerer::RuntimeFeature::ToDouble);
                    lowerer_.requireStrReleaseMaybe();
                    break;
                default:
                    lowerer_.requestHelper(Lowerer::RuntimeFeature::ToInt);
                    lowerer_.requireStrReleaseMaybe();
                    break;
            }
        }
    }

    void before(const InputChStmt &)
    {
        lowerer_.requireLineInputChErr();
        lowerer_.requestHelper(Lowerer::RuntimeFeature::SplitFields);
        lowerer_.requireStrReleaseMaybe();
    }

    void after(const InputChStmt &stmt)
    {
        const auto &name = stmt.target.name;
        if (name.empty())
            return;
        Type astTy = inferAstTypeFromName(name);
        switch (astTy)
        {
            case Type::Str:
                break;
            case Type::F64:
                lowerer_.requestHelper(Lowerer::RuntimeFeature::ParseDouble);
                lowerer_.requestHelper(Lowerer::RuntimeFeature::Val);
                break;
            default:
                lowerer_.requestHelper(Lowerer::RuntimeFeature::ParseInt64);
                lowerer_.requestHelper(Lowerer::RuntimeFeature::Val);
                break;
        }
    }

    void before(const LineInputChStmt &)
    {
        lowerer_.requireLineInputChErr();
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

  private:
    [[nodiscard]] Lowerer::ExprType scanExpr(const Expr &expr)
    {
        return scanExprType(lowerer_, expr);
    }

    void handleBinaryRuntimeNeeds(const BinaryExpr &expr,
                                  Lowerer::ExprType lhs,
                                  Lowerer::ExprType rhs)
    {
        using Feature = Lowerer::RuntimeFeature;
        using Op = BinaryExpr::Op;
        if (expr.op == Op::Pow)
        {
            lowerer_.trackRuntime(Feature::Pow);
            return;
        }
        if (expr.op == Op::Add && lhs == Lowerer::ExprType::Str && rhs == Lowerer::ExprType::Str)
        {
            lowerer_.requestHelper(Feature::Concat);
            return;
        }
        if (expr.op == Op::Eq || expr.op == Op::Ne)
        {
            if (lhs == Lowerer::ExprType::Str || rhs == Lowerer::ExprType::Str)
                lowerer_.requestHelper(Feature::StrEq);
            return;
        }
    }

    void handlePrintChArg(const Expr &expr, Lowerer::ExprType ty, PrintChStmt::Mode mode)
    {
        using Feature = Lowerer::RuntimeFeature;
        if (ty == Lowerer::ExprType::Str)
        {
            if (mode == PrintChStmt::Mode::Write)
                lowerer_.requestHelper(Feature::CsvQuote);
            return;
        }
        TypeRules::NumericType numericType = lowerer_.classifyNumericType(expr);
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

    void applyBuiltinRuntimeNeeds(const BuiltinCallExpr &c)
    {
        const auto &rule = getBuiltinScanRule(c.builtin);
        std::vector<std::optional<Lowerer::ExprType>> argTypes(c.args.size());

        auto scanArg = [&](std::size_t idx) {
            if (idx >= c.args.size())
                return;
            const auto &arg = c.args[idx];
            if (!arg)
                return;
            argTypes[idx] = scanExpr(*arg);
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

        if (c.builtin == BuiltinCallExpr::Builtin::Str && !c.args.empty() && c.args[0])
        {
            TypeRules::NumericType numericType = lowerer_.classifyNumericType(*c.args[0]);
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

        auto hasArg = [&](std::size_t idx) {
            return idx < c.args.size() && c.args[idx] != nullptr;
        };

        auto argType = [&](std::size_t idx) -> std::optional<Lowerer::ExprType> {
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
                    lowerer_.requestHelper(feature.feature);
                    break;
                case Feature::Action::Track:
                    lowerer_.trackRuntime(feature.feature);
                    break;
            }
        }
    }

    Lowerer &lowerer_;
    int lvalueDepth_{0};
};

void scanStmtRuntimeNeeds(Lowerer &lowerer, const Stmt &stmt)
{
    RuntimeNeedsWalker walker(lowerer);
    walker.evaluateStmt(stmt);
}

void scanProgramRuntimeNeeds(Lowerer &lowerer, const Program &prog)
{
    RuntimeNeedsWalker walker(lowerer);
    walker.evaluateProgram(prog);
}

} // namespace scan_detail

} // namespace il::frontends::basic
