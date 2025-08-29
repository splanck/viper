#pragma once
#include "il/core/Module.h"
#include "rt.h"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::vm {

union Slot {
  int64_t i64;
  double f64;
  void *ptr;
  rt_str str;
};

struct Frame {
  const il::core::Function *func;
  std::vector<Slot> regs;
  std::array<uint8_t, 1024> stack;
  size_t sp = 0;
};

class VM {
public:
  VM(const il::core::Module &m, bool trace = false);
  int64_t run();

private:
  const il::core::Module &mod;
  bool trace;
  std::unordered_map<std::string, const il::core::Function *> fnMap;
  std::unordered_map<std::string, rt_str> strMap;

  int64_t execFunction(const il::core::Function &fn);
  Slot eval(Frame &fr, const il::core::Value &v);
};

} // namespace il::vm
