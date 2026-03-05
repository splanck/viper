//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file combined.cpp
 * @brief Smoke test combining TCO (tail-calls), externs, opcode counters, and polling pause/resume.
 *
 * @details
 * This example demonstrates several VM features working together:
 * - Tail-call optimization (TCO)
 * - External function registration and invocation
 * - Opcode counting
 * - Polling pause/resume execution model
 */

#include "il/build/IRBuilder.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "viper/vm/VM.hpp"
#include <cstdint>
#include <iostream>

static void times2_handler(void **args, void *result) {
  const auto x = *reinterpret_cast<const int64_t *>(args[0]);
  *reinterpret_cast<int64_t *>(result) = x * 2;
}

int main() {
  using namespace il::core;
  using il::runtime::signatures::SigParam;

  il::vm::ExternDesc ext;
  ext.name = "times2";
  ext.signature = il::runtime::signatures::make_signature(
      "times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
  ext.fn = reinterpret_cast<void *>(&times2_handler);

  Module m;
  il::build::IRBuilder b(m);
  b.addExtern("times2", Type(Type::Kind::I64), {Type(Type::Kind::I64)});

  // f3(): ret times2(21)
  auto &f3 = b.startFunction("f3", Type(Type::Kind::I64), {});
  auto &bb3 = b.addBlock(f3, "entry");
  b.setInsertPoint(bb3);
  unsigned r3 = b.reserveTempId();
  Instr c3; c3.result=r3; c3.op=Opcode::Call; c3.type=Type(Type::Kind::I64); c3.callee="times2"; c3.operands.push_back(Value::constInt(21)); bb3.instructions.push_back(c3);
  Instr ret3; ret3.op=Opcode::Ret; ret3.type=Type(Type::Kind::Void); ret3.operands.push_back(Value::temp(r3)); bb3.instructions.push_back(ret3); bb3.terminated=true;

  // f2(): t=f3(); ret t  (TCO eligible)
  auto &f2 = b.startFunction("f2", Type(Type::Kind::I64), {});
  auto &bb2 = b.addBlock(f2, "entry"); b.setInsertPoint(bb2);
  unsigned r2t = b.reserveTempId();
  Instr c2; c2.result=r2t; c2.op=Opcode::Call; c2.type=Type(Type::Kind::I64); c2.callee="f3"; bb2.instructions.push_back(c2);
  Instr ret2; ret2.op=Opcode::Ret; ret2.type=Type(Type::Kind::Void); ret2.operands.push_back(Value::temp(r2t)); bb2.instructions.push_back(ret2); bb2.terminated=true;

  // f1(): t=f2(); ret t  (TCO eligible)
  auto &f1 = b.startFunction("f1", Type(Type::Kind::I64), {});
  auto &bb1 = b.addBlock(f1, "entry"); b.setInsertPoint(bb1);
  unsigned r1t = b.reserveTempId();
  Instr c1; c1.result=r1t; c1.op=Opcode::Call; c1.type=Type(Type::Kind::I64); c1.callee="f2"; bb1.instructions.push_back(c1);
  Instr ret1; ret1.op=Opcode::Ret; ret1.type=Type(Type::Kind::Void); ret1.operands.push_back(Value::temp(r1t)); bb1.instructions.push_back(ret1); bb1.terminated=true;

  // main: return f1()
  auto &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
  auto &mbb = b.addBlock(mainFn, "entry");
  b.setInsertPoint(mbb);
  unsigned mdst = b.reserveTempId();
  Instr cm; cm.result = mdst; cm.op=Opcode::Call; cm.type=Type(Type::Kind::I64); cm.callee="f1"; mbb.instructions.push_back(cm);
  Instr mr; mr.op=Opcode::Ret; mr.type=Type(Type::Kind::Void); mr.operands.push_back(Value::temp(mdst)); mbb.instructions.push_back(mr); mbb.terminated=true;

  il::vm::RunConfig cfg;
  cfg.externs.push_back(ext);
  cfg.interruptEveryN = 1;
  int polls = 0;
  cfg.pollCallback = [&polls](il::vm::VM &) { return (++polls < 5); };
  il::vm::Runner r(m, cfg);
  r.resetOpcodeCounts();
  auto st = r.continueRun();
  std::cout << "first run status=" << static_cast<int>(st) << " polls=" << polls << "\n";
  auto st2 = r.continueRun();
  std::cout << "second run status=" << static_cast<int>(st2) << "\n";
  // Counters are enabled by default; full inspection shown in vm-profiling docs.
  std::cout << "smoke complete\n";
  return 0;
}
