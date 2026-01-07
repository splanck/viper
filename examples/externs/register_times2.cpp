// File: examples/externs/register_times2.cpp
// Purpose: Demonstrate registering a simple extern with the VM runtime bridge.

#include "il/build/IRBuilder.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "viper/vm/VM.hpp"
#include <cstdint>
#include <iostream>

// Runtime handler ABI: void(void** args, void* result)
static void times2_handler(void **args, void *result) {
  const auto x = *reinterpret_cast<const int64_t *>(args[0]);
  *reinterpret_cast<int64_t *>(result) = x * 2;
}

int main() {
  using il::runtime::signatures::SigParam;
  // Prepare extern description to inject via RunConfig.
  il::vm::ExternDesc ext;
  ext.name = "times2";
  ext.signature = il::runtime::signatures::make_signature(
      "times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
  ext.fn = reinterpret_cast<void *>(&times2_handler);

  // Build a tiny IL module that calls @times2(21) and returns the result.
  il::core::Module m;
  il::build::IRBuilder b(m);
  b.addExtern("times2", il::core::Type(il::core::Type::Kind::I64),
              {il::core::Type(il::core::Type::Kind::I64)});
  auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
  auto &bb = b.addBlock(fn, "entry");
  b.setInsertPoint(bb);
  auto c21 = il::core::Value::constInt(21);
  auto dst = b.reserveTempId();
  il::core::Instr call;
  call.result = dst;
  call.op = il::core::Opcode::Call;
  call.type = il::core::Type(il::core::Type::Kind::I64);
  call.callee = "times2";
  call.operands.push_back(c21);
  bb.instructions.push_back(call);
  il::core::Instr ret;
  ret.op = il::core::Opcode::Ret;
  ret.type = il::core::Type(il::core::Type::Kind::Void);
  ret.operands.push_back(il::core::Value::temp(dst));
  bb.instructions.push_back(ret);
  bb.terminated = true;

  il::vm::RunConfig cfg;
  cfg.externs.push_back(ext);
  il::vm::Runner r(m, cfg);
  auto status = r.continueRun();
  std::cout << "run status=" << static_cast<int>(status) << "\n";
  return 0;
}
