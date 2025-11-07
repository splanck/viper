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
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include "support/diagnostics.hpp"

#include <string>
#include <utility>

namespace il::frontends::basic
{

namespace
{
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

    for (const auto &stmtPtr : program.main)
    {
        if (!stmtPtr)
        {
            continue;
        }

        if (stmtPtr->stmtKind() != Stmt::Kind::ClassDecl)
        {
            continue;
        }

        const auto &classDecl = static_cast<const ClassDecl &>(*stmtPtr);

        ClassInfo info;
        info.name = classDecl.name;
        info.fields.reserve(classDecl.fields.size());
        for (const auto &field : classDecl.fields)
        {
            info.fields.push_back(ClassInfo::FieldInfo{field.name, field.type});
        }

        for (const auto &member : classDecl.members)
        {
            if (!member)
            {
                continue;
            }

            switch (member->stmtKind())
            {
                case Stmt::Kind::ConstructorDecl:
                {
                    info.hasConstructor = true;
                    break;
                }
                case Stmt::Kind::DestructorDecl:
                {
                    info.hasDestructor = true;
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
                    emitMissingReturn(classDecl, method, emitter);
                    info.methods[method.name] = std::move(sig);
                    break;
                }
                default:
                    break;
            }
        }

        if (!info.hasConstructor)
        {
            info.hasSynthCtor = true;
        }

        index.classes()[info.name] = std::move(info);
    }
}

} // namespace il::frontends::basic
