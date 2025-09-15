// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Implements function-level IL verification.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own functions.
// Links: docs/il-spec.md

#include "il/verify/FunctionVerifier.hpp"
#include "il/verify/BlockVerifier.hpp"
#include <unordered_set>

using namespace il::core;

namespace il::verify
{

bool FunctionVerifier::verify(const Function &fn,
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
    if (fn.blocks.front().label != "entry")
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

    BlockVerifier bv;
    for (const auto &bb : fn.blocks)
        ok &= bv.verify(fn, bb, blockMap, externs, funcs, temps, err);

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

} // namespace il::verify
