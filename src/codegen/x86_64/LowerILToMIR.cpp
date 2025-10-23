// src/codegen/x86_64/LowerILToMIR.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Define the IL→MIR adapter that translates provisional IL structures
//          into Machine IR for the x86-64 backend.
// Invariants: Each IL SSA id is mapped to a unique virtual register and block
//             parameter edges emit PX_COPY pairs. Lowering presently covers a
//             minimal opcode subset required for Phase A prototyping.
// Ownership: The adapter borrows IL input objects, materialises MIR by value,
//            and records call plans for later consumption by call lowering.
// Notes: Semantics of several instructions are placeholders awaiting full IL
//        integration; TODO markers highlight follow-up work.

#include "LowerILToMIR.hpp"

#include <cassert>
#include <string_view>

namespace viper::codegen::x64 {

namespace {
[[nodiscard]] Operand cloneOperand(const Operand& operand) {
  return operand;
}
} // namespace

LowerILToMIR::LowerILToMIR(const TargetInfo& target) noexcept : target_{&target} {}

const std::vector<CallLoweringPlan>& LowerILToMIR::callPlans() const noexcept {
  return callPlans_;
}

void LowerILToMIR::resetFunctionState() {
  nextVReg_ = 1U;
  valueToVReg_.clear();
  blockInfo_.clear();
  callPlans_.clear();
}

RegClass LowerILToMIR::regClassFor(ILValue::Kind kind) noexcept {
  switch (kind) {
  case ILValue::Kind::I64:
  case ILValue::Kind::I1:
  case ILValue::Kind::PTR:
    return RegClass::GPR;
  case ILValue::Kind::F64:
    return RegClass::XMM;
  case ILValue::Kind::LABEL:
    return RegClass::GPR;
  }
  return RegClass::GPR;
}

VReg LowerILToMIR::ensureVReg(int id, ILValue::Kind kind) {
  assert(id >= 0 && "SSA value without identifier");
  const auto it = valueToVReg_.find(id);
  if (it != valueToVReg_.end()) {
    assert(it->second.cls == regClassFor(kind) && "SSA id reused with new type");
    return it->second;
  }
  const VReg vreg{nextVReg_++, regClassFor(kind)};
  valueToVReg_.emplace(id, vreg);
  return vreg;
}

bool LowerILToMIR::isImmediate(const ILValue& value) const noexcept {
  return value.id < 0;
}

Operand LowerILToMIR::makeOperandForValue(MBasicBlock& block, const ILValue& value,
                                          RegClass cls) {
  (void)block;
  if (value.kind == ILValue::Kind::LABEL) {
    return makeLabelOperand(value);
  }

  (void)cls;

  if (!isImmediate(value)) {
    const VReg vreg = ensureVReg(value.id, value.kind);
    return makeVRegOperand(vreg.cls, vreg.id);
  }

  switch (value.kind) {
  case ILValue::Kind::I64:
  case ILValue::Kind::I1:
  case ILValue::Kind::PTR:
    return makeImmOperand(value.i64);
  case ILValue::Kind::F64: {
    // TODO: Materialise f64 immediates using a constant pool.
    const auto approx = static_cast<int64_t>(value.f64);
    return makeImmOperand(approx);
  }
  case ILValue::Kind::LABEL:
    break;
  }
  return makeImmOperand(0);
}

Operand LowerILToMIR::makeLabelOperand(const ILValue& value) const {
  assert(value.kind == ILValue::Kind::LABEL && "label operand expected");
  return x64::makeLabelOperand(value.label);
}

void LowerILToMIR::lowerBinary(const ILInstr& instr, MBasicBlock& block,
                               MOpcode opcRR, MOpcode opcRI, RegClass cls) {
  if (instr.resultId < 0 || instr.ops.size() < 2) {
    return;
  }

  const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
  const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
  const Operand lhs = makeOperandForValue(block, instr.ops[0], cls);
  const Operand rhs = makeOperandForValue(block, instr.ops[1], cls);

  if (std::holds_alternative<OpImm>(lhs)) {
    block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), lhs}));
  } else {
    block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), lhs}));
  }

  if (opcRI != opcRR && std::holds_alternative<OpImm>(rhs)) {
    block.append(MInstr::make(opcRI, std::vector<Operand>{cloneOperand(dest), rhs}));
  } else {
    block.append(MInstr::make(opcRR, std::vector<Operand>{cloneOperand(dest), rhs}));
  }
}

