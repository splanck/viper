// File: src/frontends/basic/Lowerer.cpp
// Purpose: Lowers BASIC AST to IL.
// Key invariants: None.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md
#include "frontends/basic/Lowerer.h"
#include "il/core/BasicBlock.h"
#include "il/core/Function.h"
#include "il/core/Instr.h"
#include "il/io/Serializer.h" // might not needed but fine
#include <cassert>
#include <functional>

using namespace il::core;

namespace il::frontends::basic {

Module Lowerer::lower(const Program &prog) {
  Module m;
  mod = &m;
  build::IRBuilder b(m);
  builder = &b;

  mangler = NameMangler();
  lineBlocks.clear();
  varSlots.clear();
  strings.clear();

  b.addExtern("rt_print_str", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
  b.addExtern("rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)});
  b.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
  b.addExtern("rt_substr", Type(Type::Kind::Str),
              {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)});

  Function &f = b.startFunction("main", Type(Type::Kind::I64), {});
  func = &f;

  b.addBlock(f, "entry");

  std::vector<int> lines;
  lines.reserve(prog.statements.size());
  for (const auto &stmt : prog.statements) {
    b.addBlock(f, mangler.block("L" + std::to_string(stmt->line)));
    lines.push_back(stmt->line);
  }
  fnExit = f.blocks.size();
  b.addBlock(f, mangler.block("exit"));

  for (size_t i = 0; i < lines.size(); ++i)
    lineBlocks[lines[i]] = i + 1;

  vars.clear();
  collectVars(prog);

  // allocate slots in entry
  BasicBlock *entry = &f.blocks.front();
  cur = entry;
  for (const auto &v : vars) {
    Value slot = emitAlloca(8);
    varSlots[v] = slot.id; // Value::temp id
  }
  if (!prog.statements.empty())
    emitBr(&f.blocks[lineBlocks[prog.statements.front()->line]]);
  else
    emitRet(Value::constInt(0));

  // lower statements sequentially
  for (size_t i = 0; i < prog.statements.size(); ++i) {
    cur = &f.blocks[lineBlocks[prog.statements[i]->line]];
    lowerStmt(*prog.statements[i]);
    if (!cur->terminated) {
      BasicBlock *next = (i + 1 < prog.statements.size())
                             ? &f.blocks[lineBlocks[prog.statements[i + 1]->line]]
                             : &f.blocks[fnExit];
      emitBr(next);
    }
  }

  cur = &f.blocks[fnExit];
  emitRet(Value::constInt(0));

  return m;
}

void Lowerer::collectVars(const Program &prog) {
  std::function<void(const Expr &)> ex = [&](const Expr &e) {
    if (auto *v = dynamic_cast<const VarExpr *>(&e)) {
      vars.insert(v->name);
    } else if (auto *b = dynamic_cast<const BinaryExpr *>(&e)) {
      ex(*b->lhs);
      ex(*b->rhs);
    } else if (auto *c = dynamic_cast<const CallExpr *>(&e)) {
      for (auto &a : c->args)
        ex(*a);
    }
  };
  std::function<void(const Stmt &)> st = [&](const Stmt &s) {
    if (auto *p = dynamic_cast<const PrintStmt *>(&s)) {
      ex(*p->expr);
    } else if (auto *l = dynamic_cast<const LetStmt *>(&s)) {
      vars.insert(l->name);
      ex(*l->expr);
    } else if (auto *i = dynamic_cast<const IfStmt *>(&s)) {
      ex(*i->cond);
      if (i->then_branch)
        st(*i->then_branch);
      if (i->else_branch)
        st(*i->else_branch);
    } else if (auto *w = dynamic_cast<const WhileStmt *>(&s)) {
      ex(*w->cond);
      for (auto &bs : w->body)
        st(*bs);
    }
  };
  for (auto &s : prog.statements)
    st(*s);
}

void Lowerer::lowerStmt(const Stmt &stmt) {
  if (auto *p = dynamic_cast<const PrintStmt *>(&stmt))
    lowerPrint(*p);
  else if (auto *l = dynamic_cast<const LetStmt *>(&stmt))
    lowerLet(*l);
  else if (auto *i = dynamic_cast<const IfStmt *>(&stmt))
    lowerIf(*i);
  else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt))
    lowerWhile(*w);
  else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt))
    lowerGoto(*g);
  else if (auto *e = dynamic_cast<const EndStmt *>(&stmt))
    lowerEnd(*e);
}

