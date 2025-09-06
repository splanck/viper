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

} // namespace

bool Verifier::verify(const Module &m, std::ostream &err)
{
    bool ok = true;

    // Track externs/globals/functions and check for duplicates/signature mismatches.
    std::unordered_map<std::string, const Extern *> externs;
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

    std::unordered_map<std::string, const Global *> globals;
    for (const auto &g : m.globals)
    {
        if (!globals.emplace(g.name, &g).second)
        {
            err << "duplicate global @" << g.name << "\n";
            ok = false;
        }
    }

    std::unordered_map<std::string, const Function *> funcs;
    for (const auto &f : m.functions)
    {
        auto [it, ins] = funcs.emplace(f.name, &f);
        if (!ins)
        {
            err << "duplicate function @" << f.name << "\n";
            ok = false;
        }
        auto itExt = externs.find(f.name);
        if (itExt != externs.end())
        {
            const Extern *e = itExt->second;
            bool sigOk = e->retType.kind == f.retType.kind && e->params.size() == f.params.size();
            if (sigOk)
                for (size_t i = 0; i < e->params.size(); ++i)
                    if (e->params[i].kind != f.params[i].type.kind)
                        sigOk = false;
            if (!sigOk)
            {
                err << "function @" << f.name << " signature mismatch with extern\n";
                ok = false;
            }
        }
    }

    for (const auto &fn : m.functions)
    {
        if (fn.blocks.empty())
        {
            err << fn.name << ": function has no blocks\n";
            ok = false;
            continue;
        }
        if (fn.blocks.front().label != "entry")
        {
            err << fn.name << ": first block must be entry\n";
            ok = false;
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
        {
            if (bb.instructions.empty())
            {
                err << fn.name << ":" << bb.label << ": empty block\n";
                ok = false;
                continue;
            }

            bool seenTerm = false;
            std::unordered_set<unsigned> defined; // track temps defined in this block
            for (const auto &kv : temps)
                defined.insert(kv.first);

            std::unordered_set<std::string> paramNames;
            std::vector<unsigned> paramIds;
            for (const auto &p : bb.params)
            {
                if (!paramNames.insert(p.name).second)
                {
                    err << fn.name << ":" << bb.label << ": duplicate param %" << p.name << "\n";
                    ok = false;
                }
                if (p.type.kind == Type::Kind::Void)
                {
                    err << fn.name << ":" << bb.label << ": param %" << p.name
                        << " has void type\n";
                    ok = false;
                }
                temps[p.id] = p.type;
                defined.insert(p.id);
                paramIds.push_back(p.id);
            }
            for (const auto &in : bb.instructions)
            {
                // Use-before-def within block (full dominance analysis TODO).
                for (const auto &op : in.operands)
                    if (op.kind == Value::Kind::Temp && !defined.count(op.id))
                    {
                        err << fn.name << ":" << bb.label << ": " << snippet(in)
                            << ": use before def of %" << op.id << "\n";
                        ok = false;
                    }

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

                switch (in.op)
                {
                    case Opcode::Alloca:
                    {
                        if (in.operands.size() != 1)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 1 operand\n";
                            ok = false;
                        }
                        if (valueType(in.operands[0], temps).kind != Type::Kind::I64)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": size must be i64\n";
                            ok = false;
                        }
                        if (in.operands[0].kind == Value::Kind::ConstInt)
                        {
                            long long sz = in.operands[0].i64;
                            if (sz < 0)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": negative alloca size\n";
                                ok = false;
                            }
                            else if (sz > (1LL << 20))
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": warning: huge alloca\n";
                            }
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::Ptr);
                            defined.insert(*in.result);
                        }
                        break;
                    }
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
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        for (const auto &op : in.operands)
                            if (valueType(op, temps).kind != Type::Kind::I64)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": operand type mismatch\n";
                                ok = false;
                            }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I64);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::FAdd:
                    case Opcode::FSub:
                    case Opcode::FMul:
                    case Opcode::FDiv:
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        for (const auto &op : in.operands)
                            if (valueType(op, temps).kind != Type::Kind::F64)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": operand type mismatch\n";
                                ok = false;
                            }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::F64);
                            defined.insert(*in.result);
                        }
                        break;
                    }
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
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        for (const auto &op : in.operands)
                            if (valueType(op, temps).kind != Type::Kind::I64)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": operand type mismatch\n";
                                ok = false;
                            }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I1);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::FCmpEQ:
                    case Opcode::FCmpNE:
                    case Opcode::FCmpLT:
                    case Opcode::FCmpLE:
                    case Opcode::FCmpGT:
                    case Opcode::FCmpGE:
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        for (const auto &op : in.operands)
                            if (valueType(op, temps).kind != Type::Kind::F64)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": operand type mismatch\n";
                                ok = false;
                            }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I1);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Sitofp:
                    {
                        if (in.operands.size() != 1 ||
                            valueType(in.operands[0], temps).kind != Type::Kind::I64)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": operand type mismatch\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::F64);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Fptosi:
                    {
                        if (in.operands.size() != 1 ||
                            valueType(in.operands[0], temps).kind != Type::Kind::F64)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": operand type mismatch\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I64);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Zext1:
                    {
                        if (in.operands.size() != 1 ||
                            valueType(in.operands[0], temps).kind != Type::Kind::I1)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": operand type mismatch\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I64);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Trunc1:
                    {
                        if (in.operands.size() != 1 ||
                            valueType(in.operands[0], temps).kind != Type::Kind::I64)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": operand type mismatch\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::I1);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::GEP:
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        else
                        {
                            if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr ||
                                valueType(in.operands[1], temps).kind != Type::Kind::I64)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": operand type mismatch\n";
                                ok = false;
                            }
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::Ptr);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Load:
                    {
                        if (in.operands.size() != 1)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 1 operand\n";
                            ok = false;
                        }
                        if (in.type.kind == Type::Kind::Void)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": void load type\n";
                            ok = false;
                        }
                        if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": pointer type mismatch\n";
                            ok = false;
                        }
                        [[maybe_unused]] size_t sz =
                            typeSize(in.type.kind); // TODO: natural alignment
                        if (in.result)
                        {
                            temps[*in.result] = in.type;
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Store:
                    {
                        if (in.operands.size() != 2)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 2 operands\n";
                            ok = false;
                        }
                        if (in.type.kind == Type::Kind::Void)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": void store type\n";
                            ok = false;
                        }
                        if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": pointer type mismatch\n";
                            ok = false;
                        }
                        if (valueType(in.operands[1], temps).kind != in.type.kind)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": value type mismatch\n";
                            ok = false;
                        }
                        [[maybe_unused]] size_t sz =
                            typeSize(in.type.kind); // TODO: natural alignment
                        break;
                    }
                    case Opcode::AddrOf:
                    {
                        if (in.operands.size() != 1 ||
                            in.operands[0].kind != Value::Kind::GlobalAddr)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": operand must be global\n";
                            ok = false;
                        }
                        else if (!globals.count(in.operands[0].str))
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": unknown global @" << in.operands[0].str << "\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::Ptr);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::ConstStr:
                    {
                        if (in.operands.size() != 1 ||
                            in.operands[0].kind != Value::Kind::GlobalAddr ||
                            !globals.count(in.operands[0].str))
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": unknown string global\n";
                            ok = false;
                        }
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::Str);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::ConstNull:
                    {
                        if (in.result)
                        {
                            temps[*in.result] = Type(Type::Kind::Ptr);
                            defined.insert(*in.result);
                        }
                        break;
                    }
                    case Opcode::Call:
                    {
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
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": unknown callee @" << in.callee << "\n";
                            ok = false;
                            break;
                        }
                        size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
                        if (in.operands.size() != paramCount)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": call arg count mismatch\n";
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
                        Type retTy = sig ? sig->retType : fnSig->retType;
                        if (retTy.kind == Type::Kind::Void)
                        {
                            if (in.result)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": void call with result\n";
                                ok = false;
                            }
                        }
                        else
                        {
                            if (!in.result)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": call missing result\n";
                                ok = false;
                            }
                            else
                            {
                                temps[*in.result] = retTy;
                                defined.insert(*in.result);
                            }
                        }
                        break;
                    }
                    case Opcode::Br:
                    {
                        if (in.labels.size() != 1)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": expected 1 label\n";
                            ok = false;
                        }
                        else
                        {
                            const std::vector<Value> *argsVec =
                                in.brArgs.size() > 0 ? &in.brArgs[0] : nullptr;
                            auto itB = blockMap.find(in.labels[0]);
                            if (itB != blockMap.end())
                            {
                                const BasicBlock &tgt = *itB->second;
                                size_t argCount = argsVec ? argsVec->size() : 0;
                                if (argCount != tgt.params.size())
                                {
                                    err << fn.name << ":" << bb.label << ": " << snippet(in)
                                        << ": branch arg count mismatch for label " << in.labels[0]
                                        << "\n";
                                    ok = false;
                                }
                                else
                                {
                                    for (size_t i = 0; i < argCount; ++i)
                                        if (valueType((*argsVec)[i], temps).kind !=
                                            tgt.params[i].type.kind)
                                        {
                                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                                << ": arg type mismatch for label " << in.labels[0]
                                                << "\n";
                                            ok = false;
                                            break;
                                        }
                                }
                            }
                        }
                        break;
                    }
                    case Opcode::CBr:
                    {
                        bool condOk = in.operands.size() == 1 && in.labels.size() == 2 &&
                                      valueType(in.operands[0], temps).kind == Type::Kind::I1;
                        if (!condOk)
                        {
                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                << ": conditional branch mismatch\n";
                            ok = false;
                        }
                        else
                        {
                            for (size_t t = 0; t < 2; ++t)
                            {
                                auto itB = blockMap.find(in.labels[t]);
                                if (itB == blockMap.end())
                                    continue;
                                const BasicBlock &tgt = *itB->second;
                                const std::vector<Value> *argsVec =
                                    in.brArgs.size() > t ? &in.brArgs[t] : nullptr;
                                size_t argCount = argsVec ? argsVec->size() : 0;
                                if (argCount != tgt.params.size())
                                {
                                    err << fn.name << ":" << bb.label << ": " << snippet(in)
                                        << ": branch arg count mismatch for label " << in.labels[t]
                                        << "\n";
                                    ok = false;
                                }
                                else
                                {
                                    for (size_t i = 0; i < argCount; ++i)
                                        if (valueType((*argsVec)[i], temps).kind !=
                                            tgt.params[i].type.kind)
                                        {
                                            err << fn.name << ":" << bb.label << ": " << snippet(in)
                                                << ": arg type mismatch for label " << in.labels[t]
                                                << "\n";
                                            ok = false;
                                            break;
                                        }
                                }
                            }
                        }
                        break;
                    }
                    case Opcode::Ret:
                    {
                        if (fn.retType.kind == Type::Kind::Void)
                        {
                            if (!in.operands.empty())
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": ret void with value\n";
                                ok = false;
                            }
                        }
                        else
                        {
                            if (in.operands.size() != 1 ||
                                valueType(in.operands[0], temps).kind != fn.retType.kind)
                            {
                                err << fn.name << ":" << bb.label << ": " << snippet(in)
                                    << ": ret value type mismatch\n";
                                ok = false;
                            }
                        }
                        break;
                    }
                    default:
                        if (in.result)
                        {
                            temps[*in.result] = in.type;
                            defined.insert(*in.result);
                        }
                        break;
                }
            }
            if (!bb.instructions.empty() && !isTerminator(bb.instructions.back().op))
            {
                err << fn.name << ":" << bb.label << ": missing terminator\n";
                ok = false;
            }
            for (unsigned id : paramIds)
                temps.erase(id);
        }

        // verify referenced labels exist
        for (const auto &bb : fn.blocks)
            for (const auto &in : bb.instructions)
                for (const auto &lbl : in.labels)
                    if (!labels.count(lbl))
                    {
                        err << fn.name << ": unknown label " << lbl << "\n";
                        ok = false;
                    }
    }
    return ok;
}

} // namespace il::verify
