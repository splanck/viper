// File: src/il/verify/Verifier.cpp
// Purpose: Implements IL verifier checking module correctness.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md

#include "il/verify/Verifier.hpp"
#include "il/core/Opcode.hpp"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::verify
{

namespace
{

bool isTerminator(Opcode op)
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret;
}

Type valueType(const Value &v, const std::unordered_map<unsigned, Type> &temps)
{
    switch (v.kind)
    {
        case Value::Kind::Temp:
        {
            auto it = temps.find(v.id);
            if (it != temps.end())
                return it->second;
            return Type(Type::Kind::Void);
        }
        case Value::Kind::ConstInt:
            return Type(Type::Kind::I64);
        case Value::Kind::ConstFloat:
            return Type(Type::Kind::F64);
        case Value::Kind::ConstStr:
            return Type(Type::Kind::Str);
        case Value::Kind::GlobalAddr:
        case Value::Kind::NullPtr:
            return Type(Type::Kind::Ptr);
    }
    return Type(Type::Kind::Void);
}

size_t typeSize(Type::Kind k)
{
    switch (k)
    {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            return 8;
        case Type::Kind::Void:
            return 0;
    }
    return 0;
}

std::string snippet(const Instr &in)
{
    std::ostringstream os;
    if (in.result)
        os << "%" << *in.result << " = ";
    os << toString(in.op);
    for (const auto &op : in.operands)
        os << " " << toString(op);
    for (size_t i = 0; i < in.labels.size(); ++i)
        os << " label " << in.labels[i];
    return os.str();
}

bool expectOperandCount(
    const Function &fn, const BasicBlock &bb, const Instr &in, size_t expected, std::ostream &err)
{
    if (in.operands.size() != expected)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": expected " << expected
            << " operand" << (expected == 1 ? "" : "s") << "\n";
        return false;
    }
    return true;
}

bool expectAllOperandType(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &in,
                          const std::unordered_map<unsigned, Type> &temps,
                          Type::Kind kind,
                          std::ostream &err)
{
    bool ok = true;
    for (const auto &op : in.operands)
        if (valueType(op, temps).kind != kind)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
            ok = false;
        }
    return ok;
}

void recordResult(const Instr &in,
                  Type ty,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined)
{
    if (in.result)
    {
        temps[*in.result] = ty;
        defined.insert(*in.result);
    }
}

bool verifyAlloca(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &in,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined,
                  std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (valueType(in.operands[0], temps).kind != Type::Kind::I64)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": size must be i64\n";
        ok = false;
    }
    if (in.operands[0].kind == Value::Kind::ConstInt)
    {
        long long sz = in.operands[0].i64;
        if (sz < 0)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in) << ": negative alloca size\n";
            ok = false;
        }
        else if (sz > (1LL << 20))
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in) << ": warning: huge alloca\n";
        }
    }
    recordResult(in, Type(Type::Kind::Ptr), temps, defined);
    return ok;
}

bool verifyIntBinary(const Function &fn,
                     const BasicBlock &bb,
                     const Instr &in,
                     std::unordered_map<unsigned, Type> &temps,
                     std::unordered_set<unsigned> &defined,
                     std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    ok &= expectAllOperandType(fn, bb, in, temps, Type::Kind::I64, err);
    recordResult(in, Type(Type::Kind::I64), temps, defined);
    return ok;
}

bool verifyFloatBinary(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &in,
                       std::unordered_map<unsigned, Type> &temps,
                       std::unordered_set<unsigned> &defined,
                       std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    ok &= expectAllOperandType(fn, bb, in, temps, Type::Kind::F64, err);
    recordResult(in, Type(Type::Kind::F64), temps, defined);
    return ok;
}

bool verifyICmp(const Function &fn,
                const BasicBlock &bb,
                const Instr &in,
                std::unordered_map<unsigned, Type> &temps,
                std::unordered_set<unsigned> &defined,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    ok &= expectAllOperandType(fn, bb, in, temps, Type::Kind::I64, err);
    recordResult(in, Type(Type::Kind::I1), temps, defined);
    return ok;
}

bool verifyFCmp(const Function &fn,
                const BasicBlock &bb,
                const Instr &in,
                std::unordered_map<unsigned, Type> &temps,
                std::unordered_set<unsigned> &defined,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    ok &= expectAllOperandType(fn, bb, in, temps, Type::Kind::F64, err);
    recordResult(in, Type(Type::Kind::I1), temps, defined);
    return ok;
}

bool verifySitofp(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &in,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined,
                  std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (valueType(in.operands[0], temps).kind != Type::Kind::I64)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::F64), temps, defined);
    return ok;
}

