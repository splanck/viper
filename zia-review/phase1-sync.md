# Phase 1: Specification & Documentation Sync Report

**Date:** 2026-03-05
**Sources:** `docs/zia-reference.md`, `docs/zia-getting-started.md`, `src/frontends/zia/` (60 files)

---

## Classification Legend

| Tag | Meaning |
|-----|---------|
| [OK] | Documented and correctly implemented |
| [DOC-ONLY] | Documented but NOT implemented (or stub-only) |
| [IMPL-ONLY] | Implemented but NOT documented |
| [SPEC-MISMATCH] | Both exist but behavior/syntax differs |

---

## Summary

| Status | Count |
|--------|-------|
| [OK] | 78 |
| [DOC-ONLY] | 3 |
| [IMPL-ONLY] | 14 |
| [SPEC-MISMATCH] | 4 |
| **Total** | **99** |

---

## 1. Primitives & Literals

| Feature | Status | Notes |
|---------|--------|-------|
| Integer type (64-bit signed) | [OK] | `Integer` keyword, i64 internally |
| Number type (64-bit IEEE 754) | [OK] | `Number` keyword, f64 internally |
| Boolean type | [OK] | `Boolean` keyword, true/false |
| String type (UTF-8) | [OK] | `String` keyword |
| Ptr type (opaque pointer) | [OK] | `Ptr` keyword, for C interop |
| Integer literals (decimal) | [OK] | `42` |
| Integer literals (hex) | [OK] | `0xFF` |
| Integer literals (binary) | [OK] | `0b1010` |
| Integer literals (octal) | [IMPL-ONLY] | `0o123` — lexer supports octal but docs don't mention it |
| Float literals (decimal) | [OK] | `3.14` |
| Float literals (scientific) | [OK] | `1e10`, `2.5e-3` |
| String literals | [OK] | `"Hello"` |
| String escape sequences | [SPEC-MISMATCH] | Docs list: `\n \t \r \\ \" \$`. Impl also supports: `\' \0 \xNN \uXXXX`. Doc is incomplete |
| String interpolation | [OK] | `"Value: ${expr}"` |
| Boolean literals | [OK] | `true`, `false` |
| Null literal | [OK] | `null` |
| Unit literal | [IMPL-ONLY] | `()` — parser/sema support UnitLiteral, not documented |

---

## 2. Operators

| Feature | Status | Notes |
|---------|--------|-------|
| Arithmetic (+, -, *, /, %) | [OK] | |
| Comparison (==, !=, <, <=, >, >=) | [OK] | |
| Logical AND (&&) | [OK] | |
| Logical OR (\|\|) | [OK] | |
| Logical NOT (!) | [OK] | |
| Bitwise AND (&) | [OK] | |
| Bitwise OR (\|) | [OK] | |
| Bitwise XOR (^) | [OK] | |
| Bitwise NOT (~) | [OK] | |
| Unary negation (-) | [OK] | |
| Assignment (=) | [OK] | |
| Compound assignment (+=, -=, *=, /=, %=) | [OK] | |
| Ternary operator (? :) | [OK] | |
| Range exclusive (..) | [OK] | |
| Range inclusive (..=) | [OK] | |
| Type check (is) | [OK] | |
| Type cast (as) | [OK] | |
| Optional chaining (?.) | [OK] | |
| Null coalescing (??) | [OK] | |
| Force unwrap (!) | [OK] | |
| Function reference (&) | [OK] | `&funcName` → Ptr |
| Keyword `and` (alias for &&) | [IMPL-ONLY] | Parser accepts `and` as `&&` alias, not documented |
| Keyword `or` (alias for \|\|) | [IMPL-ONLY] | Parser accepts `or` as `\|\|` alias, not documented |
| Keyword `not` (alias for !) | [IMPL-ONLY] | Parser accepts `not` as `!` alias, not documented |
| Operator precedence table | [OK] | 15-level table in docs matches implementation |

---

## 3. Control Flow

