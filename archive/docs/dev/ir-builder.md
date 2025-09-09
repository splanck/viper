# IR Builder Helpers

The IR builder provides convenience routines for constructing control flow with
block parameters and branch arguments.

## Creating Blocks

```cpp
Module m;
IRBuilder b(m);
Function &f = b.startFunction("f", Type(Type::Kind::Void), {});
BasicBlock &loop = b.createBlock(f, "loop", {{"i", Type(Type::Kind::I64)}});
```

`createBlock` assigns value identifiers to each parameter automatically.

## Accessing Block Parameters

```cpp
Value i = b.blockParam(loop, 0);
```

## Branches with Arguments

```cpp
b.br(loop, {Value::constInt(0)});               // unconditional
b.cbr(i, loop, {i}, loop, {i});                 // conditional
```

Both helpers assert that the number of arguments matches the destination block's
parameter list.
