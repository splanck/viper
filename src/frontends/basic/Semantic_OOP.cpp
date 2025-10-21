//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements BASIC OOP indexing utilities for class metadata extraction. The
// helpers scan program-level declarations, cache discovered class shapes, and
// expose lookup utilities so semantic analysis and lowering can query field and
// method layouts without repeatedly walking the AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/AST.hpp"


#include <utility>

namespace il::frontends::basic
{

/// @brief Look up a mutable class descriptor by name.
///
/// Performs a hash-table search of the cached class metadata built during the
/// most recent indexing pass. Returning `nullptr` signals that the program has
/// not declared the requested class, allowing callers to fall back to
/// diagnostic emission.
///
/// @param name Class identifier to search for.
/// @return Pointer to the mutable @ref ClassInfo entry or `nullptr` when absent.
ClassInfo *OopIndex::findClass(const std::string &name)
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Retrieve an immutable view of a class descriptor by name.
///
/// Mirrors the mutable overload but returns a const-qualified pointer so
/// read-only queries can traverse field and method metadata without risking
/// mutation.
///
/// @param name Class identifier to search for.
/// @return Pointer to an immutable @ref ClassInfo entry or `nullptr` when not found.
const ClassInfo *OopIndex::findClass(const std::string &name) const
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Populate an @ref OopIndex by scanning top-level class declarations.
///
/// Clears the supplied index and walks the program's main body, collecting class
/// declarations into structured @ref ClassInfo records. Field and method
/// metadata are copied so subsequent analyses can use the index without holding
/// references to the AST. The optional diagnostic emitter is reserved for future
/// validation hooks.
///
/// @param program Parsed BASIC program containing potential class declarations.
/// @param index Destination cache receiving the rebuilt metadata.
/// @param emitter Diagnostic sink reserved for semantic validation (unused).
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