Lowerer::RVal Lowerer::lowerExpr(const Expr &expr) {
  if (auto *i = dynamic_cast<const IntExpr *>(&expr)) {
    return {Value::constInt(i->value), Type(Type::Kind::I64)};
  } else if (auto *s = dynamic_cast<const StringExpr *>(&expr)) {
    std::string lbl = getStringLabel(s->value);
    Value tmp = emitConstStr(lbl);
    return {tmp, Type(Type::Kind::Str)};
  } else if (auto *v = dynamic_cast<const VarExpr *>(&expr)) {
    auto it = varSlots.find(v->name);
    assert(it != varSlots.end());
    Value ptr = Value::temp(it->second);
    Value val = emitLoad(Type(Type::Kind::I64), ptr);
    return {val, Type(Type::Kind::I64)};
  } else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr)) {
    RVal lhs = lowerExpr(*b->lhs);
    RVal rhs = lowerExpr(*b->rhs);
    Opcode op = Opcode::Add;
    Type ty(Type::Kind::I64);
    switch (b->op) {
    case BinaryExpr::Op::Add:
      op = Opcode::Add;
      break;
    case BinaryExpr::Op::Sub:
      op = Opcode::Sub;
      break;
    case BinaryExpr::Op::Mul:
      op = Opcode::Mul;
      break;
    case BinaryExpr::Op::Div:
      op = Opcode::SDiv;
      break;
    case BinaryExpr::Op::Eq:
      op = Opcode::ICmpEq;
      ty = Type(Type::Kind::I1);
      break;
    case BinaryExpr::Op::Ne:
      op = Opcode::ICmpNe;
      ty = Type(Type::Kind::I1);
      break;
    case BinaryExpr::Op::Lt:
      op = Opcode::SCmpLT;
      ty = Type(Type::Kind::I1);
      break;
    case BinaryExpr::Op::Le:
      op = Opcode::SCmpLE;
      ty = Type(Type::Kind::I1);
      break;
    case BinaryExpr::Op::Gt:
      op = Opcode::SCmpGT;
      ty = Type(Type::Kind::I1);
      break;
    case BinaryExpr::Op::Ge:
      op = Opcode::SCmpGE;
      ty = Type(Type::Kind::I1);
      break;
    }
    Value res = emitBinary(op, ty, lhs.value, rhs.value);
    return {res, ty};
  } else if (auto *c = dynamic_cast<const CallExpr *>(&expr)) {
    if (c->builtin == CallExpr::Builtin::Len) {
      RVal s = lowerExpr(*c->args[0]);
      Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
      return {res, Type(Type::Kind::I64)};
    } else if (c->builtin == CallExpr::Builtin::Mid) {
      RVal s = lowerExpr(*c->args[0]);
      RVal i = lowerExpr(*c->args[1]);
      RVal l = lowerExpr(*c->args[2]);
      Value res = emitCallRet(Type(Type::Kind::Str), "rt_substr", {s.value, i.value, l.value});
      return {res, Type(Type::Kind::Str)};
    }
  }
  return {Value::constInt(0), Type(Type::Kind::I64)};
}

void Lowerer::lowerLet(const LetStmt &stmt) {
  RVal v = lowerExpr(*stmt.expr);
  auto it = varSlots.find(stmt.name);
  assert(it != varSlots.end());
  emitStore(v.type, Value::temp(it->second), v.value);
}

void Lowerer::lowerPrint(const PrintStmt &stmt) {
  RVal v = lowerExpr(*stmt.expr);
  if (v.type.kind == Type::Kind::Str)
    emitCall("rt_print_str", {v.value});
  else
    emitCall("rt_print_i64", {v.value});
}

