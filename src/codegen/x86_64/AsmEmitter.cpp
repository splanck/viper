// src/codegen/x86_64/AsmEmitter.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Provide the x86-64 assembly emission routines that translate
//          Machine IR instructions into AT&T syntax while maintaining
//          deterministic literal pools for the .rodata section.
// Invariants: Emission preserves operand ordering, conditions, and label
//             bindings from Machine IR. Literal pools deduplicate entries and
//             assign stable labels across emissions.
// Ownership: AsmEmitter borrows the rodata pool supplied at construction; no
//            ownership transfers occur.
// Notes: Depends only on AsmEmitter.hpp and the C++ standard library.

#include "AsmEmitter.hpp"

#include <bit>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viper::codegen::x64 {

namespace {

template <typename... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};
template <typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

[[nodiscard]] std::string formatBytes(const std::string& bytes) {
  std::ostringstream os;
  constexpr std::size_t kBytesPerLine = 16U;
  for (std::size_t i = 0; i < bytes.size();) {
    os << "  .byte ";
    for (std::size_t j = 0; j < kBytesPerLine && i < bytes.size(); ++j, ++i) {
      if (j != 0) {
        os << ", ";
      }
      const auto value = static_cast<unsigned>(
          static_cast<unsigned char>(bytes[i]));
      os << value;
    }
    os << '\n';
  }
  if (bytes.empty()) {
    os << "  # empty literal\n";
  }
  return os.str();
}

} // namespace

int AsmEmitter::RoDataPool::addStringLiteral(std::string bytes) {
  if (const auto it = stringLookup_.find(bytes); it != stringLookup_.end()) {
    return it->second;
  }
  const int index = static_cast<int>(stringLiterals_.size());
  stringLookup_.emplace(bytes, index);
  stringLiterals_.push_back(std::move(bytes));
  return index;
}

int AsmEmitter::RoDataPool::addF64Literal(double value) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  if (const auto it = f64Lookup_.find(bits); it != f64Lookup_.end()) {
    return it->second;
  }
  const int index = static_cast<int>(f64Literals_.size());
  f64Lookup_.emplace(bits, index);
  f64Literals_.push_back(value);
  return index;
}

std::string AsmEmitter::RoDataPool::stringLabel(int index) const {
  return ".LC_str_" + std::to_string(index);
}

std::string AsmEmitter::RoDataPool::f64Label(int index) const {
  return ".LC_f64_" + std::to_string(index);
}

void AsmEmitter::RoDataPool::emit(std::ostream& os) const {
  if (empty()) {
    return;
  }
  os << ".section .rodata\n";
  for (std::size_t i = 0; i < stringLiterals_.size(); ++i) {
    os << stringLabel(static_cast<int>(i)) << ":\n";
    os << formatBytes(stringLiterals_[i]);
  }
  for (std::size_t i = 0; i < f64Literals_.size(); ++i) {
    os << f64Label(static_cast<int>(i)) << ":\n";
    const auto bits = std::bit_cast<std::uint64_t>(f64Literals_[i]);
    const auto oldFlags = os.flags();
    const auto oldFill = os.fill();
    os << "  .quad 0x" << std::hex << std::setw(16) << std::setfill('0') << bits
       << std::dec;
    os.fill(oldFill);
    os.flags(oldFlags);
    os << '\n';
  }
}

bool AsmEmitter::RoDataPool::empty() const noexcept {
  return stringLiterals_.empty() && f64Literals_.empty();
}

AsmEmitter::AsmEmitter(RoDataPool& pool) noexcept : pool_{&pool} {}

void AsmEmitter::emitFunction(std::ostream& os, const MFunction& func,
                              const TargetInfo& target) const {
  os << ".text\n";
  os << ".globl " << func.name << "\n";
  os << func.name << ":\n";

  for (std::size_t i = 0; i < func.blocks.size(); ++i) {
    const auto& block = func.blocks[i];
    const bool isEntry = (i == 0U && block.label == func.name);
    if (isEntry) {
      for (const auto& instr : block.instructions) {
        emitInstruction(os, instr, target);
      }
    } else {
      emitBlock(os, block, target);
    }
    if (i + 1 < func.blocks.size()) {
      os << '\n';
    }
  }
}

void AsmEmitter::emitRoData(std::ostream& os) const {
  if (pool_ && !pool_->empty()) {
    pool_->emit(os);
  }
}

AsmEmitter::RoDataPool& AsmEmitter::roDataPool() noexcept {
  return *pool_;
}

const AsmEmitter::RoDataPool& AsmEmitter::roDataPool() const noexcept {
  return *pool_;
}

void AsmEmitter::emitBlock(std::ostream& os, const MBasicBlock& block,
                           const TargetInfo& target) {
  if (!block.label.empty()) {
    os << block.label << ":\n";
  }
  for (const auto& instr : block.instructions) {
    emitInstruction(os, instr, target);
  }
}

