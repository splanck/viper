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

  vars.clear();
  usesInput = false;
  collectVars(prog);

  b.addExtern("rt_print_str", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
  b.addExtern("rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)});
  b.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
  b.addExtern("rt_substr", Type(Type::Kind::Str),
              {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)});
  if (usesInput) {
    b.addExtern("rt_input_line", Type(Type::Kind::Str), {});
    b.addExtern("rt_to_int", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
  }

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

  // allocate slots in entry
  BasicBlock *entry = &f.blocks.front();
  cur = entry;
  for (const auto &v : vars) {
    curLoc = {};
    Value slot = emitAlloca(8);
    varSlots[v] = slot.id; // Value::temp id
  }
  if (!prog.statements.empty()) {
    curLoc = {};
    emitBr(&f.blocks[lineBlocks[prog.statements.front()->line]]);
  } else {
    curLoc = {};
    emitRet(Value::constInt(0));
  }

  // lower statements sequentially
  for (size_t i = 0; i < prog.statements.size(); ++i) {
    cur = &f.blocks[lineBlocks[prog.statements[i]->line]];
    lowerStmt(*prog.statements[i]);
    if (!cur->terminated) {
      BasicBlock *next = (i + 1 < prog.statements.size())
                             ? &f.blocks[lineBlocks[prog.statements[i + 1]->line]]
                             : &f.blocks[fnExit];
      curLoc = prog.statements[i]->loc;
      emitBr(next);
    }
  }

  cur = &f.blocks[fnExit];
  curLoc = {};
  emitRet(Value::constInt(0));

  return m;
}

void Lowerer::collectVars(const Program &prog) {
  std::function<void(const Expr &)> ex = [&](const Expr &e) {
    if (auto *v = dynamic_cast<const VarExpr *>(&e)) {
      vars.insert(v->name);
    } else if (auto *u = dynamic_cast<const UnaryExpr *>(&e)) {
      ex(*u->expr);
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
    } else if (auto *in = dynamic_cast<const InputStmt *>(&s)) {
      vars.insert(in->name);
      usesInput = true;
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
    } else if (auto *f = dynamic_cast<const ForStmt *>(&s)) {
      vars.insert(f->var);
      ex(*f->start);
      ex(*f->end);
      if (f->step)
        ex(*f->step);
      for (auto &bs : f->body)
        st(*bs);
    }
  };
  for (auto &s : prog.statements)
    st(*s);
}

void Lowerer::lowerStmt(const Stmt &stmt) {
  curLoc = stmt.loc;
  if (auto *p = dynamic_cast<const PrintStmt *>(&stmt))
    lowerPrint(*p);
  else if (auto *l = dynamic_cast<const LetStmt *>(&stmt))
    lowerLet(*l);
  else if (auto *in = dynamic_cast<const InputStmt *>(&stmt))
    lowerInput(*in);
  else if (auto *i = dynamic_cast<const IfStmt *>(&stmt))
    lowerIf(*i);
  else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt))
    lowerWhile(*w);
  else if (auto *f = dynamic_cast<const ForStmt *>(&stmt))
    lowerFor(*f);
  else if (auto *n = dynamic_cast<const NextStmt *>(&stmt))
    lowerNext(*n);
  else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt))
    lowerGoto(*g);
  else if (auto *e = dynamic_cast<const EndStmt *>(&stmt))
    lowerEnd(*e);
}

