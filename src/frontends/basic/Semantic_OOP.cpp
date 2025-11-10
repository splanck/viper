//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/Semantic_OOP.cpp
//
// Summary:
//   Implements the semantic index used by the BASIC front end to store
//   information about classes declared in the source program.  The translation
//   unit builds and queries the index so later lowering phases can reason about
//   constructors, destructors, fields, and method signatures without repeatedly
//   walking the AST.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief OOP index helpers for the BASIC front end.
/// @details Provides lookup methods over @ref OopIndex and the
///          @ref buildOopIndex routine that populates the index by scanning the
///          parsed BASIC program.  The helpers copy relevant metadata from the
///          AST into stable structures, enabling deterministic queries during
///          lowering passes.

#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/AST.hpp"
#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include "support/diagnostics.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace il::frontends::basic
{

namespace
{
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
        if (!fields_.count(stmt.name))
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

[[nodiscard]] bool methodMustReturn(const Stmt &stmt)
{
    if (auto *lst = dynamic_cast<const StmtList *>(&stmt))
        return !lst->stmts.empty() && methodMustReturn(*lst->stmts.back());
    if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt))
        return ret->value != nullptr;
    if (auto *ifs = dynamic_cast<const IfStmt *>(&stmt))
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
    if (dynamic_cast<const WhileStmt *>(&stmt) != nullptr ||
        dynamic_cast<const ForStmt *>(&stmt) != nullptr)
        return false;
    return false;
}

[[nodiscard]] bool methodBodyMustReturn(const std::vector<StmtPtr> &stmts)
{
    if (stmts.empty())
        return false;
    const StmtPtr &tail = stmts.back();
    return tail && methodMustReturn(*tail);
}

void emitMissingReturn(const ClassDecl &klass, const MethodDecl &method, DiagnosticEmitter *emitter)
{
    if (!emitter)
        return;
    if (!method.ret)
        return;
    if (methodBodyMustReturn(method.body))
        return;

    std::string qualified = klass.name;
    if (!qualified.empty())
        qualified += '.';
    qualified += method.name;

    std::string msg = "missing return in FUNCTION " + qualified;
    emitter->emit(il::support::Severity::Error, "B1007", method.loc, 3, std::move(msg));
}
} // namespace