void AsmEmitter::emitInstruction(std::ostream& os, const MInstr& instr,
                                 const TargetInfo& target) {
  switch (instr.opcode) {
  case MOpcode::PX_COPY: {
    os << "  # px_copy";
    bool first = true;
    for (const auto& operand : instr.operands) {
      if (first) {
        os << ' ' << formatOperand(operand, target);
        first = false;
      } else {
        os << ", " << formatOperand(operand, target);
      }
    }
    os << '\n';
    return;
  }
  case MOpcode::RET:
    os << "  ret\n";
    return;
  case MOpcode::JMP: {
    os << "  jmp ";
    if (!instr.operands.empty()) {
      const auto& targetOp = instr.operands.front();
      if (std::holds_alternative<OpLabel>(targetOp)) {
        os << formatOperand(targetOp, target);
      } else {
        os << '*' << formatOperand(targetOp, target);
      }
    } else {
      os << "#<missing>";
    }
    os << '\n';
    return;
  }
  case MOpcode::JCC: {
    const Operand* branchTarget = nullptr;
    const OpImm* cond = nullptr;
    for (const auto& operand : instr.operands) {
      if (!cond) {
        cond = std::get_if<OpImm>(&operand);
      }
      if (!branchTarget && std::holds_alternative<OpLabel>(operand)) {
        branchTarget = &operand;
      }
    }
    if (!branchTarget && !instr.operands.empty()) {
      branchTarget = &instr.operands.back();
    }
    const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
    os << "  j" << suffix << ' ';
    if (branchTarget) {
      if (std::holds_alternative<OpLabel>(*branchTarget)) {
        os << formatOperand(*branchTarget, target);
      } else {
        os << '*' << formatOperand(*branchTarget, target);
      }
    } else {
      os << "#<missing>";
    }
    os << '\n';
    return;
  }
  case MOpcode::SETcc: {
    const Operand* dest = nullptr;
    const OpImm* cond = nullptr;
    for (const auto& operand : instr.operands) {
      if (!cond) {
        cond = std::get_if<OpImm>(&operand);
      }
      if (!dest && (std::holds_alternative<OpReg>(operand) ||
                    std::holds_alternative<OpMem>(operand))) {
        dest = &operand;
      }
    }
    const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
    os << "  set" << suffix << ' ';
    if (dest) {
      os << formatOperand(*dest, target);
    } else {
      os << "#<missing>";
    }
    os << '\n';
    return;
  }
  case MOpcode::CALL:
    os << "  callq ";
    if (!instr.operands.empty()) {
      os << formatCallTarget(instr.operands.front(), target);
    } else {
      os << "#<missing>";
    }
    os << '\n';
    return;
  case MOpcode::LEA:
    if (instr.operands.size() < 2) {
      os << "  leaq #<missing>\n";
      return;
    }
    os << "  leaq " << formatLeaSource(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  default:
    break;
  }

  const char* mnemonic = mnemonicFor(instr.opcode);
  if (!mnemonic) {
    os << "  # <unknown opcode>\n";
    return;
  }

  switch (instr.opcode) {
  case MOpcode::MOVrr:
  case MOpcode::ADDrr:
  case MOpcode::SUBrr:
  case MOpcode::IMULrr:
  case MOpcode::XORrr32:
  case MOpcode::MOVZXrr32:
  case MOpcode::FADD:
  case MOpcode::FSUB:
  case MOpcode::FMUL:
  case MOpcode::FDIV:
  case MOpcode::UCOMIS:
  case MOpcode::MOVSDrr:
  case MOpcode::CVTSI2SD:
  case MOpcode::CVTTSD2SI: {
    if (instr.operands.size() < 2) {
      os << "  " << mnemonic << " #<missing>\n";
      return;
    }
    os << "  " << mnemonic << ' '
       << formatOperand(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  }
  case MOpcode::MOVri:
  case MOpcode::ADDri:
  case MOpcode::CMPri: {
    if (instr.operands.size() < 2) {
      os << "  " << mnemonic << " #<missing>\n";
      return;
    }
    os << "  " << mnemonic << ' '
       << formatOperand(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  }
  case MOpcode::CMPrr:
  case MOpcode::TESTrr: {
    if (instr.operands.size() < 2) {
      os << "  " << mnemonic << " #<missing>\n";
      return;
    }
    os << "  " << mnemonic << ' '
       << formatOperand(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  }
  case MOpcode::MOVSDrm: {
    if (instr.operands.size() < 2) {
      os << "  movsd #<missing>\n";
      return;
    }
    os << "  movsd " << formatOperand(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  }
  case MOpcode::MOVSDmr: {
    if (instr.operands.size() < 2) {
      os << "  movsd #<missing>\n";
      return;
    }
    os << "  movsd " << formatOperand(instr.operands[1], target) << ", "
       << formatOperand(instr.operands[0], target) << '\n';
    return;
  }
  default:
    break;
  }

  os << "  " << mnemonic;
  if (!instr.operands.empty()) {
    os << ' ';
    bool first = true;
    for (const auto& operand : instr.operands) {
      if (!first) {
        os << ", ";
      }
      os << formatOperand(operand, target);
      first = false;
    }
  }
  os << '\n';
}

std::string AsmEmitter::formatOperand(const Operand& operand,
                                      const TargetInfo& target) {
  return std::visit(
      Overload{[&](const OpReg& reg) { return formatReg(reg, target); },
               [&](const OpImm& imm) { return formatImm(imm); },
               [&](const OpMem& mem) { return formatMem(mem, target); },
               [&](const OpLabel& label) { return formatLabel(label); }},
      operand);
}

std::string AsmEmitter::formatReg(const OpReg& reg, const TargetInfo&) {
  if (reg.isPhys) {
    const auto phys = static_cast<PhysReg>(reg.idOrPhys);
    return regName(phys);
  }
  std::ostringstream os;
  os << "%v" << static_cast<unsigned>(reg.idOrPhys);
  return os.str();
}

std::string AsmEmitter::formatImm(const OpImm& imm) {
  std::ostringstream os;
  os << '$' << imm.val;
  return os.str();
}

std::string AsmEmitter::formatMem(const OpMem& mem, const TargetInfo& target) {
  std::ostringstream os;
  if (mem.disp != 0) {
    os << mem.disp;
  }
  os << '(' << formatReg(mem.base, target) << ')';
  return os.str();
}

std::string AsmEmitter::formatLabel(const OpLabel& label) {
  return label.name;
}

std::string AsmEmitter::formatLeaSource(const Operand& operand,
                                        const TargetInfo& target) {
  return std::visit(
      Overload{[&](const OpLabel& label) {
                 std::string result = label.name;
                 result += "(%rip)";
                 return result;
               },
               [&](const OpMem& mem) { return formatMem(mem, target); },
               [&](const OpReg& reg) { return formatReg(reg, target); },
               [&](const OpImm& imm) { return formatImm(imm); }},
      operand);
}

std::string AsmEmitter::formatCallTarget(const Operand& operand,
                                         const TargetInfo& target) {
  return std::visit(
      Overload{[&](const OpLabel& label) { return label.name; },
               [&](const OpReg& reg) {
                 return std::string{"*"} + formatReg(reg, target);
               },
               [&](const OpMem& mem) {
                 return std::string{"*"} + formatMem(mem, target);
               },
               [&](const OpImm& imm) { return formatImm(imm); }},
      operand);
}

std::string_view AsmEmitter::conditionSuffix(std::int64_t code) noexcept {
  switch (static_cast<int>(code)) {
  case 0:
    return "e";
  case 1:
    return "ne";
  case 2:
    return "l";
  case 3:
    return "le";
  case 4:
    return "g";
  case 5:
    return "ge";
  case 6:
    return "a";
  case 7:
    return "ae";
  case 8:
    return "b";
  case 9:
    return "be";
  case 10:
    return "p";
  case 11:
    return "np";
  default:
    return "e";
  }
}

const char* AsmEmitter::mnemonicFor(MOpcode opcode) noexcept {
  switch (opcode) {
  case MOpcode::MOVrr:
  case MOpcode::MOVri:
    return "movq";
  case MOpcode::LEA:
    return "leaq";
  case MOpcode::ADDrr:
  case MOpcode::ADDri:
    return "addq";
  case MOpcode::SUBrr:
    return "subq";
  case MOpcode::IMULrr:
    return "imulq";
  case MOpcode::DIVS64rr:
  case MOpcode::REMS64rr:
    return nullptr;
  case MOpcode::CQO:
    return "cqto";
  case MOpcode::IDIVrm:
    return "idivq";
  case MOpcode::XORrr32:
    return "xorl";
  case MOpcode::CMPrr:
  case MOpcode::CMPri:
    return "cmpq";
  case MOpcode::SETcc:
    return "set";
  case MOpcode::MOVZXrr32:
    return "movl";
  case MOpcode::TESTrr:
    return "testq";
  case MOpcode::JMP:
    return "jmp";
  case MOpcode::JCC:
    return "j";
  case MOpcode::CALL:
    return "callq";
  case MOpcode::RET:
    return "ret";
  case MOpcode::PX_COPY:
    return nullptr;
  case MOpcode::FADD:
    return "addsd";
  case MOpcode::FSUB:
    return "subsd";
  case MOpcode::FMUL:
    return "mulsd";
  case MOpcode::FDIV:
    return "divsd";
  case MOpcode::UCOMIS:
    return "ucomisd";
  case MOpcode::CVTSI2SD:
    return "cvtsi2sdq";
  case MOpcode::CVTTSD2SI:
    return "cvttsd2siq";
  case MOpcode::MOVSDrr:
  case MOpcode::MOVSDrm:
  case MOpcode::MOVSDmr:
    return "movsd";
  }
  return nullptr;
}

} // namespace viper::codegen::x64

