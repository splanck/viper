//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Scan_RuntimeNeeds.cpp
// Purpose: Tracks runtime feature requirements during BASIC scan passes.
// Key invariants: Traversal requests helpers and bookkeeping only; no IR
//                 emission occurs.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

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

/// @brief AST walker that records runtime helper requirements for BASIC code.
///
/// @details The scanner traverses expressions and statements, invoking helper
///          routines on @ref Lowerer to record required runtime bridges,
///          builtin helpers, and symbol bookkeeping.  It performs no IR
///          emission, making it safe to run during the scan phase.
class RuntimeNeedsScanner final : public BasicAstWalker<RuntimeNeedsScanner>
{
  public:
    /// @brief Construct a scanner bound to the lowering context.
    ///
    /// @param lowerer Lowerer used to accumulate runtime requirements.
    explicit RuntimeNeedsScanner(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Analyse a single statement and record runtime requirements.
    ///
    /// @param stmt Statement node to visit.
    void evaluateStmt(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    /// @brief Analyse an entire program, scanning declarations and main body.
    ///
    /// @param prog BASIC program containing procedure declarations and main.
    void evaluateProgram(const Program &prog)
    {
        for (const auto &decl : prog.procs)
            if (decl)
                decl->accept(*this);
        for (const auto &stmt : prog.main)
            if (stmt)
                stmt->accept(*this);
    }

    /// @brief Defer builtin call traversal to bespoke runtime logic.
    bool shouldVisitChildren(const BuiltinCallExpr &)
    {
        return false;
    }

    /// @brief Skip procedure call traversal; arguments are processed manually.
    bool shouldVisitChildren(const CallExpr &)
    {
        return false;
    }

    /// @brief Skip constructor arguments; runtime tracking occurs explicitly.
    bool shouldVisitChildren(const NewExpr &)
    {
        return false;
    }

    /// @brief Skip member access traversal; base expressions handled explicitly.
    bool shouldVisitChildren(const MemberAccessExpr &)
    {
        return false;
    }

    /// @brief Skip method call traversal; helper handles base and args.
    bool shouldVisitChildren(const MethodCallExpr &)
    {
        return false;
    }

    // Expression hooks --------------------------------------------------
    /// @brief Track runtime helpers required for array element access.
    ///
    /// @param expr Array expression encountered in rvalue position.
    void after(const ArrayExpr &expr)
    {
        if (lvalueDepth_ > 0)
            return;
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
        lowerer_.requireArrayI32Len();
        lowerer_.requireArrayI32Get();
        lowerer_.requireArrayOobPanic();
    }

    /// @brief Mark array usage and ensure LBOUND helpers are tracked.
    ///
    /// @param expr LBOUND expression referencing an array.
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Mark array usage and request runtime support for UBOUND.
    ///
    /// @param expr UBOUND expression referencing an array.
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        lowerer_.requireArrayI32Len();
    }

    /// @brief Delegate binary expression analysis to helper logic.
    ///
    /// @param expr Binary expression whose operands may require helpers.
    void after(const BinaryExpr &expr)
    {
        applyBinaryRuntimeNeeds(expr);
    }

    /// @brief Record runtime needs for builtin calls and consume arguments.
    ///
    /// @param expr Builtin call expression.
    void after(const BuiltinCallExpr &expr)
    {
        std::vector<std::optional<Lowerer::ExprType>> argTypes(expr.args.size());
        for (std::size_t i = 0; i < expr.args.size(); ++i)
        {
            const auto &arg = expr.args[i];
            if (!arg)
                continue;
            argTypes[i] = lowerer_.scanExpr(*arg);
            consumeExpr(*arg);
        }
        applyBuiltinRuntimeNeeds(expr, argTypes);
    }

    /// @brief Consume procedure call arguments to avoid leaking temporaries.
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
    }

    /// @brief Consume constructor arguments to track nested runtime needs.
    ///
    /// @param expr New-expression node whose args may require helpers.
    void after(const NewExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
    }

    /// @brief Consume the base expression of member access to maintain balance.
    ///
    /// @param expr Member access expression encountered during scanning.
    void after(const MemberAccessExpr &expr)
    {
        if (expr.base)
            consumeExpr(*expr.base);
    }

