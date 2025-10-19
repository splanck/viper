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
#include "il/runtime/RuntimeSignatures.hpp"
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

    // NOTE: CallExpr always pushes exactly one stack slot in the scan pass.
    // When a call appears in statement position (CallStmt), we must discard
    // that slot in `after(const CallStmt&)` to keep the stack balanced.
    // Do not change one without the other.
    void after(const CallExpr &expr)
    {
        for (const auto &arg : expr.args)
            if (arg)
                consumeExpr(*arg);
        if (const auto *sig = lowerer_.findProcSignature(expr.callee))
        {
            using K = il::core::Type::Kind;
            switch (sig->retType.kind)
            {
                case K::F64: push(ExprType::F64); break;
                case K::Ptr: // arrays decayed to ptr or string runtime handles
                case K::I1:
                case K::I16:
                case K::I32:
                case K::I64: push(ExprType::I64); break;
                case K::Str: push(ExprType::Str); break;
                case K::Void:
                default:     push(ExprType::I64); break; // SUB in expr is illegal; semantics catches it
            }
        }
        else
        {
            push(ExprType::I64);
        }
    }
    bool shouldVisitChildren(const NewExpr &) { return false; }

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

    void after(const MeExpr &)
    {
        push(ExprType::I64);
    }

    bool shouldVisitChildren(const MemberAccessExpr &) { return false; }

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

    bool shouldVisitChildren(const MethodCallExpr &) { return false; }

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
            handlePrintChArg(**it, ty, stmt.mode);
        }
        if (stmt.mode == PrintChStmt::Mode::Write && stmt.args.size() > 1)
        {
            lowerer_.requestHelper(Lowerer::RuntimeFeature::Concat);
        }
        if (stmt.channelExpr)
            pop();
    }

    // NOTE: CallExpr always pushes exactly one stack slot in the scan pass.
    // When a call appears in statement position (CallStmt), we must discard
    // that slot in `after(const CallStmt&)` to keep the stack balanced.
    // Do not change one without the other.
    void after(const CallStmt &stmt)
    {
        discardIf(stmt.call != nullptr);
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
        // IMPORTANT: Externs for runtime helpers are emitted between scan and emit.
        // If we only request helpers during emission, the verifier can see 'call'
        // to an undeclared callee. Therefore, mark terminal helpers as required
        // here in scan().
        // CLS needs terminal clear helper available at declareRequiredRuntime().
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermCls);
    }

    void after(const ColorStmt &stmt)
    {
        discardIf(stmt.bg != nullptr);
        discardIf(stmt.fg != nullptr);
        // IMPORTANT: Externs for runtime helpers are emitted between scan and emit.
        // If we only request helpers during emission, the verifier can see 'call'
        // to an undeclared callee. Therefore, mark terminal helpers as required
        // here in scan(). Ensure terminal color helper is declared up front.
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermColor);
    }

    void after(const LocateStmt &stmt)
    {
        discardIf(stmt.col != nullptr);
        discardIf(stmt.row != nullptr);
        // IMPORTANT: Externs for runtime helpers are emitted between scan and emit.
        // If we only request helpers during emission, the verifier can see 'call'
        // to an undeclared callee. Therefore, mark terminal helpers as required
        // here in scan(). Ensure cursor locate helper is declared up front.
        lowerer_.requestHelper(il::runtime::RuntimeFeature::TermLocate);
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
                if (stmt.expr)
                {
                    std::string className;
                    if (const auto *alloc = dynamic_cast<const NewExpr *>(stmt.expr.get()))
                    {
                        className = alloc->className;
                    }
                    else
                    {
                        className = lowerer_.resolveObjectClass(*stmt.expr);
                    }
                    if (!className.empty())
                        lowerer_.setSymbolObjectType(var->name, className);
                }
                const auto *info = lowerer_.findSymbol(var->name);
                if (!info || !info->hasType)
                    lowerer_.setSymbolType(var->name, inferAstTypeFromName(var->name));
                if (const auto *arrayInfo = lowerer_.findSymbol(var->name);
                    arrayInfo && arrayInfo->isArray)
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

    void after(const SelectCaseStmt &stmt)
    {
        discardIf(stmt.selector != nullptr);
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

    void before(const SeekStmt &)
    {
        lowerer_.requireSeekChErr();
    }

    void after(const SeekStmt &stmt)
    {
        discardIf(stmt.positionExpr != nullptr);
        discardIf(stmt.channelExpr != nullptr);
    }

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

    void after(const InputStmt &stmt)
    {
        discardIf(stmt.prompt != nullptr);
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
        const auto *info = lowerer_.findSymbol(name);
        if (!info || !info->hasType)
            lowerer_.setSymbolType(name, astTy);
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

    void after(const DeleteStmt &stmt)
    {
        discardIf(stmt.target != nullptr);
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

    void handlePrintChArg(const Expr &expr, ExprType ty, PrintChStmt::Mode mode)
    {
        if (ty == ExprType::Str)
        {
            if (mode == PrintChStmt::Mode::Write)
                lowerer_.requestHelper(Lowerer::RuntimeFeature::CsvQuote);
            return;
        }
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
    std::vector<std::string> inputVarNames_{};
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
