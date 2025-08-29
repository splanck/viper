#include "vm/VM.h"
#include "il/core/Opcode.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace il::core;

namespace il::vm {

RuntimeBridge::RuntimeBridge(const Module &m) {
  for (const auto &g : m.globals) {
    if (g.type.kind == Type::Kind::Str) {
      globals_[g.name] = rt_const_cstr(g.init.c_str());
    }
  }
}

rt_str RuntimeBridge::getConstStr(const std::string &name) const {
  auto it = globals_.find(name);
  return it == globals_.end() ? nullptr : it->second;
}

bool RuntimeBridge::call(const std::string &name, const std::vector<Slot> &args, Slot &ret) const {
  (void)ret;
  if (name == "rt_print_str") {
    rt_print_str(args[0].str);
    return true;
  }
  if (name == "rt_print_i64") {
    rt_print_i64(args[0].i64);
    return true;
  }
  return false;
}

VM::VM(bool trace) : trace_(trace) {}

int64_t VM::run(const Module &m) {
  module_ = &m;
  RuntimeBridge bridge(m);
  bridge_ = &bridge;
  for (size_t i = 0; i < m.functions.size(); ++i) {
    if (m.functions[i].name == "main")
      return execFunction(m.functions[i]);
  }
  std::cerr << "No @main found\n";
  return 1;
}

Slot VM::eval(const Value &v, Frame &f) {
  Slot s{};
  switch (v.kind) {
  case Value::Kind::Temp:
    return f.temps[v.id];
  case Value::Kind::ConstInt:
    s.i64 = v.i64;
    return s;
  case Value::Kind::ConstFloat:
    s.f64 = v.f64;
    return s;
  case Value::Kind::NullPtr:
    s.ptr = nullptr;
    return s;
  case Value::Kind::ConstStr:
    s.str = rt_const_cstr(v.str.c_str());
    return s;
  default:
    assert(false && "unhandled value kind");
  }
  return s; // unreachable
}

int64_t VM::execFunction(const Function &fn) {
  Frame fr;
  std::unordered_map<std::string, size_t> labelToIndex;
  for (size_t i = 0; i < fn.blocks.size(); ++i)
    labelToIndex[fn.blocks[i].label] = i;
  size_t bi = 0;
  size_t ip = 0;
  while (true) {
    auto &block = fn.blocks[bi];
    auto &in = block.instructions[ip];
    if (trace_)
      std::cout << "@" << fn.name << "/" << block.label << ": " << toString(in.op) << "\n";
    Slot res{};
    switch (in.op) {
    case Opcode::Alloca: {
      auto sz = eval(in.operands[0], fr).i64;
      res.ptr = fr.alloca(static_cast<size_t>(sz));
      break;
    }
    case Opcode::Load: {
      Slot ptr = eval(in.operands[0], fr);
      if (!ptr.ptr) {
        std::cerr << "trap: null load\n";
        return 1;
      }
      if (in.type.kind == Type::Kind::I64)
        res.i64 = *reinterpret_cast<int64_t *>(ptr.ptr);
      else if (in.type.kind == Type::Kind::Str)
        res.str = *reinterpret_cast<rt_str *>(ptr.ptr);
      else
        assert(false && "unsupported load type");
      break;
    }
    case Opcode::Store: {
      Slot ptr = eval(in.operands[0], fr);
      Slot val = eval(in.operands[1], fr);
      if (!ptr.ptr) {
        std::cerr << "trap: null store\n";
        return 1;
      }
      if (in.type.kind == Type::Kind::I64)
        *reinterpret_cast<int64_t *>(ptr.ptr) = val.i64;
      else if (in.type.kind == Type::Kind::Str)
        *reinterpret_cast<rt_str *>(ptr.ptr) = val.str;
      else
        assert(false && "unsupported store type");
      break;
    }
    case Opcode::Add: {
      Slot a = eval(in.operands[0], fr);
      Slot b = eval(in.operands[1], fr);
      res.i64 = a.i64 + b.i64;
      break;
    }
    case Opcode::Mul: {
      Slot a = eval(in.operands[0], fr);
      Slot b = eval(in.operands[1], fr);
      res.i64 = a.i64 * b.i64;
      break;
    }
    case Opcode::SCmpGT: {
      Slot a = eval(in.operands[0], fr);
      Slot b = eval(in.operands[1], fr);
      res.i64 = a.i64 > b.i64;
      break;
    }
    case Opcode::SCmpLE: {
      Slot a = eval(in.operands[0], fr);
      Slot b = eval(in.operands[1], fr);
      res.i64 = a.i64 <= b.i64;
      break;
    }
    case Opcode::CBr: {
      Slot cond = eval(in.operands[0], fr);
      bi = labelToIndex[cond.i64 ? in.labels[0] : in.labels[1]];
      ip = 0;
      continue;
    }
    case Opcode::Br: {
      bi = labelToIndex[in.labels[0]];
      ip = 0;
      continue;
    }
    case Opcode::Ret: {
      if (in.operands.empty())
        return 0;
      Slot v = eval(in.operands[0], fr);
      return v.i64;
    }
    case Opcode::ConstStr: {
      res.str = bridge_->getConstStr(in.operands[0].str);
      break;
    }
    case Opcode::Call: {
      std::vector<Slot> args;
      for (const auto &op : in.operands)
        args.push_back(eval(op, fr));
      if (!bridge_->call(in.callee, args, res)) {
        std::cerr << "unknown extern: " << in.callee << "\n";
        return 1;
      }
      break;
    }
    default:
      std::cerr << "unimplemented opcode\n";
      return 1;
    }
    if (in.result) {
      if (fr.temps.size() <= *in.result)
        fr.temps.resize(*in.result + 1);
      fr.temps[*in.result] = res;
    }
    ip++;
  }
}

} // namespace il::vm
