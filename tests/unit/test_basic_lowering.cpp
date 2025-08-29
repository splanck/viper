#include "frontends/basic/LoweringContext.h"
#include "frontends/basic/NameMangler.h"
#include "il/build/IRBuilder.h"
#include "il/core/Module.h"
#include <cassert>

using namespace il::frontends::basic;
using namespace il::build;
using namespace il::core;

int main() {
  // NameMangler tests
  NameMangler nm;
  assert(nm.nextTemp() == "%t0");
  assert(nm.nextTemp() == "%t1");
  assert(nm.block("entry") == "entry");
  assert(nm.block("entry") == "entry1");
  assert(nm.block("then") == "then");

  // LoweringContext tests
  Module m;
  IRBuilder builder(m);
  Function &fn = builder.startFunction("main", Type(Type::Kind::Void), {});
  LoweringContext ctx(builder, fn);

  std::string slot = ctx.getOrCreateSlot("x");
  assert(slot == "%x_slot");
  assert(ctx.getOrCreateSlot("x") == slot);

  BasicBlock *b1 = ctx.getOrCreateBlock(10);
  BasicBlock *b2 = ctx.getOrCreateBlock(10);
  assert(b1 == b2);

  std::string s0 = ctx.getOrAddString("hello");
  std::string s1 = ctx.getOrAddString("world");
  assert(s0 == ".L0");
  assert(s1 == ".L1");
  assert(ctx.getOrAddString("hello") == s0);

  return 0;
}