    /// @brief Consume method call base and arguments to surface nested helpers.
    ///
    /// @param expr Method call expression.
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
    }

    // Statement hooks ---------------------------------------------------

    /// @brief Ensure PRINT# statements request channel-aware runtime helpers.
    void before(const PrintChStmt &stmt)
    {
        std::size_t actualArgs = 0;
        for (const auto &arg : stmt.args)
        {
            if (arg)
                ++actualArgs;
        }

        if (stmt.mode == PrintChStmt::Mode::Write)
        {
            lowerer_.requirePrintlnChErr();
            return;
        }

        if (stmt.trailingNewline)
            lowerer_.requirePrintlnChErr();

        if (actualArgs > 0 && (!stmt.trailingNewline || actualArgs > 1))
            lowerer_.requireWriteChErr();
    }

    /// @brief Analyse PRINT# arguments and request supporting helpers.
    ///
    /// @param stmt PRINT# statement containing arguments and mode.
    void after(const PrintChStmt &stmt)
    {
        for (const auto &arg : stmt.args)
        {
            if (!arg)
                continue;
            auto ty = lowerer_.scanExpr(*arg);
            handlePrintChArg(*arg, ty, stmt.mode);
        }
        if (stmt.mode == PrintChStmt::Mode::Write && stmt.args.size() > 1)
            lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
    }

    /// @brief GOSUB needs trap handling when used with error recovery.
    void before(const GosubStmt &)
    {
        lowerer_.requireTrap();
    }

    /// @brief RETURN interacts with trap-based unwinding.
    void before(const ReturnStmt &)
    {
        lowerer_.requireTrap();
    }

    /// @brief CLS requires terminal helper support.
    void after(const ClsStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermCls);
    }

    /// @brief COLOR requires terminal colour helper support.
    void after(const ColorStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermColor);
    }

    /// @brief LOCATE requires terminal cursor helper support.
    void after(const LocateStmt &)
    {
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermLocate);
    }

    /// @brief Track runtime needs stemming from LET targets and values.
    ///
    /// @param stmt LET statement being analysed.
    void after(const LetStmt &stmt)
    {
        if (!stmt.target)
            return;
        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
            handleLetVarTarget(*var, stmt.expr.get());
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
            handleLetArrayTarget(*arr);
    }

    /// @brief Prime symbol tracking and runtime helpers for DIM statements.
    ///
    /// @param stmt DIM statement describing variable storage.
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

    /// @brief Track helpers needed for REDIM resizing operations.
    ///
    /// @param stmt REDIM statement with array metadata.
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

    /// @brief RANDOMIZE requires the random number helper.
    void before(const RandomizeStmt &)
    {
        lowerer_.trackRuntime(Lowerer::RuntimeFeature::RandomizeI64);
    }

    /// @brief Ensure FOR loop variables have inferred types for runtime helpers.
    ///
    /// @param stmt FOR loop under analysis.
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
    }

    /// @brief OPEN requires file runtime error helpers.
    void before(const OpenStmt &)
    {
        lowerer_.requireOpenErrVstr();
    }

    /// @brief CLOSE requires runtime helpers for closing file handles.
    void before(const CloseStmt &)
    {
        lowerer_.requireCloseErr();
    }

    /// @brief SEEK needs runtime helpers for repositioning file channels.
    void before(const SeekStmt &)
    {
        lowerer_.requireSeekChErr();
    }

    /// @brief Prepare runtime helpers and bookkeeping before INPUT executes.
    ///
    /// @param stmt INPUT statement referencing destination variables.
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

    /// @brief Post-process INPUT to request conversions for destination types.
    void after(const InputStmt &)
    {
        for (const auto &name : inputVarNames_)
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

            const auto *info = lowerer_.findSymbol(name);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(name, astTy);
        }
        inputVarNames_.clear();
    }

    /// @brief Prepare helpers for INPUT# channel reads.
    void before(const InputChStmt &)
    {
        lowerer_.requireLineInputChErr();
        lowerer_.requestHelper(Lowerer::RuntimeFeature::SplitFields);
        lowerer_.requireStrReleaseMaybe();
    }

    /// @brief Request conversions required after INPUT# completes.
    ///
    /// @param stmt INPUT# statement describing the destination variable.
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
        const auto *info = lowerer_.findSymbol(name);
        if (!info || !info->hasType)
            lowerer_.setSymbolType(name, astTy);
    }

    /// @brief Ensure LINE INPUT# requests error-reporting helpers.
    void before(const LineInputChStmt &)
    {
        lowerer_.requireLineInputChErr();
    }

  private:
    /// @brief Evaluate an expression solely to surface nested runtime needs.
    ///
    /// @param expr Expression subtree to visit.
    void consumeExpr(const Expr &expr)
    {
        expr.accept(*this);
    }

    /// @brief Record helpers required by binary operators such as POW or string addition.
    ///
    /// @param expr Binary expression currently being analysed.
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

    /// @brief Apply builtin-specific runtime tracking based on scan rules.
    ///
    /// @param expr Builtin expression being analysed.
    /// @param argTypes Cached argument classifications gathered earlier.
    void applyBuiltinRuntimeNeeds(const BuiltinCallExpr &expr,
                                  const std::vector<std::optional<Lowerer::ExprType>> &argTypes)
    {
        if (expr.builtin == BuiltinCallExpr::Builtin::Str && !expr.args.empty() && expr.args[0])
            lowerer_.requestHelper(
                strFeatureForNumeric(lowerer_.classifyNumericType(*expr.args[0])));

        const auto &rule = getBuiltinScanRule(expr.builtin);
        auto hasArg = [&](std::size_t idx)
        { return idx < expr.args.size() && expr.args[idx] != nullptr; };
        auto argType = [&](std::size_t idx) -> std::optional<Lowerer::ExprType>
        { return idx < argTypes.size() ? argTypes[idx] : std::nullopt; };

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

        // --- begin: ensure manual helpers for file-position builtins ---
        switch (expr.builtin)
        {
            case BuiltinCallExpr::Builtin::Eof:
                lowerer_.requireEofCh();
                break;
            case BuiltinCallExpr::Builtin::Lof:
                lowerer_.requireLofCh();
                break;
            case BuiltinCallExpr::Builtin::Loc:
                lowerer_.requireLocCh();
                break;
            default:
                break;
        }
        // --- end: ensure manual helpers for file-position builtins ---
    }

    /// @brief Request helpers needed to print an expression via PRINT#.
    ///
    /// @param expr Expression being printed.
    /// @param ty Expression classification from scan-time inference.
    /// @param mode PRINT# mode (WRITE vs PRINT) guiding formatting.
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

    /// @brief Map numeric categories to string conversion helper requirements.
    ///
    /// @param type Numeric classification determined by TypeRules.
    /// @return Runtime helper that implements the corresponding conversion.
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

    /// @brief Ensure a symbol has a known AST type, defaulting to @p fallback.
    ///
    /// @param name Symbol to update.
    /// @param fallback Type inferred from naming conventions when unresolved.
    void ensureSymbolType(const std::string &name, Type fallback)
    {
        if (const auto *info = lowerer_.findSymbol(name); !info || !info->hasType)
            lowerer_.setSymbolType(name, fallback);
    }

    /// @brief Record runtime needs when LET assigns to a variable target.
    ///
    /// @param var Target variable expression.
    /// @param value Optional value expression driving helper requirements.
    void handleLetVarTarget(const VarExpr &var, const Expr *value)
    {
        if (var.name.empty())
            return;
        if (value)
        {
            std::string className;
            if (const auto *alloc = dynamic_cast<const NewExpr *>(value))
                className = alloc->className;
            else
                className = lowerer_.resolveObjectClass(*value);
            if (!className.empty())
                lowerer_.setSymbolObjectType(var.name, className);
        }
        ensureSymbolType(var.name, inferAstTypeFromName(var.name));
        if (const auto *info = lowerer_.findSymbol(var.name); info && info->isArray)
        {
            lowerer_.requireArrayI32Retain();
            lowerer_.requireArrayI32Release();
            return;
        }
        const auto *symInfo = lowerer_.findSymbol(var.name);
        if ((symInfo ? symInfo->type : inferAstTypeFromName(var.name)) == Type::Str)
        {
            lowerer_.requireStrRetainMaybe();
            lowerer_.requireStrReleaseMaybe();
        }
    }

    /// @brief Record runtime needs when LET assigns into an array element.
    ///
    /// @param arr Array expression describing the target element.
    void handleLetArrayTarget(const ArrayExpr &arr)
    {
        if (arr.name.empty())
            return;
        ensureSymbolType(arr.name, inferAstTypeFromName(arr.name));
        lowerer_.markSymbolReferenced(arr.name);
        lowerer_.markArray(arr.name);
        lowerer_.requireArrayI32Len();
        lowerer_.requireArrayI32Set();
        lowerer_.requireArrayOobPanic();
    }

    /// @brief Track when the scanner descends into the lvalue side of LET.
    ///
    /// @param stmt LET statement currently being visited.
    /// @param child Child expression about to be visited.
    void beforeChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            ++lvalueDepth_;
    }

    /// @brief Restore lvalue tracking after visiting a LET target.
    ///
    /// @param stmt LET statement currently being visited.
    /// @param child Child expression that was visited.
    void afterChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            --lvalueDepth_;
    }

    /// @brief Ensure LINE INPUT# target variables receive string types.
    ///
    /// @param stmt LINE INPUT# statement under analysis.
    /// @param child Expression about to be visited.
    void beforeChild(const LineInputChStmt &stmt, const Expr &child)
    {
        if (!(stmt.targetVar && stmt.targetVar.get() == &child))
            return;
        if (auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get());
            var && !var->name.empty())
            lowerer_.setSymbolType(var->name, Type::Str);
    }

    Lowerer &lowerer_;
    std::vector<std::string> inputVarNames_{};
    int lvalueDepth_{0};
};

} // namespace detail

/// @brief Analyse a single statement to record runtime helper requirements.
///
/// @param lowerer Lowerer receiving runtime requirement updates.
/// @param stmt Statement to analyse.
void scanStmtRuntimeNeeds(Lowerer &lowerer, const Stmt &stmt)
{
    detail::RuntimeNeedsScanner(lowerer).evaluateStmt(stmt);
}

/// @brief Analyse an entire BASIC program to record runtime helper requirements.
///
/// @param lowerer Lowerer receiving runtime requirement updates.
/// @param prog Program containing declarations and main body statements.
void scanProgramRuntimeNeeds(Lowerer &lowerer, const Program &prog)
{
    detail::RuntimeNeedsScanner(lowerer).evaluateProgram(prog);
}

} // namespace il::frontends::basic::lower