void Lowerer::lowerIf(const IfStmt &stmt) {
  RVal cond = lowerExpr(*stmt.cond);
  BasicBlock *thenBB = &builder->addBlock(*func, mangler.block("then"));
  BasicBlock *exitBB = &builder->addBlock(*func, mangler.block("exit"));
  BasicBlock *elseBB = nullptr;
  if (stmt.else_branch)
    elseBB = &builder->addBlock(*func, mangler.block("else"));
  emitCBr(cond.value, thenBB, elseBB ? elseBB : exitBB);

  // then branch
  cur = thenBB;
  lowerStmt(*stmt.then_branch);
  if (!cur->terminated)
    emitBr(exitBB);

  if (stmt.else_branch) {
    cur = elseBB;
    lowerStmt(*stmt.else_branch);
    if (!cur->terminated)
      emitBr(exitBB);
  }

  cur = exitBB;
}

void Lowerer::lowerWhile(const WhileStmt &stmt) {
  // Adding blocks may reallocate the function's block list; capture index and
  // reacquire pointers to guarantee stability.
  size_t start = func->blocks.size();
  builder->addBlock(*func, mangler.block("loop_head"));
  builder->addBlock(*func, mangler.block("loop_body"));
  builder->addBlock(*func, mangler.block("done"));
  BasicBlock *head = &func->blocks[start];
  BasicBlock *body = &func->blocks[start + 1];
  BasicBlock *done = &func->blocks[start + 2];

  emitBr(head);

  // head
  cur = head;
  RVal cond = lowerExpr(*stmt.cond);
  emitCBr(cond.value, body, done);

  // body
  cur = body;
  for (auto &s : stmt.body) {
    lowerStmt(*s);
    if (cur->terminated)
      break;
  }
  if (!cur->terminated)
    emitBr(head);

  cur = done;
}

void Lowerer::lowerGoto(const GotoStmt &stmt) {
  auto it = lineBlocks.find(stmt.target);
  if (it != lineBlocks.end())
    emitBr(&func->blocks[it->second]);
}

void Lowerer::lowerEnd(const EndStmt &) { emitBr(&func->blocks[fnExit]); }

Value Lowerer::emitAlloca(int bytes) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = Opcode::Alloca;
  in.type = Type(Type::Kind::Ptr);
  in.operands.push_back(Value::constInt(bytes));
  cur->instructions.push_back(in);
  return Value::temp(id);
}

Value Lowerer::emitLoad(Type ty, Value addr) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = Opcode::Load;
  in.type = ty;
  in.operands.push_back(addr);
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val) {
  Instr in;
  in.op = Opcode::Store;
  in.type = ty;
  in.operands = {addr, val};
  cur->instructions.push_back(in);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = op;
  in.type = ty;
  in.operands = {lhs, rhs};
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitBr(BasicBlock *target) {
  Instr in;
  in.op = Opcode::Br;
  in.type = Type(Type::Kind::Void);
  in.labels.push_back(target->label);
  cur->instructions.push_back(in);
  cur->terminated = true;
}

void Lowerer::emitCBr(Value cond, BasicBlock *t, BasicBlock *f) {
  Instr in;
  in.op = Opcode::CBr;
  in.type = Type(Type::Kind::Void);
  in.operands.push_back(cond);
  in.labels.push_back(t->label);
  in.labels.push_back(f->label);
  cur->instructions.push_back(in);
  cur->terminated = true;
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args) {
  Instr in;
  in.op = Opcode::Call;
  in.type = Type(Type::Kind::Void);
  in.callee = callee;
  in.operands = args;
  cur->instructions.push_back(in);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = Opcode::Call;
  in.type = ty;
  in.callee = callee;
  in.operands = args;
  cur->instructions.push_back(in);
  return Value::temp(id);
}

Value Lowerer::emitConstStr(const std::string &globalName) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = Opcode::ConstStr;
  in.type = Type(Type::Kind::Str);
  in.operands.push_back(Value::global(globalName));
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitRet(Value v) {
  Instr in;
  in.op = Opcode::Ret;
  in.type = Type(Type::Kind::Void);
  in.operands.push_back(v);
  cur->instructions.push_back(in);
  cur->terminated = true;
}

std::string Lowerer::getStringLabel(const std::string &s) {
  auto it = strings.find(s);
  if (it != strings.end())
    return it->second;
  std::string name = ".L" + std::to_string(strings.size());
  builder->addGlobalStr(name, s);
  strings[s] = name;
  return name;
}

unsigned Lowerer::nextTempId() {
  std::string name = mangler.nextTemp();
  return static_cast<unsigned>(std::stoul(name.substr(2)));
}

} // namespace il::frontends::basic