void LowerILToMIR::lowerCmp(const ILInstr& instr, MBasicBlock& block, RegClass cls) {
  if (instr.ops.size() < 2) {
    return;
  }

  const Operand lhs = makeOperandForValue(block, instr.ops[0], cls);
  const Operand rhs = makeOperandForValue(block, instr.ops[1], cls);

  if (cls == RegClass::GPR) {
    block.append(MInstr::make(MOpcode::CMPrr, std::vector<Operand>{cloneOperand(lhs), rhs}));
  } else {
    block.append(MInstr::make(MOpcode::UCOMIS, std::vector<Operand>{cloneOperand(lhs), rhs}));
  }

  if (instr.resultId >= 0) {
    const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
    const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
    block.append(MInstr::make(MOpcode::XORrr32, std::vector<Operand>{cloneOperand(dest), dest}));
    block.append(
        MInstr::make(MOpcode::SETcc,
                     std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
  }
}

void LowerILToMIR::lowerSelect(const ILInstr& instr, MBasicBlock& block) {
  if (instr.resultId < 0 || instr.ops.size() < 3) {
    return;
  }

  const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
  const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
  const Operand cond = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
  const Operand trueVal = makeOperandForValue(block, instr.ops[1], destReg.cls);
  const Operand falseVal = makeOperandForValue(block, instr.ops[2], destReg.cls);

  // Phase A placeholder: materialise false path then overwrite with true path using SETcc mask.
  if (std::holds_alternative<OpImm>(falseVal)) {
    block.append(
        MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(dest), falseVal}));
  } else {
    block.append(
        MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), falseVal}));
  }

  // TODO: Replace with CMOV-based lowering once available.
  block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
  block.append(MInstr::make(MOpcode::SETcc,
                            std::vector<Operand>{makeImmOperand(1), cloneOperand(dest)}));
  (void)trueVal;
}

void LowerILToMIR::lowerBranch(const ILInstr& instr, MBasicBlock& block) {
  if (instr.ops.empty()) {
    return;
  }
  block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

void LowerILToMIR::lowerCondBranch(const ILInstr& instr, MBasicBlock& block) {
  if (instr.ops.size() < 3) {
    return;
  }

  const Operand cond = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
  const Operand trueLabel = makeLabelOperand(instr.ops[1]);
  const Operand falseLabel = makeLabelOperand(instr.ops[2]);

  block.append(MInstr::make(MOpcode::TESTrr, std::vector<Operand>{cloneOperand(cond), cond}));
  block.append(
      MInstr::make(MOpcode::JCC, std::vector<Operand>{makeImmOperand(1), trueLabel}));
  block.append(MInstr::make(MOpcode::JMP, std::vector<Operand>{falseLabel}));
}

void LowerILToMIR::lowerReturn(const ILInstr& instr, MBasicBlock& block) {
  // TODO: Materialise return value copies once ABI details are defined.
  (void)instr;
  block.append(MInstr::make(MOpcode::RET, {}));
}

void LowerILToMIR::lowerCall(const ILInstr& instr, MBasicBlock& block) {
  if (instr.ops.empty()) {
    return;
  }

  CallLoweringPlan plan{};
  plan.calleeLabel = instr.ops.front().label;

  for (std::size_t idx = 1; idx < instr.ops.size(); ++idx) {
    const auto& argVal = instr.ops[idx];
    CallArg arg{};
    arg.kind = regClassFor(argVal.kind) == RegClass::GPR ? CallArg::GPR : CallArg::XMM;

    if (isImmediate(argVal)) {
      arg.isImm = true;
      arg.imm = argVal.i64;
    } else {
      const Operand operand = makeOperandForValue(block, argVal, regClassFor(argVal.kind));
      if (const auto* reg = std::get_if<OpReg>(&operand)) {
        arg.vreg = reg->idOrPhys;
      } else if (const auto* imm = std::get_if<OpImm>(&operand)) {
        arg.isImm = true;
        arg.imm = imm->val;
      }
    }

    plan.args.push_back(arg);
  }

  if (instr.resultId >= 0) {
    ensureVReg(instr.resultId, instr.resultKind);
    if (instr.resultKind == ILValue::Kind::F64) {
      plan.returnsF64 = true;
    }
  }

  callPlans_.push_back(plan);
  block.append(MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand(instr.ops[0])}));
}