Lowerer::RVal Lowerer::lowerExpr(const Expr &expr) {
  curLoc = expr.loc;
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
    if (!v->name.empty() && v->name.back() == '$') {
      Value val = emitLoad(Type(Type::Kind::Str), ptr);
      return {val, Type(Type::Kind::Str)};
    }
    Value val = emitLoad(Type(Type::Kind::I64), ptr);
    return {val, Type(Type::Kind::I64)};
  } else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr)) {
    RVal val = lowerExpr(*u->expr);
    curLoc = expr.loc;
    Value cmp = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), val.value, Value::constInt(0));
    return {cmp, Type(Type::Kind::I1)};
  } else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr)) {
    if (b->op == BinaryExpr::Op::And || b->op == BinaryExpr::Op::Or) {
      RVal lhs = lowerExpr(*b->lhs);
      curLoc = expr.loc;
      Value addr = emitAlloca(1);
      if (b->op == BinaryExpr::Op::And) {
        BasicBlock *rhsBB = &builder->addBlock(*func, mangler.block("and_rhs"));
        BasicBlock *falseBB = &builder->addBlock(*func, mangler.block("and_false"));
        BasicBlock *doneBB = &builder->addBlock(*func, mangler.block("and_done"));
        curLoc = expr.loc;
        emitCBr(lhs.value, rhsBB, falseBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b->rhs);
        curLoc = expr.loc;
        emitStore(Type(Type::Kind::I1), addr, rhs.value);
        curLoc = expr.loc;
        emitBr(doneBB);
        cur = falseBB;
        curLoc = expr.loc;
        emitStore(Type(Type::Kind::I1), addr, Value::constInt(0));
        curLoc = expr.loc;
        emitBr(doneBB);
        cur = doneBB;
      } else {
        BasicBlock *trueBB = &builder->addBlock(*func, mangler.block("or_true"));
        BasicBlock *rhsBB = &builder->addBlock(*func, mangler.block("or_rhs"));
        BasicBlock *doneBB = &builder->addBlock(*func, mangler.block("or_done"));
        curLoc = expr.loc;
        emitCBr(lhs.value, trueBB, rhsBB);
        cur = trueBB;
        curLoc = expr.loc;
        emitStore(Type(Type::Kind::I1), addr, Value::constInt(1));
        curLoc = expr.loc;
        emitBr(doneBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b->rhs);
        curLoc = expr.loc;
        emitStore(Type(Type::Kind::I1), addr, rhs.value);
        curLoc = expr.loc;
        emitBr(doneBB);
        cur = doneBB;
      }
      curLoc = expr.loc;
      Value res = emitLoad(Type(Type::Kind::I1), addr);
      return {res, Type(Type::Kind::I1)};
    }
    RVal lhs = lowerExpr(*b->lhs);
    RVal rhs = lowerExpr(*b->rhs);
    curLoc = expr.loc;
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
    case BinaryExpr::Op::And:
    case BinaryExpr::Op::Or:
      break; // handled above
    }
    Value res = emitBinary(op, ty, lhs.value, rhs.value);
    return {res, ty};
  } else if (auto *c = dynamic_cast<const CallExpr *>(&expr)) {
    if (c->builtin == CallExpr::Builtin::Len) {
      RVal s = lowerExpr(*c->args[0]);
      curLoc = expr.loc;
      Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
      return {res, Type(Type::Kind::I64)};
    } else if (c->builtin == CallExpr::Builtin::Mid) {
      RVal s = lowerExpr(*c->args[0]);
      RVal i = lowerExpr(*c->args[1]);
      RVal l = lowerExpr(*c->args[2]);
      curLoc = expr.loc;
      Value res = emitCallRet(Type(Type::Kind::Str), "rt_substr", {s.value, i.value, l.value});
      return {res, Type(Type::Kind::Str)};
    }
  }
  curLoc = expr.loc;
  return {Value::constInt(0), Type(Type::Kind::I64)};
}

void Lowerer::lowerLet(const LetStmt &stmt) {
  RVal v = lowerExpr(*stmt.expr);
  auto it = varSlots.find(stmt.name);
  assert(it != varSlots.end());
  curLoc = stmt.loc;
  emitStore(v.type, Value::temp(it->second), v.value);
}

void Lowerer::lowerInput(const InputStmt &stmt) {
  Value line = emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});
  auto it = varSlots.find(stmt.name);
  assert(it != varSlots.end());
  Value slot = Value::temp(it->second);
  if (!stmt.name.empty() && stmt.name.back() == '$') {
    emitStore(Type(Type::Kind::Str), slot, line);
  } else {
    Value num = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {line});
    emitStore(Type(Type::Kind::I64), slot, num);
  }
}

