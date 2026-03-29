# ZIA-FEAT-04: Single-Expression Function Syntax `= expr;`

## Context
The reference documents `func double(x: Integer) -> Integer = x * 2;` as a
shorthand for functions whose body is a single return expression. Currently
the parser requires a `{` block. This is a parser-only change.

**Complexity: S** | **Risk: Very Low** (desugars to standard ReturnStmt in BlockStmt)

## Design
After parsing parameters and optional return type, if the next token is `=`
instead of `{`, parse a single expression, wrap it in `return expr;`, and
wrap that in a block statement. The resulting AST is identical to:
```zia
func double(x: Integer) -> Integer { return x * 2; }
```

No new AST nodes needed. No sema or lowerer changes.

## Files to Modify

### 1. Parser_Decl.cpp — `parseFunctionDecl()` (~lines 324-331)
Replace the current body parsing:
```cpp
// Current:
if (check(TokenKind::LBrace)) {
    func->body = parseBlock();
    if (!func->body) return nullptr;
} else {
    error("expected function body");
    return nullptr;
}
```

With:
```cpp
if (check(TokenKind::LBrace)) {
    func->body = parseBlock();
    if (!func->body) return nullptr;
} else if (match(TokenKind::Equal)) {
    SourceLoc exprLoc = peek().loc;
    ExprPtr bodyExpr = parseExpression();
    if (!bodyExpr) return nullptr;
    if (!expect(TokenKind::Semicolon, ";")) return nullptr;
    // Desugar: = expr; → { return expr; }
    auto returnStmt = std::make_unique<ReturnStmt>(exprLoc, std::move(bodyExpr));
    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(returnStmt));
    func->body = std::make_unique<BlockStmt>(exprLoc, std::move(stmts));
} else {
    error("expected function body");
    return nullptr;
}
```

### 2. Parser_Decl.cpp — `parseMethodDecl()` (~lines 911-918)
Apply the same change. Methods in classes/structs should also support `= expr;`.
The existing code has:
```cpp
if (check(TokenKind::LBrace)) {
    method->body = parseBlock();
} else {
    // No body - interface method signature
    if (!expect(TokenKind::Semicolon, ";")) return nullptr;
}
```

Change to:
```cpp
if (check(TokenKind::LBrace)) {
    method->body = parseBlock();
} else if (check(TokenKind::Equal)) {
    advance();
    SourceLoc exprLoc = peek().loc;
    ExprPtr bodyExpr = parseExpression();
    if (!bodyExpr) return nullptr;
    if (!expect(TokenKind::Semicolon, ";")) return nullptr;
    auto returnStmt = std::make_unique<ReturnStmt>(exprLoc, std::move(bodyExpr));
    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(returnStmt));
    method->body = std::make_unique<BlockStmt>(exprLoc, std::move(stmts));
} else {
    // No body - interface method signature
    if (!expect(TokenKind::Semicolon, ";")) return nullptr;
}
```

### Disambiguation
The `=` after a return type is unambiguous:
- Parameter defaults appear inside `()` and are consumed during param parsing
- Property setters use `set(v) { ... }` syntax
- Assignment `=` only appears in statement/expression context, not after `)`/`-> Type`

## Verification
```zia
func double(x: Integer) -> Integer = x * 2;
func identity(x: String) -> String = x;
func pi() -> Number = 3.14159;

class Math {
    expose func square(n: Integer) -> Integer = n * n;
}

func start() {
    SayInt(double(5));     // 10
    Say(identity("hi"));   // hi
    SayNum(pi());          // 3.14159
    var m = new Math();
    SayInt(m.square(7));   // 49
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
