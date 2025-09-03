# IR Builder Helpers

New convenience APIs for constructing control-flow with block parameters:

```cpp
IRBuilder b(m);
Function &f = b.startFunction("f", Type(Type::Kind::Void), {});
BasicBlock &entry = b.addBlock(f, "entry");
BasicBlock &loop = b.addBlock(f, "loop", { {"i", Type(Type::Kind::I64)} });

b.setInsertPoint(entry);
b.br(loop, {Value::constInt(0)}, {});

b.setInsertPoint(loop);
Value i = b.blockParam(loop, 0);
b.cbr(Value::constInt(1), entry, {i}, loop, {i}, {});
```

`addBlock` accepts an optional list of parameters. `blockParam` fetches a block's parameter as a temporary value. `br` and `cbr` take argument lists matching the destination block parameters.
