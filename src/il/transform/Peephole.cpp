// File: src/il/transform/Peephole.cpp
// Purpose: Implements local IL peephole optimizations.
// Key invariants: Transformations preserve program semantics.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md
#include "il/transform/Peephole.h"
#include "il/core/Function.h"
#include "il/core/Instr.h"

using namespace il::core;

namespace il::transform {
namespace {

static bool isConstInt(const Value &v, long long &out) {
  if (v.kind == Value::Kind::ConstInt) {
    out = v.i64;
    return true;
  }
  return false;
}

static bool isConstEq(const Value &v, long long target) {
  long long c;
  return isConstInt(v, c) && c == target;
}

static void replaceAll(Function &f, unsigned id, const Value &v) {
  for (auto &b : f.blocks)
    for (auto &in : b.instructions)
      for (auto &op : in.operands)
        if (op.kind == Value::Kind::Temp && op.id == id)
          op = v;
}

} // namespace

void peephole(Module &m) {
  for (auto &f : m.functions) {
    for (auto &b : f.blocks) {
      for (size_t i = 0; i < b.instructions.size(); ++i) {
        Instr &in = b.instructions[i];
        if (in.op == Opcode::CBr) {
          long long v;
          bool known = false;
          if (isConstInt(in.operands[0], v)) {
            known = true;
          } else if (in.operands[0].kind == Value::Kind::Temp) {
            unsigned id = in.operands[0].id;
            for (size_t j = 0; j < i; ++j) {
              Instr &def = b.instructions[j];
              if (def.result && *def.result == id && def.operands.size() == 2) {
                long long l, r;
                if (isConstInt(def.operands[0], l) && isConstInt(def.operands[1], r)) {
                  switch (def.op) {
                  case Opcode::ICmpEq:
                    v = (l == r);
                    known = true;
                    break;
                  case Opcode::ICmpNe:
                    v = (l != r);
                    known = true;
                    break;
                  case Opcode::SCmpLT:
                    v = (l < r);
                    known = true;
                    break;
                  case Opcode::SCmpLE:
                    v = (l <= r);
                    known = true;
                    break;
                  case Opcode::SCmpGT:
                    v = (l > r);
                    known = true;
                    break;
                  case Opcode::SCmpGE:
                    v = (l >= r);
                    known = true;
                    break;
                  default:
                    break;
                  }
                }
              }
              if (known)
                break;
            }
          }
          if (known) {
            in.op = Opcode::Br;
            in.labels = {v ? in.labels[0] : in.labels[1]};
            in.operands.clear();
          }
          continue;
        }
        if (!in.result || in.operands.size() != 2)
          continue;
        Value repl{};
        bool match = false;
        switch (in.op) {
        case Opcode::Add:
          if (isConstEq(in.operands[0], 0)) {
            repl = in.operands[1];
            match = true;
          } else if (isConstEq(in.operands[1], 0)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::Sub:
          if (isConstEq(in.operands[1], 0)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::Mul:
          if (isConstEq(in.operands[0], 1)) {
            repl = in.operands[1];
            match = true;
          } else if (isConstEq(in.operands[1], 1)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::And:
          if (isConstEq(in.operands[0], -1)) {
            repl = in.operands[1];
            match = true;
          } else if (isConstEq(in.operands[1], -1)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::Or:
          if (isConstEq(in.operands[0], 0)) {
            repl = in.operands[1];
            match = true;
          } else if (isConstEq(in.operands[1], 0)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::Xor:
          if (isConstEq(in.operands[0], 0)) {
            repl = in.operands[1];
            match = true;
          } else if (isConstEq(in.operands[1], 0)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
          if (isConstEq(in.operands[1], 0)) {
            repl = in.operands[0];
            match = true;
          }
          break;
        default:
          break;
        }
        if (match) {
          replaceAll(f, *in.result, repl);
          b.instructions.erase(b.instructions.begin() + i);
          --i;
        }
      }
    }
  }
}

} // namespace il::transform
