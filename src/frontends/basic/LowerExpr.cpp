// File: src/frontends/basic/LowerExpr.cpp
// Purpose: Implements expression lowering helpers for the BASIC front end.
// Key invariants: Expression lowering preserves operand types, injecting
//                 conversions to match IL expectations and runtime helpers.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

// Purpose: lower var expr.
// Parameters: const VarExpr &v.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerVarExpr(const VarExpr &v)
{
    curLoc = v.loc;
    auto it = varSlots.find(v.name);
    assert(it != varSlots.end());
    Value ptr = Value::temp(it->second);
    bool isArray = arrays.count(v.name);
    bool isStr = !v.name.empty() && v.name.back() == '$';
    bool isF64 = !v.name.empty() && v.name.back() == '#';
    bool isBoolVar = false;
    auto typeIt = varTypes.find(v.name);
    if (typeIt != varTypes.end() && typeIt->second == AstType::Bool)
        isBoolVar = true;
    Type ty = isArray ? Type(Type::Kind::Ptr)
                      : (isStr ? Type(Type::Kind::Str)
                               : (isF64 ? Type(Type::Kind::F64)
                                        : (isBoolVar ? ilBoolTy() : Type(Type::Kind::I64))));
    Value val = emitLoad(ty, ptr);
    return {val, ty};
}

// Purpose: lower unary expr.
// Parameters: const UnaryExpr &u.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerUnaryExpr(const UnaryExpr &u)
{
    RVal val = lowerExpr(*u.expr);
    curLoc = u.loc;
    Value cond = val.value;
    if (val.type.kind != Type::Kind::I1)
        cond = emitUnary(Opcode::Trunc1, ilBoolTy(), cond);

    BasicBlock *origin = cur;
    BasicBlock *thenBlk = nullptr;
    BasicBlock *elseBlk = nullptr;
    Value slot;
    Value *prevSlotPtr = boolBranchSlotPtr;
    boolBranchSlotPtr = &slot;
    IlValue result = emitBoolFromBranches(
        [&]() {
            thenBlk = cur;
            curLoc = u.loc;
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
        },
        [&]() {
            elseBlk = cur;
            curLoc = u.loc;
            emitStore(ilBoolTy(), slot, emitBoolConst(true));
        });
    boolBranchSlotPtr = prevSlotPtr;

    BasicBlock *joinBlk = cur;
    cur = origin;
    curLoc = u.loc;
    emitCBr(cond, thenBlk, elseBlk);
    cur = joinBlk;
    return {result, ilBoolTy()};
}

