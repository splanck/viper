// File: src/il/build/IRBuilder.cc
// Purpose: Implements helpers to construct IL modules.
// Key invariants: None.
// Ownership/Lifetime: Builder references module owned externally.
// Links: docs/il-spec.md
#include "il/build/IRBuilder.h"
#include <cassert>

namespace il::build {

IRBuilder::IRBuilder(Module &m) : mod(m) {}

Extern &IRBuilder::addExtern(const std::string &name, Type ret, const std::vector<Type> &params) {
  mod.externs.push_back({name, ret, params});
  return mod.externs.back();
}

Global &IRBuilder::addGlobalStr(const std::string &name, const std::string &value) {
  mod.globals.push_back({name, Type(Type::Kind::Str), value});
  return mod.globals.back();
}

Function &IRBuilder::startFunction(const std::string &name, Type ret,
                                   const std::vector<Param> &params) {
  mod.functions.push_back({name, ret, params, {}});
  curFunc = &mod.functions.back();
  curBlock = nullptr;
  nextTemp = 0;
  return *curFunc;
}

BasicBlock &IRBuilder::addBlock(Function &fn, const std::string &label) {
  fn.blocks.push_back({label, {}, false});
  return fn.blocks.back();
}

void IRBuilder::setInsertPoint(BasicBlock &bb) { curBlock = &bb; }

Instr &IRBuilder::append(Instr instr) {
  assert(curBlock && "insert point not set");
  if (isTerminator(instr.op)) {
    assert(!curBlock->terminated && "block already terminated");
    curBlock->terminated = true;
  }
  curBlock->instructions.push_back(std::move(instr));
  return curBlock->instructions.back();
}

bool IRBuilder::isTerminator(Opcode op) const {
  return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret || op == Opcode::Trap;
}

Value IRBuilder::emitConstStr(const std::string &globalName) {
  unsigned id = nextTemp++;
  Instr instr;
  instr.result = id;
  instr.op = Opcode::ConstStr;
  instr.type = Type(Type::Kind::Str);
  instr.operands.push_back(Value::global(globalName));
  append(std::move(instr));
  return Value::temp(id);
}

void IRBuilder::emitCall(const std::string &callee, const std::vector<Value> &args) {
  Instr instr;
  instr.op = Opcode::Call;
  instr.type = Type(Type::Kind::Void);
  instr.callee = callee;
  instr.operands = args;
  append(std::move(instr));
}

void IRBuilder::emitRet(const std::optional<Value> &v) {
  Instr instr;
  instr.op = Opcode::Ret;
  instr.type = Type(Type::Kind::Void);
  if (v)
    instr.operands.push_back(*v);
  append(std::move(instr));
}

} // namespace il::build