void Lowerer::lowerPrint(const PrintStmt &stmt) {
  RVal v = lowerExpr(*stmt.expr);
  curLoc = stmt.loc;
  if (v.type.kind == Type::Kind::Str)
    emitCall("rt_print_str", {v.value});
  else
    emitCall("rt_print_i64", {v.value});
}

void Lowerer::lowerIf(const IfStmt &stmt) {
  RVal cond = lowerExpr(*stmt.cond);
  size_t curIdx = cur - &func->blocks[0];
  size_t base = func->blocks.size();
  builder->addBlock(*func, mangler.block("then"));
  builder->addBlock(*func, mangler.block("exit"));
  size_t thenIdx = base;
  size_t exitIdx = base + 1;
  size_t elseIdx = 0;
  if (stmt.else_branch) {
    builder->addBlock(*func, mangler.block("else"));
    elseIdx = base + 2;
  }
  cur = &func->blocks[curIdx];
  curLoc = stmt.loc;
  emitCBr(cond.value, &func->blocks[thenIdx],
          stmt.else_branch ? &func->blocks[elseIdx] : &func->blocks[exitIdx]);

  // then branch
  cur = &func->blocks[thenIdx];
  lowerStmt(*stmt.then_branch);
  if (!cur->terminated) {
    curLoc = stmt.loc;
    emitBr(&func->blocks[exitIdx]);
  }

  if (stmt.else_branch) {
    cur = &func->blocks[elseIdx];
    lowerStmt(*stmt.else_branch);
    if (!cur->terminated) {
      curLoc = stmt.loc;
      emitBr(&func->blocks[exitIdx]);
    }
  }

  cur = &func->blocks[exitIdx];
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

  curLoc = stmt.loc;
  emitBr(head);

  // head
  cur = head;
  RVal cond = lowerExpr(*stmt.cond);
  curLoc = stmt.loc;
  emitCBr(cond.value, body, done);

  // body
  cur = body;
  for (auto &s : stmt.body) {
    lowerStmt(*s);
    if (cur->terminated)
      break;
  }
  if (!cur->terminated) {
    curLoc = stmt.loc;
    emitBr(head);
  }

  cur = done;
}