// Purpose: lower logical binary.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    curLoc = b.loc;

    if (b.op == BinaryExpr::Op::LogicalOr)
    {
        if (auto *rhsBool = dynamic_cast<const BoolExpr *>(b.rhs.get()); rhsBool && !rhsBool->value)
        {
            Value cond = lhs.value;
            if (lhs.type.kind != Type::Kind::I1)
            {
                curLoc = b.loc;
                cond = emitUnary(Opcode::Trunc1, ilBoolTy(), cond);
            }

            BasicBlock *origin = cur;
            BasicBlock *thenBlk = nullptr;
            BasicBlock *elseBlk = nullptr;
            Value slot;
            Value *prevSlotPtr = boolBranchSlotPtr;
            boolBranchSlotPtr = &slot;
            IlValue result = emitBoolFromBranches(
                [&]() {
                    thenBlk = cur;
                    curLoc = b.loc;
                    emitStore(ilBoolTy(), slot, emitBoolConst(true));
                },
                [&]() {
                    elseBlk = cur;
                    curLoc = b.loc;
                    emitStore(ilBoolTy(), slot, emitBoolConst(false));
                });
            boolBranchSlotPtr = prevSlotPtr;
            BasicBlock *joinBlk = cur;
            cur = origin;
            curLoc = b.loc;
            emitCBr(cond, thenBlk, elseBlk);
            cur = joinBlk;
            return {result, ilBoolTy()};
        }
    }

    Value addr = emitAlloca(1);
    if (b.op == BinaryExpr::Op::LogicalAndShort || b.op == BinaryExpr::Op::LogicalAnd)
    {
        std::string rhsLbl = blockNamer ? blockNamer->generic("and_rhs") : mangler.block("and_rhs");
        std::string falseLbl =
            blockNamer ? blockNamer->generic("and_false") : mangler.block("and_false");
        std::string doneLbl =
            blockNamer ? blockNamer->generic("and_done") : mangler.block("and_done");
        BasicBlock *rhsBB = &builder->addBlock(*func, rhsLbl);
        BasicBlock *falseBB = &builder->addBlock(*func, falseLbl);
        BasicBlock *doneBB = &builder->addBlock(*func, doneLbl);
        curLoc = b.loc;
        emitCBr(lhs.value, rhsBB, falseBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b.rhs);
        curLoc = b.loc;
        emitStore(ilBoolTy(), addr, rhs.value);
        curLoc = b.loc;
        emitBr(doneBB);
        cur = falseBB;
        curLoc = b.loc;
        emitStore(ilBoolTy(), addr, emitBoolConst(false));
        curLoc = b.loc;
        emitBr(doneBB);
        cur = doneBB;
    }
    else
    {
        std::string trueLbl =
            blockNamer ? blockNamer->generic("or_true") : mangler.block("or_true");
        std::string rhsLbl = blockNamer ? blockNamer->generic("or_rhs") : mangler.block("or_rhs");
        std::string doneLbl =
            blockNamer ? blockNamer->generic("or_done") : mangler.block("or_done");
        BasicBlock *trueBB = &builder->addBlock(*func, trueLbl);
        BasicBlock *rhsBB = &builder->addBlock(*func, rhsLbl);
        BasicBlock *doneBB = &builder->addBlock(*func, doneLbl);
        curLoc = b.loc;
        emitCBr(lhs.value, trueBB, rhsBB);
        cur = trueBB;
        curLoc = b.loc;
        emitStore(ilBoolTy(), addr, emitBoolConst(true));
        curLoc = b.loc;
        emitBr(doneBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b.rhs);
        curLoc = b.loc;
        emitStore(ilBoolTy(), addr, rhs.value);
        curLoc = b.loc;
        emitBr(doneBB);
        cur = doneBB;
    }
    curLoc = b.loc;
    Value res = emitLoad(ilBoolTy(), addr);
    return {res, ilBoolTy()};
}

// Purpose: lower div or mod.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    curLoc = b.loc;
    Value cond = emitBinary(Opcode::ICmpEq, ilBoolTy(), rhs.value, Value::constInt(0));
    std::string trapLbl = blockNamer ? blockNamer->generic("div0") : mangler.block("div0");
    std::string okLbl = blockNamer ? blockNamer->generic("divok") : mangler.block("divok");
    BasicBlock *trapBB = &builder->addBlock(*func, trapLbl);
    BasicBlock *okBB = &builder->addBlock(*func, okLbl);
    emitCBr(cond, trapBB, okBB);
    cur = trapBB;
    curLoc = b.loc;
    emitTrap();
    cur = okBB;
    curLoc = b.loc;
    Opcode op = (b.op == BinaryExpr::Op::IDiv) ? Opcode::SDiv : Opcode::SRem;
    Value res = emitBinary(op, Type(Type::Kind::I64), lhs.value, rhs.value);
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower string binary.
// Parameters: const BinaryExpr &b, RVal lhs, RVal rhs.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerStringBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (b.op == BinaryExpr::Op::Add)
    {
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_concat", {lhs.value, rhs.value});
        return {res, Type(Type::Kind::Str)};
    }
    Value eq = emitCallRet(ilBoolTy(), "rt_str_eq", {lhs.value, rhs.value});
    if (b.op == BinaryExpr::Op::Ne)
    {
        Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eq);
        Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), z, Value::constInt(1));
        Value res = emitUnary(Opcode::Trunc1, ilBoolTy(), x);
        return {res, ilBoolTy()};
    }
    return {eq, ilBoolTy()};
}

