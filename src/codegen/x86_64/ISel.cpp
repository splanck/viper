// src/codegen/x86_64/ISel.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Define the instruction selection helpers that map pseudo Machine IR
//          emitted by LowerILToMIR into concrete x86-64 encodings for Phase A.
// Invariants: Transformations keep instruction ordering stable while mutating
//             opcode/operand pairs to legal forms (e.g. cmp with immediates,
//             MOVZX after SETcc). Resulting instruction streams remain valid for
//             subsequent register allocation and emission passes.
// Ownership: Operates on Machine IR in place; no additional resources beyond the
//            borrowed target description are acquired.
// Notes: Depends only on ISel.hpp and standard library utilities.

#include "ISel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace viper::codegen::x64 {

namespace {

[[nodiscard]] Operand cloneOperand(const Operand& operand) {
  return operand;
}

[[nodiscard]] bool isImm(const Operand& operand) noexcept {
  return std::holds_alternative<OpImm>(operand);
}

[[nodiscard]] OpImm* asImm(Operand& operand) noexcept {
  return std::get_if<OpImm>(&operand);
}

[[nodiscard]] OpReg* asReg(Operand& operand) noexcept {
  return std::get_if<OpReg>(&operand);
}

[[nodiscard]] const OpReg* asReg(const Operand& operand) noexcept {
  return std::get_if<OpReg>(&operand);
}

[[nodiscard]] bool sameRegister(const Operand& lhs, const Operand& rhs) noexcept {
  const auto* lhsReg = asReg(lhs);
  const auto* rhsReg = asReg(rhs);
  if (!lhsReg || !rhsReg) {
    return false;
  }
  return lhsReg->isPhys == rhsReg->isPhys && lhsReg->cls == rhsReg->cls &&
         lhsReg->idOrPhys == rhsReg->idOrPhys;
}

void ensureMovzxAfterSetcc(MBasicBlock& block, std::size_t index) {
  if (index >= block.instructions.size()) {
    return;
  }
  auto& setcc = block.instructions[index];
  Operand* destOperand = nullptr;
  for (auto& operand : setcc.operands) {
    if (std::holds_alternative<OpReg>(operand)) {
      destOperand = &operand;
      break;
    }
  }
  if (!destOperand) {
    return;
  }

  if (index + 1 < block.instructions.size()) {
    auto& next = block.instructions[index + 1];
    if (next.opcode == MOpcode::MOVZXrr32 && next.operands.size() >= 2 &&
        sameRegister(next.operands[0], *destOperand) &&
        sameRegister(next.operands[1], *destOperand)) {
      return;
    }
  }

  MInstr movzx = MInstr::make(
      MOpcode::MOVZXrr32,
      std::vector<Operand>{cloneOperand(*destOperand), cloneOperand(*destOperand)});
  block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index + 1),
                            std::move(movzx));
}

void canonicaliseCmp(MInstr& instr) {
  if (instr.operands.size() < 2) {
    return;
  }
  if (instr.opcode == MOpcode::CMPrr && isImm(instr.operands[1])) {
    instr.opcode = MOpcode::CMPri;
  }
  if (instr.opcode == MOpcode::CMPri && !isImm(instr.operands[1])) {
    instr.opcode = MOpcode::CMPrr;
  }
}

void canonicaliseAddSub(MInstr& instr) {
  if (instr.operands.size() < 2) {
    return;
  }
  switch (instr.opcode) {
  case MOpcode::ADDrr:
    if (isImm(instr.operands[1])) {
      instr.opcode = MOpcode::ADDri;
    }
    break;
  case MOpcode::SUBrr:
    if (auto* imm = asImm(instr.operands[1])) {
      imm->val = -imm->val;
      instr.opcode = MOpcode::ADDri;
    }
    break;
  default:
    break;
  }
}

} // namespace

ISel::ISel(const TargetInfo& target) noexcept : target_{&target} {}

void ISel::lowerArithmetic(MFunction& func) const {
  (void)target_;
  for (auto& block : func.blocks) {
    for (auto& instr : block.instructions) {
      switch (instr.opcode) {
      case MOpcode::ADDrr:
      case MOpcode::ADDri:
      case MOpcode::SUBrr:
        canonicaliseAddSub(instr);
        break;
      case MOpcode::IMULrr:
      case MOpcode::FADD:
      case MOpcode::FSUB:
      case MOpcode::FMUL:
      case MOpcode::FDIV:
        // These already encode legal register-register forms in Phase A.
        break;
      default:
        break;
      }
    }
  }
}

void ISel::lowerCompareAndBranch(MFunction& func) const {
  (void)target_;
  for (auto& block : func.blocks) {
    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
      auto& instr = block.instructions[idx];
      switch (instr.opcode) {
      case MOpcode::CMPrr:
      case MOpcode::CMPri:
        canonicaliseCmp(instr);
        break;
      case MOpcode::SETcc:
        ensureMovzxAfterSetcc(block, idx);
        break;
      case MOpcode::TESTrr:
        if (instr.operands.size() >= 2 && isImm(instr.operands[1])) {
          // Replace TEST with CMP against zero when a constant sneaks through.
          instr.opcode = MOpcode::CMPri;
          instr.operands[1] = makeImmOperand(0);
        }
        break;
      default:
        break;
      }
    }
  }
}

void ISel::lowerSelect(MFunction& func) const {
  (void)target_;
  for (auto& block : func.blocks) {
    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
      auto& instr = block.instructions[idx];
      if (instr.opcode == MOpcode::SETcc) {
        ensureMovzxAfterSetcc(block, idx);
      }
    }
  }
}

} // namespace viper::codegen::x64

