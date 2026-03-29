# ZIA-FEAT-01: Shift Operators `<<` and `>>`

## Context
The Zia reference documents `<<` (left shift) and `>>` (right shift) as bitwise
operators, but neither is implemented. The IL already has `Shl`, `LShr`, and
`AShr` opcodes, and both native backends support them. Only the Zia frontend
pipeline is missing.

**Complexity: S** | **Risk: Low** (IL and backends already support shifts)

## Design Note: `>>` vs Generic Close
`>>` could conflict with nested generic types like `List[List[Integer]]` where
the parser sees `>>` as a shift instead of two close brackets. However, Zia
generics use `[` and `]` (not `<` and `>`), so there is **no conflict**. The
lexer can greedily produce `<<` and `>>` tokens without ambiguity.

## Files to Modify

### 1. Token.hpp (~line 500, bitwise operators section)
Add two new TokenKind values:
```cpp
ShiftLeft,   // <<
ShiftRight,  // >>
```

### 2. Lexer.cpp (in `next()`, the `case '<':` and `case '>':` branches)
Currently `<` produces `Less` or `LessEqual`. Add:
- After checking `<=`, check if next char is `<` → produce `ShiftLeft`
- Similarly for `>`: after `>=`, check `>` → produce `ShiftRight`

Also update `tokenKindToString` with `"<<"` and `">>"`.

### 3. AST_Expr.hpp (line 428, BinaryOp enum)
Add after `BitXor`:
```cpp
Shl,    ///< Left shift: `a << b`
Shr,    ///< Right shift: `a >> b`
```

### 4. Parser_Expr.cpp — New precedence level
Shift operators sit between additive and comparison in standard precedence.
Insert `parseShift()` between `parseAdditive()` (line 350) and
`parseComparison()` (line 312):

```cpp
ExprPtr Parser::parseShift() {
    ExprPtr expr = parseAdditive();
    while (check(TokenKind::ShiftLeft) || check(TokenKind::ShiftRight)) {
        Token opTok = advance();
        BinaryOp op = opTok.kind == TokenKind::ShiftLeft ? BinaryOp::Shl : BinaryOp::Shr;
        ExprPtr right = parseAdditive();
        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }
    return expr;
}
```

Update `parseComparison()` to call `parseShift()` instead of `parseAdditive()`.

### 5. Sema_Expr_Ops.cpp (in `analyzeBinary`, ~line 107)
Add cases:
```cpp
case BinaryOp::Shl:
case BinaryOp::Shr:
    if (!leftType->isIntegral() || !rightType->isIntegral())
        error(expr->loc, "Shift operators require integer operands");
    return types::integer();
```

### 6. Lowerer_Expr_Binary.cpp (~line 635)
Add cases after BitXor:
```cpp
case BinaryOp::Shl:
    op = Opcode::Shl;
    break;
case BinaryOp::Shr:
    op = Opcode::AShr;  // Arithmetic right shift (sign-extending)
    break;
```

### 7. Parser.hpp
Declare `ExprPtr parseShift();`.

## Verification
```zia
func start() {
    SayInt(1 << 4);     // 16
    SayInt(16 >> 2);    // 4
    SayInt(0xFF << 8);  // 65280
    SayInt(-8 >> 1);    // -4 (arithmetic shift)
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
