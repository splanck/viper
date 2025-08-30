// File: src/il/core/Opcode.h
// Purpose: Enumerates IL instruction opcodes.
// Key invariants: Enumeration values match IL spec.
// Ownership/Lifetime: Not applicable.
// Links: docs/il-spec.md
#pragma once
#include <string>

namespace il::core {

/// @brief All instruction opcodes defined by the IL.
/// @see docs/il-spec.md §3 for opcode descriptions.
enum class Opcode {
  Add,
  Sub,
  Mul,
  SDiv,
  UDiv,
  SRem,
  URem,
  And,
  Or,
  Xor,
  Shl,
  LShr,
  AShr,
  FAdd,
  FSub,
  FMul,
  FDiv,
  ICmpEq,
  ICmpNe,
  SCmpLT,
  SCmpLE,
  SCmpGT,
  SCmpGE,
  UCmpLT,
  UCmpLE,
  UCmpGT,
  UCmpGE,
  FCmpEQ,
  FCmpNE,
  FCmpLT,
  FCmpLE,
  FCmpGT,
  FCmpGE,
  Sitofp,
  Fptosi,
  Zext1,
  Trunc1,
  Alloca,
  GEP,
  Load,
  Store,
  AddrOf,
  ConstStr,
  ConstNull,
  Call,
  Br,
  CBr,
  Ret,
  Trap
};

/// @brief Convert opcode @p op to its mnemonic string.
/// @param op Opcode to stringify.
/// @return Lowercase mnemonic defined by the IL spec.
inline std::string toString(Opcode op) {
  switch (op) {
  case Opcode::Add:
    return "add";
  case Opcode::Sub:
    return "sub";
  case Opcode::Mul:
    return "mul";
  case Opcode::SDiv:
    return "sdiv";
  case Opcode::UDiv:
    return "udiv";
  case Opcode::SRem:
    return "srem";
  case Opcode::URem:
    return "urem";
  case Opcode::And:
    return "and";
  case Opcode::Or:
    return "or";
  case Opcode::Xor:
    return "xor";
  case Opcode::Shl:
    return "shl";
  case Opcode::LShr:
    return "lshr";
  case Opcode::AShr:
    return "ashr";
  case Opcode::FAdd:
    return "fadd";
  case Opcode::FSub:
    return "fsub";
  case Opcode::FMul:
    return "fmul";
  case Opcode::FDiv:
    return "fdiv";
  case Opcode::ICmpEq:
    return "icmp_eq";
  case Opcode::ICmpNe:
    return "icmp_ne";
  case Opcode::SCmpLT:
    return "scmp_lt";
  case Opcode::SCmpLE:
    return "scmp_le";
  case Opcode::SCmpGT:
    return "scmp_gt";
  case Opcode::SCmpGE:
    return "scmp_ge";
  case Opcode::UCmpLT:
    return "ucmp_lt";
  case Opcode::UCmpLE:
    return "ucmp_le";
  case Opcode::UCmpGT:
    return "ucmp_gt";
  case Opcode::UCmpGE:
    return "ucmp_ge";
  case Opcode::FCmpEQ:
    return "fcmp_eq";
  case Opcode::FCmpNE:
    return "fcmp_ne";
  case Opcode::FCmpLT:
    return "fcmp_lt";
  case Opcode::FCmpLE:
    return "fcmp_le";
  case Opcode::FCmpGT:
    return "fcmp_gt";
  case Opcode::FCmpGE:
    return "fcmp_ge";
  case Opcode::Sitofp:
    return "sitofp";
  case Opcode::Fptosi:
    return "fptosi";
  case Opcode::Zext1:
    return "zext1";
  case Opcode::Trunc1:
    return "trunc1";
  case Opcode::Alloca:
    return "alloca";
  case Opcode::GEP:
    return "gep";
  case Opcode::Load:
    return "load";
  case Opcode::Store:
    return "store";
  case Opcode::AddrOf:
    return "addr_of";
  case Opcode::ConstStr:
    return "const_str";
  case Opcode::ConstNull:
    return "const_null";
  case Opcode::Call:
    return "call";
  case Opcode::Br:
    return "br";
  case Opcode::CBr:
    return "cbr";
  case Opcode::Ret:
    return "ret";
  case Opcode::Trap:
    return "trap";
  }
  return "";
}

} // namespace il::core