bool verifyFptosi(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &in,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined,
                  std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (valueType(in.operands[0], temps).kind != Type::Kind::F64)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::I64), temps, defined);
    return ok;
}

bool verifyZext1(const Function &fn,
                 const BasicBlock &bb,
                 const Instr &in,
                 std::unordered_map<unsigned, Type> &temps,
                 std::unordered_set<unsigned> &defined,
                 std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (valueType(in.operands[0], temps).kind != Type::Kind::I1)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::I64), temps, defined);
    return ok;
}

bool verifyTrunc1(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &in,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined,
                  std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (valueType(in.operands[0], temps).kind != Type::Kind::I64)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::I1), temps, defined);
    return ok;
}

bool verifyGEP(const Function &fn,
               const BasicBlock &bb,
               const Instr &in,
               std::unordered_map<unsigned, Type> &temps,
               std::unordered_set<unsigned> &defined,
               std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    if (ok)
    {
        if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr ||
            valueType(in.operands[1], temps).kind != Type::Kind::I64)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand type mismatch\n";
            ok = false;
        }
    }
    recordResult(in, Type(Type::Kind::Ptr), temps, defined);
    return ok;
}

bool verifyLoad(const Function &fn,
                const BasicBlock &bb,
                const Instr &in,
                std::unordered_map<unsigned, Type> &temps,
                std::unordered_set<unsigned> &defined,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 1, err);
    if (in.type.kind == Type::Kind::Void)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": void load type\n";
        ok = false;
    }
    if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": pointer type mismatch\n";
        ok = false;
    }
    [[maybe_unused]] size_t sz = typeSize(in.type.kind);
    recordResult(in, in.type, temps, defined);
    return ok;
}

bool verifyStore(const Function &fn,
                 const BasicBlock &bb,
                 const Instr &in,
                 std::unordered_map<unsigned, Type> &temps,
                 std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, in, 2, err);
    if (in.type.kind == Type::Kind::Void)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": void store type\n";
        ok = false;
    }
    if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": pointer type mismatch\n";
        ok = false;
    }
    if (valueType(in.operands[1], temps).kind != in.type.kind)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": value type mismatch\n";
        ok = false;
    }
    [[maybe_unused]] size_t sz = typeSize(in.type.kind);
    return ok;
}

bool verifyAddrOf(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &in,
                  std::unordered_map<unsigned, Type> &temps,
                  std::unordered_set<unsigned> &defined,
                  std::ostream &err)
{
    bool ok = true;
    if (in.operands.size() != 1 || in.operands[0].kind != Value::Kind::GlobalAddr)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": operand must be global\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::Ptr), temps, defined);
    return ok;
}

bool verifyConstStr(const Function &fn,
                    const BasicBlock &bb,
                    const Instr &in,
                    std::unordered_map<unsigned, Type> &temps,
                    std::unordered_set<unsigned> &defined,
                    std::ostream &err)
{
    bool ok = true;
    if (in.operands.size() != 1 || in.operands[0].kind != Value::Kind::GlobalAddr)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": unknown string global\n";
        ok = false;
    }
    recordResult(in, Type(Type::Kind::Str), temps, defined);
    return ok;
}

bool verifyConstNull(const Instr &in,
                     std::unordered_map<unsigned, Type> &temps,
                     std::unordered_set<unsigned> &defined)
{
    recordResult(in, Type(Type::Kind::Ptr), temps, defined);
    return true;
}

bool validateBlockParams(const Function &fn,
                         const BasicBlock &bb,
                         std::unordered_map<unsigned, Type> &temps,
                         std::unordered_set<unsigned> &defined,
                         std::vector<unsigned> &paramIds,
                         std::ostream &err)
{
    bool ok = true;
    std::unordered_set<std::string> paramNames;
    for (const auto &p : bb.params)
    {
        if (!paramNames.insert(p.name).second)
        {
            err << fn.name << ":" << bb.label << ": duplicate param %" << p.name << "\n";
            ok = false;
        }
        if (p.type.kind == Type::Kind::Void)
        {
            err << fn.name << ":" << bb.label << ": param %" << p.name << " has void type\n";
            ok = false;
        }
        temps[p.id] = p.type;
        defined.insert(p.id);
        paramIds.push_back(p.id);
    }
    return ok;
}

using VerifyInstrFn = bool (*)(const Function &fn,
                               const BasicBlock &bb,
                               const Instr &in,
                               const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                               const std::unordered_map<std::string, const Extern *> &externs,
                               const std::unordered_map<std::string, const Function *> &funcs,
                               std::unordered_map<unsigned, Type> &temps,
                               std::unordered_set<unsigned> &defined,
                               std::ostream &err);

