//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Semantic_OOP_Helpers.cpp
//
// Summary:
//   Implements helper functions and AST walkers used during OOP semantic
//   analysis. This includes:
//   - Member shadowing detection (locals shadowing class fields)
//   - ME keyword validation in static contexts
//   - Return statement analysis for methods
//   - Utility functions for qualified name handling
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/detail/Semantic_OOP_Internal.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/StringUtils.hpp"

namespace il::frontends::basic::detail
{

namespace
{

//===----------------------------------------------------------------------===//
// AST Walkers
//===----------------------------------------------------------------------===//

/// @brief Walker to detect local variables that shadow class fields.
class MemberShadowCheckWalker final : public BasicAstWalker<MemberShadowCheckWalker>
{
  public:
    MemberShadowCheckWalker(const std::string &className,
                            const std::unordered_set<std::string> &fields,
                            DiagnosticEmitter *emitter) noexcept
        : className_(className), fields_(fields), emitter_(emitter)
    {
    }

    void before(const DimStmt &stmt)
    {
        if (!emitter_ || stmt.name.empty())
            return;
        if (!fields_.contains(stmt.name))
            return;

        std::string qualifiedField = className_;
        if (!qualifiedField.empty())
            qualifiedField += '.';
        qualifiedField += stmt.name;

        std::string msg = "local '" + stmt.name + "' shadows field '" + qualifiedField +
                          "'; use Me." + stmt.name + " to access the field";
        emitter_->emit(il::support::Severity::Warning,
                       "B2016",
                       stmt.loc,
                       static_cast<uint32_t>(stmt.name.size()),
                       std::move(msg));
    }

  private:
    std::string className_;
    const std::unordered_set<std::string> &fields_;
    DiagnosticEmitter *emitter_;
};

/// @brief Walker to detect use of 'ME' in static contexts.
class MeUseWalker : public BasicAstWalker<MeUseWalker>
{
  public:
    DiagnosticEmitter *em;
    const char *errorCode;
    const char *message;

    MeUseWalker(DiagnosticEmitter *e, const char *code, const char *msg)
        : em(e), errorCode(code), message(msg)
    {
    }

    void visit(const MeExpr &expr)
    {
        if (!em)
            return;
        em->emit(il::support::Severity::Error, errorCode, expr.loc, 1, message);
    }
};

//===----------------------------------------------------------------------===//
// Return Analysis Helpers
//===----------------------------------------------------------------------===//

/// @brief Check if a statement definitely returns a value.
[[nodiscard]] bool methodMustReturn(const Stmt &stmt)
{
    if (const auto *lst = as<const StmtList>(stmt))
        return !lst->stmts.empty() && methodMustReturn(*lst->stmts.back());
    if (const auto *ret = as<const ReturnStmt>(stmt))
        return ret->value != nullptr;
    if (const auto *ifs = as<const IfStmt>(stmt))
    {
        if (!ifs->then_branch || !methodMustReturn(*ifs->then_branch))
            return false;
        for (const auto &elifBranch : ifs->elseifs)
            if (!elifBranch.then_branch || !methodMustReturn(*elifBranch.then_branch))
                return false;
        if (!ifs->else_branch)
            return false;
        return methodMustReturn(*ifs->else_branch);
    }
    if (is<WhileStmt>(stmt) || is<ForStmt>(stmt))
        return false;
    return false;
}

} // namespace

//===----------------------------------------------------------------------===//
// Public Helper Functions
//===----------------------------------------------------------------------===//

void checkMemberShadowing(const std::vector<StmtPtr> &body,
                          const ClassDecl &klass,
                          const std::unordered_set<std::string> &fieldNames,
                          DiagnosticEmitter *emitter)
{
    if (!emitter || fieldNames.empty())
        return;

    MemberShadowCheckWalker walker(klass.name, fieldNames, emitter);
    for (const auto &stmt : body)
        if (stmt)
            walker.walkStmt(*stmt);
}

void checkMeInStaticContext(const std::vector<StmtPtr> &body,
                            DiagnosticEmitter *emitter,
                            const char *errorCode,
                            const char *message)
{
    if (!emitter)
        return;
    MeUseWalker walker(emitter, errorCode, message);
    for (const auto &s : body)
        if (s)
            walker.walkStmt(*s);
}

bool methodBodyMustReturn(const std::vector<StmtPtr> &stmts)
{
    if (stmts.empty())
        return false;
    const StmtPtr &tail = stmts.back();
    return tail && methodMustReturn(*tail);
}

bool methodHasImplicitReturn(const MethodDecl &method)
{
    auto isNameAssign = [&](const Stmt &s) -> bool
    {
        if (auto *let = as<const LetStmt>(s))
        {
            if (let->target)
            {
                if (auto *v = as<VarExpr>(*let->target))
                    return string_utils::iequals(v->name, method.name);
            }
        }
        return false;
    };
    std::function<bool(const Stmt &)> walk = [&](const Stmt &s) -> bool
    {
        if (isNameAssign(s))
            return true;
        if (auto *list = as<const StmtList>(s))
        {
            for (const auto &sp : list->stmts)
                if (sp && walk(*sp))
                    return true;
        }
        if (auto *ifs = as<const IfStmt>(s))
        {
            if (ifs->then_branch && walk(*ifs->then_branch))
                return true;
            for (const auto &e : ifs->elseifs)
                if (e.then_branch && walk(*e.then_branch))
                    return true;
            if (ifs->else_branch && walk(*ifs->else_branch))
                return true;
        }
        return false;
    };
    for (const auto &sp : method.body)
        if (sp && walk(*sp))
            return true;
    return false;
}

void emitMissingReturn(const ClassDecl &klass, const MethodDecl &method, DiagnosticEmitter *emitter)
{
    if (!emitter)
        return;
    if (!method.ret)
        return;
    if (methodBodyMustReturn(method.body))
        return;
    if (methodHasImplicitReturn(method))
        return;

    std::string qualified = klass.name;
    if (!qualified.empty())
        qualified += '.';
    qualified += method.name;

    std::string msg = "missing return in FUNCTION " + qualified;
    emitter->emit(il::support::Severity::Error, "B1007", method.loc, 3, std::move(msg));
}

std::string joinQualified(const std::vector<std::string> &segs)
{
    std::string out;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (i)
            out.push_back('.');
        out += segs[i];
    }
    return out;
}

} // namespace il::frontends::basic::detail
