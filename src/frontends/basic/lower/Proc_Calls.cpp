//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Calls.cpp
// Purpose: Implements procedure-level helpers focused on signature management
//          and parameter materialisation for BASIC lowering.
// Key invariants: Signatures remain cached per-lowerer instance and parameter
//                 slots mirror IL calling conventions.
// Ownership/Lifetime: Borrows Lowerer state; no persistent allocations beyond
//                     signature caches.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "frontends/basic/lower/Emitter.hpp"

#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <utility>

using namespace il::core;

namespace viper::basic::lower::calls
{

::il::frontends::basic::ProcedureLowering::LoweringContext
make_context(::il::frontends::basic::ProcedureLowering &lowering,
             const std::string &name,
             const std::vector<::il::frontends::basic::Param> &params,
             const std::vector<::il::frontends::basic::StmtPtr> &body,
             const ::il::frontends::basic::Lowerer::ProcedureConfig &config)
{
    return lowering.makeContext(name, params, body, config);
}

} // namespace viper::basic::lower::calls

namespace il::frontends::basic
{

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

ProcedureLowering::LoweringContext::LoweringContext(Lowerer &lowerer,
                                                    std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
                                                    il::build::IRBuilder &builder,
                                                    lower::Emitter &emitter,
                                                    std::string name,
                                                    const std::vector<Param> &params,
                                                    const std::vector<StmtPtr> &body,
                                                    const Lowerer::ProcedureConfig &config)
    : lowerer(lowerer), symbols(symbols), builder(builder), emitter(emitter), name(std::move(name)), params(params),
      body(body), config(config)
{
}

ProcedureLowering::LoweringContext ProcedureLowering::makeContext(const std::string &name,
                                                                  const std::vector<Param> &params,
                                                                  const std::vector<StmtPtr> &body,
                                                                  const Lowerer::ProcedureConfig &config)
{
    assert(lowerer.builder && "makeContext requires an active IRBuilder");
    return LoweringContext(
        lowerer, lowerer.symbols, *lowerer.builder, lowerer.emitter(), name, params, body, config);
}

void ProcedureLowering::collectProcedureSignatures(const Program &prog)
{
    lowerer.procSignatures.clear();
    for (const auto &decl : prog.procs)
    {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = lowerer.functionRetTypeFromHint(fn->name, fn->explicitRetType);
            sig.paramTypes.reserve(fn->params.size());
            for (const auto &p : fn->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : pipeline_detail::coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(fn->name, std::move(sig));
        }
        else if (auto *sub = dynamic_cast<const SubDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = il::core::Type(il::core::Type::Kind::Void);
            sig.paramTypes.reserve(sub->params.size());
            for (const auto &p : sub->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : pipeline_detail::coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(sub->name, std::move(sig));
        }
    }
}

const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    auto it = procSignatures.find(name);
    if (it == procSignatures.end())
        return nullptr;
    return &it->second;
}

void Lowerer::collectProcedureSignatures(const Program &prog)
{
    procedureLowering->collectProcedureSignatures(prog);
}

void Lowerer::materializeParams(const std::vector<Param> &params)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "materializeParams requires an active function");
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        bool isBoolParam = !p.is_array && p.type == AstType::Bool;
        Value slot = emitAlloca(isBoolParam ? 1 : 8);
        if (p.is_array)
        {
            markArray(p.name);
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        setSymbolType(p.name, p.type);
        markSymbolReferenced(p.name);
        auto &info = ensureSymbol(p.name);
        info.slotId = slot.id;
        il::core::Type ty = func->params[i].type;
        Value incoming = Value::temp(func->params[i].id);
        if (p.is_array)
        {
            storeArray(slot, incoming);
        }
        else
        {
            emitStore(ty, slot, incoming);
        }
    }
}

void Lowerer::lowerFunctionDecl(const FunctionDecl &decl)
{
    auto defaultRet = [&]()
    {
        switch (decl.ret)
        {
            case ::il::frontends::basic::Type::I64:
                return Value::constInt(0);
            case ::il::frontends::basic::Type::F64:
                return Value::constFloat(0.0);
            case ::il::frontends::basic::Type::Str:
                return emitConstStr(getStringLabel(""));
            case ::il::frontends::basic::Type::Bool:
                return emitBoolConst(false);
        }
        return Value::constInt(0);
    };

    ProcedureConfig config;
    config.retType = functionRetTypeFromHint(decl.name, decl.explicitRetType);
    config.postCollect = [&]() { setSymbolType(decl.name, decl.ret); };
    config.emitEmptyBody = [&]() { emitRet(defaultRet()); };
    config.emitFinalReturn = [&]() { emitRet(defaultRet()); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    ProcedureConfig config;
    config.retType = Type(Type::Kind::Void);
    config.emitEmptyBody = [&]() { emitRetVoid(); };
    config.emitFinalReturn = [&]() { emitRetVoid(); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

} // namespace il::frontends::basic
