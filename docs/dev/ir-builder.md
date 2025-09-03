# IR Builder

Helper routines for constructing in-memory IL.

## Block parameters and branch arguments

```cpp
Module m;
il::build::IRBuilder b(m);
Function &f = b.startFunction("f", Type(Type::Kind::Void), {});
BasicBlock &entry = b.addBlock(f, "entry");
BasicBlock &loop = b.addBlock(f, "loop", {{"i", Type(Type::Kind::I64), 0}});

b.setInsertPoint(entry);
b.emitBr(loop, {Value::constInt(0)});

b.setInsertPoint(loop);
Value iv = b.blockParam(loop, 0);
// ...
```

### API summary

- `addBlock(fn, label, params)` – create a block with optional parameters.
- `blockParam(block, idx)` – fetch the SSA value for a block parameter.
- `emitBr(dst, args)` – branch to `dst` passing arguments.
- `emitCBr(cond, t, targs, f, fargs)` – conditional branch with arguments for both edges.

All branch helpers assert that argument counts match the destination block's parameter list.