void LowerILToMIR::lowerLoad(const ILInstr& instr, MBasicBlock& block, RegClass cls) {
  if (instr.resultId < 0 || instr.ops.empty()) {
    return;
  }

  Operand baseOp = makeOperandForValue(block, instr.ops[0], RegClass::GPR);
  const auto* baseReg = std::get_if<OpReg>(&baseOp);
  if (!baseReg) {
    return;
  }

  const int32_t disp = instr.ops.size() > 1 ? static_cast<int32_t>(instr.ops[1].i64) : 0;
  const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
  const Operand dest = makeVRegOperand(destReg.cls, destReg.id);
  const Operand mem = makeMemOperand(*baseReg, disp);

  if (cls == RegClass::GPR) {
    block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), mem}));
  } else {
    block.append(MInstr::make(MOpcode::MOVSDmr, std::vector<Operand>{cloneOperand(dest), mem}));
  }
}

void LowerILToMIR::lowerStore(const ILInstr& instr, MBasicBlock& block) {
  if (instr.ops.size() < 2) {
    return;
  }

  const Operand value = makeOperandForValue(block, instr.ops[0], regClassFor(instr.ops[0].kind));
  Operand baseOp = makeOperandForValue(block, instr.ops[1], RegClass::GPR);
  const auto* baseReg = std::get_if<OpReg>(&baseOp);
  if (!baseReg) {
    return;
  }
  const int32_t disp = instr.ops.size() > 2 ? static_cast<int32_t>(instr.ops[2].i64) : 0;
  const Operand mem = makeMemOperand(*baseReg, disp);

  if (std::holds_alternative<OpReg>(value)) {
    const auto cls = std::get<OpReg>(value).cls;
    if (cls == RegClass::XMM) {
      block.append(MInstr::make(MOpcode::MOVSDrm, std::vector<Operand>{mem, value}));
    } else {
      block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{mem, value}));
    }
  } else {
    block.append(MInstr::make(MOpcode::MOVri, std::vector<Operand>{mem, value}));
  }
}

void LowerILToMIR::lowerCast(const ILInstr& instr, MBasicBlock& block, MOpcode opc,
                             RegClass dstCls, RegClass srcCls) {
  if (instr.resultId < 0 || instr.ops.empty()) {
    return;
  }

  const Operand src = makeOperandForValue(block, instr.ops[0], srcCls);
  const VReg destReg = ensureVReg(instr.resultId, instr.resultKind);
  const Operand dest = makeVRegOperand(destReg.cls, destReg.id);

  if (opc == MOpcode::MOVrr || std::holds_alternative<OpImm>(src)) {
    block.append(MInstr::make(MOpcode::MOVrr, std::vector<Operand>{cloneOperand(dest), src}));
  } else {
    block.append(MInstr::make(opc, std::vector<Operand>{cloneOperand(dest), src}));
  }

  (void)dstCls;
}