bool iterateBlockInstructions(VerifyInstrFn verifyInstrFn,
                              const Function &fn,
                              const BasicBlock &bb,
                              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              std::unordered_map<unsigned, Type> &temps,
                              std::unordered_set<unsigned> &defined,
                              std::ostream &err)
{
    bool ok = true;
    for (const auto &in : bb.instructions)
    {
        for (const auto &op : in.operands)
            if (op.kind == Value::Kind::Temp && !defined.count(op.id))
            {
                err << fn.name << ":" << bb.label << ": " << snippet(in) << ": use before def of %"
                    << op.id << "\n";
                ok = false;
            }

        ok &= verifyInstrFn(fn, bb, in, blockMap, externs, funcs, temps, defined, err);

        if (isTerminator(in.op))
            break;
    }
    return ok;
}

bool checkBlockTerminators(const Function &fn, const BasicBlock &bb, std::ostream &err)
{
    if (bb.instructions.empty())
    {
        err << fn.name << ":" << bb.label << ": empty block\n";
        return false;
    }

    bool ok = true;
    bool seenTerm = false;
    for (const auto &in : bb.instructions)
    {
        if (isTerminator(in.op))
        {
            if (seenTerm)
            {
                err << fn.name << ":" << bb.label << ": " << snippet(in)
                    << ": multiple terminators\n";
                ok = false;
                break;
            }
            seenTerm = true;
        }
        else if (seenTerm)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in)
                << ": instruction after terminator\n";
            ok = false;
            break;
        }
    }

    if (ok && !isTerminator(bb.instructions.back().op))
    {
        err << fn.name << ":" << bb.label << ": missing terminator\n";
        ok = false;
    }

    return ok;
}

bool verifyCall(const Function &fn,
                const BasicBlock &bb,
                const Instr &in,
                const std::unordered_map<std::string, const Extern *> &externs,
                const std::unordered_map<std::string, const Function *> &funcs,
                std::unordered_map<unsigned, Type> &temps,
                std::unordered_set<unsigned> &defined,
                std::ostream &err)
{
    bool ok = true;
    const Extern *sig = nullptr;
    const Function *fnSig = nullptr;
    auto itE = externs.find(in.callee);
    if (itE != externs.end())
        sig = itE->second;
    else
    {
        auto itF = funcs.find(in.callee);
        if (itF != funcs.end())
            fnSig = itF->second;
    }
    if (!sig && !fnSig)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": unknown callee @"
            << in.callee << "\n";
        return false;
    }
    size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
    if (in.operands.size() != paramCount)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": call arg count mismatch\n";
        ok = false;
    }
    for (size_t i = 0; i < in.operands.size() && i < paramCount; ++i)
    {
        Type expected = sig ? sig->params[i] : fnSig->params[i].type;
        if (valueType(in.operands[i], temps).kind != expected.kind)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in)
                << ": call arg type mismatch\n";
            ok = false;
        }
    }
    if (in.result)
    {
        Type ret = sig ? sig->retType : fnSig->retType;
        recordResult(in, ret, temps, defined);
    }
    return ok;
}

bool verifyBr(const Function &fn,
              const BasicBlock &bb,
              const Instr &in,
              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
              std::unordered_map<unsigned, Type> &temps,
              std::ostream &err)
{
    bool ok = true;
    bool argsOk = in.operands.empty() && in.labels.size() == 1;
    if (!argsOk)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in) << ": branch mismatch\n";
        return false;
    }
    auto itB = blockMap.find(in.labels[0]);
    if (itB != blockMap.end())
    {
        const BasicBlock &tgt = *itB->second;
        const std::vector<Value> *argsVec = in.brArgs.size() > 0 ? &in.brArgs[0] : nullptr;
        size_t argCount = argsVec ? argsVec->size() : 0;
        if (argCount != tgt.params.size())
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in)
                << ": branch arg count mismatch for label " << in.labels[0] << "\n";
            ok = false;
        }
        else
        {
            for (size_t i = 0; i < argCount; ++i)
                if (valueType((*argsVec)[i], temps).kind != tgt.params[i].type.kind)
                {
                    err << fn.name << ":" << bb.label << ": " << snippet(in)
                        << ": arg type mismatch for label " << in.labels[0] << "\n";
                    ok = false;
                    break;
                }
        }
    }
    return ok;
}

