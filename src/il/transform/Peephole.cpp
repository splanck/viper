// File: src/il/transform/Peephole.cpp
// Purpose: Implements local IL peephole optimizations.
// Key invariants: Transformations preserve program semantics.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md
#include "il/transform/Peephole.h"
#include "il/core/Function.h"
#include "il/core/Instr.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>

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

static void replaceAll(Function &f, unsigned id, const Value &v) {
  for (auto &b : f.blocks)
    for (auto &in : b.instructions)
      for (auto &op : in.operands)
        if (op.kind == Value::Kind::Temp && op.id == id)
          op = v;
}

static bool hasOtherUses(const Function &f, unsigned id, const Instr *skip) {
  for (const auto &b : f.blocks)
    for (const auto &in : b.instructions)
      if (&in != skip)
        for (const auto &op : in.operands)
          if (op.kind == Value::Kind::Temp && op.id == id)
            return true;
  return false;
}

static void removeDeadBlocks(Function &f) {
  std::unordered_map<std::string, size_t> labelToIdx;
  for (size_t i = 0; i < f.blocks.size(); ++i)
    labelToIdx[f.blocks[i].label] = i;

  std::unordered_set<size_t> live;
  std::queue<size_t> q;
  if (f.blocks.empty())
    return;
  q.push(0);
  live.insert(0);
  while (!q.empty()) {
    size_t idx = q.front();
    q.pop();
    const BasicBlock &b = f.blocks[idx];
    if (b.instructions.empty())
      continue;
    const Instr &term = b.instructions.back();
    for (const auto &lbl : term.labels) {
      auto it = labelToIdx.find(lbl);
      if (it != labelToIdx.end() && !live.count(it->second)) {
        live.insert(it->second);
        q.push(it->second);
      }
    }
  }

  std::vector<BasicBlock> newBlocks;
  newBlocks.reserve(live.size());
  for (size_t i = 0; i < f.blocks.size(); ++i)
    if (live.count(i))
      newBlocks.push_back(std::move(f.blocks[i]));
  f.blocks = std::move(newBlocks);
}

} // namespace

void peephole(Module &m) {
  for (auto &f : m.functions) {
    for (auto &b : f.blocks) {
      for (size_t i = 0; i < b.instructions.size(); ++i) {
        // cbr true/false -> br
        if (b.instructions[i].op == Opcode::CBr && !b.instructions[i].operands.empty()) {
          Instr &in = b.instructions[i];
          long long v;
          bool known = false;
          size_t defIdx = static_cast<size_t>(-1);
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
                  if (known) {
                    defIdx = j;
                    break;
                  }
                }
              }
            }
          }
          if (known) {
            if (defIdx != static_cast<size_t>(-1)) {
              b.instructions.erase(b.instructions.begin() + defIdx);
              if (defIdx < i)
                --i;
            }
            Instr &cur = b.instructions[i];
            cur.op = Opcode::Br;
            std::string target = v ? cur.labels[0] : cur.labels[1];
            cur.labels = {target};
            cur.operands.clear();
          }
        }
        if (i >= b.instructions.size())
          break;
        Instr &in = b.instructions[i];
        // arithmetic identities
        if (in.result && in.operands.size() == 2) {
          Value repl{};
          bool match = false;
          switch (in.op) {
          case Opcode::Add:
            if (isConstEq(in.operands[1], 0)) {
              repl = in.operands[0];
              match = true;
            } else if (isConstEq(in.operands[0], 0)) {
              repl = in.operands[1];
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
            if (isConstEq(in.operands[1], 1)) {
              repl = in.operands[0];
              match = true;
            } else if (isConstEq(in.operands[0], 1)) {
              repl = in.operands[1];
              match = true;
            } else if (isConstEq(in.operands[0], 0) || isConstEq(in.operands[1], 0)) {
              repl = Value::constInt(0);
              match = true;
            }
            break;
          case Opcode::And:
            if (isConstEq(in.operands[1], -1)) {
              repl = in.operands[0];
              match = true;
            } else if (isConstEq(in.operands[0], -1)) {
              repl = in.operands[1];
              match = true;
            } else if (isConstEq(in.operands[0], 0) || isConstEq(in.operands[1], 0)) {
              repl = Value::constInt(0);
              match = true;
            }
            break;
          case Opcode::Or:
            if (isConstEq(in.operands[1], 0)) {
              repl = in.operands[0];
              match = true;
            } else if (isConstEq(in.operands[0], 0)) {
              repl = in.operands[1];
              match = true;
            } else if (isConstEq(in.operands[0], -1) || isConstEq(in.operands[1], -1)) {
              repl = Value::constInt(-1);
              match = true;
            }
            break;
          case Opcode::Xor:
            if (isConstEq(in.operands[1], 0)) {
              repl = in.operands[0];
              match = true;
            } else if (isConstEq(in.operands[0], 0)) {
              repl = in.operands[1];
              match = true;
            } else if (sameValue(in.operands[0], in.operands[1])) {
              repl = Value::constInt(0);
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
            continue;
          }
        }
        // load-store elimination
        if (in.op == Opcode::Load && in.result && i + 1 < b.instructions.size()) {
          Instr &nxt = b.instructions[i + 1];
          if (nxt.op == Opcode::Store && nxt.operands.size() == 2 &&
              sameValue(in.operands[0], nxt.operands[0]) &&
              nxt.operands[1].kind == Value::Kind::Temp && nxt.operands[1].id == *in.result &&
              !hasOtherUses(f, *in.result, &nxt)) {
            b.instructions.erase(b.instructions.begin() + i + 1);
            b.instructions.erase(b.instructions.begin() + i);
            --i;
            continue;
          }
        }
      }
    }
    removeDeadBlocks(f);
  }
}

} // namespace il::transform
