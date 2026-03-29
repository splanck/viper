# ZIA-FEAT-03: `func(...)` Lambda Expression Syntax

## Context
The reference documents two lambda syntaxes:
- Arrow: `(x: Integer) => x * 2` — **works**
- Block: `func(a: Integer, b: Integer) -> Integer { return a + b; }` — **fails**

Arrow lambdas are fully implemented with closure capture and environment
structs. The `func(...)` form is a parser-only change that produces the same
LambdaExpr AST node.

**Complexity: S** | **Risk: Low** (reuses all existing lambda infrastructure)

## Design
When the parser sees `func` in an expression context (not at declaration level),
and the next token is `(` (not an identifier for a named function), it should
parse a lambda expression. The parsed result is a `LambdaExpr` node identical
to what arrow syntax produces.

Key disambiguation: At the declaration level, `func name(...)` is a function
declaration. In an expression, `func(...)` (no name) is a lambda. The parser
already knows the context — expression parsing calls `parsePrimary()` which
doesn't overlap with `parseDeclaration()`.

## Files to Modify

### 1. Parser_Expr_Primary.cpp (in `parsePrimary()`)
Add a case for `TokenKind::KwFunc` that parses a lambda expression.

Before the default error case, add:
```cpp
case TokenKind::KwFunc: {
    // func(...) -> Type { body } lambda expression
    advance(); // consume 'func'
    if (!expect(TokenKind::LParen, "(")) return nullptr;

    // Parse parameters (reuse existing parseParameters())
    auto params = parseParameters();
    if (!expect(TokenKind::RParen, ")")) return nullptr;

    // Optional return type
    TypePtr returnType;
    if (match(TokenKind::Arrow)) {
        returnType = parseType();
    }

    // Parse body block
    if (!check(TokenKind::LBrace)) {
        error("expected '{' for lambda body");
        return nullptr;
    }
    StmtPtr body = parseBlock();

    // Convert to LambdaExpr
    // LambdaExpr stores params, optional return type, and body
    auto lambda = std::make_unique<LambdaExpr>(loc);
    lambda->params = std::move(params);
    lambda->returnType = std::move(returnType);
    lambda->body = std::move(body);
    return lambda;
}
```

The exact field names depend on LambdaExpr's actual definition. Let me verify:

### 2. Verify LambdaExpr AST node
Check `AST_Expr.hpp` for `LambdaExpr` to confirm field names match.
The arrow lambda parser already constructs this node — follow the same pattern.

### 3. Parser.hpp
No changes needed if the lambda parsing is inline in the switch case.

## What Does NOT Change
- **Sema**: LambdaExpr is already analyzed for both forms.
- **Lowerer**: `lowerLambda()` handles LambdaExpr regardless of source syntax.
- **Closure capture**: Works identically — same environment struct, same ABI.

## Verification
```zia
func start() {
    // Block lambda
    var add = func(a: Integer, b: Integer) -> Integer {
        return a + b;
    };
    SayInt(add(3, 4));  // 7

    // Void lambda
    var greet = func(name: String) {
        Say("Hello, " + name);
    };
    greet("World");

    // Lambda with closure
    var factor = 3;
    var mul = func(x: Integer) -> Integer {
        return x * factor;
    };
    SayInt(mul(5));  // 15

    // As argument
    applyAndPrint(10, func(x: Integer) -> Integer { return x * x; });
}

func applyAndPrint(x: Integer, fn: (Integer) -> Integer) {
    SayInt(fn(x));
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