void Lowerer::lowerFor(const ForStmt &stmt) {
  RVal start = lowerExpr(*stmt.start);
  RVal end = lowerExpr(*stmt.end);
  RVal step = stmt.step ? lowerExpr(*stmt.step) : RVal{Value::constInt(1), Type(Type::Kind::I64)};
  auto it = varSlots.find(stmt.var);
  assert(it != varSlots.end());
  Value slot = Value::temp(it->second);
  curLoc = stmt.loc;
  emitStore(Type(Type::Kind::I64), slot, start.value);

  bool constStep = !stmt.step || dynamic_cast<const IntExpr *>(stmt.step.get());
  long stepConst = 1;
  if (stmt.step) {
    if (auto *ie = dynamic_cast<const IntExpr *>(stmt.step.get()))
      stepConst = ie->value;
  }
  if (constStep) {
    size_t curIdx = cur - &func->blocks[0];
    size_t base = func->blocks.size();
    builder->addBlock(*func, mangler.block("for_head"));
    builder->addBlock(*func, mangler.block("for_body"));
    builder->addBlock(*func, mangler.block("for_inc"));
    builder->addBlock(*func, mangler.block("for_done"));
    size_t headIdx = base;
    size_t bodyIdx = base + 1;
    size_t incIdx = base + 2;
    size_t doneIdx = base + 3;
    cur = &func->blocks[curIdx];
    curLoc = stmt.loc;
    emitBr(&func->blocks[headIdx]);
    cur = &func->blocks[headIdx];
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    Opcode cmp = stepConst >= 0 ? Opcode::SCmpLE : Opcode::SCmpGE;
    curLoc = stmt.loc;
    Value cond = emitBinary(cmp, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cond, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
    cur = &func->blocks[bodyIdx];
    for (auto &s : stmt.body) {
      lowerStmt(*s);
      if (cur->terminated)
        break;
    }
    if (!cur->terminated) {
      curLoc = stmt.loc;
      emitBr(&func->blocks[incIdx]);
    }
    cur = &func->blocks[incIdx];
    curLoc = stmt.loc;
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step.value);
    curLoc = stmt.loc;
    emitStore(Type(Type::Kind::I64), slot, add);
    curLoc = stmt.loc;
    emitBr(&func->blocks[headIdx]);
    cur = &func->blocks[doneIdx];
  } else {
    curLoc = stmt.loc;
    Value stepNonNeg =
        emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), step.value, Value::constInt(0));
    size_t curIdx = cur - &func->blocks[0];
    size_t base = func->blocks.size();
    builder->addBlock(*func, mangler.block("for_head_pos"));
    builder->addBlock(*func, mangler.block("for_head_neg"));
    builder->addBlock(*func, mangler.block("for_body"));
    builder->addBlock(*func, mangler.block("for_inc"));
    builder->addBlock(*func, mangler.block("for_done"));
    size_t headPosIdx = base;
    size_t headNegIdx = base + 1;
    size_t bodyIdx = base + 2;
    size_t incIdx = base + 3;
    size_t doneIdx = base + 4;
    cur = &func->blocks[curIdx];
    curLoc = stmt.loc;
    emitCBr(stepNonNeg, &func->blocks[headPosIdx], &func->blocks[headNegIdx]);
    cur = &func->blocks[headPosIdx];
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpPos = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpPos, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
    cur = &func->blocks[headNegIdx];
    curLoc = stmt.loc;
    curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpNeg = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpNeg, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
    cur = &func->blocks[bodyIdx];
    for (auto &s : stmt.body) {
      lowerStmt(*s);
      if (cur->terminated)
        break;
    }
    if (!cur->terminated) {
      curLoc = stmt.loc;
      emitBr(&func->blocks[incIdx]);
    }
    cur = &func->blocks[incIdx];
    curLoc = stmt.loc;
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step.value);
    curLoc = stmt.loc;
    emitStore(Type(Type::Kind::I64), slot, add);
    curLoc = stmt.loc;
    emitCBr(stepNonNeg, &func->blocks[headPosIdx], &func->blocks[headNegIdx]);
    cur = &func->blocks[doneIdx];
  }
}

void Lowerer::lowerNext(const NextStmt &) {}

void Lowerer::lowerGoto(const GotoStmt &stmt) {
  auto it = lineBlocks.find(stmt.target);
  if (it != lineBlocks.end()) {
    curLoc = stmt.loc;
    emitBr(&func->blocks[it->second]);
  }
}

void Lowerer::lowerEnd(const EndStmt &stmt) {
  curLoc = stmt.loc;
  emitBr(&func->blocks[fnExit]);
}

Value Lowerer::emitAlloca(int bytes) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = Opcode::Alloca;
  in.type = Type(Type::Kind::Ptr);
  in.operands.push_back(Value::constInt(bytes));
  in.loc = curLoc;
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
  in.loc = curLoc;
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val) {
  Instr in;
  in.op = Opcode::Store;
  in.type = ty;
  in.operands = {addr, val};
  in.loc = curLoc;
  cur->instructions.push_back(in);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs) {
  unsigned id = nextTempId();
  Instr in;
  in.result = id;
  in.op = op;
  in.type = ty;
  in.operands = {lhs, rhs};
  in.loc = curLoc;
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitBr(BasicBlock *target) {
  Instr in;
  in.op = Opcode::Br;
  in.type = Type(Type::Kind::Void);
  in.labels.push_back(target->label);
  in.loc = curLoc;
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
  in.loc = curLoc;
  cur->instructions.push_back(in);
  cur->terminated = true;
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args) {
  Instr in;
  in.op = Opcode::Call;
  in.type = Type(Type::Kind::Void);
  in.callee = callee;
  in.operands = args;
  in.loc = curLoc;
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
  in.loc = curLoc;
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
  in.loc = curLoc;
  cur->instructions.push_back(in);
  return Value::temp(id);
}

void Lowerer::emitRet(Value v) {
  Instr in;
  in.op = Opcode::Ret;
  in.type = Type(Type::Kind::Void);
  in.operands.push_back(v);
  in.loc = curLoc;
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
