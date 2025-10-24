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


#include <utility>

namespace il::frontends::basic
{

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
///          the AST.  Diagnostic emission is currently unused but retained for
///          future validation hooks.
/// @param program Parsed BASIC program supplying class declarations.
/// @param index Index instance that receives the reconstructed metadata.
/// @param emitter Optional diagnostics interface reserved for future checks.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter * /*emitter*/)
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
                    info.methods[method.name] = std::move(sig);
                    break;
                }
                default:
                    break;
            }
        }

        index.classes()[info.name] = std::move(info);
    }
}

} // namespace il::frontends::basic