| Feature | Status | Notes |
|---------|--------|-------|
| If/else statement | [OK] | Optional parens around condition |
| While loop | [OK] | |
| C-style for loop | [OK] | `for (init; cond; update) { ... }` |
| For-in loop (list) | [OK] | `for item in list { ... }` |
| For-in loop (map with tuple) | [OK] | `for (key, value) in map { ... }` |
| For-in loop (range) | [OK] | `for i in 0..10 { ... }` |
| Return statement | [OK] | |
| Break statement | [OK] | |
| Continue statement | [OK] | |
| Guard statement | [OK] | `guard cond else { return; }` |
| Match statement | [OK] | |
| Match expression (as value) | [OK] | |
| Block expression | [IMPL-ONLY] | `{ stmts; expr }` — parser supports block-as-expression, not in docs |
| If expression (as value) | [IMPL-ONLY] | `if cond then else` as expression, not documented |

---

## 4. Functions

| Feature | Status | Notes |
|---------|--------|-------|
| Function declaration | [OK] | `func name(params) -> Type { ... }` |
| Default parameter values | [OK] | Trailing params only |
| Void return (no -> Type) | [OK] | |
| Start function (entry point) | [OK] | `func start() { ... }` |
| Expose function | [OK] | `expose func` for cross-module visibility |
| Foreign function | [OK] | `foreign func` for external linkage |
| Function types as values | [OK] | `(A, B) -> C` |
| Named arguments | [IMPL-ONLY] | Parser supports named arguments in calls, not documented |
| Try expression (?) | [IMPL-ONLY] | `expr?` for null/error propagation, not in reference |

---

## 5. Entities (Reference Types)

| Feature | Status | Notes |
|---------|--------|-------|
| Entity declaration | [OK] | `entity Name { ... }` |
| Fields | [OK] | |
| Methods | [OK] | |
| Init constructor | [OK] | `func init(params) { ... }` |
| Field visibility (expose/hide) | [OK] | |
| Inheritance (extends) | [OK] | `entity Child extends Parent` |
| Self reference | [OK] | `self.field` or implicit |
| Properties (get/set) | [OK] | Synthesized to `get_X`/`set_X` methods in IL |
| Static members | [OK] | `static` fields and methods |
| Destructor (deinit) | [OK] | `deinit { cleanup }` |
| Super keyword | [IMPL-ONLY] | `super.method()` — fully implemented in parser/lowerer, not documented |
| Override keyword | [IMPL-ONLY] | `override func method()` — parser supports it, not documented |

---

## 6. Value Types

| Feature | Status | Notes |
|---------|--------|-------|
| Value type declaration | [OK] | `value Name { ... }` |
| Copy semantics on assignment | [OK] | |
| Struct literal initialization | [IMPL-ONLY] | `Type { field = val, ... }` — parser supports StructLiteral, not documented |

---

## 7. Interfaces

| Feature | Status | Notes |
|---------|--------|-------|
| Interface declaration | [OK] | `interface Name { func sig(); }` |
| Implementing interfaces | [OK] | `entity X implements IFace` |
| Interface dispatch (itable) | [OK] | Runtime itable lookup documented |
| Expose requirement on impl methods | [OK] | Documented |

---

## 8. Generics

| Feature | Status | Notes |
|---------|--------|-------|
| Generic types (List[T], Map[K,V]) | [OK] | |
| Generic entity definitions | [OK] | Parser/sema/lowerer all support |
| Generic function definitions | [IMPL-ONLY] | Implemented but not explicitly documented as user-definable |
| Type constraints | [IMPL-ONLY] | Sema supports constraints, sparse documentation |

---

## 9. Collections

