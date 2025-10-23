// src/codegen/x86_64/MachineIR.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Provide definitions for the minimal Machine IR representation used
//          by the x86-64 backend during Phase A.
// Invariants: Helper utilities preserve operand invariants defined in the
//             header and avoid introducing invalid combinations.
// Ownership: Functions operate on value-based IR nodes with no implicit sharing.
// Notes: Depends only on MachineIR.hpp and the C++ standard library.

#include "MachineIR.hpp"

#include <sstream>
#include <string_view>
#include <utility>

namespace viper::codegen::x64 {

namespace {
[[nodiscard]] const char* opcodeName(MOpcode opc) noexcept {
  switch (opc) {
  case MOpcode::MOVrr:
    return "MOVrr";
  case MOpcode::MOVri:
    return "MOVri";
  case MOpcode::LEA:
    return "LEA";
  case MOpcode::ADDrr:
    return "ADDrr";
  case MOpcode::ADDri:
    return "ADDri";
  case MOpcode::SUBrr:
    return "SUBrr";
  case MOpcode::IMULrr:
    return "IMULrr";
  case MOpcode::XORrr32:
    return "XORrr32";
  case MOpcode::CMPrr:
    return "CMPrr";
  case MOpcode::CMPri:
    return "CMPri";
  case MOpcode::SETcc:
    return "SETcc";
  case MOpcode::MOVZXrr32:
    return "MOVZXrr32";
  case MOpcode::TESTrr:
    return "TESTrr";
  case MOpcode::JMP:
    return "JMP";
  case MOpcode::JCC:
    return "JCC";
  case MOpcode::CALL:
    return "CALL";
  case MOpcode::RET:
    return "RET";
  case MOpcode::PX_COPY:
    return "PX_COPY";
  case MOpcode::FADD:
    return "FADD";
  case MOpcode::FSUB:
    return "FSUB";
  case MOpcode::FMUL:
    return "FMUL";
  case MOpcode::FDIV:
    return "FDIV";
  case MOpcode::UCOMIS:
    return "UCOMIS";
  case MOpcode::CVTSI2SD:
    return "CVTSI2SD";
  case MOpcode::CVTTSD2SI:
    return "CVTTSD2SI";
  case MOpcode::MOVSDrr:
    return "MOVSDrr";
  case MOpcode::MOVSDrm:
    return "MOVSDrm";
  case MOpcode::MOVSDmr:
    return "MOVSDmr";
  }
  return "<unknown>";
}

[[nodiscard]] std::string_view regClassSuffix(RegClass cls) noexcept {
  switch (cls) {
  case RegClass::GPR:
    return "gpr";
  case RegClass::XMM:
    return "xmm";
  }
  return "unknown";
}
} // namespace

MInstr MInstr::make(MOpcode opc, std::vector<Operand> ops) {
  return MInstr{opc, std::move(ops)};
}

MInstr MInstr::make(MOpcode opc, std::initializer_list<Operand> ops) {
  return MInstr{opc, std::vector<Operand>{ops}};
}

MInstr& MInstr::addOperand(Operand op) {
  operands.push_back(std::move(op));
  return *this;
}

MBasicBlock& MFunction::addBlock(MBasicBlock block) {
  blocks.push_back(std::move(block));
  return blocks.back();
}

MInstr& MBasicBlock::append(MInstr instr) {
  instructions.push_back(std::move(instr));
  return instructions.back();
}

OpReg makeVReg(RegClass cls, uint16_t id) noexcept {
  OpReg result{};
  result.isPhys = false;
  result.cls = cls;
  result.idOrPhys = id;
  return result;
}

OpReg makePhysReg(RegClass cls, uint16_t phys) noexcept {
  OpReg result{};
  result.isPhys = true;
  result.cls = cls;
  result.idOrPhys = phys;
  return result;
}

Operand makeVRegOperand(RegClass cls, uint16_t id) {
  return Operand{makeVReg(cls, id)};
}

Operand makePhysRegOperand(RegClass cls, uint16_t phys) {
  return Operand{makePhysReg(cls, phys)};
}

Operand makeImmOperand(int64_t value) {
  return Operand{OpImm{value}};
}

Operand makeMemOperand(OpReg base, int32_t disp) {
  assert(base.cls == RegClass::GPR && "Phase A expects GPR base registers");
  return Operand{OpMem{std::move(base), disp}};
}

Operand makeLabelOperand(std::string name) {
  return Operand{OpLabel{std::move(name)}};
}

std::string toString(const OpReg& op) {
  std::ostringstream os;
  if (op.isPhys) {
    const auto phys = static_cast<PhysReg>(op.idOrPhys);
    os << '@' << regName(phys);
  } else {
    os << "%v" << static_cast<unsigned>(op.idOrPhys);
  }
  os << ':' << regClassSuffix(op.cls);
  return os.str();
}

std::string toString(const OpImm& op) {
  std::ostringstream os;
  os << '#' << op.val;
  return os.str();
}

std::string toString(const OpMem& op) {
  std::ostringstream os;
  os << '[' << toString(op.base);
  if (op.disp != 0) {
    if (op.disp > 0) {
      os << " + " << op.disp;
    } else {
      os << " - " << -op.disp;
    }
  }
  os << ']';
  return os.str();
}

std::string toString(const OpLabel& op) {
  return op.name;
}

std::string toString(const Operand& operand) {
  return std::visit(
      [](const auto& value) { return toString(value); }, operand);
}

std::string toString(const MInstr& instr) {
  std::ostringstream os;
  os << opcodeName(instr.opcode);
  bool first = true;
  for (const auto& operand : instr.operands) {
    if (first) {
      os << ' ' << toString(operand);
      first = false;
    } else {
      os << ", " << toString(operand);
    }
  }
  return os.str();
}

std::string toString(const MBasicBlock& block) {
  std::ostringstream os;
  os << block.label << ":\n";
  for (const auto& inst : block.instructions) {
    os << "  " << toString(inst) << '\n';
  }
  return os.str();
}

std::string toString(const MFunction& func) {
  std::ostringstream os;
  os << "function " << func.name;
  if (func.metadata.isVarArg) {
    os << " (vararg)";
  }
  os << "\n";
  for (const auto& block : func.blocks) {
    os << toString(block);
  }
  return os.str();
}

} // namespace viper::codegen::x64

