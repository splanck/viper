// File: src/il/verify/InstructionChecker.cpp
// Purpose: Implements helpers that validate non-control IL instructions.
// Key invariants: Relies on TypeInference to keep operand types consistent.
// Ownership/Lifetime: Functions operate on caller-provided structures.
// Links: docs/il-spec.md

#include "il/verify/InstructionChecker.hpp"
#include "il/core/Opcode.hpp"

using namespace il::core;

namespace il::verify
{

namespace
{

bool expectOperandCount(const Function &fn,
                        const BasicBlock &bb,
                        const Instr &instr,
                        size_t expected,
                        std::ostream &err)
{
    if (instr.operands.size() != expected)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": expected " << expected
            << " operand" << (expected == 1 ? "" : "s") << "\n";
        return false;
    }
    return true;
}

bool expectAllOperandType(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          TypeInference &types,
                          Type::Kind kind,
                          std::ostream &err)
{
    bool ok = true;
    for (const auto &op : instr.operands)
        if (types.valueType(op).kind != kind)
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": operand type mismatch\n";
            ok = false;
        }
    return ok;
}

bool verifyAlloca(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &instr,
                  TypeInference &types,
                  std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 1, err);
    if (instr.operands.size() < 1)
        return false;
    if (types.valueType(instr.operands[0]).kind != Type::Kind::I64)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": size must be i64\n";
        ok = false;
    }
    if (instr.operands[0].kind == Value::Kind::ConstInt)
    {
        long long sz = instr.operands[0].i64;
        if (sz < 0)
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": negative alloca size\n";
            ok = false;
        }
        else if (sz > (1LL << 20))
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": warning: huge alloca\n";
        }
    }
    types.recordResult(instr, Type(Type::Kind::Ptr));
    return ok;
}

bool verifyIntBinary(const Function &fn,
                     const BasicBlock &bb,
                     const Instr &instr,
                     TypeInference &types,
                     std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    ok &= expectAllOperandType(fn, bb, instr, types, Type::Kind::I64, err);
    types.recordResult(instr, Type(Type::Kind::I64));
    return ok;
}

bool verifyFloatBinary(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &instr,
                       TypeInference &types,
                       std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    ok &= expectAllOperandType(fn, bb, instr, types, Type::Kind::F64, err);
    types.recordResult(instr, Type(Type::Kind::F64));
    return ok;
}

bool verifyICmp(const Function &fn,
                const BasicBlock &bb,
                const Instr &instr,
                TypeInference &types,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    ok &= expectAllOperandType(fn, bb, instr, types, Type::Kind::I64, err);
    types.recordResult(instr, Type(Type::Kind::I1));
    return ok;
}

bool verifyFCmp(const Function &fn,
                const BasicBlock &bb,
                const Instr &instr,
                TypeInference &types,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    ok &= expectAllOperandType(fn, bb, instr, types, Type::Kind::F64, err);
    types.recordResult(instr, Type(Type::Kind::I1));
    return ok;
}

bool verifyUnaryExpected(const Function &fn,
                         const BasicBlock &bb,
                         const Instr &instr,
                         TypeInference &types,
                         Type::Kind operandKind,
                         Type resultType,
                         std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 1, err);
    if (instr.operands.size() < 1)
        return false;
    if (types.valueType(instr.operands[0]).kind != operandKind)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": operand type mismatch\n";
        ok = false;
    }
    types.recordResult(instr, resultType);
    return ok;
}

bool verifyGEP(const Function &fn,
               const BasicBlock &bb,
               const Instr &instr,
               TypeInference &types,
               std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr ||
        types.valueType(instr.operands[1]).kind != Type::Kind::I64)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": operand type mismatch\n";
        ok = false;
    }
    types.recordResult(instr, Type(Type::Kind::Ptr));
    return ok;
}

bool verifyLoad(const Function &fn,
                const BasicBlock &bb,
                const Instr &instr,
                TypeInference &types,
                std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 1, err);
    if (instr.operands.size() < 1)
        return false;
    if (instr.type.kind == Type::Kind::Void)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": void load type\n";
        ok = false;
    }
    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": pointer type mismatch\n";
        ok = false;
    }
    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
    types.recordResult(instr, instr.type);
    return ok;
}

