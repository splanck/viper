// File: src/il/verify/ModuleVerifier.cpp
// Purpose: Implements module-level IL verification.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md

#include "il/verify/ModuleVerifier.hpp"
#include "il/verify/FunctionVerifier.hpp"
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

bool ModuleVerifier::verify(const Module &m, std::ostream &err)
{
    std::unordered_map<std::string, const Extern *> externs;
    std::unordered_map<std::string, const Global *> globals;
    std::unordered_map<std::string, const Function *> funcs;
    bool ok = true;

    ok &= verifyExterns(m, err, externs);
    ok &= verifyGlobals(m, err, globals);

    FunctionVerifier fv;
    for (const auto &f : m.functions)
    {
        if (!funcs.emplace(f.name, &f).second)
        {
            err << "duplicate function @" << f.name << "\n";
            ok = false;
        }
        ok &= fv.verify(f, externs, funcs, err);
    }

    return ok;
}

bool ModuleVerifier::verifyExterns(const Module &m,
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

bool ModuleVerifier::verifyGlobals(const Module &m,
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

} // namespace il::verify
