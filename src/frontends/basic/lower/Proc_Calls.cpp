//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Proc_Calls.cpp
// Purpose: Implements procedure lowering helpers focused on call semantics,
//          including signature discovery and declaration orchestration.
// Key invariants: Helpers operate on the owning Lowerer instance via the
//                 friend interface to avoid leaking internal state.
// Ownership/Lifetime: Functions borrow the Lowerer and ProcedureLowering
//                     instances passed to them.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "frontends/basic/TypeSuffix.hpp"

#include "il/core/Function.hpp"
#include "il/core/Type.hpp"

#include <utility>

namespace viper::basic::lower::calls
{

using il::frontends::basic::FunctionDecl;
using il::frontends::basic::Lowerer;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Program;
using il::frontends::basic::SubDecl;
using il::frontends::basic::pipeline_detail::coreTypeForAstType;

struct Access
{
    static void collectProcedureSignatures(ProcedureLowering &self, const Program &prog);
    static const Lowerer::ProcedureSignature *findProcSignature(const Lowerer &lowerer, const std::string &name);
    static void lowerFunctionDecl(Lowerer &lowerer, const FunctionDecl &decl);
    static void lowerSubDecl(Lowerer &lowerer, const SubDecl &decl);
};

void Access::collectProcedureSignatures(ProcedureLowering &self, const Program &prog)
{
    auto &lowerer = self.lowerer;
    lowerer.procSignatures.clear();
    for (const auto &decl : prog.procs)
    {
        if (const auto *fn = dynamic_cast<const FunctionDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = lowerer.functionRetTypeFromHint(fn->name, fn->explicitRetType);
            sig.paramTypes.reserve(fn->params.size());
            for (const auto &p : fn->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(fn->name, std::move(sig));
        }
        else if (const auto *sub = dynamic_cast<const SubDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = il::core::Type(il::core::Type::Kind::Void);
            sig.paramTypes.reserve(sub->params.size());
            for (const auto &p : sub->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(sub->name, std::move(sig));
        }
    }
}

const Lowerer::ProcedureSignature *Access::findProcSignature(const Lowerer &lowerer, const std::string &name)
{
    auto it = lowerer.procSignatures.find(name);
    if (it == lowerer.procSignatures.end())
        return nullptr;
    return &it->second;
}

void Access::lowerFunctionDecl(Lowerer &lowerer, const FunctionDecl &decl)
{
    auto defaultRet = [&]() {
        using ::il::frontends::basic::Type;
        switch (decl.ret)
        {
            case Type::I64:
                return il::core::Value::constInt(0);
            case Type::F64:
                return il::core::Value::constFloat(0.0);
            case Type::Str:
                return lowerer.emitConstStr(lowerer.getStringLabel(""));
            case Type::Bool:
                return lowerer.emitBoolConst(false);
        }
        return il::core::Value::constInt(0);
    };

    Lowerer::ProcedureConfig config;
    config.retType = lowerer.functionRetTypeFromHint(decl.name, decl.explicitRetType);
    config.postCollect = [&]() { lowerer.setSymbolType(decl.name, decl.ret); };
    config.emitEmptyBody = [&]() { lowerer.emitRet(defaultRet()); };
    config.emitFinalReturn = [&]() { lowerer.emitRet(defaultRet()); };

    lowerer.lowerProcedure(decl.name, decl.params, decl.body, config);
}

void Access::lowerSubDecl(Lowerer &lowerer, const SubDecl &decl)
{
    Lowerer::ProcedureConfig config;
    config.retType = il::core::Type(il::core::Type::Kind::Void);
    config.emitEmptyBody = [&]() { lowerer.emitRetVoid(); };
    config.emitFinalReturn = [&]() { lowerer.emitRetVoid(); };

    lowerer.lowerProcedure(decl.name, decl.params, decl.body, config);
}

} // namespace viper::basic::lower::calls
