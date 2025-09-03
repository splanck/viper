# IR Builder Helpers

Convenience routines for constructing IL functions and basic blocks.

## Block parameters and branches

```cpp
Module m;
IRBuilder b(m);
Function &f = b.startFunction("foo", Type(Type::Kind::Void), {});
BasicBlock &entry = b.addBlock(f, "entry");
BasicBlock &blk = b.addBlock(f, "blk", {Param{"x", Type(Type::Kind::I64)}});

// Pass constant value to block parameter
b.setInsertPoint(entry);
b.br(blk, {Value::constInt(1)});

// Access block parameter inside `blk`
Value xv = b.blockParam(blk, 0);
```

`cbr` allows supplying separate argument lists for the true and false targets:

```cpp
BasicBlock &t = b.addBlock(f, "then", {Param{"y", Type(Type::Kind::I64)}});
BasicBlock &fblk = b.addBlock(f, "else");
Value cond = Value::constInt(0);
b.cbr(cond, t, {Value::constInt(2)}, fblk, {});
```
