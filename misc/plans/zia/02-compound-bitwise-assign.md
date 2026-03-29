# ZIA-FEAT-02: Compound Bitwise/Shift Assignment Operators

## Context
The reference documents `<<=`, `>>=`, `&=`, `|=`, `^=` as compound assignment
operators, but only `+=`, `-=`, `*=`, `/=`, `%=` are implemented. The parser
already has a clean desugaring pattern: `a += b` → `a = a + b`. This feature
extends that pattern to 5 more operators.

**Depends on: ZIA-FEAT-01** (shift operators must exist first for `<<=`/`>>=`)

**Complexity: S** | **Risk: Very Low** (purely mechanical extension of existing pattern)

## Files to Modify

### 1. Token.hpp (~line 485, after PercentEqual)
Add five new TokenKind values:
```cpp
ShiftLeftEqual,    // <<=
ShiftRightEqual,   // >>=
AmpersandEqual,    // &=
PipeEqual,         // |=
CaretEqual,        // ^=
```

### 2. Lexer.cpp (in `next()`)
Extend the `<`, `>`, `&`, `|`, `^` character branches:
- `<`: after checking `<<`, check if next char is `=` → `ShiftLeftEqual`
- `>`: after checking `>>`, check if next char is `=` → `ShiftRightEqual`
- `&`: after checking `&&`, check if next char is `=` → `AmpersandEqual`
- `|`: after checking `||`, check if next char is `=` → `PipeEqual`
- `^`: check if next char is `=` → `CaretEqual`

Update `tokenKindToString` for all five.

### 3. Parser_Expr.cpp (lines 87-128, `parseAssignment()`)
Extend the `compoundOp` lambda (line 89):
```cpp
case TokenKind::ShiftLeftEqual:   return BinaryOp::Shl;
case TokenKind::ShiftRightEqual:  return BinaryOp::Shr;
case TokenKind::AmpersandEqual:   return BinaryOp::BitAnd;
case TokenKind::PipeEqual:        return BinaryOp::BitOr;
case TokenKind::CaretEqual:       return BinaryOp::BitXor;
```

Extend the match chain (line 107):
```cpp
|| match(TokenKind::ShiftLeftEqual, &compTok)
|| match(TokenKind::ShiftRightEqual, &compTok)
|| match(TokenKind::AmpersandEqual, &compTok)
|| match(TokenKind::PipeEqual, &compTok)
|| match(TokenKind::CaretEqual, &compTok)
```

No sema or lowerer changes needed — the desugaring produces standard BinaryOp
nodes that are already handled.

## Verification
```zia
func start() {
    var x = 1;
    x <<= 4;  SayInt(x);   // 16
    x >>= 2;  SayInt(x);   // 4
    x = 0xFF;
    x &= 0x0F; SayInt(x);  // 15
    x |= 0xF0; SayInt(x);  // 255
    x ^= 0xFF; SayInt(x);  // 0
}
```
Run full test suite: `ctest --test-dir build --output-on-failure`