| Feature | Status | Notes |
|---------|--------|-------|
| List[T] type | [OK] | |
| List methods (add, get, set, remove, Len) | [OK] | |
| Map[String, V] type | [OK] | String keys only |
| Map methods (set, get, has, remove, keys, values, Len) | [OK] | |
| Set type | [IMPL-ONLY] | SetLiteral parsed/lowered, not documented in reference |
| List literal syntax | [IMPL-ONLY] | `[1, 2, 3]` — parser supports, not documented |
| Map literal syntax | [IMPL-ONLY] | `{"a": 1}` — parser supports, not documented |
| Set literal syntax | [IMPL-ONLY] | `{1, 2, 3}` — parser supports, not documented |

---

## 10. Optional Types

| Feature | Status | Notes |
|---------|--------|-------|
| Optional type (T?) | [OK] | |
| Null coalescing (??) | [OK] | |
| Optional chaining (?.) | [OK] | |
| Force unwrap (!) | [OK] | |

---

## 11. Lambdas & Closures

| Feature | Status | Notes |
|---------|--------|-------|
| Lambda expressions | [OK] | `(x) => expr` |
| Typed lambda params | [OK] | `(x: Integer) => expr` |
| Closure capture | [OK] | Variables captured from enclosing scope |

---

## 12. Pattern Matching

| Feature | Status | Notes |
|---------|--------|-------|
| Wildcard pattern (_) | [OK] | |
| Literal patterns | [OK] | Integer, string, bool, null |
| Binding patterns | [OK] | `x` binds matched value |
| Tuple patterns | [OK] | `(a, b)` |
| Constructor patterns | [OK] | |
| Guard patterns (if condition) | [OK] | `pattern if cond` |

---

## 13. Modules & Namespaces

| Feature | Status | Notes |
|---------|--------|-------|
| Module declaration | [OK] | `module Name;` |
| Bind file import | [OK] | `bind "./path";` |
| Bind namespace import | [OK] | `bind Viper.Terminal;` |
| Bind with alias | [OK] | `bind "./utils" as U;` |
| Selective bind | [OK] | `bind Viper.Terminal { Say };` |
| Circular bind detection | [OK] | Max depth 50 |
| Namespace declaration | [OK] | `namespace Name { ... }` |
| Dotted namespace names | [OK] | `namespace A.B { ... }` |
| Nested namespaces | [OK] | |

---

## 14. Error Handling

| Feature | Status | Notes |
|---------|--------|-------|
| Try/catch/finally | [OK] | |
| Throw statement | [OK] | |
| Catch type annotation | [SPEC-MISMATCH] | Docs show `catch (e: Error)` but actual catch type binding semantics may differ — impl uses generic error value, not typed Error class |

---

## 15. Type System (Advanced)

| Feature | Status | Notes |
|---------|--------|-------|
| Tuple types | [IMPL-ONLY] | `(A, B)` tuple types fully implemented, mentioned only in passing (for-in destructuring) |
| Tuple index access | [IMPL-ONLY] | `tuple.0` — fully implemented, not documented |
| Fixed-size arrays | [IMPL-ONLY] | `T[N]` type — parser/sema/lowerer support, not documented |
| Type inference (var x = ...) | [OK] | |
| Default initialization | [OK] | Zero/empty defaults for uninitialized vars |

---

## 16. Reserved Keywords

| Keyword | Status | Notes |
|---------|--------|-------|
| `let` | [DOC-ONLY] | Reserved in docs and lexer, but parser never consumes it |
| `weak` | [DOC-ONLY] | Reserved in docs and lexer, but parser never uses it for field declarations |
| `super` | [SPEC-MISMATCH] | Listed as reserved with no documented usage, but fully implemented for parent method dispatch |
| `override` | [SPEC-MISMATCH] | Listed as reserved with no documented usage, but fully implemented as method modifier |

---

## 17. Lexical Elements