bool verifyCBr(const Function &fn,
               const BasicBlock &bb,
               const Instr &in,
               const std::unordered_map<std::string, const BasicBlock *> &blockMap,
               std::unordered_map<unsigned, Type> &temps,
               std::ostream &err)
{
    bool ok = true;
    bool condOk = in.operands.size() == 1 && in.labels.size() == 2 &&
                  valueType(in.operands[0], temps).kind == Type::Kind::I1;
    if (!condOk)
    {
        err << fn.name << ":" << bb.label << ": " << snippet(in)
            << ": conditional branch mismatch\n";
        return false;
    }
    for (size_t t = 0; t < 2; ++t)
    {
        auto itB = blockMap.find(in.labels[t]);
        if (itB == blockMap.end())
            continue;
        const BasicBlock &tgt = *itB->second;
        const std::vector<Value> *argsVec = in.brArgs.size() > t ? &in.brArgs[t] : nullptr;
        size_t argCount = argsVec ? argsVec->size() : 0;
        if (argCount != tgt.params.size())
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in)
                << ": branch arg count mismatch for label " << in.labels[t] << "\n";
            ok = false;
        }
        else
        {
            for (size_t i = 0; i < argCount; ++i)
                if (valueType((*argsVec)[i], temps).kind != tgt.params[i].type.kind)
                {
                    err << fn.name << ":" << bb.label << ": " << snippet(in)
                        << ": arg type mismatch for label " << in.labels[t] << "\n";
                    ok = false;
                    break;
                }
        }
    }
    return ok;
}

bool verifyRet(const Function &fn,
               const BasicBlock &bb,
               const Instr &in,
               std::unordered_map<unsigned, Type> &temps,
               std::ostream &err)
{
    bool ok = true;
    if (fn.retType.kind == Type::Kind::Void)
    {
        if (!in.operands.empty())
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in) << ": ret void with value\n";
            ok = false;
        }
    }
    else
    {
        if (in.operands.size() != 1 || valueType(in.operands[0], temps).kind != fn.retType.kind)
        {
            err << fn.name << ":" << bb.label << ": " << snippet(in)
                << ": ret value type mismatch\n";
            ok = false;
        }
    }
    return ok;
}

bool verifyDefault(const Instr &in,
                   std::unordered_map<unsigned, Type> &temps,
                   std::unordered_set<unsigned> &defined)
{
    recordResult(in, in.type, temps, defined);
    return true;
}

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

    std::vector<unsigned> paramIds;
    ok &= validateBlockParams(fn, bb, temps, defined, paramIds, err);
    auto verifyInstrFn = &Verifier::verifyInstr;
    ok &= iterateBlockInstructions(
        verifyInstrFn, fn, bb, blockMap, externs, funcs, temps, defined, err);
    ok &= checkBlockTerminators(fn, bb, err);

    for (unsigned id : paramIds)
        temps.erase(id);

    return ok;
}

bool Verifier::verifyInstr(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &in,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           std::unordered_map<unsigned, Type> &temps,
                           std::unordered_set<unsigned> &defined,
                           std::ostream &err)
{
    switch (in.op)
    {
        case Opcode::Alloca:
            return verifyAlloca(fn, bb, in, temps, defined, err);
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            return verifyIntBinary(fn, bb, in, temps, defined, err);
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            return verifyFloatBinary(fn, bb, in, temps, defined, err);
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
            return verifyICmp(fn, bb, in, temps, defined, err);
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return verifyFCmp(fn, bb, in, temps, defined, err);
        case Opcode::Sitofp:
            return verifySitofp(fn, bb, in, temps, defined, err);
        case Opcode::Fptosi:
            return verifyFptosi(fn, bb, in, temps, defined, err);
        case Opcode::Zext1:
            return verifyZext1(fn, bb, in, temps, defined, err);
        case Opcode::Trunc1:
            return verifyTrunc1(fn, bb, in, temps, defined, err);
        case Opcode::GEP:
            return verifyGEP(fn, bb, in, temps, defined, err);
        case Opcode::Load:
            return verifyLoad(fn, bb, in, temps, defined, err);
        case Opcode::Store:
            return verifyStore(fn, bb, in, temps, err);
        case Opcode::AddrOf:
            return verifyAddrOf(fn, bb, in, temps, defined, err);
        case Opcode::ConstStr:
            return verifyConstStr(fn, bb, in, temps, defined, err);
        case Opcode::ConstNull:
            return verifyConstNull(in, temps, defined);
        case Opcode::Call:
            return verifyCall(fn, bb, in, externs, funcs, temps, defined, err);
        case Opcode::Br:
            return verifyBr(fn, bb, in, blockMap, temps, err);
        case Opcode::CBr:
            return verifyCBr(fn, bb, in, blockMap, temps, err);
        case Opcode::Ret:
            return verifyRet(fn, bb, in, temps, err);
        default:
            return verifyDefault(in, temps, defined);
    }
}

} // namespace il::verify
