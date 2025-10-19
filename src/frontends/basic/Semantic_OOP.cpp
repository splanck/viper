// File: src/frontends/basic/Semantic_OOP.cpp
// Purpose: Implements BASIC OOP indexing utilities for class metadata extraction.
// Key invariants: Index rebuilding clears previous entries before populating anew.
// Ownership/Lifetime: Operates on borrowed AST references and stores copies in the index.
// Links: docs/codemap.md

#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/AST.hpp"


#include <utility>

namespace il::frontends::basic
{

ClassInfo *OopIndex::findClass(const std::string &name)
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

const ClassInfo *OopIndex::findClass(const std::string &name) const
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

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