/// @brief Look up a mutable class record by name.
/// @details Searches the internal @c std::unordered_map for the requested class
///          name and returns a pointer to the stored @ref ClassInfo instance
///          when found.  Returning @c nullptr keeps callers explicit about the
///          missing-class case without performing map insertions.
/// @param name Class identifier to locate.
/// @return Pointer to the associated @ref ClassInfo or @c nullptr when absent.
ClassInfo *OopIndex::findClass(const std::string &name)
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Look up an immutable class record by name.
/// @details Const-qualified overload used by read-only consumers.  The method
///          performs the same map probe as the mutable variant but preserves
///          const-correctness so callers cannot mutate the stored metadata.
/// @param name Class identifier to locate.
/// @return Pointer to the stored @ref ClassInfo or @c nullptr when absent.
const ClassInfo *OopIndex::findClass(const std::string &name) const
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Populate the OOP index from a parsed BASIC program.
/// @details Clears any pre-existing entries then walks the top-level statements
///          collecting class declarations.  For each class the helper captures
///          field metadata, method signatures, and constructor/destructor flags
///          so downstream passes can query structural details without revisiting
///          the AST.  When @p emitter is provided the helper reports missing
///          return diagnostics for value-returning methods.
/// @param program Parsed BASIC program supplying class declarations.
/// @param index Index instance that receives the reconstructed metadata.
/// @param emitter Optional diagnostics interface reserved for future checks.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter)
{
    index.clear();

    // Keep a simple namespace stack to form qualified names.
    std::vector<std::string> nsStack;
    auto joinNs = [&]() -> std::string {
        if (nsStack.empty())
            return {};
        std::string prefix;
        std::size_t size = 0;
        for (const auto &s : nsStack)
            size += s.size() + 1;
        if (size)
            size -= 1; // trailing dot not needed
        prefix.reserve(size);
        for (std::size_t i = 0; i < nsStack.size(); ++i)
        {
            if (i)
                prefix.push_back('.');
            prefix += nsStack[i];
        }
        return prefix;
    };

    // Recursive lambda to scan statements and populate the index.
    std::function<void(const std::vector<StmtPtr>&)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts) {
        for (const auto &stmtPtr : stmts)
        {
            if (!stmtPtr)
                continue;
            switch (stmtPtr->stmtKind())
            {
                case Stmt::Kind::NamespaceDecl:
                {
                    const auto &ns = static_cast<const NamespaceDecl &>(*stmtPtr);
                    for (const auto &seg : ns.path)
                        nsStack.push_back(seg);
                    scan(ns.body);
                    nsStack.resize(nsStack.size() >= ns.path.size() ? nsStack.size() - ns.path.size() : 0);
                    break;
                }
                case Stmt::Kind::ClassDecl:
                {
                    const auto &classDecl = static_cast<const ClassDecl &>(*stmtPtr);
                    ClassInfo info;
                    info.name = classDecl.name;
                    std::string prefix = joinNs();
                    if (!prefix.empty())
                        info.qualifiedName = prefix + "." + classDecl.name;
                    else
                        info.qualifiedName = classDecl.name;

                    info.fields.reserve(classDecl.fields.size());
                    std::unordered_set<std::string> classFieldNames;
                    classFieldNames.reserve(classDecl.fields.size());
                    for (const auto &field : classDecl.fields)
                    {
                        info.fields.push_back(ClassInfo::FieldInfo{field.name, field.type, field.access});
                        classFieldNames.insert(field.name);
                    }

                    for (const auto &member : classDecl.members)
                    {
                        if (!member)
                            continue;
                        switch (member->stmtKind())
                        {
                            case Stmt::Kind::ConstructorDecl:
                            {
                                const auto &ctor = static_cast<const ConstructorDecl &>(*member);
                                info.hasConstructor = true;
                                info.ctorParams.clear();
                                info.ctorParams.reserve(ctor.params.size());
                                for (const auto &param : ctor.params)
                                {
                                    ClassInfo::CtorParam sigParam;
                                    sigParam.type = param.type;
                                    sigParam.isArray = param.is_array;
                                    info.ctorParams.push_back(sigParam);
                                }
                                checkMemberShadowing(ctor.body, classDecl, classFieldNames, emitter);
                                break;
                            }
                            case Stmt::Kind::DestructorDecl:
                            {
                                info.hasDestructor = true;
                                const auto &dtor = static_cast<const DestructorDecl &>(*member);
                                checkMemberShadowing(dtor.body, classDecl, classFieldNames, emitter);
                                break;
                            }
                            case Stmt::Kind::MethodDecl:
                            {
                                const auto &method = static_cast<const MethodDecl &>(*member);
                                MethodSig sig;
                                sig.paramTypes.reserve(method.params.size());
                                for (const auto &param : method.params)
                                {
                                    sig.paramTypes.push_back(param.type);
                                }
                                if (method.ret.has_value())
                                {
                                    sig.returnType = method.ret;
                                }
                                else if (auto suffixType = inferAstTypeFromSuffix(method.name))
                                {
                                    sig.returnType = suffixType;
                                }
                                sig.access = method.access;
                                emitMissingReturn(classDecl, method, emitter);
                                checkMemberShadowing(method.body, classDecl, classFieldNames, emitter);
                                info.methods[method.name] = std::move(sig);
                                break;
                            }
                            default:
                                break;
                        }
                    }

                    if (!info.hasConstructor)
                        info.hasSynthCtor = true;

                    index.classes()[info.qualifiedName] = std::move(info);
                    break;
                }
                default:
                    break;
            }
        }
    };

    scan(program.main);
}

} // namespace il::frontends::basic
