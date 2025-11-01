//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Calls.cpp
// Purpose: Handle procedure signature discovery helpers for BASIC lowering.
// Key invariants: Cached signatures reflect declared parameter/return types and
//                 normalise array parameters to pointer types.
// Ownership/Lifetime: Operates on the owning Lowerer without taking ownership
//                     of AST declarations.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/LoweringPipeline.hpp"

#include <utility>

using namespace il::core;

namespace viper::basic::lower::calls
{

using il::frontends::basic::FunctionDecl;
using il::frontends::basic::Lowerer;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Program;
using il::frontends::basic::SubDecl;
using il::frontends::basic::pipeline_detail::coreTypeForAstType;

struct API
{
    static void collectProcedureSignatures(ProcedureLowering &lowering, const Program &prog)
    {
        auto &lowerer = lowering.lowerer;
        lowerer.procSignatures.clear();
        for (const auto &decl : prog.procs)
        {
            if (const auto *fn = dynamic_cast<const FunctionDecl *>(decl.get()))
            {
                Lowerer::ProcedureSignature sig;
                sig.retType = lowering.lowerer.functionRetTypeFromHint(fn->name, fn->explicitRetType);
                sig.paramTypes.reserve(fn->params.size());
                for (const auto &p : fn->params)
                {
                    Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
                    sig.paramTypes.push_back(ty);
                }
                lowerer.procSignatures.emplace(fn->name, std::move(sig));
            }
            else if (const auto *sub = dynamic_cast<const SubDecl *>(decl.get()))
            {
                Lowerer::ProcedureSignature sig;
                sig.retType = Type(Type::Kind::Void);
                sig.paramTypes.reserve(sub->params.size());
                for (const auto &p : sub->params)
                {
                    Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
                    sig.paramTypes.push_back(ty);
                }
                lowerer.procSignatures.emplace(sub->name, std::move(sig));
            }
        }
    }

    static const Lowerer::ProcedureSignature *findProcSignature(const Lowerer &lowerer,
                                                                const std::string &name)
    {
        auto it = lowerer.procSignatures.find(name);
        if (it == lowerer.procSignatures.end())
            return nullptr;
        return &it->second;
    }
};

} // namespace viper::basic::lower::calls

namespace il::frontends::basic
{

void ProcedureLowering::collectProcedureSignatures(const Program &prog)
{
    viper::basic::lower::calls::API::collectProcedureSignatures(*this, prog);
}

const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    return viper::basic::lower::calls::API::findProcSignature(*this, name);
}

void Lowerer::collectProcedureSignatures(const Program &prog)
{
    procedureLowering->collectProcedureSignatures(prog);
}

} // namespace il::frontends::basic

