// File: src/il/core/OpcodeInfo.cpp
// Purpose: Defines metadata describing IL opcode signatures and behaviours.
// Key invariants: Table entries stay in sync with the Opcode enumeration order.
// Ownership/Lifetime: Static storage duration, read-only access via kOpcodeTable.
// Links: docs/il-spec.md

#include "il/core/OpcodeInfo.hpp"

#include <string>

namespace il::core
{

namespace
{
constexpr std::array<TypeCategory, kMaxOperandCategories> makeOperands(TypeCategory a,
                                                                        TypeCategory b = TypeCategory::None,
                                                                        TypeCategory c = TypeCategory::None)
{
    return {a, b, c};
}
} // namespace

const std::array<OpcodeInfo, kNumOpcodes> kOpcodeTable = {
    {
        {"add", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::Add},
        {"sub", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::Sub},
        {"mul", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::Mul},
        {"sdiv", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"udiv", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"srem", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"urem", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"and", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"or", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"xor", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::Xor},
        {"shl", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::Shl},
        {"lshr", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"ashr", ResultArity::One, TypeCategory::I64, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"fadd", ResultArity::One, TypeCategory::F64, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FAdd},
        {"fsub", ResultArity::One, TypeCategory::F64, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FSub},
        {"fmul", ResultArity::One, TypeCategory::F64, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FMul},
        {"fdiv", ResultArity::One, TypeCategory::F64, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FDiv},
        {"icmp_eq", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::ICmpEq},
        {"icmp_ne", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::ICmpNe},
        {"scmp_lt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::SCmpLT},
        {"scmp_le", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::SCmpLE},
        {"scmp_gt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::SCmpGT},
        {"scmp_ge", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::SCmpGE},
        {"ucmp_lt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"ucmp_le", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"ucmp_gt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"ucmp_ge", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::I64, TypeCategory::I64),
         false, 0, false, VMDispatch::None},
        {"fcmp_eq", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpEQ},
        {"fcmp_ne", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpNE},
        {"fcmp_lt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpLT},
        {"fcmp_le", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpLE},
        {"fcmp_gt", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpGT},
        {"fcmp_ge", ResultArity::One, TypeCategory::I1, 2, 2, makeOperands(TypeCategory::F64, TypeCategory::F64),
         false, 0, false, VMDispatch::FCmpGE},
        {"sitofp", ResultArity::One, TypeCategory::F64, 1, 1, makeOperands(TypeCategory::I64),
         false, 0, false, VMDispatch::Sitofp},
        {"fptosi", ResultArity::One, TypeCategory::I64, 1, 1, makeOperands(TypeCategory::F64),
         false, 0, false, VMDispatch::Fptosi},
        {"zext1", ResultArity::One, TypeCategory::I64, 1, 1, makeOperands(TypeCategory::I1),
         false, 0, false, VMDispatch::TruncOrZext1},
        {"trunc1", ResultArity::One, TypeCategory::I1, 1, 1, makeOperands(TypeCategory::I64),
         false, 0, false, VMDispatch::TruncOrZext1},
        {"alloca", ResultArity::One, TypeCategory::Ptr, 1, 1, makeOperands(TypeCategory::I64),
         true, 0, false, VMDispatch::Alloca},
        {"gep", ResultArity::One, TypeCategory::Ptr, 2, 2, makeOperands(TypeCategory::Ptr, TypeCategory::I64),
         false, 0, false, VMDispatch::GEP},
        {"load", ResultArity::One, TypeCategory::InstrType, 1, 1, makeOperands(TypeCategory::Ptr),
         false, 0, false, VMDispatch::Load},
        {"store", ResultArity::None, TypeCategory::None, 2, 2, makeOperands(TypeCategory::Ptr, TypeCategory::InstrType),
         true, 0, false, VMDispatch::Store},
        {"addr_of", ResultArity::One, TypeCategory::Ptr, 1, 1, makeOperands(TypeCategory::Ptr),
         false, 0, false, VMDispatch::AddrOf},
        {"const_str", ResultArity::One, TypeCategory::Str, 1, 1, makeOperands(TypeCategory::Ptr),
         false, 0, false, VMDispatch::ConstStr},
        {"const_null", ResultArity::One, TypeCategory::Ptr, 0, 0, makeOperands(TypeCategory::None),
         false, 0, false, VMDispatch::None},
        {"call", ResultArity::Optional, TypeCategory::Dynamic, 0, kVariadicOperandCount,
         makeOperands(TypeCategory::Any, TypeCategory::Any, TypeCategory::Any), true, 0, false, VMDispatch::Call},
        {"br", ResultArity::None, TypeCategory::None, 0, 0, makeOperands(TypeCategory::None),
         true, 1, true, VMDispatch::Br},
        {"cbr", ResultArity::None, TypeCategory::None, 1, 1, makeOperands(TypeCategory::I1),
         true, 2, true, VMDispatch::CBr},
        {"ret", ResultArity::None, TypeCategory::None, 0, 1, makeOperands(TypeCategory::Any),
         true, 0, true, VMDispatch::Ret},
        {"trap", ResultArity::None, TypeCategory::None, 0, 0, makeOperands(TypeCategory::None),
         true, 0, true, VMDispatch::Trap},
    }
};

static_assert(kOpcodeTable.size() == kNumOpcodes, "Opcode table must match enum count");

std::string toString(Opcode op)
{
    const size_t idx = static_cast<size_t>(op);
    if (idx >= kOpcodeTable.size())
        return "";
    return kOpcodeTable[idx].name;
}

} // namespace il::core
