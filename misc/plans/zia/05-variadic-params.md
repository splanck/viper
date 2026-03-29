# ZIA-FEAT-05: Variadic Parameters `...Type`

## Context
The reference documents variadic parameters: `func sum(nums: ...Integer)` accepts
zero or more Integer arguments, collected as a List[Integer] inside the body.
Currently not implemented — the parser rejects `...` in parameter lists.

**Complexity: M** | **Risk: Medium** (touches lexer, parser, sema call resolution, and lowerer arg packing)

## Design

### Syntax
```zia
func sum(nums: ...Integer) -> Integer { ... }
sum(1, 2, 3);  // nums = [1, 2, 3] inside
sum();          // nums = [] inside
```

### Semantics
- Only the **last** parameter may be variadic
- Inside the function body, the variadic param has type `List[ElementType]`
- At call sites, extra arguments after fixed params are packed into a list
- The variadic param type determines the element type for type checking

### Implementation Strategy
**At the call site** (lowerer): create a temporary List, push each variadic
argument into it, then pass the list as a single argument to the callee.
**Inside the callee**: the parameter is a regular `List[T]` — no special handling.

## Files to Modify

### 1. Token.hpp (~line 590, special operators section)
Add:
```cpp
Ellipsis,  // ...
```

### 2. Lexer.cpp (in `next()`, the `case '.':` branch)
Currently handles `.`, `..`, `..=`. Extend:
```
.   → Dot
..  → then check: '.' → Ellipsis (...), '=' → DotDotEqual (..=), else DotDot (..)
```

Logic: after consuming first `.`, if next is `.`, consume it. Then:
- if next is `.` → consume, produce `Ellipsis`
- if next is `=` → consume, produce `DotDotEqual`
- else → produce `DotDot`

Update `tokenKindToString`: `Ellipsis → "..."`.

### 3. AST_Decl.hpp (~line 160, struct Param)
Add field:
```cpp
bool isVariadic = false;
```

### 4. Parser_Decl.cpp (~line 360, `parseParameters()`)
In the Swift-style parameter branch, after consuming `:`, check:
```cpp
bool variadic = match(TokenKind::Ellipsis);
TypePtr paramType = parseType();
// ...
param.isVariadic = variadic;
```

After the parameter loop, validate:
```cpp
for (size_t i = 0; i + 1 < params.size(); ++i) {
    if (params[i].isVariadic) {
        error(params[i].loc, "Only the last parameter can be variadic");
    }
}
```

### 5. Sema_Decl.cpp (function analysis)
When analyzing a function's parameters, if `param.isVariadic`, resolve the
declared element type and register the parameter's semantic type as
`List[ElementType]`. The sema type system already supports `List[T]`.

### 6. Sema_Expr_Call.cpp (call argument checking)
In argument count/type checking, when the last parameter is variadic:
- Accept `0..N` additional arguments beyond the fixed params
- Type-check each extra argument against the variadic element type
- Don't require exact argument count match

### 7. Lowerer_Expr_Call.cpp (call argument lowering)
When lowering a call to a function with a variadic last parameter:
1. Lower all fixed arguments normally
2. Create a temporary List: `emitCall(kListCreate, {})`
3. For each variadic argument, box it and push: `emitCall(kListPush, {list, boxed})`
4. Pass the list pointer as the last argument

### 8. RuntimeNames.hpp
Verify `kListCreate` and `kListPush` (or `kListAdd`) constants exist.
They should — they're used by list literal lowering already.

## Verification
```zia
func sum(nums: ...Integer) -> Integer {
    var total = 0;
    for n in nums { total += n; }
    return total;
}

func log(prefix: String, messages: ...String) {
    for msg in messages { Say(prefix + ": " + msg); }
}

func start() {
    SayInt(sum(1, 2, 3, 4, 5));  // 15
    SayInt(sum());                // 0
    log("INFO", "start", "processing", "done");
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
