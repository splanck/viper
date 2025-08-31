// File: src/il/transform/ConstFold.cpp
// Purpose: Implements constant folding for simple IL integer operations.
// Key invariants: Uses wraparound semantics for 64-bit integer arithmetic.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md
#include "il/transform/ConstFold.h"
#include "il/core/Instr.h"
#include <cstdint>

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

static bool sameValue(const Value &a, const Value &b) {
  if (a.kind != b.kind)
    return false;
  switch (a.kind) {
  case Value::Kind::Temp:
    return a.id == b.id;
  case Value::Kind::ConstInt:
    return a.i64 == b.i64;
  case Value::Kind::ConstStr:
  case Value::Kind::GlobalAddr:
    return a.str == b.str;
  case Value::Kind::ConstFloat:
    return a.f64 == b.f64;
  case Value::Kind::NullPtr:
    return true;
  }
  return false;
}

static long long wrapAdd(long long a, long long b) {
  return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
static long long wrapSub(long long a, long long b) {
  return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
static long long wrapMul(long long a, long long b) {
  return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

static void replaceAll(Function &f, unsigned id, const Value &v) {
  for (auto &b : f.blocks)
    for (auto &in : b.instructions)
      for (auto &op : in.operands)
        if (op.kind == Value::Kind::Temp && op.id == id)
          op = v;
}

} // namespace

void constFold(Module &m) {
  for (auto &f : m.functions) {
    for (auto &b : f.blocks) {
      for (size_t i = 0; i < b.instructions.size(); ++i) {
        Instr &in = b.instructions[i];
        if (!in.result || in.operands.size() != 2)
          continue;
        long long lhsVal = 0, rhsVal = 0;
        bool lhsConst = isConstInt(in.operands[0], lhsVal);
        bool rhsConst = isConstInt(in.operands[1], rhsVal);
        if (!lhsConst && !rhsConst)
          continue;
        bool folded = false;
        Value repl;
        if (lhsConst && rhsConst) {
          long long res = 0;
          folded = true;
          switch (in.op) {
          case Opcode::Add:
            res = wrapAdd(lhsVal, rhsVal);
            break;
          case Opcode::Sub:
            res = wrapSub(lhsVal, rhsVal);
            break;
          case Opcode::Mul:
            res = wrapMul(lhsVal, rhsVal);
            break;
          case Opcode::And:
            res = lhsVal & rhsVal;
            break;
          case Opcode::Or:
            res = lhsVal | rhsVal;
            break;
          case Opcode::Xor:
            res = lhsVal ^ rhsVal;
            break;
          case Opcode::ICmpEq:
          case Opcode::ICmpNe:
          case Opcode::SCmpLT:
          case Opcode::SCmpLE:
          case Opcode::SCmpGT:
          case Opcode::SCmpGE:
            folded = false;
            break;
          default:
            folded = false;
            break;
          }
          if (folded)
            repl = Value::constInt(res);
        } else {
          switch (in.op) {
          case Opcode::Add:
            if (lhsConst && lhsVal == 0) {
              repl = in.operands[1];
              folded = true;
            } else if (rhsConst && rhsVal == 0) {
              repl = in.operands[0];
              folded = true;
            }
            break;
          case Opcode::Sub:
            if (rhsConst && rhsVal == 0) {
              repl = in.operands[0];
              folded = true;
            }
            break;
          case Opcode::Mul:
            if ((lhsConst && lhsVal == 1)) {
              repl = in.operands[1];
              folded = true;
            } else if ((rhsConst && rhsVal == 1)) {
              repl = in.operands[0];
              folded = true;
            } else if ((lhsConst && lhsVal == 0) || (rhsConst && rhsVal == 0)) {
              repl = Value::constInt(0);
              folded = true;
            }
            break;
          case Opcode::And:
            if ((lhsConst && lhsVal == -1)) {
              repl = in.operands[1];
              folded = true;
            } else if ((rhsConst && rhsVal == -1)) {
              repl = in.operands[0];
              folded = true;
            } else if ((lhsConst && lhsVal == 0) || (rhsConst && rhsVal == 0)) {
              repl = Value::constInt(0);
              folded = true;
            }
            break;
          case Opcode::Or:
            if ((lhsConst && lhsVal == 0)) {
              repl = in.operands[1];
              folded = true;
            } else if ((rhsConst && rhsVal == 0)) {
              repl = in.operands[0];
              folded = true;
            } else if ((lhsConst && lhsVal == -1) || (rhsConst && rhsVal == -1)) {
              repl = Value::constInt(-1);
              folded = true;
            }
            break;
          case Opcode::Xor:
            if ((lhsConst && lhsVal == 0)) {
              repl = in.operands[1];
              folded = true;
            } else if ((rhsConst && rhsVal == 0)) {
              repl = in.operands[0];
              folded = true;
            } else if (sameValue(in.operands[0], in.operands[1])) {
              repl = Value::constInt(0);
              folded = true;
            }
            break;
          default:
            break;
          }
        }
        if (folded) {
          replaceAll(f, *in.result, repl);
          b.instructions.erase(b.instructions.begin() + i);
          --i;
        }
      }
    }
  }
}

} // namespace il::transform
