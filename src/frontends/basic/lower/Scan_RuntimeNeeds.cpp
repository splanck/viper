// File: src/frontends/basic/lower/Scan_RuntimeNeeds.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Tracks runtime feature requirements during BASIC scan passes.
// Key invariants: Traversal requests helpers and bookkeeping only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeRules.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic::lower
{
namespace detail
{

class RuntimeNeedsScanner final : public BasicAstWalker<RuntimeNeedsScanner>
{
  public:
    explicit RuntimeNeedsScanner(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void evaluateStmt(const Stmt &stmt) { stmt.accept(*this); }
    void evaluateProgram(const Program &prog)
    {
        for (const auto &decl : prog.procs)
            if (decl) decl->accept(*this);
        for (const auto &stmt : prog.main)
            if (stmt) stmt->accept(*this);
    }
    bool shouldVisitChildren(const BuiltinCallExpr &) { return false; }
    bool shouldVisitChildren(const CallExpr &) { return false; }
    bool shouldVisitChildren(const NewExpr &) { return false; }
    bool shouldVisitChildren(const MemberAccessExpr &) { return false; }
    bool shouldVisitChildren(const MethodCallExpr &) { return false; }
    // Expression hooks --------------------------------------------------
    void after(const ArrayExpr &expr)
    {
        if (lvalueDepth_ > 0) return;
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
        lowerer_.requireArrayI32Len(); lowerer_.requireArrayI32Get(); lowerer_.requireArrayOobPanic();
    }
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        lowerer_.requireArrayI32Len();
    }
    void after(const BinaryExpr &expr) { applyBinaryRuntimeNeeds(expr); }
    void after(const BuiltinCallExpr &expr)
    {
        std::vector<std::optional<Lowerer::ExprType>> argTypes(expr.args.size());
        for (std::size_t i = 0; i < expr.args.size(); ++i)
        {
            const auto &arg = expr.args[i];
            if (!arg) continue;
            argTypes[i] = lowerer_.scanExpr(*arg);
            consumeExpr(*arg);
        }
        applyBuiltinRuntimeNeeds(expr, argTypes);
    }
    void after(const CallExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg) continue;
            consumeExpr(*arg);
        }
    }
    void after(const NewExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg) continue;
            consumeExpr(*arg);
        }
    }
    void after(const MemberAccessExpr &expr) { if (expr.base) consumeExpr(*expr.base); }

    void after(const MethodCallExpr &expr)
    {
        if (expr.base) consumeExpr(*expr.base);
        for (const auto &arg : expr.args)
        {
            if (!arg) continue;
            consumeExpr(*arg);
        }
    }
    // Statement hooks ---------------------------------------------------

    void before(const PrintChStmt &) { lowerer_.requirePrintlnChErr(); }
    void after(const PrintChStmt &stmt)
    {
        for (const auto &arg : stmt.args)
        {
            if (!arg) continue;
            auto ty = lowerer_.scanExpr(*arg);
            handlePrintChArg(*arg, ty, stmt.mode);
        }
        if (stmt.mode == PrintChStmt::Mode::Write && stmt.args.size() > 1)
            lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
    }
    void before(const GosubStmt &) { lowerer_.requireTrap(); }
    void before(const ReturnStmt &) { lowerer_.requireTrap(); }
    void after(const ClsStmt &) { lowerer_.requestHelper(il::runtime::RuntimeFeature::TermCls); }
    void after(const ColorStmt &) { lowerer_.requestHelper(il::runtime::RuntimeFeature::TermColor); }
    void after(const LocateStmt &) { lowerer_.requestHelper(il::runtime::RuntimeFeature::TermLocate); }
    void after(const LetStmt &stmt)
    {
        if (!stmt.target) return;
        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
            handleLetVarTarget(*var, stmt.expr.get());
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
            handleLetArrayTarget(*arr);
    }
    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty()) return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
        {
            lowerer_.markArray(stmt.name);
            lowerer_.requireArrayI32New(); lowerer_.requireArrayI32Retain(); lowerer_.requireArrayI32Release();
        }
    }
    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty()) return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
        lowerer_.requireArrayI32Resize(); lowerer_.requireArrayI32Retain(); lowerer_.requireArrayI32Release();
    }
    void before(const RandomizeStmt &) { lowerer_.trackRuntime(Lowerer::RuntimeFeature::RandomizeI64); }
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType) lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
    }
    void before(const OpenStmt &) { lowerer_.requireOpenErrVstr(); }

    void before(const CloseStmt &) { lowerer_.requireCloseErr(); }

    void before(const SeekStmt &) { lowerer_.requireSeekChErr(); }

    void before(const InputStmt &stmt)
    {
        lowerer_.requestHelper(Lowerer::RuntimeFeature::InputLine);
        if (stmt.vars.size() > 1)
        {
            lowerer_.requestHelper(Lowerer::RuntimeFeature::SplitFields);
            lowerer_.requireStrReleaseMaybe();
        }
        inputVarNames_ = stmt.vars;
    }
    void after(const InputStmt &)
    {
        for (const auto &name : inputVarNames_)
        {
            if (name.empty()) continue;
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

            const auto *info = lowerer_.findSymbol(name);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(name, astTy);
        }
        inputVarNames_.clear();
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
        if (name.empty()) return;

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
        const auto *info = lowerer_.findSymbol(name);
        if (!info || !info->hasType)
            lowerer_.setSymbolType(name, astTy);
    }
    void before(const LineInputChStmt &) { lowerer_.requireLineInputChErr(); }

  private:
    void consumeExpr(const Expr &expr) { expr.accept(*this); }

    void applyBinaryRuntimeNeeds(const BinaryExpr &expr)
    {
        using Op = BinaryExpr::Op;
        if (expr.op == Op::Pow)
        {
            lowerer_.trackRuntime(Lowerer::RuntimeFeature::Pow);
            return;
        }
        if (expr.op == Op::Add)
        {
            auto lhsType = expr.lhs ? lowerer_.scanExpr(*expr.lhs) : Lowerer::ExprType::I64;
            auto rhsType = expr.rhs ? lowerer_.scanExpr(*expr.rhs) : Lowerer::ExprType::I64;
            if (lhsType == Lowerer::ExprType::Str && rhsType == Lowerer::ExprType::Str)
                lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
            return;
        }
        if (expr.op == Op::Eq || expr.op == Op::Ne)
        {
            auto lhsType = expr.lhs ? lowerer_.scanExpr(*expr.lhs) : Lowerer::ExprType::I64;
            auto rhsType = expr.rhs ? lowerer_.scanExpr(*expr.rhs) : Lowerer::ExprType::I64;
            if (lhsType == Lowerer::ExprType::Str || rhsType == Lowerer::ExprType::Str)
                lowerer_.requestHelper(Lowerer::RuntimeFeature::StrEq);
        }
    }
    void applyBuiltinRuntimeNeeds(const BuiltinCallExpr &expr,
                                  const std::vector<std::optional<Lowerer::ExprType>> &argTypes)
    {
        if (expr.builtin == BuiltinCallExpr::Builtin::Str && !expr.args.empty() && expr.args[0])
            lowerer_.requestHelper(strFeatureForNumeric(lowerer_.classifyNumericType(*expr.args[0])));

        const auto &rule = getBuiltinScanRule(expr.builtin);
        auto hasArg = [&](std::size_t idx) { return idx < expr.args.size() && expr.args[idx] != nullptr; };
        auto argType = [&](std::size_t idx) -> std::optional<Lowerer::ExprType> {
            return idx < argTypes.size() ? argTypes[idx] : std::nullopt;
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
    void handlePrintChArg(const Expr &expr, Lowerer::ExprType ty, PrintChStmt::Mode mode)
    {
        if (ty == Lowerer::ExprType::Str)
        {
            if (mode == PrintChStmt::Mode::Write)
                lowerer_.requestHelper(Lowerer::RuntimeFeature::CsvQuote);
            return;
        }
        lowerer_.requestHelper(strFeatureForNumeric(lowerer_.classifyNumericType(expr)));
    }
    static Lowerer::RuntimeFeature strFeatureForNumeric(TypeRules::NumericType type)
    {
        using Feature = Lowerer::RuntimeFeature;
        static constexpr std::array<Feature, 4> kMap{
            Feature::StrFromI16,
            Feature::StrFromI32,
            Feature::StrFromSingle,
            Feature::StrFromDouble,
        };
        auto idx = static_cast<std::size_t>(type);
        return idx < kMap.size() ? kMap[idx] : Feature::StrFromDouble;
    }
    void ensureSymbolType(const std::string &name, Type fallback)
    {
        if (const auto *info = lowerer_.findSymbol(name); !info || !info->hasType)
            lowerer_.setSymbolType(name, fallback);
    }
    void handleLetVarTarget(const VarExpr &var, const Expr *value)
    {
        if (var.name.empty()) return;
        if (value)
        {
            std::string className;
            if (const auto *alloc = dynamic_cast<const NewExpr *>(value))
                className = alloc->className;
            else
                className = lowerer_.resolveObjectClass(*value);
            if (!className.empty()) lowerer_.setSymbolObjectType(var.name, className);
        }
        ensureSymbolType(var.name, inferAstTypeFromName(var.name));
        if (const auto *info = lowerer_.findSymbol(var.name); info && info->isArray)
        {
            lowerer_.requireArrayI32Retain(); lowerer_.requireArrayI32Release();
            return;
        }
        const auto *symInfo = lowerer_.findSymbol(var.name);
        if ((symInfo ? symInfo->type : inferAstTypeFromName(var.name)) == Type::Str)
        {
            lowerer_.requireStrRetainMaybe(); lowerer_.requireStrReleaseMaybe();
        }
    }
    void handleLetArrayTarget(const ArrayExpr &arr)
    {
        if (arr.name.empty()) return;
        ensureSymbolType(arr.name, inferAstTypeFromName(arr.name));
        lowerer_.markSymbolReferenced(arr.name);
        lowerer_.markArray(arr.name);
        lowerer_.requireArrayI32Len(); lowerer_.requireArrayI32Set(); lowerer_.requireArrayOobPanic();
    }
    void beforeChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child) ++lvalueDepth_;
    }
    void afterChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child) --lvalueDepth_;
    }
    void beforeChild(const LineInputChStmt &stmt, const Expr &child)
    {
        if (!(stmt.targetVar && stmt.targetVar.get() == &child)) return;
        if (auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get()); var && !var->name.empty())
            lowerer_.setSymbolType(var->name, Type::Str);
    }
    Lowerer &lowerer_;
    std::vector<std::string> inputVarNames_{};
    int lvalueDepth_{0};
};

} // namespace detail

void scanStmtRuntimeNeeds(Lowerer &lowerer, const Stmt &stmt) { detail::RuntimeNeedsScanner(lowerer).evaluateStmt(stmt); }
void scanProgramRuntimeNeeds(Lowerer &lowerer, const Program &prog) { detail::RuntimeNeedsScanner(lowerer).evaluateProgram(prog); }

} // namespace il::frontends::basic::lower
