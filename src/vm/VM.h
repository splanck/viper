#pragma once
#include "il/core/Module.h"
#include "rt.h"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace il::vm {

struct Slot {
  union {
    int64_t i64;
    double f64;
    void *ptr;
    rt_str str;
  };
};

struct Frame {
  std::vector<Slot> temps;
  std::vector<uint8_t> mem;
  size_t sp = 0;
  void *alloca(size_t bytes) {
    size_t base = sp;
    mem.resize(sp + bytes);
    memset(&mem[base], 0, bytes);
    sp += bytes;
    return mem.data() + base;
  }
};

class RuntimeBridge {
public:
  explicit RuntimeBridge(const il::core::Module &m);
  rt_str getConstStr(const std::string &name) const;
  bool call(const std::string &name, const std::vector<Slot> &args, Slot &ret) const;

private:
  std::unordered_map<std::string, rt_str> globals_;
};

class VM {
public:
  explicit VM(bool trace = false);
  int64_t run(const il::core::Module &m);

private:
  bool trace_;
  const il::core::Module *module_ = nullptr;
  RuntimeBridge *bridge_ = nullptr;

  Slot eval(const il::core::Value &v, Frame &f);
  int64_t execFunction(const il::core::Function &fn);
};

} // namespace il::vm
