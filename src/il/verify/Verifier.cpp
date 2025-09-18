// File: src/il/verify/Verifier.cpp
// Purpose: Implements IL verifier checking module correctness.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md

#include "il/verify/Verifier.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::verify
{

namespace
{
struct ExternSig
{
    Type ret;
    std::vector<Type> params;
};

const std::unordered_map<std::string, ExternSig> kExternSigs = {
    {"rt_trap", {Type(Type::Kind::Void), {Type(Type::Kind::Ptr)}}},
    {"rt_abort", {Type(Type::Kind::Void), {Type(Type::Kind::Ptr)}}},
    {"rt_print_str", {Type(Type::Kind::Void), {Type(Type::Kind::Str)}}},
    {"rt_print_i64", {Type(Type::Kind::Void), {Type(Type::Kind::I64)}}},
    {"rt_print_f64", {Type(Type::Kind::Void), {Type(Type::Kind::F64)}}},
    {"rt_input_line", {Type(Type::Kind::Str), {}}},
    {"rt_len", {Type(Type::Kind::I64), {Type(Type::Kind::Str)}}},
    {"rt_concat", {Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::Str)}}},
    {"rt_substr",
     {Type(Type::Kind::Str),
      {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)}}},
    {"rt_left", {Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)}}},
    {"rt_right", {Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)}}},
    {"rt_mid2", {Type(Type::Kind::Str), {Type(Type::Kind::Str), Type(Type::Kind::I64)}}},
    {"rt_mid3",
     {Type(Type::Kind::Str),
      {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)}}},
    {"rt_instr3",
     {Type(Type::Kind::I64),
      {Type(Type::Kind::I64), Type(Type::Kind::Str), Type(Type::Kind::Str)}}},
    {"rt_instr2", {Type(Type::Kind::I64), {Type(Type::Kind::Str), Type(Type::Kind::Str)}}},
    {"rt_ltrim", {Type(Type::Kind::Str), {Type(Type::Kind::Str)}}},
    {"rt_rtrim", {Type(Type::Kind::Str), {Type(Type::Kind::Str)}}},
    {"rt_trim", {Type(Type::Kind::Str), {Type(Type::Kind::Str)}}},
    {"rt_ucase", {Type(Type::Kind::Str), {Type(Type::Kind::Str)}}},
    {"rt_lcase", {Type(Type::Kind::Str), {Type(Type::Kind::Str)}}},
    {"rt_chr", {Type(Type::Kind::Str), {Type(Type::Kind::I64)}}},
    {"rt_asc", {Type(Type::Kind::I64), {Type(Type::Kind::Str)}}},
    {"rt_str_eq", {Type(Type::Kind::I1), {Type(Type::Kind::Str), Type(Type::Kind::Str)}}},
    {"rt_to_int", {Type(Type::Kind::I64), {Type(Type::Kind::Str)}}},
    {"rt_int_to_str", {Type(Type::Kind::Str), {Type(Type::Kind::I64)}}},
    {"rt_f64_to_str", {Type(Type::Kind::Str), {Type(Type::Kind::F64)}}},
    {"rt_val", {Type(Type::Kind::F64), {Type(Type::Kind::Str)}}},
    {"rt_str", {Type(Type::Kind::Str), {Type(Type::Kind::F64)}}},
    {"rt_sqrt", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_floor", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_ceil", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_sin", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_cos", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_pow", {Type(Type::Kind::F64), {Type(Type::Kind::F64), Type(Type::Kind::F64)}}},
    {"rt_abs_i64", {Type(Type::Kind::I64), {Type(Type::Kind::I64)}}},
    {"rt_abs_f64", {Type(Type::Kind::F64), {Type(Type::Kind::F64)}}},
    {"rt_randomize_i64", {Type(Type::Kind::Void), {Type(Type::Kind::I64)}}},
    {"rt_rnd", {Type(Type::Kind::F64), {}}},
    {"rt_alloc", {Type(Type::Kind::Ptr), {Type(Type::Kind::I64)}}},
    {"rt_const_cstr", {Type(Type::Kind::Str), {Type(Type::Kind::Ptr)}}},
};

} // namespace

bool Verifier::verify(const Module &m, std::ostream &err)
{
    std::unordered_map<std::string, const Extern *> externs;
    std::unordered_map<std::string, const Global *> globals;
    std::unordered_map<std::string, const Function *> funcs;
    bool ok = true;

    ok &= verifyExterns(m, err, externs);
    ok &= verifyGlobals(m, err, globals);

    for (const auto &f : m.functions)
    {
        if (!funcs.emplace(f.name, &f).second)
        {
            err << "duplicate function @" << f.name << "\n";
            ok = false;
        }
        ok &= verifyFunction(f, externs, funcs, err);
    }

    return ok;
}