void LowerILToMIR::lowerInstruction(const ILInstr& instr, MBasicBlock& block) {
  const std::string_view opc{instr.opcode};

  if (opc == "add") {
    const RegClass cls = regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::ADDrr : MOpcode::FADD;
    const MOpcode opRI = cls == RegClass::GPR ? MOpcode::ADDri : opRR;
    lowerBinary(instr, block, opRR, opRI, cls);
    return;
  }
  if (opc == "sub") {
    const RegClass cls = regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::SUBrr : MOpcode::FSUB;
    lowerBinary(instr, block, opRR, opRR, cls);
    return;
  }
  if (opc == "mul") {
    const RegClass cls = regClassFor(instr.resultKind);
    const MOpcode opRR = cls == RegClass::GPR ? MOpcode::IMULrr : MOpcode::FMUL;
    lowerBinary(instr, block, opRR, opRR, cls);
    return;
  }
  if (opc == "cmp") {
    lowerCmp(instr, block, regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
    return;
  }
  if (opc == "select") {
    lowerSelect(instr, block);
    return;
  }
  if (opc == "br") {
    lowerBranch(instr, block);
    return;
  }
  if (opc == "cbr") {
    lowerCondBranch(instr, block);
    return;
  }
  if (opc == "ret") {
    lowerReturn(instr, block);
    return;
  }
  if (opc == "call") {
    lowerCall(instr, block);
    return;
  }
  if (opc == "load") {
    lowerLoad(instr, block, regClassFor(instr.resultKind));
    return;
  }
  if (opc == "store") {
    lowerStore(instr, block);
    return;
  }
  if (opc == "zext" || opc == "sext" || opc == "trunc") {
    lowerCast(instr, block, MOpcode::MOVrr, regClassFor(instr.resultKind),
              regClassFor(instr.ops.empty() ? instr.resultKind : instr.ops.front().kind));
    return;
  }
  if (opc == "sitofp") {
    lowerCast(instr, block, MOpcode::CVTSI2SD, RegClass::XMM, RegClass::GPR);
    return;
  }
  if (opc == "fptosi") {
    lowerCast(instr, block, MOpcode::CVTTSD2SI, RegClass::GPR, RegClass::XMM);
    return;
  }
  // TODO: handle division and additional opcodes.
}

void LowerILToMIR::emitEdgeCopies(const ILBlock& source, MBasicBlock& block) {
  for (const auto& edge : source.terminatorEdges) {
    const auto destIt = blockInfo_.find(edge.to);
    if (destIt == blockInfo_.end() || destIt->second.paramVRegs.empty()) {
      continue;
    }
    const auto& params = destIt->second.paramVRegs;
    if (edge.argIds.empty()) {
      continue;
    }

    MInstr px = MInstr::make(MOpcode::PX_COPY, {});
    for (std::size_t idx = 0; idx < params.size() && idx < edge.argIds.size(); ++idx) {
      const auto valIt = valueToVReg_.find(edge.argIds[idx]);
      if (valIt == valueToVReg_.end()) {
        continue;
      }
      px.operands.push_back(
          makeVRegOperand(params[idx].cls, params[idx].id));
      px.operands.push_back(
          makeVRegOperand(valIt->second.cls, valIt->second.id));
    }

    if (!px.operands.empty()) {
      block.append(std::move(px));
    }
  }
}

MFunction LowerILToMIR::lower(const ILFunction& func) {
  resetFunctionState();

  MFunction result{};
  result.name = func.name;

  result.blocks.reserve(func.blocks.size());

  for (std::size_t idx = 0; idx < func.blocks.size(); ++idx) {
    const auto& ilBlock = func.blocks[idx];
    BlockInfo info{};
    info.index = idx;
    info.paramVRegs.reserve(ilBlock.paramIds.size());

    MBasicBlock block{};
    block.label = ilBlock.name;

    for (std::size_t p = 0; p < ilBlock.paramIds.size() && p < ilBlock.paramKinds.size(); ++p) {
      const int paramId = ilBlock.paramIds[p];
      const auto kind = ilBlock.paramKinds[p];
      if (paramId >= 0) {
        info.paramVRegs.push_back(ensureVReg(paramId, kind));
      }
    }

    blockInfo_[ilBlock.name] = info;
    result.addBlock(std::move(block));
  }

  for (std::size_t idx = 0; idx < func.blocks.size(); ++idx) {
    const auto& ilBlock = func.blocks[idx];
    auto& mirBlock = result.blocks[idx];

    for (const auto& instr : ilBlock.instrs) {
      lowerInstruction(instr, mirBlock);
    }

    emitEdgeCopies(ilBlock, mirBlock);
  }

  return result;
}

} // namespace viper::codegen::x64