bool verifyStore(const Function &fn,
                 const BasicBlock &bb,
                 const Instr &instr,
                 TypeInference &types,
                 std::ostream &err)
{
    bool ok = expectOperandCount(fn, bb, instr, 2, err);
    if (instr.operands.size() < 2)
        return false;
    if (instr.type.kind == Type::Kind::Void)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": void store type\n";
        ok = false;
    }
    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": pointer type mismatch\n";
        ok = false;
    }
    if (types.valueType(instr.operands[1]).kind != instr.type.kind)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": value type mismatch\n";
        ok = false;
    }
    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
    return ok;
}

bool verifyAddrOf(const Function &fn,
                  const BasicBlock &bb,
                  const Instr &instr,
                  TypeInference &types,
                  std::ostream &err)
{
    bool ok = true;
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": operand must be global\n";
        ok = false;
    }
    types.recordResult(instr, Type(Type::Kind::Ptr));
    return ok;
}

bool verifyConstStr(const Function &fn,
                    const BasicBlock &bb,
                    const Instr &instr,
                    TypeInference &types,
                    std::ostream &err)
{
    bool ok = true;
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": unknown string global\n";
        ok = false;
    }
    types.recordResult(instr, Type(Type::Kind::Str));
    return ok;
}

bool verifyConstNull(const Instr &instr, TypeInference &types)
{
    types.recordResult(instr, Type(Type::Kind::Ptr));
    return true;
}

bool verifyCall(const Function &fn,
                const BasicBlock &bb,
                const Instr &instr,
                const std::unordered_map<std::string, const Extern *> &externs,
                const std::unordered_map<std::string, const Function *> &funcs,
                TypeInference &types,
                std::ostream &err)
{
    bool ok = true;
    const Extern *sig = nullptr;
    const Function *fnSig = nullptr;
    auto itE = externs.find(instr.callee);
    if (itE != externs.end())
        sig = itE->second;
    else
    {
        auto itF = funcs.find(instr.callee);
        if (itF != funcs.end())
            fnSig = itF->second;
    }
    if (!sig && !fnSig)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": unknown callee @" << instr.callee
            << "\n";
        return false;
    }
    size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
    if (instr.operands.size() != paramCount)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": call arg count mismatch\n";
        ok = false;
    }
    size_t checked = std::min(instr.operands.size(), paramCount);
    for (size_t i = 0; i < checked; ++i)
    {
        Type expected = sig ? sig->params[i] : fnSig->params[i].type;
        if (types.valueType(instr.operands[i]).kind != expected.kind)
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": call arg type mismatch\n";
            ok = false;
        }
    }
    if (instr.result)
    {
        Type ret = sig ? sig->retType : fnSig->retType;
        types.recordResult(instr, ret);
    }
    return ok;
}

bool verifyDefault(const Instr &instr, TypeInference &types)
{
    types.recordResult(instr, instr.type);
    return true;
}

} // namespace

bool verifyInstruction(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &instr,
                       const std::unordered_map<std::string, const Extern *> &externs,
                       const std::unordered_map<std::string, const Function *> &funcs,
                       TypeInference &types,
                       std::ostream &err)
{
    switch (instr.op)
    {
        case Opcode::Alloca:
            return verifyAlloca(fn, bb, instr, types, err);
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
            return verifyIntBinary(fn, bb, instr, types, err);
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            return verifyFloatBinary(fn, bb, instr, types, err);
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
            return verifyICmp(fn, bb, instr, types, err);
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return verifyFCmp(fn, bb, instr, types, err);
        case Opcode::Sitofp:
            return verifyUnaryExpected(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::F64), err);
        case Opcode::Fptosi:
            return verifyUnaryExpected(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::I64), err);
        case Opcode::Zext1:
            return verifyUnaryExpected(fn, bb, instr, types, Type::Kind::I1, Type(Type::Kind::I64), err);
        case Opcode::Trunc1:
            return verifyUnaryExpected(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I1), err);
        case Opcode::GEP:
            return verifyGEP(fn, bb, instr, types, err);
        case Opcode::Load:
            return verifyLoad(fn, bb, instr, types, err);
        case Opcode::Store:
            return verifyStore(fn, bb, instr, types, err);
        case Opcode::AddrOf:
            return verifyAddrOf(fn, bb, instr, types, err);
        case Opcode::ConstStr:
            return verifyConstStr(fn, bb, instr, types, err);
        case Opcode::ConstNull:
            return verifyConstNull(instr, types);
        case Opcode::Call:
            return verifyCall(fn, bb, instr, externs, funcs, types, err);
        default:
            return verifyDefault(instr, types);
    }
}

} // namespace il::verify