// Purpose: lower numeric binary.
// Parameters: const BinaryExpr &b, RVal lhs, RVal rhs.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerNumericBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (lhs.type.kind == Type::Kind::I64 && rhs.type.kind == Type::Kind::F64)
    {
        lhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), lhs.value);
        lhs.type = Type(Type::Kind::F64);
    }
    else if (lhs.type.kind == Type::Kind::F64 && rhs.type.kind == Type::Kind::I64)
    {
        rhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), rhs.value);
        rhs.type = Type(Type::Kind::F64);
    }
    bool isFloat = lhs.type.kind == Type::Kind::F64;
    Opcode op = Opcode::Add;
    Type ty = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
            op = isFloat ? Opcode::FAdd : Opcode::Add;
            break;
        case BinaryExpr::Op::Sub:
            op = isFloat ? Opcode::FSub : Opcode::Sub;
            break;
        case BinaryExpr::Op::Mul:
            op = isFloat ? Opcode::FMul : Opcode::Mul;
            break;
        case BinaryExpr::Op::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDiv;
            break;
        case BinaryExpr::Op::Eq:
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Ne:
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            ty = ilBoolTy();
            break;
        default:
            break; // other ops handled elsewhere
    }
    Value res = emitBinary(op, ty, lhs.value, rhs.value);
    return {res, ty};
}

// Purpose: lower binary expr.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerBinaryExpr(const BinaryExpr &b)
{
    if (b.op == BinaryExpr::Op::LogicalAndShort || b.op == BinaryExpr::Op::LogicalOrShort ||
        b.op == BinaryExpr::Op::LogicalAnd || b.op == BinaryExpr::Op::LogicalOr)
        return lowerLogicalBinary(b);
    if (b.op == BinaryExpr::Op::IDiv || b.op == BinaryExpr::Op::Mod)
        return lowerDivOrMod(b);

    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    if ((b.op == BinaryExpr::Op::Add || b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne) &&
        lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        return lowerStringBinary(b, lhs, rhs);
    return lowerNumericBinary(b, lhs, rhs);
}

// Purpose: lower arg.
// Parameters: const BuiltinCallExpr &c, size_t idx.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerArg(const BuiltinCallExpr &c, size_t idx)
{
    assert(idx < c.args.size() && c.args[idx]);
    return lowerExpr(*c.args[idx]);
}

// Purpose: ensure i64.
// Parameters: RVal v, il::support::SourceLoc loc.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::ensureI64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::I1)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    else if (v.type.kind == Type::Kind::F64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    return v;
}

// Purpose: ensure f64.
// Parameters: RVal v, il::support::SourceLoc loc.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::ensureF64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::F64)
        return v;
    v = ensureI64(std::move(v), loc);
    if (v.type.kind == Type::Kind::I64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
        v.type = Type(Type::Kind::F64);
    }
    return v;
}