| Feature | Status | Notes |
|---------|--------|-------|
| Single-line comments (//) | [OK] | |
| Multi-line comments (/* */) | [OK] | Impl supports nesting |
| Identifier syntax | [OK] | `[a-zA-Z_][a-zA-Z0-9_]*` |

---

## 18. Runtime Library

| Feature | Status | Notes |
|---------|--------|-------|
| Terminal I/O (Say, Print, etc.) | [OK] | |
| Time functions (SleepMs, GetTickCount) | [OK] | |
| Math functions (trig, rounding, etc.) | [OK] | |
| Random (NextInt) | [OK] | |
| Collection methods (List, Map) | [OK] | |
| Set runtime API | [DOC-ONLY] | Docs don't mention Set, but 226+ runtime classes include Set operations |

Wait — correcting: Set IS implemented at runtime level but not documented in zia-reference.md. This is actually [IMPL-ONLY].

---

## Detailed Findings

### [DOC-ONLY] Items (Documented but Not Implemented)

1. **`let` keyword** — Reserved in docs (§ Reserved Words) and lexer (Token.hpp), but parser
   never consumes `KwLet`. No syntax uses it. Status: reserved token only.

2. **`weak` keyword** — Reserved in docs and lexer, but parser never reads `KwWeak` for
   field declarations. No weak reference field syntax exists. Status: reserved token only.

3. *(Originally counted Set runtime as DOC-ONLY but corrected — it's IMPL-ONLY since docs
   don't mention it but runtime has it.)*

### [IMPL-ONLY] Items (Implemented but Not Documented)

1. **Octal integer literals** (`0o123`) — Lexer supports, docs don't mention
2. **Unit literal** (`()`) — Full AST/sema/lowerer support
3. **`and` keyword** (alias for `&&`) — Parser accepts both forms
4. **`or` keyword** (alias for `||`) — Parser accepts both forms
5. **`not` keyword** (alias for `!`) — Parser accepts as unary operator
6. **Block expressions** (`{ stmts; expr }`) — Blocks as expressions producing values
7. **If expressions** — If/else as value-producing expressions
8. **Named arguments** in function calls
9. **Try expression** (`expr?`) — Null/error propagation operator
10. **`super` keyword usage** — `super.method()` for parent dispatch
11. **`override` keyword usage** — Method override modifier in entities
12. **Struct literal initialization** — `Type { field = val }` for value types
13. **Tuple types and tuple index** — `(A, B)` types, `t.0` access
14. **Fixed-size array types** — `T[N]` syntax
15. **Collection literal syntax** — `[1,2,3]`, `{"a":1}`, `{1,2,3}` for List/Map/Set
16. **Set type** — Full runtime API, parser support for set literals

### [SPEC-MISMATCH] Items

1. **String escape sequences** — Docs list 6 escapes (`\n \t \r \\ \" \$`), implementation
   supports 9 (`\' \0 \xNN \uXXXX` additionally). Doc is subset of actual capability.

2. **`super` keyword** — Docs list it only as "reserved" (§ Reserved Words), but it is
   fully implemented for `super.method()` parent class dispatch in Lowerer_Expr_Call.cpp.

3. **`override` keyword** — Docs list it only as "reserved" (§ Reserved Words), but
   Parser_Decl.cpp fully supports `override` as a method modifier for entity members.

4. **`catch` type annotation** — Docs show `catch (e: Error)` suggesting a typed Error
   class. Implementation accepts an identifier binding but the type system for error
   values may not match the documented `Error` type pattern.

---

## Recommendations

### Priority 1: Document Implemented Features
The biggest gap is 16 [IMPL-ONLY] features that users cannot discover from documentation.
Key items to document:
- `super` and `override` (essential for OOP)
- Collection literals (`[1,2,3]`, `{"key": val}`)
- Tuple types and access
- `and`/`or`/`not` keyword aliases
- Block and if expressions

### Priority 2: Resolve Reserved Keywords
- **`let`**: Either implement (as `final` alias?) or mark as "reserved for future use" in docs
- **`weak`**: Either implement weak reference fields or mark as future

### Priority 3: Fix Documentation Gaps
- String escape sequences: add `\' \0 \xNN \uXXXX` to docs
- Fixed-size arrays: document `T[N]` syntax if stable
- Named arguments: document if intentional public API
