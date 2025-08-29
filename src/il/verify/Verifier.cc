#include "il/verify/Verifier.h"
#include "il/core/Opcode.h"
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::verify {

namespace {
bool isTerminator(Opcode op) { return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret; }

Type valueType(const Value &v, const std::unordered_map<unsigned, Type> &temps) {
  switch (v.kind) {
  case Value::Kind::Temp: {
    auto it = temps.find(v.id);
    if (it != temps.end())
      return it->second;
    return Type(Type::Kind::Void);
  }
  case Value::Kind::ConstInt:
    return Type(Type::Kind::I64);
  case Value::Kind::ConstFloat:
    return Type(Type::Kind::F64);
  case Value::Kind::ConstStr:
    return Type(Type::Kind::Str);
  case Value::Kind::GlobalAddr:
  case Value::Kind::NullPtr:
    return Type(Type::Kind::Ptr);
  }
  return Type(Type::Kind::Void);
}

} // namespace

bool Verifier::verify(const Module &m, std::ostream &err) {
  std::unordered_map<std::string, const Extern *> externs;
  for (const auto &e : m.externs)
    externs[e.name] = &e;
  std::unordered_map<std::string, const Function *> funcs;
  for (const auto &f : m.functions)
    funcs[f.name] = &f;

  bool ok = true;
  for (const auto &fn : m.functions) {
    std::unordered_set<std::string> labels;
    std::unordered_map<unsigned, Type> temps;
    for (const auto &bb : fn.blocks) {
      if (!labels.insert(bb.label).second) {
        err << "Duplicate label " << bb.label << " in function " << fn.name << "\n";
        ok = false;
      }
    }
    for (const auto &bb : fn.blocks) {
      if (bb.instructions.empty()) {
        err << "Empty block " << bb.label << " in function " << fn.name << "\n";
        ok = false;
        continue;
      }
      if (!isTerminator(bb.instructions.back().op)) {
        err << "Block " << bb.label << " missing terminator in function " << fn.name << "\n";
        ok = false;
      }
      bool seenTerm = false;
      for (const auto &in : bb.instructions) {
        if (seenTerm) {
          err << "Instruction after terminator in block " << bb.label << "\n";
          ok = false;
          break;
        }
        if (isTerminator(in.op))
          seenTerm = true;
        switch (in.op) {
        case Opcode::Alloca:
          if (in.operands.size() != 1) {
            err << "alloca operand count\n";
            ok = false;
          }
          if (in.result)
            temps[*in.result] = Type(Type::Kind::Ptr);
          break;
        case Opcode::Add:
        case Opcode::Mul: {
          if (in.operands.size() != 2) {
            err << "binary op operand count\n";
            ok = false;
          }
          for (const auto &v : in.operands) {
            if (valueType(v, temps).kind != Type::Kind::I64) {
              err << "binary op type mismatch\n";
              ok = false;
            }
          }
          if (in.result)
            temps[*in.result] = Type(Type::Kind::I64);
          break;
        }
        case Opcode::SCmpGT:
        case Opcode::SCmpLE: {
          if (in.operands.size() != 2) {
            err << "cmp operand count\n";
            ok = false;
          }
          for (const auto &v : in.operands) {
            if (valueType(v, temps).kind != Type::Kind::I64) {
              err << "cmp type mismatch\n";
              ok = false;
            }
          }
          if (in.result)
            temps[*in.result] = Type(Type::Kind::I1);
          break;
        }
        case Opcode::Load: {
          if (in.operands.size() != 1) {
            err << "load operand count\n";
            ok = false;
          }
          if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr) {
            err << "load pointer type mismatch\n";
            ok = false;
          }
          if (in.result)
            temps[*in.result] = in.type;
          break;
        }
        case Opcode::Store: {
          if (in.operands.size() != 2) {
            err << "store operand count\n";
            ok = false;
          }
          if (valueType(in.operands[0], temps).kind != Type::Kind::Ptr) {
            err << "store pointer type mismatch\n";
            ok = false;
          }
          if (valueType(in.operands[1], temps).kind != in.type.kind) {
            err << "store value type mismatch\n";
            ok = false;
          }
          break;
        }
        case Opcode::ConstStr: {
          if (in.operands.size() != 1 || in.operands[0].kind != Value::Kind::GlobalAddr) {
            err << "const_str operand\n";
            ok = false;
          } else {
            bool found = false;
            for (const auto &g : m.globals)
              if (g.name == in.operands[0].str)
                found = true;
            if (!found) {
              err << "unknown global @" << in.operands[0].str << "\n";
              ok = false;
            }
          }
          if (in.result)
            temps[*in.result] = Type(Type::Kind::Str);
          break;
        }
        case Opcode::Call: {
          const Extern *sig = nullptr;
          auto itExt = externs.find(in.callee);
          if (itExt != externs.end())
            sig = itExt->second;
          else {
            auto itFn = funcs.find(in.callee);
            if (itFn != funcs.end()) {
              sig = reinterpret_cast<const Extern *>(itFn->second);
            }
          }
          if (!sig) {
            err << "unknown callee @" << in.callee << "\n";
            ok = false;
            break;
          }
          if (in.operands.size() != sig->params.size()) {
            err << "call arg count\n";
            ok = false;
          }
          for (size_t i = 0; i < in.operands.size() && i < sig->params.size(); ++i) {
            if (valueType(in.operands[i], temps).kind != sig->params[i].kind) {
              err << "call arg type mismatch\n";
              ok = false;
            }
          }
          if (in.result)
            temps[*in.result] = sig->retType;
          break;
        }
        case Opcode::Br: {
          if (in.labels.size() != 1) {
            err << "br label count\n";
            ok = false;
          }
          break;
        }
        case Opcode::CBr: {
          if (in.operands.size() != 1 || in.labels.size() != 2) {
            err << "cbr operand/label count\n";
            ok = false;
          }
          if (valueType(in.operands[0], temps).kind != Type::Kind::I1) {
            err << "cbr condition type\n";
            ok = false;
          }
          break;
        }
        case Opcode::Ret: {
          if (fn.retType.kind == Type::Kind::Void) {
            if (!in.operands.empty()) {
              err << "ret void with value\n";
              ok = false;
            }
          } else {
            if (in.operands.size() != 1) {
              err << "ret missing value\n";
              ok = false;
            } else if (valueType(in.operands[0], temps).kind != fn.retType.kind) {
              err << "ret value type mismatch\n";
              ok = false;
            }
          }
          break;
        }
        default:
          break;
        }
      }
    }
    for (const auto &bb : fn.blocks) {
      for (const auto &in : bb.instructions) {
        for (const auto &lbl : in.labels) {
          if (!labels.count(lbl)) {
            err << "unknown label " << lbl << " in function " << fn.name << "\n";
            ok = false;
          }
        }
      }
    }
  }
  return ok;
}

} // namespace il::verify