// Purpose: lower rnd.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRnd(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_rnd", {});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower len.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLen(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower mid.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerMid(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal i = ensureI64(lowerArg(c, 1), c.loc);
    Value start0 = emitBinary(Opcode::Add, Type(Type::Kind::I64), i.value, Value::constInt(-1));
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[2])
    {
        RVal n = ensureI64(lowerArg(c, 2), c.loc);
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid3", {s.value, start0, n.value});
        needRtMid3 = true;
        return {res, Type(Type::Kind::Str)};
    }
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid2", {s.value, start0});
    needRtMid2 = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower left.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLeft(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_left", {s.value, n.value});
    needRtLeft = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower right.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRight(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_right", {s.value, n.value});
    needRtRight = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower str.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerStr(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_f64_to_str", {v.value});
        return {res, Type(Type::Kind::Str)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_int_to_str", {v.value});
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower val.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerVal(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {s.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower int.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerInt(const BuiltinCallExpr &c)
{
    RVal f = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), f.value);
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower instr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerInstr(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[0])
    {
        RVal start = ensureI64(lowerArg(c, 0), c.loc);
        Value start0 =
            emitBinary(Opcode::Add, Type(Type::Kind::I64), start.value, Value::constInt(-1));
        RVal hay = lowerArg(c, 1);
        RVal needle = lowerArg(c, 2);
        Value res =
            emitCallRet(Type(Type::Kind::I64), "rt_instr3", {start0, hay.value, needle.value});
        needRtInstr3 = true;
        return {res, Type(Type::Kind::I64)};
    }
    RVal hay = lowerArg(c, 0);
    RVal needle = lowerArg(c, 1);
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_instr2", {hay.value, needle.value});
    needRtInstr2 = true;
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower ltrim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ltrim", {s.value});
    needRtLtrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower rtrim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_rtrim", {s.value});
    needRtRtrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower trim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerTrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_trim", {s.value});
    needRtTrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower ucase.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerUcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ucase", {s.value});
    needRtUcase = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower lcase.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_lcase", {s.value});
    needRtLcase = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower chr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerChr(const BuiltinCallExpr &c)
{
    RVal code = ensureI64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_chr", {code.value});
    needRtChr = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower asc.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerAsc(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_asc", {s.value});
    needRtAsc = true;
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower sqr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerSqr(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sqrt", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower abs.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerAbs(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::F64), "rt_abs_f64", {v.value});
        return {res, Type(Type::Kind::F64)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_abs_i64", {v.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower floor.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerFloor(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_floor", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower ceil.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerCeil(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_ceil", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower sin.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerSin(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sin", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower cos.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerCos(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_cos", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower pow.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerPow(const BuiltinCallExpr &c)
{
    RVal a = ensureF64(lowerArg(c, 0), c.loc);
    RVal b = ensureF64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_pow", {a.value, b.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower builtin call.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerBuiltinCall(const BuiltinCallExpr &c)
{
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.lower)
        return (this->*(info.lower))(c);
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

// Purpose: lower expr.
// Parameters: const Expr &expr.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        return {Value::constInt(i->value), Type(Type::Kind::I64)};
    }
    else if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
    {
        return {Value::constFloat(f->value), Type(Type::Kind::F64)};
    }
    else if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        std::string lbl = getStringLabel(s->value);
        Value tmp = emitConstStr(lbl);
        return {tmp, Type(Type::Kind::Str)};
    }
    else if (auto *b = dynamic_cast<const BoolExpr *>(&expr))
    {
        return {emitBoolConst(b->value), ilBoolTy()};
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&expr))
    {
        return lowerVarExpr(*v);
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        return lowerUnaryExpr(*u);
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr))
    {
        return lowerBinaryExpr(*b);
    }
    else if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&expr))
    {
        return lowerBuiltinCall(*c);
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        const Function *callee = nullptr;
        for (const auto &f : mod->functions)
            if (f.name == c->callee)
            {
                callee = &f;
                break;
            }
        std::vector<Value> args;
        for (size_t i = 0; i < c->args.size(); ++i)
        {
            RVal a = lowerExpr(*c->args[i]);
            if (callee && i < callee->params.size())
            {
                Type paramTy = callee->params[i].type;
                if (paramTy.kind == Type::Kind::F64 && a.type.kind == Type::Kind::I64)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), a.value);
                    a.type = Type(Type::Kind::F64);
                }
                else if (paramTy.kind == Type::Kind::F64 && a.type.kind == Type::Kind::I1)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), a.value);
                    a.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), a.value);
                    a.type = Type(Type::Kind::F64);
                }
                else if (paramTy.kind == Type::Kind::I64 && a.type.kind == Type::Kind::I1)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), a.value);
                    a.type = Type(Type::Kind::I64);
                }
            }
            args.push_back(a.value);
        }
        curLoc = expr.loc;
        if (callee && callee->retType.kind != Type::Kind::Void)
        {
            Value res = emitCallRet(callee->retType, c->callee, args);
            return {res, callee->retType};
        }
        emitCall(c->callee, args);
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&expr))
    {
        Value ptr = lowerArrayAddr(*a);
        curLoc = expr.loc;
        Value val = emitLoad(Type(Type::Kind::I64), ptr);
        return {val, Type(Type::Kind::I64)};
    }
    curLoc = expr.loc;
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::basic

