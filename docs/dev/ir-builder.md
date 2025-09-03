# IR Builder Helpers

The IRBuilder now supports basic block parameters and branch arguments.

## Creating Blocks
```cpp
// Create a block with two parameters
il::build::IRBuilder b(mod);
auto &fn = b.startFunction("f", il::core::Type(il::core::Type::Kind::Void), {});
auto &entry = b.addBlock(fn, "entry");
auto &loop = b.createBlock("loop", {{"i", il::core::Type(il::core::Type::I64)},
                                    {"sum", il::core::Type(il::core::Type::I64)}});
```

## Accessing Block Parameters
```cpp
b.setInsertPoint(loop);
il::core::Value i = b.blockParam(loop, 0);
```

## Branching with Arguments
```cpp
b.setInsertPoint(entry);
b.br(loop, {il::core::Value::constInt(0), il::core::Value::constInt(0)});
```
For conditional branches:
```cpp
b.cbr(cond, thenBlock, {v1}, elseBlock, {v2});
```