bool Verifier::verifyExterns(const Module &m,
                             std::ostream &err,
                             std::unordered_map<std::string, const Extern *> &externs)
{
    bool ok = true;
    for (const auto &e : m.externs)
    {
        auto [it, ins] = externs.emplace(e.name, &e);
        if (!ins)
        {
            const Extern *prev = it->second;
            bool sigOk =
                prev->retType.kind == e.retType.kind && prev->params.size() == e.params.size();
            if (sigOk)
                for (size_t i = 0; i < e.params.size(); ++i)
                    if (prev->params[i].kind != e.params[i].kind)
                        sigOk = false;
            err << "duplicate extern @" << e.name;
            if (!sigOk)
                err << " with mismatched signature";
            err << "\n";
            ok = false;
            continue;
        }

        auto itKnown = kExternSigs.find(e.name);
        if (itKnown != kExternSigs.end())
        {
            const ExternSig &sig = itKnown->second;
            bool sigOk = e.retType.kind == sig.ret.kind && e.params.size() == sig.params.size();
            if (sigOk)
                for (size_t i = 0; i < sig.params.size(); ++i)
                    if (e.params[i].kind != sig.params[i].kind)
                        sigOk = false;
            if (!sigOk)
            {
                err << "extern @" << e.name << " signature mismatch\n";
                ok = false;
            }
        }
    }
    return ok;
}

bool Verifier::verifyGlobals(const Module &m,
                             std::ostream &err,
                             std::unordered_map<std::string, const Global *> &globals)
{
    bool ok = true;
    for (const auto &g : m.globals)
    {
        if (!globals.emplace(g.name, &g).second)
        {
            err << "duplicate global @" << g.name << "\n";
            ok = false;
        }
    }
    return ok;
}

bool Verifier::verifyFunction(const Function &fn,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              std::ostream &err)
{
    bool ok = true;
    if (fn.blocks.empty())
    {
        err << fn.name << ": function has no blocks\n";
        return false;
    }
    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
    if (!isEntry)
    {
        err << fn.name << ": first block must be entry\n";
        ok = false;
    }

    auto itExt = externs.find(fn.name);
    if (itExt != externs.end())
    {
        const Extern *e = itExt->second;
        bool sigOk = e->retType.kind == fn.retType.kind && e->params.size() == fn.params.size();
        if (sigOk)
            for (size_t i = 0; i < e->params.size(); ++i)
                if (e->params[i].kind != fn.params[i].type.kind)
                    sigOk = false;
        if (!sigOk)
        {
            err << "function @" << fn.name << " signature mismatch with extern\n";
            ok = false;
        }
    }

    std::unordered_set<std::string> labels;
    std::unordered_map<std::string, const BasicBlock *> blockMap;
    for (const auto &bb : fn.blocks)
    {
        if (!labels.insert(bb.label).second)
        {
            err << fn.name << ": duplicate label " << bb.label << "\n";
            ok = false;
        }
        blockMap[bb.label] = &bb;
    }

    std::unordered_map<unsigned, Type> temps;
    for (const auto &p : fn.params)
        temps[p.id] = p.type;

    for (const auto &bb : fn.blocks)
        ok &= verifyBlock(fn, bb, blockMap, externs, funcs, temps, err);

    for (const auto &bb : fn.blocks)
        for (const auto &in : bb.instructions)
            for (const auto &lbl : in.labels)
                if (!labels.count(lbl))
                {
                    err << fn.name << ": unknown label " << lbl << "\n";
                    ok = false;
                }

    return ok;
}

bool Verifier::verifyBlock(const Function &fn,
                           const BasicBlock &bb,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           std::unordered_map<unsigned, Type> &temps,
                           std::ostream &err)
{
    bool ok = true;
    std::unordered_set<unsigned> defined;
    for (const auto &kv : temps)
        defined.insert(kv.first);

    TypeInference types(temps, defined);
    std::vector<unsigned> paramIds;
    ok &= validateBlockParams(fn, bb, types, paramIds, err);
    auto verifyInstrFn = &Verifier::verifyInstr;
    ok &= iterateBlockInstructions(
        verifyInstrFn, fn, bb, blockMap, externs, funcs, types, err);
    ok &= checkBlockTerminators(fn, bb, err);

    for (unsigned id : paramIds)
        types.removeTemp(id);

    return ok;
}

bool Verifier::verifyInstr(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &in,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           TypeInference &types,
                           std::ostream &err)
{
    switch (in.op)
    {
        case Opcode::Br:
            return verifyBr(fn, bb, in, blockMap, types, err);
        case Opcode::CBr:
            return verifyCBr(fn, bb, in, blockMap, types, err);
        case Opcode::Ret:
            return verifyRet(fn, bb, in, types, err);
        default:
            return verifyInstruction(fn, bb, in, externs, funcs, types, err);
    }
}

} // namespace il::verify
