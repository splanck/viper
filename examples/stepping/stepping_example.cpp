// File: examples/stepping/stepping_example.cpp
// Purpose: Minimal usage of Runner step/continue API.

#include "il/build/IRBuilder.hpp"
#include "viper/vm/VM.hpp"
#include <iostream>

int main() {
  using namespace il::core;
  Module m;
  il::build::IRBuilder b(m);
  auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
  auto &bb = b.addBlock(fn, "entry");
  b.setInsertPoint(bb);
  // t0 = add 40, 2 ; ret t0
  Instr add;
  add.result = b.reserveTempId();
  add.op = Opcode::Add;
  add.type = Type(Type::Kind::I64);
  add.operands.push_back(Value::constInt(40));
  add.operands.push_back(Value::constInt(2));
  bb.instructions.push_back(add);
  Instr ret;
  ret.op = Opcode::Ret;
  ret.type = Type(Type::Kind::Void);
  ret.operands.push_back(Value::temp(*add.result));
  bb.instructions.push_back(ret);
  bb.terminated = true;

  il::vm::RunConfig cfg; // defaults
  il::vm::Runner runner(m, cfg);
  auto s1 = runner.step();
  std::cout << "Step status: " << static_cast<int>(s1.status) << "\n";
  auto rs = runner.continueRun();
  std::cout << "Run status: " << static_cast<int>(rs) << "\n";
  return 0;
}
