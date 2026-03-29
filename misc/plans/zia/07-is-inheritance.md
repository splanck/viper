# ZIA-FEAT-07: `is` Operator Inheritance Check

## Context
The `is` operator currently does **exact type matching** only. `dog is Animal`
returns `false` even though Dog extends Animal. The correct OOP semantics are
that `is` should return true for the target type and all its subtypes.

The lowerer at `Lowerer_Expr_Lambda.cpp:365-404` emits:
```
classId = call rt_obj_class_id(obj)
result  = icmp_eq classId, targetClassId
```

This only matches the exact class. The fix is to also match descendant classes.

**Complexity: S** | **Risk: Low** (single function change, no API changes)

## Design

### Approach: Compile-Time Descendant Collection
At compile time, the lowerer already has `classTypes_` — a map of all known
class names to `ClassTypeInfo`, which includes `baseClass` (parent name) and
`classId`. We can build the set of all class IDs that are subtypes of the
target (including the target itself) and emit an OR chain of comparisons.

This is preferable to a runtime function because:
- Class hierarchies are typically shallow (2-4 levels)
- No runtime metadata or function needed
- Generated code is simple branching (likely optimized by the IL optimizer)

### Semantics
- `obj is T` returns true if the runtime class of `obj` is `T` or any class
  that directly or transitively extends `T`
- Interface checks (`obj is SomeInterface`) are a separate future feature

## Files to Modify

### 1. Lowerer_Expr_Lambda.cpp (lines 365-404, `lowerIsExpr`)

Replace the current implementation with:

```cpp
LowerResult Lowerer::lowerIsExpr(IsExpr *expr) {
    auto source = lowerExpr(expr->value.get());

    TypeRef targetType = sema_.resolveType(expr->type.get());
    if (!targetType)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    std::string targetName = targetType->name;
    auto it = classTypes_.find(targetName);
    if (it == classTypes_.end()) {
        diag_.report({il::support::Severity::Warning,
                      "'is' check against non-class type '" + targetName +
                          "' always evaluates to false",
                      expr->loc, "W019"});
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Collect target class ID + all descendant class IDs
    std::vector<int64_t> matchIds;
    std::function<void(const std::string &)> collectDescendants =
        [&](const std::string &name) {
            auto cit = classTypes_.find(name);
            if (cit == classTypes_.end()) return;
            matchIds.push_back(static_cast<int64_t>(cit->second.classId));
            // Find all classes whose baseClass is this name
            for (const auto &[className, info] : classTypes_) {
                if (info.baseClass == name) {
                    collectDescendants(className);
                }
            }
        };
    collectDescendants(targetName);

    // Emit: classId = call rt_obj_class_id(source)
    Value classId = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id",
                                {source.value});

    // Single class — emit simple icmp_eq (common case, no overhead)
    if (matchIds.size() == 1) {
        unsigned cmpId = nextTempId();
        il::core::Instr cmpInstr;
        cmpInstr.result = cmpId;
        cmpInstr.op = Opcode::ICmpEq;
        cmpInstr.type = Type(Type::Kind::I64);
        cmpInstr.operands = {classId, Value::constInt(matchIds[0])};
        cmpInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(cmpInstr);
        return {Value::temp(cmpId), Type(Type::Kind::I64)};
    }

    // Multiple classes — emit OR chain
    // result = (classId == id0) | (classId == id1) | ...
    Value result;
    for (size_t i = 0; i < matchIds.size(); ++i) {
        unsigned cmpId = nextTempId();
        il::core::Instr cmpInstr;
        cmpInstr.result = cmpId;
        cmpInstr.op = Opcode::ICmpEq;
        cmpInstr.type = Type(Type::Kind::I64);
        cmpInstr.operands = {classId, Value::constInt(matchIds[i])};
        cmpInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(cmpInstr);

        if (i == 0) {
            result = Value::temp(cmpId);
        } else {
            unsigned orId = nextTempId();
            il::core::Instr orInstr;
            orInstr.result = orId;
            orInstr.op = Opcode::Or;
            orInstr.type = Type(Type::Kind::I64);
            orInstr.operands = {result, Value::temp(cmpId)};
            orInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(orInstr);
            result = Value::temp(orId);
        }
    }

    return {result, Type(Type::Kind::I64)};
}
```

### No other files need changes
- Sema already returns Boolean type for `is` expressions
- AST node `IsExpr` is sufficient
- Parser handles `value is Type` correctly

## Verification
```zia
class Animal {
    expose func init() {}
}
class Dog extends Animal {}
class Cat extends Animal {}
class Puppy extends Dog {}

func start() {
    var d = new Dog();
    var p = new Puppy();
    var a = new Animal();

    // Exact match
    SayBool(d is Dog);       // true
    SayBool(a is Animal);    // true

    // Subtype match (currently fails, should pass after fix)
    SayBool(d is Animal);    // true (Dog extends Animal)
    SayBool(p is Animal);    // true (Puppy extends Dog extends Animal)
    SayBool(p is Dog);       // true (Puppy extends Dog)

    // Non-subtype
    SayBool(a is Dog);       // false (Animal is not a Dog)
    SayBool(d is Cat);       // false (Dog is not a Cat)
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
