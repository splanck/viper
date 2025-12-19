# ViperLang Complete Implementation Plan

## Version 2.0 — Verified & Updated

**Status:** Ready for Implementation
**Last Updated:** December 2024
**IL Version:** v0.1
**Target:** Viper Compiler Toolchain

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Testing Philosophy](#testing-philosophy)
3. [Platform Constraints](#platform-constraints)
4. [Available Runtime APIs](#available-runtime-apis)
5. [Phase 0: Prerequisites](#phase-0-prerequisites)
6. [Phase 1: Core Foundation](#phase-1-core-foundation)
7. [Phase 2: Type System](#phase-2-type-system)
8. [Phase 3: Functions and Methods](#phase-3-functions-and-methods)
9. [Phase 4: Control Flow](#phase-4-control-flow)
10. [Phase 5: Error Handling](#phase-5-error-handling)
11. [Phase 6: Concurrency](#phase-6-concurrency)
12. [Phase 7: Collections](#phase-7-collections)
13. [Phase 8: Advanced Features](#phase-8-advanced-features)
14. [Phase 9: Standard Library](#phase-9-standard-library)
15. [Phase 10: Demo Applications](#phase-10-demo-applications)
16. [Progress Tracking](#progress-tracking)
17. [Appendix: IL Lowering Patterns](#appendix-il-lowering-patterns)

---

## Executive Summary

ViperLang is a modern, safe programming language targeting the Viper IL and runtime. This document provides a verified implementation plan aligned with actual platform capabilities.

### Language Characteristics

```viper
module HelloWorld;

// Java/C#-style declarations with semicolons
Integer count = 42;
String name = "Alice";

// Two type kinds: values (copied) and entities (referenced)
value Point {
    Number x;
    Number y;
}

entity User {
    String name;
    final String id;

    expose func greet() -> String {
        return "Hello, ${name}!";
    }
}

// Entry point is start(), not main()
func start() {
    Point p = Point(1.0, 2.0);      // Value: no 'new'
    User u = new User("Alice", "1"); // Entity: with 'new'
    print(u.greet());
}
```

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Entry point | `func start()` | Distinct from C conventions |
| Declarations | Type-first | Java/C# familiarity |
| Statement terminator | Semicolons | Explicit, unambiguous |
| Type inference | `var` keyword | C#-style |
| Immutability | `final` keyword | Java-style |
| Entity creation | `new` keyword | Clear allocation intent |
| Error handling | `Result[T]` + `?` | No exceptions |
| Optionals | `T?` + `null` | Explicit null safety |
| Visibility | Private by default | `expose` for public |

---

## Testing Philosophy

### Core Principle: Test-First, No Surprises

Every phase includes mandatory testing to ensure no broken functionality at the end. The goal is **continuous validation** — if something works in Phase 2, it still works in Phase 8.

### Test Categories

| Category | Purpose | When |
|----------|---------|------|
| **Unit Tests** | Verify individual components (lexer, parser, type checker) | During development |
| **Golden Tests** | Compare output against known-good baselines | After each feature |
| **Integration Tests** | End-to-end compilation and execution | Phase completion |
| **Example Programs** | Progressive complexity, showcasing features | Each phase |
| **Regression Tests** | Ensure fixes don't break existing code | After every bug fix |

### Progressive Example Program

Build a single application that grows with each phase, demonstrating cumulative features:

| Phase | Example Addition | Features Tested |
|-------|------------------|-----------------|
| 1 | `ex01_hello.viper` | Basic parsing, print |
| 2 | `ex02_point.viper` | Values, fields |
| 3 | `ex03_counter.viper` | Entities, methods |
| 4 | `ex04_fizzbuzz.viper` | Control flow, loops |
| 5 | `ex05_calculator.viper` | Error handling, Result |
| 6 | `ex06_parallel.viper` | Threads (optional) |
| 7 | `ex07_inventory.viper` | Collections |
| 8 | `ex08_shapes.viper` | Interfaces, inheritance |
| 9 | `ex09_fileio.viper` | Standard library |
| 10 | `frogger.viper`, `vtris.viper` | Full demos |

### Test File Structure

```
tests/viperlang/
├── unit/
│   ├── lexer/
│   │   ├── test_tokens.cpp
│   │   └── test_strings.cpp
│   ├── parser/
│   │   ├── test_expressions.cpp
│   │   └── test_statements.cpp
│   └── typechecker/
│       ├── test_inference.cpp
│       └── test_errors.cpp
├── golden/
│   ├── phase1/
│   │   ├── hello.viper
│   │   └── hello.expected
│   ├── phase2/
│   └── ...
├── integration/
│   └── e2e/
│       ├── test_vm_execution.cpp
│       └── test_native_codegen.cpp
└── examples/
    ├── ex01_hello.viper
    ├── ex02_point.viper
    └── ...
```

### Phase Completion Checklist

Before marking any phase complete:

- [ ] All unit tests pass
- [ ] All golden tests pass
- [ ] Example program for this phase works
- [ ] All previous example programs still work (regression)
- [ ] Documentation updated
- [ ] No compiler warnings

---

## Available Runtime APIs

The Viper runtime already provides these APIs. ViperLang should expose them naturally.

### Terminal I/O (Viper.Terminal)

```
// Output
Terminal.Say(msg: String)           // Print with newline
Terminal.Print(msg: String)         // Print without newline
Terminal.PrintInt(n: Integer)
Terminal.PrintNum(n: Number)

// Input
Terminal.ReadLine() -> String
Terminal.InKey() -> String          // Non-blocking key
Terminal.GetKey() -> String         // Blocking key
Terminal.Ask(prompt: String) -> String

// Screen Control
Terminal.Clear()
Terminal.SetPosition(row: Integer, col: Integer)
Terminal.SetColor(fg: Integer, bg: Integer)
Terminal.SetCursorVisible(visible: Boolean)
Terminal.Bell()

// Buffered Rendering (flicker-free)
Terminal.BeginBatch()
Terminal.EndBatch()
Terminal.Flush()
```

### File I/O (Viper.IO.File)

```
File.ReadAllText(path: String) -> String
File.WriteAllText(path: String, content: String)
File.ReadAllLines(path: String) -> List[String]
File.Append(path: String, content: String)
File.Exists(path: String) -> Boolean
File.Delete(path: String)
File.Size(path: String) -> Integer
```

### Random Numbers (Viper.Random)

```
Random.Next() -> Number             // 0.0 to 1.0
Random.NextInt(max: Integer) -> Integer
Random.Seed(seed: Integer)
```

### Time (Viper.Time)

```
Time.SleepMs(ms: Integer)
Time.Now() -> Integer               // Epoch milliseconds
```

### String Operations (Viper.String)

```
String.Concat(a: String, b: String) -> String
String.Length(s: String) -> Integer
String.Left(s: String, n: Integer) -> String
String.Right(s: String, n: Integer) -> String
String.Mid(s: String, start: Integer, len: Integer) -> String
String.Upper(s: String) -> String
String.Lower(s: String) -> String
String.Trim(s: String) -> String
String.IndexOf(s: String, needle: String) -> Integer
String.FromInt(n: Integer) -> String
String.FromNum(n: Number) -> String
```

---

## Platform Constraints

### CRITICAL: IL Type System Limitations

The Viper IL has exactly 10 primitive types. **There are no struct types.**

```cpp
// From src/il/core/Type.hpp
enum class Kind {
    Void,      // No return value
    I1,        // Boolean (1-bit)
    I16,       // 16-bit signed integer
    I32,       // 32-bit signed integer
    I64,       // 64-bit signed integer
    F64,       // 64-bit floating point
    Ptr,       // Pointer (object reference)
    Str,       // String reference
    Error,     // Error value
    ResumeTok  // Resume token (coroutines)
};
```

**Critical Implications:**

1. **NO i8 type** — `Byte` must lower to `i16` or `i32`
2. **NO struct types** — Values must use `ptr + offset` layout
3. **NO aggregate returns** — Multi-value returns need heap allocation
4. **switch.i32 only** — No switch.i64 for pattern matching

### Value Type Lowering Pattern

Values MUST be lowered using pointer + offset calculations:

```
// ViperLang source
value Point { Number x; Number y; }

// IL lowering (correct)
%slot = alloca 16           ; Allocate 16 bytes (2 x f64)
store.f64 %slot, %x         ; x at offset 0
%y_ptr = offset %slot, 8
store.f64 %y_ptr, %y        ; y at offset 8

// NOT THIS (invalid - no struct types):
// %point = struct { f64, f64 }  // INVALID - IL has no structs
```

### Optional Type Pattern (IL-Compliant)

Optionals use i64 for the hasValue flag (NOT i8):

```
// Integer? in IL
%opt_slot = alloca 16       ; 8 bytes flag + 8 bytes value
%flag_ptr = %opt_slot       ; hasValue at offset 0
%val_ptr = offset %opt_slot, 8  ; value at offset 8

// Set to null
store.i64 %flag_ptr, 0

// Set to value
store.i64 %flag_ptr, 1
store.i64 %val_ptr, %value

// Check hasValue
%flag = load.i64 %flag_ptr
%has = icmp.ne %flag, 0
```

### Collection Semantics

**List (recommended for ViperLang `List[T]`):**
- Retains/releases elements (safe by default under ARC)
- Uses **pointer-identity** for `Find`/`Has`, NOT value-equality (value types need a wrapper)

**Seq (low-level / legacy):**
- Uses **pointer-identity** for `Find`/`Has`, NOT value-equality
- Does NOT retain/release elements on push/pop
- Use only when element ownership is explicit and managed elsewhere

**Bag Limitation:**
- Bag is **string-only** — `Set[Integer]` is NOT possible with current runtime
- For Set[T] where T != String, need boxing or alternative implementation

### Runtime Function Conventions

All runtime functions use `rt_*` prefix in C, but IL calls use `Viper.*` namespace:

| IL Call | C Function | Signature |
|---------|------------|-----------|
| `Viper.String.Concat` | `rt_concat` | `str(str,str)` |
| `Viper.Collections.Seq.New` | `rt_seq_new` | `obj()` |
| `Viper.Threads.Thread.Start` | `rt_thread_start` | `obj(ptr,ptr)` |

### Concurrency Model

**Thread-first, NOT async/await.** Current runtime provides:

```
// Available in Viper.Threads.*
Thread.Start(entry: ptr, arg: ptr) -> obj
Thread.Join(thread: obj) -> void
Thread.TryJoin(thread: obj) -> i1
Thread.JoinFor(thread: obj, ms: i64) -> i1
Thread.Sleep(ms: i64) -> void
Thread.Yield() -> void

Monitor.Enter(obj) -> void
Monitor.TryEnter(obj) -> i1
Monitor.TryEnterFor(obj, ms: i64) -> i1
Monitor.Exit(obj) -> void
Monitor.Wait(obj) -> void
Monitor.WaitFor(obj, ms: i64) -> i1
Monitor.Pause(obj) -> void
Monitor.PauseAll(obj) -> void

SafeI64.New(val: i64) -> obj
SafeI64.Get(obj) -> i64
SafeI64.Set(obj, i64) -> void
SafeI64.Add(obj, i64) -> i64
SafeI64.CompareExchange(obj, expected: i64, desired: i64) -> i64
```

**Monitor semantics (runtime):**
- FIFO-fair acquisition (repeatable scheduling under contention)
- Re-entrant for the owning thread
- Timeout parameters are milliseconds (`i64`)
- `Wait/WaitFor` release the monitor and re-acquire before returning
- `Pause/PauseAll` wake one/all threads waiting via `Wait/WaitFor` (caller must own the monitor)

Async/await would require Task runtime that does NOT exist.

---

## Phase 0: Prerequisites

Before implementing ViperLang, these runtime components must be created.

### 0.1 Boxing Runtime

**Status:** [ ] Not Started

Generic collections require boxing primitives to `obj` (Ptr):

```cpp
// Required: src/runtime/rt_box.c

// Box creation
void* rt_box_i64(int64_t val);
void* rt_box_f64(double val);
void* rt_box_i1(int64_t val);  // Use i64 storage for i1
void* rt_box_str(viper_string val);

// Unbox
int64_t rt_unbox_i64(void* box);
double rt_unbox_f64(void* box);
int64_t rt_unbox_i1(void* box);
viper_string rt_unbox_str(void* box);

// Type tag checking
int64_t rt_box_type(void* box);  // Returns type tag
```

**IL Signatures (runtime.def):**

```
RT_FUNC(BoxI64, rt_box_i64, "Viper.Box.I64", "obj(i64)")
RT_FUNC(BoxF64, rt_box_f64, "Viper.Box.F64", "obj(f64)")
RT_FUNC(BoxStr, rt_box_str, "Viper.Box.Str", "obj(str)")
RT_FUNC(UnboxI64, rt_unbox_i64, "Viper.Box.ToI64", "i64(obj)")
RT_FUNC(UnboxF64, rt_unbox_f64, "Viper.Box.ToF64", "f64(obj)")
RT_FUNC(UnboxStr, rt_unbox_str, "Viper.Box.ToStr", "str(obj)")
RT_FUNC(BoxType, rt_box_type, "Viper.Box.Type", "i64(obj)")
```

### 0.2 Value-Equality Collection Wrappers

**Status:** [ ] Not Started

Runtime collections use pointer-identity for `Find/Has`. For value-type collections, need value-equality helpers.

```cpp
// For List[Integer].has(42) to work correctly
// Need specialized find that unboxes and compares values

int64_t rt_list_find_i64(void* list, int64_t needle);
int64_t rt_list_find_f64(void* list, double needle);
int64_t rt_list_find_str(void* list, rt_string needle);  // Compare via rt_str_eq (value equality)
```

### 0.3 Iterator Protocol Runtime

**Status:** [ ] Not Started

For `for (item in collection)` loops:

```cpp
// Iterator object
void* rt_iter_new(void* collection);
void* rt_iter_next(void* iter);  // Returns null when done
void rt_iter_drop(void* iter);

// Or simpler: iteration by index (already possible with List.get_Count + List.get_Item)
```

### 0.4 Set Implementation for Non-Strings

**Status:** [ ] Not Started

Current Bag is string-only. Options:
1. Add typed Bag variants: `rt_bag_i64_*`, `rt_bag_f64_*`
2. Use boxing + Bag (performance hit)
3. Implement hash-based set with value comparison

### 0.5 Closure Runtime

**Status:** [ ] Not Started

For lambdas/closures:

```cpp
// Closure = { function_ptr, captures... }
void* rt_closure_new(void* fn, int64_t capture_count);
void rt_closure_set_capture(void* closure, int64_t index, void* value);
void* rt_closure_get_capture(void* closure, int64_t index);
void* rt_closure_fn(void* closure);
```

### 0.6 Safe Weak References (Zeroing + Load Retains)

**Status:** [ ] Not Started

ViperLang v0.1 requires **safe** weak references (no dangling pointers), even under threads.

**Target semantics:**
- A `weak T?` field does **not** contribute to ARC refcount.
- When the target object is destroyed, all weak references to it become `null` (zeroing).
- Loading a weak reference returns either `null` or a **strong** reference (retained) so it is safe to use.

**Runtime API (already present today, but must be upgraded to safe semantics):**

```cpp
// Store weak reference into a field (tracks/untracks slot; does not retain target)
void rt_weak_store(void** addr, void* value);

// Load weak reference from a field (returns retained strong ref or NULL)
void* rt_weak_load(void** addr);
```

**Runtime implementation requirements:**
1. Maintain a global (or per-context) weak table keyed by target object pointer, holding the list of weak slots (`void**`) referencing it.
2. `rt_weak_store` must:
   - Remove `addr` from the old target’s slot list (if any)
   - Add `addr` to the new target’s slot list (if non-null)
   - Write the new pointer into `*addr`
3. `rt_weak_load` must:
   - Read `*addr`
   - If non-null, retain the target before returning it (so the caller sees a stable strong ref)
4. Object destruction (`rt_obj_free` / finalization path) must:
   - Before freeing the object memory, zero all registered weak slots to `NULL`
   - Remove the object’s entry from the weak table

**Threading requirements:**
- All weak-table operations must be synchronized (single global mutex is acceptable for v0.1).
- Determinism requirement: weak-slot clearing order does not matter as long as the result is always `null`.

---

## Phase 1: Core Foundation

### 1.1 Lexer

**Status:** [ ] Not Started

**Token Types:**

```cpp
enum class TokenKind {
    // Keywords (29 total)
    Module, Import, As,
    Value, Entity, Interface,
    Implements, Extends,
    Func, Return, Var, Final, New,
    If, Else, Let, Match, While, For, In, Is,
    Break, Continue, Guard,
    Override, Weak, Expose, Hide,
    Self, Super,
    True, False, Null,

    // Future (v0.2)
    Async, Await, Spawn,

    // Literals
    IntLit, FloatLit, StringLit, CharLit,

    // Identifiers
    Ident,

    // Operators
    Plus, Minus, Star, Slash, Percent,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or, Not,
    Dot, QuestionDot, Question, QuestionQuestion,
    DotDot, DotDotEq,
    Arrow, FatArrow,
    Colon, Semicolon, Comma,
    LParen, RParen, LBracket, RBracket, LBrace, RBrace,
    Assign,

    // Special
    Eof, Error
};
```

**String Literals:**
- Regular: `"hello"`
- Multi-line: `"""multi\nline"""`
- Interpolation: `"Hello, ${name}!"`

### 1.2 Parser

**Status:** [ ] Not Started

**AST Node Types:**

```cpp
// Declarations
struct ModuleDecl { std::string name; };
struct ImportDecl { std::string path; std::optional<std::string> alias; };
struct ValueDecl { std::string name; std::vector<Field> fields; std::vector<Method> methods; };
struct EntityDecl { std::string name; std::optional<std::string> parent; std::vector<Field> fields; std::vector<Method> methods; };
struct FuncDecl { std::string name; std::vector<Param> params; std::optional<Type> returnType; Block body; };

// Statements
struct VarStmt { bool isFinal; std::optional<Type> type; std::string name; Expr init; };
struct AssignStmt { Expr target; Expr value; };
struct ReturnStmt { std::optional<Expr> value; };
struct IfStmt { Expr cond; Block thenBranch; std::optional<Block> elseBranch; };
struct WhileStmt { Expr cond; Block body; };
struct ForStmt { std::string var; Expr iterable; Block body; };
struct MatchStmt { Expr subject; std::vector<MatchArm> arms; };

// Expressions
struct IntLit { int64_t value; };
struct FloatLit { double value; };
struct StringLit { std::string value; bool isInterpolated; };
struct BoolLit { bool value; };
struct NullLit {};
struct NameExpr { std::string name; };
struct FieldExpr { Expr object; std::string field; };
struct IndexExpr { Expr object; Expr index; };
struct CallExpr { Expr callee; std::vector<Arg> args; };
struct NewExpr { Type type; std::vector<Arg> args; };
struct BinaryExpr { BinaryOp op; Expr left; Expr right; };
struct UnaryExpr { UnaryOp op; Expr operand; };
struct TernaryExpr { Expr cond; Expr thenExpr; Expr elseExpr; };
struct LambdaExpr { std::vector<Param> params; Expr body; };
```

### 1.3 Type System Foundation

**Status:** [ ] Not Started

### 1.4 Phase 1 Testing & Examples

**Unit Tests:**
- Lexer: All token types, string escapes, interpolation markers
- Parser: Expression precedence, statement parsing, error recovery

**Golden Tests:**
```viper
// tests/golden/phase1/hello.viper
func start() {
    print("Hello, ViperLang!");
}
```
Expected output: `Hello, ViperLang!`

**Example Program - ex01_hello.viper:**
```viper
// First ViperLang program
func start() {
    print("Hello, World!");
    print("ViperLang v0.1");
}
```

**Milestone:** Compile and run Hello World successfully.

---

**Core Types:**

```cpp
struct ViperType {
    enum Kind {
        Integer,    // i64
        Number,     // f64
        Boolean,    // i1
        String,     // str
        Byte,       // i32 (NOT i8 - IL doesn't have i8)
        Unit,       // void - single value ()
        Void,       // No return type
        Optional,   // T?
        Result,     // Result[T]
        List,       // List[T]
        Map,        // Map[K,V]
        Set,        // Set[T]
        Function,   // func(A,B) -> C
        Value,      // user-defined value
        Entity,     // user-defined entity
        Interface,  // interface type
        Error,      // Error value
        Ptr,        // Opaque pointer (for thread args)
        Unknown,    // For type inference
    };

    Kind kind;
    std::vector<ViperType> typeArgs;  // For generics
    std::string name;                  // For named types
};
```

**Type Mapping to IL:**

| ViperLang | IL Type | Size |
|-----------|---------|------|
| `Integer` | `i64` | 8 |
| `Number` | `f64` | 8 |
| `Boolean` | `i1` | 1 (stored as i64) |
| `String` | `str` | 8 (ptr) |
| `Byte` | `i32` | 4 (NOT i8 — IL has no i8) |
| `Unit` | `void` | 0 (single value `()`) |
| `T?` | in-memory `{i64 flag, T payload}` accessed via `ptr + gep` | 8 + sizeof(T) |
| `value {...}` | stack/inline by default (passed via `ptr`) | sum of fields |
| `entity {...}` | ptr (to heap) | 8 |

---

## Phase 2: Type System

### 2.1 Values (Copy Types)

**Status:** [x] Complete
- [x] Parser: Field and method declarations in value types
- [x] Lowerer: Value type layout computation (field offsets/sizes)
- [x] Lowerer: Method lowering with `self` parameter
- [x] Lowerer: Implicit field access (self.x) in methods
- [x] Value type construction (Point(1, 2))
- [x] Value type method calls (p.getX())
- [x] External field access (p.x)
- [ ] Note: Field expressions in function call arguments need parser fix

**Syntax:**

```viper
value Color {
    Integer r;
    Integer g;
    Integer b;

    func brightness() -> Number {
        return (r + g + b) / 3.0;
    }
}

// Creation (no 'new')
Color red = Color(255, 0, 0);
Color myColor = red;  // Copy
```

**IL Lowering:**

```
; Color.new(255, 0, 0)
%color_slot = alloca 24        ; 3 x i64
%r_ptr = %color_slot
store.i64 %r_ptr, 255
%g_ptr = offset %color_slot, 8
store.i64 %g_ptr, 0
%b_ptr = offset %color_slot, 16
store.i64 %b_ptr, 0

; Copy: Color myColor = red
%my_slot = alloca 24
call void @memcpy(%my_slot, %color_slot, 24)
```

### 2.2 Entities (Reference Types)

**Status:** [ ] Not Started

**Syntax:**

```viper
entity User {
    String name;
    final String id;

    expose func greet() -> String {
        return "Hello, ${name}!";
    }
}

// Creation (with 'new')
User u = new User(name: "Alice", id: "123");
User u2 = u;  // Same reference
```

**IL Lowering:**

```
; new User("Alice", "123")
%size = const.i64 24           ; 8 vptr + 2 x str fields
%obj = call ptr @rt_obj_new_i64(%class_id, %size)     ; refcount = 1
store ptr, %obj, const_null                         ; vptr (set later for vtables)
%name_ptr = gep %obj, 8
store str, %name_ptr, %name_str
%id_ptr = gep %obj, 16
store str, %id_ptr, %id_str

; User u2 = u (reference copy + retain)
%u2 = %u
call void @rt_obj_retain_maybe(%u2)
```

### 2.3 Optionals

**Status:** [ ] Not Started

**Syntax:**

```viper
String? maybeName = null;
String? name = "Alice";

if (name != null) {
    print(name);
}

String display = maybeName ?? "Guest";
```

**IL Lowering (Value-Type Optional):**

```
; Integer? x = null
%opt_slot = alloca 16          ; 8 (flag) + 8 (value)
store.i64 %opt_slot, 0         ; hasValue = false

; Integer? x = 42
store.i64 %opt_slot, 1         ; hasValue = true
%val_ptr = offset %opt_slot, 8
store.i64 %val_ptr, 42

; Check: x != null
%flag = load.i64 %opt_slot
%has = icmp.ne %flag, 0
cbr %has, @has_value, @no_value

; Unwrap: access value
@has_value:
%val_ptr = offset %opt_slot, 8
%val = load.i64 %val_ptr
```

**IL Lowering (Reference-Type Optional):**

```
; User? maybeUser = null
%user_ptr = const.ptr null

; Check: maybeUser != null
%has = icmp.ne %user_ptr, null
cbr %has, @has_value, @no_value
```

### 2.4 Generics

**Status:** [ ] Not Started

**Syntax:**

```viper
value Box[T] {
    T contents;

    func map[U](f: func(T) -> U) -> Box[U] {
        return Box[U](f(contents));
    }
}

Box[Integer] intBox = Box(42);
Box[String] strBox = intBox.map((x: Integer) -> "${x}");
```

**Lowering Strategy:** Monomorphization

Each instantiation `Box[Integer]`, `Box[String]` generates separate IL code:

```
; Box[Integer] (alias: Box_Integer)
func @Box_Integer_init(ptr %out, i64 %val) -> void {
    ; Write into caller-provided storage (alloca/out-param). alloca must not escape.
    store i64, %out, %val
    ret
}

; Box[String] (alias: Box_String)
func @Box_String_init(ptr %out, str %val) -> void {
    store str, %out, %val
    ret
}
```

**Rule:** Value types (including monomorphized generics) must be constructed into caller-owned storage (`alloca` or out-params). Never return pointers to `alloca` storage.

### 2.5 Phase 2 Testing & Examples

**Unit Tests:**
- Value construction and field access
- Entity construction with `new`
- Optional null/non-null states
- Type inference with `var`

**Golden Tests:**
```viper
// tests/golden/phase2/point.viper
value Point { Number x; Number y; }

func start() {
    Point p = Point(3.0, 4.0);
    print(p.x);
    print(p.y);
}
```
Expected output: `3.0` then `4.0`

**Example Program - ex02_point.viper:**
```viper
value Point {
    Number x;
    Number y;
}

entity Counter {
    Integer count = 0;
}

func start() {
    Point origin = Point(0.0, 0.0);
    Point target = Point(10.0, 20.0);

    Counter c = new Counter();

    print("Origin: ${origin.x}, ${origin.y}");
    print("Target: ${target.x}, ${target.y}");
}
```

**Milestone:** Values copy correctly, entities share references.

---

## Phase 3: Functions and Methods

### 3.1 Functions

**Status:** [ ] Not Started

**Syntax:**

```viper
// Basic function
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// Default parameters
func greet(name: String, greeting: String = "Hello") -> String {
    return "${greeting}, ${name}!";
}

// No return type = Void
func sayHello(name: String) {
    print("Hello, ${name}!");
}

// Named arguments at call site
greet(name: "Alice", greeting: "Hi");
greet("Bob");  // Uses default
```

**IL Lowering:**

```
func @add(i64 %a, i64 %b) -> i64 {
    %result = iadd.ovf %a, %b    ; Overflow-checking add
    ret %result
}

func @greet(str %name, str %greeting) -> str {
    ; String interpolation via concat
    %part1 = call str @Viper.String.Concat(%greeting, ", ")
    %part2 = call str @Viper.String.Concat(%part1, %name)
    %result = call str @Viper.String.Concat(%part2, "!")
    ret %result
}
```

### 3.2 Methods

**Status:** [ ] Not Started

**Syntax:**

```viper
entity Counter {
    Integer count = 0;

    expose func increment() {
        count = count + 1;
    }

    expose func getCount() -> Integer {
        return count;
    }
}
```

**IL Lowering:**

```
; Methods receive 'self' as first parameter
func @Counter_increment(ptr %self) -> void {
    ; Load current count
    %count_ptr = %self
    %count = load.i64 %count_ptr

    ; Increment
    %new_count = iadd.ovf %count, 1

    ; Store back
    store.i64 %count_ptr, %new_count
    ret
}

func @Counter_getCount(ptr %self) -> i64 {
    %count_ptr = %self
    %count = load.i64 %count_ptr
    ret %count
}
```

### 3.3 Closures/Lambdas

**Status:** [ ] Not Started (Requires Phase 0.5)

**Syntax:**

```viper
List[Integer] nums = [1, 2, 3];
List[Integer] doubled = nums.map((x: Integer) -> x * 2);

// With captures
Integer multiplier = 3;
List[Integer] tripled = nums.map((x: Integer) -> x * multiplier);
```

**IL Lowering (with captures):**

```
; Create closure with captured 'multiplier'
%closure = call obj @Viper.Closure.New(@lambda_1, 1)  ; 1 capture
call void @Viper.Closure.SetCapture(%closure, 0, %multiplier_boxed)

; The lambda function
func @lambda_1(ptr %closure, i64 %x) -> i64 {
    ; Get captured multiplier
    %cap_box = call obj @Viper.Closure.GetCapture(%closure, 0)
    %multiplier = call i64 @Viper.Box.ToI64(%cap_box)

    %result = imul.ovf %x, %multiplier
    ret %result
}
```

### 3.4 Phase 3 Testing & Examples

**Unit Tests:**
- Function calls with positional/named arguments
- Default parameter handling
- Method dispatch on values and entities
- Return value propagation

**Example Program - ex03_counter.viper:**
```viper
entity Counter {
    Integer count = 0;

    expose func increment() {
        count = count + 1;
    }

    expose func add(n: Integer) {
        count = count + n;
    }

    expose func getCount() -> Integer {
        return count;
    }
}

func start() {
    Counter c = new Counter();
    c.increment();
    c.increment();
    c.add(10);
    print("Count: ${c.getCount()}");  // Count: 12
}
```

**Milestone:** Methods work on entities; functions callable with named args.

---

## Phase 4: Control Flow

### 4.1 If/Else

**Status:** [ ] Not Started

**Syntax:**

```viper
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}

// Ternary
Integer abs = x >= 0 ? x : -x;
```

**IL Lowering:**

```
; if (x > 0)
%cond = scmp.gt %x, 0
cbr %cond, @then_block, @else_if

@then_block:
call void @print(%str_positive)
br @end

@else_if:
%cond2 = scmp.lt %x, 0
cbr %cond2, @else_if_then, @else_block

@else_if_then:
call void @print(%str_negative)
br @end

@else_block:
call void @print(%str_zero)
br @end

@end:
```

### 4.2 Match Expression

**Status:** [ ] Not Started

**Syntax:**

```viper
value Shape =
    | Circle(radius: Number)
    | Rectangle(width: Number, height: Number);

func area(shape: Shape) -> Number {
    return match (shape) {
        Circle(r) => 3.14159 * r * r;
        Rectangle(w, h) => w * h;
    };
}
```

**IL Lowering:**

Sum types use a tag (i32 for switch.i32 compatibility):

```
; Shape layout: { i32 tag, union data }
; Circle: tag=0, data={f64 radius}
; Rectangle: tag=1, data={f64 width, f64 height}

; match lowering
%tag_ptr = %shape
%tag = load.i32 %tag_ptr
switch.i32 %tag, [0: @circle_case, 1: @rect_case], @unreachable

@circle_case:
%data_ptr = offset %shape, 8     ; After tag + padding
%r = load.f64 %data_ptr
%r_sq = fmul %r, %r
%area = fmul 3.14159, %r_sq
br @end(%area)

@rect_case:
%data_ptr = offset %shape, 8
%w = load.f64 %data_ptr
%h_ptr = offset %data_ptr, 8
%h = load.f64 %h_ptr
%area = fmul %w, %h
br @end(%area)

@end(%result: f64):
ret %result
```

### 4.3 Loops

**Status:** [ ] Not Started

**For Loop:**

```viper
// Range (half-open)
for (i in 0..10) {
    print(i);
}

// Collection
for (item in list) {
    print(item);
}
```

**IL Lowering (Range):**

```
; for (i in 0..10)
%i = alloca 8
store.i64 %i, 0

@loop_start:
%i_val = load.i64 %i
%cond = scmp.lt %i_val, 10
cbr %cond, @loop_body, @loop_end

@loop_body:
; loop body using %i_val
call void @print_i64(%i_val)

; increment
%i_next = iadd %i_val, 1
store.i64 %i, %i_next
br @loop_start

@loop_end:
```

**IL Lowering (Collection):**

```
; for (item in list) - using index iteration
%idx = alloca 8
store.i64 %idx, 0
%len = call i64 @Viper.Collections.List.get_Count(%list)

@loop_start:
%i = load.i64 %idx
%cond = scmp.lt %i, %len
cbr %cond, @loop_body, @loop_end

@loop_body:
%item = call obj @Viper.Collections.List.get_Item(%list, %i)
; Use %item...

%i_next = iadd %i, 1
store.i64 %idx, %i_next
br @loop_start

@loop_end:
```

### 4.4 Phase 4 Testing & Examples

**Unit Tests:**
- If/else branching, nested conditions
- Match exhaustiveness checking
- For loop with ranges (0..10, 0..=10)
- While loop, break, continue
- Guard statement with early return

**Example Program - ex04_fizzbuzz.viper:**
```viper
func start() {
    for (i in 1..=100) {
        if (i % 15 == 0) {
            print("FizzBuzz");
        } else if (i % 3 == 0) {
            print("Fizz");
        } else if (i % 5 == 0) {
            print("Buzz");
        } else {
            print("${i}");
        }
    }
}
```

**Milestone:** All control flow constructs work; match is exhaustive.

### 4.5 Guard

**Status:** [ ] Not Started

**Syntax:**

```viper
func process(data: String?) {
    guard (data != null) else {
        print("No data");
        return;
    }
    // data is non-null here
    print(data);
}
```

**IL Lowering:**

```
func @process(ptr %data) -> void {
    ; guard (data != null)
    %has_value = icmp.ne %data, null
    cbr %has_value, @continue, @guard_else

@guard_else:
    call void @print(%str_no_data)
    ret

@continue:
    ; data is known non-null
    call void @print(%data)
    ret
}
```

---

## Phase 5: Error Handling

### 5.1 Result Type

**Status:** [ ] Not Started

**Syntax:**

```viper
func divide(a: Number, b: Number) -> Result[Number] {
    if (b == 0) {
        return Err(Error(code: "DIV_ZERO", message: "Division by zero"));
    }
    return Ok(a / b);
}
```

**IL Representation:**

```
; Result[T] layout: { i32 tag, union { T value, Error err } }
; Ok: tag = 0, value at offset 8
; Err: tag = 1, error at offset 8

; Result[Unit] for success-only results:
; - Ok(()) sets tag = 0, no payload needed
; - Err(e) sets tag = 1, error at offset 8
```

**Unit Type:**
- `Unit` has exactly one value: `()`
- `Result[Unit]` is used when a function can fail but has no meaningful return value
- `return Ok(());` indicates success with no data

### 5.2 The ? Operator

**Status:** [ ] Not Started

**Syntax:**

```viper
func calculate(x: String, y: String) -> Result[Number] {
    Number a = parse(x)?;  // Early return on error
    Number b = parse(y)?;
    return Ok(a + b);
}
```

**IL Lowering:**

```
func @calculate(str %x, str %y) -> ptr {
    ; Number a = parse(x)?
    %result_a = call ptr @parse(%x)
    %tag_a = load.i32 %result_a
    %is_err_a = icmp.eq %tag_a, 1
    cbr %is_err_a, @propagate_err_a, @continue_a

@propagate_err_a:
    ret %result_a       ; Return the error Result

@continue_a:
    %val_ptr_a = offset %result_a, 8
    %a = load.f64 %val_ptr_a

    ; Number b = parse(y)?
    %result_b = call ptr @parse(%y)
    %tag_b = load.i32 %result_b
    %is_err_b = icmp.eq %tag_b, 1
    cbr %is_err_b, @propagate_err_b, @continue_b

@propagate_err_b:
    ret %result_b

@continue_b:
    %val_ptr_b = offset %result_b, 8
    %b = load.f64 %val_ptr_b

    ; return Ok(a + b)
    %sum = fadd %a, %b
    %result = call ptr @Result_Ok_f64(%sum)
    ret %result
}
```

---

## Phase 6: Concurrency

### Thread-First Model (v0.1)

**Important:** ViperLang v0.1 uses thread-based concurrency, NOT async/await.

The runtime provides:

| Function | IL Signature | Purpose |
|----------|--------------|---------|
| `Thread.Start` | `obj(ptr,ptr)` | Start OS thread (native + VM) |
| `Thread.Join` | `void(obj)` | Wait for thread |
| `Thread.TryJoin` | `i1(obj)` | Non-blocking join |
| `Thread.JoinFor` | `i1(obj,i64)` | Join with timeout (ms) |
| `Thread.Sleep` | `void(i64)` | Sleep current thread (ms) |
| `Thread.Yield` | `void()` | Yield scheduler |
| `Monitor.Enter` | `void(obj)` | FIFO-fair, re-entrant lock acquisition |
| `Monitor.TryEnter` | `i1(obj)` | Non-blocking acquisition |
| `Monitor.TryEnterFor` | `i1(obj,i64)` | Acquisition with timeout (ms) |
| `Monitor.Exit` | `void(obj)` | Release lock |
| `Monitor.Wait` | `void(obj)` | Wait (releases lock; re-acquires before return) |
| `Monitor.WaitFor` | `i1(obj,i64)` | Wait with timeout (ms) |
| `Monitor.Pause` | `void(obj)` | Wake 1 waiter (must own lock) |
| `Monitor.PauseAll` | `void(obj)` | Wake all waiters (must own lock) |
| `SafeI64.New` | `obj(i64)` | FIFO-safe integer cell |
| `SafeI64.Get` | `i64(obj)` | Get value |
| `SafeI64.Set` | `void(obj,i64)` | Set value |
| `SafeI64.Add` | `i64(obj,i64)` | Add delta |
| `SafeI64.CompareExchange` | `i64(obj,i64,i64)` | CAS (returns old) |

**Thread.Start entry signature restriction (v0.1):**
- Must be `func() -> Void` or `func(Ptr) -> Void`
- Enforce at compile-time (diagnostic) and at runtime (trap message matches runtime: `Thread.Start: invalid entry signature`)

**Mutex/Lock model (v0.1):**
- Provide a `Viper.Threads.Mutex` entity as the *primary* explicit lock object.
- Implement `Mutex` methods by calling `Viper.Threads.Monitor.*(self)` under the hood.
- Also expose `Monitor.*` so advanced users can lock arbitrary objects directly.

**Mutex API (v0.1):**

| Method | Returns | Notes |
|--------|---------|-------|
| `enter()` | `Void` | FIFO-fair, re-entrant; blocks indefinitely |
| `tryEnter()` | `Boolean` | Never blocks |
| `tryEnterFor(ms: Integer)` | `Boolean` | Timeout in ms (negative clamps to 0) |
| `exit()` | `Void` | Must balance `enter()`/`tryEnter*()` recursion |
| `wait()` | `Void` | Must own lock; releases + re-acquires before returning |
| `waitFor(ms: Integer)` | `Boolean` | True if signaled, false on timeout |
| `pause()` | `Void` | Must own lock; wakes one waiter |
| `pauseAll()` | `Void` | Must own lock; wakes all waiters |

**Syntax:**

```viper
import Viper.Threads;

func worker(arg: Ptr) {
    // Thread work here
}

func start() {
    Thread t = Thread.start(worker, null);
    t.join();
}

// With synchronization
entity Counter {
    Mutex mu = new Mutex();
    Integer count = 0;

    expose func increment() {
        mu.enter();
        count = count + 1;
        mu.exit();
    }
}
```

**Wait/Pause (v0.1):**

```viper
import Viper.Threads;

entity Mailbox {
    Mutex mu = new Mutex();
    String? message = null;
}

func producer(arg: Ptr) {
    Mailbox box = arg as Mailbox;
    box.mu.enter();
    box.message = "ready";
    box.mu.pauseAll();      // wake all waiters (FIFO re-acquire)
    box.mu.exit();
}

func consumer(arg: Ptr) {
    Mailbox box = arg as Mailbox;
    box.mu.enter();
    while (box.message == null) {
        box.mu.wait();      // releases + re-acquires mu
    }
    String msg = box.message ?? "<null>";
    box.mu.exit();
    print(msg);
}
```

### Async/Await (v0.2 - Deferred)

**Status:** Deferred - requires Task runtime

The language spec mentions async/await for v0.2, but this requires:
1. Task[T] runtime objects
2. Async executor/scheduler
3. Await suspension points
4. Coroutine transforms

These are NOT currently available in the runtime.

---

## Phase 7: Collections

### 7.1 List

**Status:** [x] Complete

**Syntax:**

```viper
List[Integer] nums = [1, 2, 3, 4, 5];
nums.push(6);

Integer first = nums[0];         // Panics if out of bounds
Integer? maybe = nums.get(10);   // Returns null if out of bounds

Integer len = nums.len();

for (num in nums) {
    print(num);
}
```

**IL Lowering:**

```
; Create list with boxing
%list = call obj @Viper.Collections.List.New()

; Push 1 (boxed)
%box1 = call obj @Viper.Box.I64(1)
call void @Viper.Collections.List.Add(%list, %box1)

; Push 2 (boxed)
%box2 = call obj @Viper.Box.I64(2)
call void @Viper.Collections.List.Add(%list, %box2)

; Access nums[0] (unbox)
%box = call obj @Viper.Collections.List.get_Item(%list, 0)
%val = call i64 @Viper.Box.ToI64(%box)
```

**Important:** Runtime `List` uses pointer-identity for Find/Has, so `List[Integer].has(42)` requires the value-equality wrapper from Phase 0.2.

### 7.2 Map

**Status:** [ ] Not Started

**Syntax:**

```viper
Map[String, Integer] scores = {
    "Alice": 100,
    "Bob": 85
};

scores["Carol"] = 92;
Integer? score = scores.get("Dave");

for ((name, score) in scores) {
    print("${name}: ${score}");
}
```

**IL Lowering:**

```
; Create map
%map = call obj @Viper.Collections.Map.New()

; Set "Alice" -> 100
%val_box = call obj @Viper.Box.I64(100)
call void @Viper.Collections.Map.Set(%map, "Alice", %val_box)

; Get with optional
%maybe = call obj @Viper.Collections.Map.Get(%map, "Dave")
; Returns null if not found
```

### 7.3 Set

**Status:** [ ] Not Started (Partially blocked - see limitations)

**Syntax:**

```viper
Set[String] tags = {"viper", "language"};
tags.put("compiler");
Boolean hasTag = tags.has("viper");
```

**Limitations:**

Current runtime Bag is **string-only**. This means:
- `Set[String]` works directly via `Viper.Collections.Bag.*`
- `Set[Integer]` requires Phase 0.4 (Set for non-strings)
- `Set[CustomType]` requires custom hashing

**IL Lowering (String Set):**

```
%set = call obj @Viper.Collections.Bag.New()
call i1 @Viper.Collections.Bag.Put(%set, "viper")
call i1 @Viper.Collections.Bag.Put(%set, "language")

%has = call i1 @Viper.Collections.Bag.Has(%set, "viper")
```

---

## Phase 8: Advanced Features

### 8.1 Interfaces

**Status:** [ ] Not Started

**Syntax:**

```viper
interface Drawable {
    func draw(canvas: Canvas);
    func bounds() -> Rectangle;
}

value Circle implements Drawable {
    Point center;
    Number radius;

    expose func draw(canvas: Canvas) {
        canvas.fillCircle(center, radius);
    }

    expose func bounds() -> Rectangle {
        return Rectangle(
            center.x - radius,
            center.y - radius,
            radius * 2,
            radius * 2
        );
    }
}
```

**IL Lowering:**

Interface dispatch uses vtables:

```
; Interface vtable layout
; Drawable_vtable = { draw_fn_ptr, bounds_fn_ptr }

; Circle's Drawable vtable
@Circle_Drawable_vtable = { @Circle_draw, @Circle_bounds }

; Interface call: shape.draw(canvas)
%vtable = call ptr @get_vtable(%shape, %Drawable_type_id)
%draw_fn = load.ptr %vtable
%self = call ptr @get_impl_ptr(%shape)
call void %draw_fn(%self, %canvas)
```

### 8.2 Inheritance

**Status:** [ ] Not Started

**Syntax:**

```viper
entity Animal {
    String name;

    func speak() -> String {
        return "...";
    }
}

entity Dog extends Animal {
    String breed;

    override func speak() -> String {
        return "Woof!";
    }
}
```

**IL Lowering:**

```
; Dog layout: { Animal fields..., Dog fields... }
; Dog: { String name (from Animal), String breed }

; Virtual dispatch via vtable
; Animal_vtable = { speak_fn_ptr }
; Dog_vtable = { Dog_speak_fn_ptr }  ; Overrides speak

func @makeNoise(ptr %animal) -> void {
    ; Get vtable from object header
    %vtable = call ptr @get_vtable(%animal)
    %speak_fn = load.ptr %vtable
    %result = call str %speak_fn(%animal)
    call void @print(%result)
    ret
}
```

### 8.3 Pattern Matching (Advanced)

**Status:** [ ] Not Started

**Syntax:**

```viper
match (value) {
    // Literal patterns
    0 => "zero";
    1 => "one";

    // With guards
    x if x > 0 => "positive";
    x if x < 0 => "negative";

    // Wildcard
    _ => "other";
}

// Nested patterns
match (result) {
    Ok(Some(value)) => use(value);
    Ok(None) => handleEmpty();
    Err(e) => handleError(e);
}
```

---

## Phase 9: Standard Library

### 9.1 Core Types (Viper.Core)

| Type | Description | Status |
|------|-------------|--------|
| `Integer` | 64-bit signed | Built-in |
| `Number` | 64-bit float | Built-in |
| `Boolean` | true/false | Built-in |
| `String` | UTF-8 string | Built-in |
| `Byte` | 8-bit unsigned | Built-in |
| `Result[T]` | Ok/Err | Phase 5 |
| `Error` | Error value | Phase 5 |

### 9.2 Collections (Viper.Collections)

| Type | Backend | Status |
|------|---------|--------|
| `List[T]` | rt_list + boxing | Phase 7 |
| `Map[K,V]` | rt_map | Phase 7 |
| `Set[String]` | rt_bag | Phase 7 |
| `Set[T]` (non-string) | Blocked | Phase 0.4 |

### 9.3 I/O (Viper.IO)

| Type | Backend | Status |
|------|---------|--------|
| `File` | rt_file_* | Available |
| `Console` | rt_io_* | Available |
| `Path` | rt_path_* | Partial |

### 9.4 Threads (Viper.Threads)

| Type | Backend | Status |
|------|---------|--------|
| `Thread` | rt_thread_* | Available |
| `Monitor` | rt_monitor_* | Available (FIFO, re-entrant, wait/pause) |
| `Mutex` | ViperLang entity; calls `Monitor.*(self)` | Phase 6 |
| `SafeI64` | rt_safe_i64_* | Available |

---

## Phase 10: Demo Applications

### 10.1 Purpose

The final validation phase: implement complete, playable games that demonstrate ViperLang's full capabilities. These are ports of existing BASIC demos, proving ViperLang can handle real-world applications.

### 10.2 Frogger Clone

**Source Reference:** `demos/basic/frogger/frogger.bas` (670+ lines)

**Required Capabilities:**

| Feature | ViperLang Construct | Phase |
|---------|---------------------|-------|
| Classes with methods | Entity types | 2, 3 |
| Arrays of objects | `List[Entity]` | 7 |
| Terminal graphics | `Viper.Terminal.*` | 9 |
| Non-blocking input | `Terminal.InKey()` | 9 |
| Sleep/timing | `Time.SleepMs()` | 9 |
| File I/O (high scores) | `File.ReadAllText()` | 9 |
| String manipulation | `String.Left()`, etc. | 9 |
| Random numbers | `Random.NextInt()` | 9 |

**Core Entities:**

```viper
entity Frog {
    Integer row;
    Integer col;
    Integer lives = 3;

    expose func moveUp() { row = row - 1; }
    expose func moveDown() { row = row + 1; }
    expose func moveLeft() { col = col - 1; }
    expose func moveRight() { col = col + 1; }
    expose func die() { lives = lives - 1; }
    expose func isAlive() -> Boolean { return lives > 0; }
}

entity Vehicle {
    Integer row;
    Integer col;
    Integer speed;
    Integer direction;  // 1 = right, -1 = left
    String symbol;
    Integer width;

    expose func move() {
        col = col + (speed * direction);
        if (col < 1) { col = 70; }
        if (col > 70) { col = 1; }
    }

    expose func checkCollision(frogRow: Integer, frogCol: Integer) -> Boolean {
        if (row != frogRow) { return false; }
        return frogCol >= col && frogCol < col + width;
    }
}

entity Platform {
    // Similar to Vehicle but for river logs/turtles
}

entity Home {
    Integer col;
    Boolean filled = false;

    expose func fill() { filled = true; }
}
```

**Game Loop Pattern:**

```viper
func gameLoop() {
    while (gameRunning && frog.isAlive()) {
        Terminal.BeginBatch();
        drawBoard();
        Terminal.EndBatch();

        handleInput();
        updateGame();
        Time.SleepMs(100);
    }
}
```

### 10.3 vTRIS Clone

**Source Reference:** `demos/basic/vtris/vtris.bas` (800+ lines, 4 files)

**Required Capabilities:**

| Feature | ViperLang Construct | Phase |
|---------|---------------------|-------|
| 3 Classes | Entity types | 2, 3 |
| 2D arrays in entities | `List[List[Integer]]` | 7 |
| Matrix rotation | Methods with loops | 3, 4 |
| Collision detection | Conditional logic | 4 |
| Line clearing | Array manipulation | 7 |
| High score system | File I/O + sorting | 7, 9 |
| ANSI colors | `Terminal.SetColor()` | 9 |
| Unicode graphics | String literals | 1 |

**Core Entities:**

```viper
entity Piece {
    Integer pieceType;
    Integer rotation;
    Integer row;
    Integer col;
    List[List[Integer]] shape;

    expose func rotate() {
        // 90° clockwise matrix rotation
        List[List[Integer]] newShape = [];
        for (i in 0..4) {
            List[Integer] newRow = [];
            for (j in 0..4) {
                newRow.push(shape[3 - j][i]);
            }
            newShape.push(newRow);
        }
        shape = newShape;
    }

    expose func moveLeft() { col = col - 1; }
    expose func moveRight() { col = col + 1; }
    expose func drop() { row = row + 1; }
}

entity Board {
    List[List[Integer]] cells;  // 20 rows x 10 cols
    Integer score = 0;
    Integer lines = 0;
    Integer level = 1;

    expose func checkLines() -> Integer {
        Integer cleared = 0;
        for (row in 0..20) {
            if (isRowFull(row)) {
                clearRow(row);
                cleared = cleared + 1;
            }
        }
        return cleared;
    }
}

entity Scoreboard {
    List[String] names;
    List[Integer] scores;

    expose func isHighScore(score: Integer) -> Boolean {
        if (scores.len() < 10) { return true; }
        return score > scores[9];
    }

    expose func addScore(name: String, score: Integer) {
        // Insert in sorted position
    }
}
```

### 10.4 Demo Completion Criteria

**Frogger:**
- [ ] Title screen with ASCII art
- [ ] Main menu (Play, High Scores, Instructions, Quit)
- [ ] Playable game with frog, vehicles, platforms
- [ ] Collision detection (cars, water)
- [ ] Platform riding (frog moves with logs)
- [ ] 5 homes to fill
- [ ] Lives system
- [ ] Scoring and high score persistence
- [ ] Pause functionality
- [ ] Game over screen

**vTRIS:**
- [ ] Title screen with ANSI art
- [ ] Main menu (New Game, High Scores, Instructions, Quit)
- [ ] All 7 Tetris pieces with colors
- [ ] Piece rotation
- [ ] Collision detection
- [ ] Line clearing with scoring
- [ ] Progressive speed (levels)
- [ ] Next piece preview
- [ ] High score system with file persistence
- [ ] Game over detection

### 10.5 Testing Strategy

**Incremental Build:**
1. Start with minimal game (just rendering)
2. Add input handling
3. Add game objects one at a time
4. Add collision detection
5. Add scoring
6. Add file persistence
7. Polish (menus, high scores)

**Regression Testing:**
After each increment, verify:
- All previous example programs still compile
- All phase tests still pass
- No memory leaks (if applicable)

**Performance Testing:**
- Games should run at 60fps equivalent (smooth animation)
- No visible flicker (use buffered rendering)
- Responsive input (<50ms latency)

---

## Progress Tracking

### Phase 0: Prerequisites

| Task | Status | Blocking |
|------|--------|----------|
| 0.1 Boxing Runtime | [x] Complete | Phase 7 |
| 0.2 Value-Equality Collections | [ ] Not Started | Phase 7 |
| 0.3 Iterator Protocol | [ ] Not Started | Phase 4.3 |
| 0.4 Set for Non-Strings | [ ] Not Started | Phase 7.3 |
| 0.5 Closure Runtime | [ ] Not Started | Phase 3.3 |
| 0.6 Safe Weak References | [ ] Not Started | Weak fields (Phase 2/Spec) |

### Phase 1: Core Foundation

| Task | Status | Dependencies |
|------|--------|--------------|
| 1.1 Lexer | [x] Complete | None |
| 1.2 AST Types | [x] Complete | None |
| 1.3 Parser | [x] Complete | 1.1, 1.2 |
| 1.4 Type System (Sema) | [x] Complete | 1.3 |
| 1.5 IL Lowerer | [x] Complete | 1.4 |
| 1.6 Compiler Driver | [x] Complete | 1.5 |
| 1.7 Hello World Test | [x] Complete | 1.6 |
| 1.8 viperlang CLI Tool | [x] Complete | 1.6 |

### Phase 2: Type System

| Task | Status | Dependencies |
|------|--------|--------------|
| 2.1 Values | [x] Complete | 1.3 |
| 2.2 Entities | [x] Complete | 1.3 |
| 2.3 Optionals | [x] Complete | 2.1, 2.2 |
| 2.4 Generics | [ ] Not Started | 2.1, 2.2 |

**Entity Notes:** Entity types support methods, field access, field assignment, and heap allocation via `new`. Entity field layout includes 8-byte header (for vtable/type info).

### Phase 3: Functions and Methods

| Task | Status | Dependencies |
|------|--------|--------------|
| 3.1 Functions | [x] Complete | 1.3 |
| 3.2 Methods | [x] Complete | 2.1, 2.2 |
| 3.3 Closures | [ ] Not Started | 0.5 |

### Phase 4: Control Flow

| Task | Status | Dependencies |
|------|--------|--------------|
| 4.1 If/Else | [x] Complete | 1.3 |
| 4.2 Match | [ ] Not Started | 2.1 |
| 4.3 Loops | [x] Complete | 0.3 |
| 4.4 Guard | [ ] Not Started | 2.3 |

### Phase 5: Error Handling

| Task | Status | Dependencies |
|------|--------|--------------|
| 5.1 Result Type | [ ] Not Started | 2.1 |
| 5.2 ? Operator | [ ] Not Started | 5.1 |

### Phase 6: Concurrency

| Task | Status | Dependencies |
|------|--------|--------------|
| 6.1 Thread-First Model | [ ] Not Started | 3.1 |
| 6.2 Async/Await | Deferred | Task Runtime |

### Phase 7: Collections

| Task | Status | Dependencies |
|------|--------|--------------|
| 7.1 List | [x] Complete | 0.1, 0.2 |
| 7.2 Map | [ ] Not Started | 0.1 |
| 7.3 Set[String] | [ ] Not Started | None |
| 7.3 Set[T] | Blocked | 0.4 |

### Phase 8: Advanced Features

| Task | Status | Dependencies |
|------|--------|--------------|
| 8.1 Interfaces | [ ] Not Started | 2.1, 2.2 |
| 8.2 Inheritance | [ ] Not Started | 2.2 |
| 8.3 Pattern Matching | [ ] Not Started | 2.1, 4.2 |

### Phase 9: Standard Library

| Task | Status | Dependencies |
|------|--------|--------------|
| 9.1 Core Types | [ ] Not Started | All prior phases |
| 9.2 Collections Wrappers | [ ] Not Started | Phase 7 |
| 9.3 I/O Wrappers | [ ] Not Started | Phase 3 |
| 9.4 Thread Wrappers | [ ] Not Started | Phase 6 |

### Phase 10: Demo Applications

| Task | Status | Dependencies |
|------|--------|--------------|
| 10.1 Frogger Clone | [ ] Not Started | All prior phases |
| 10.2 vTRIS Clone | [ ] Not Started | All prior phases |
| 10.3 Demo Documentation | [ ] Not Started | 10.1, 10.2 |

### Example Programs Status

| Example | Phase | Status | Features Tested |
|---------|-------|--------|-----------------|
| `ex01_hello.viper` | 1 | [x] Complete | Print, entry point |
| `ex02_point.viper` | 2 | [ ] | Values, entities, fields |
| `ex03_counter.viper` | 3 | [ ] | Methods, state |
| `ex04_fizzbuzz.viper` | 4 | [ ] | Loops, conditionals |
| `ex05_calculator.viper` | 5 | [ ] | Result, ? operator |
| `ex06_parallel.viper` | 6 | [ ] | Threads (optional) |
| `ex07_inventory.viper` | 7 | [ ] | List, Map operations |
| `ex08_shapes.viper` | 8 | [ ] | Interfaces, polymorphism |
| `ex09_fileio.viper` | 9 | [ ] | File read/write |
| `frogger.viper` | 10 | [ ] | Full game |
| `vtris.viper` | 10 | [ ] | Full game |

---

## Appendix: IL Lowering Patterns

### A.1 Memory Layout Reference

| Type | IL Representation | Size | Alignment |
|------|-------------------|------|-----------|
| `Integer` | `i64` | 8 | 8 |
| `Number` | `f64` | 8 | 8 |
| `Boolean` | `i64` (stored) | 8 | 8 |
| `Byte` | `i32` | 4 | 4 |
| `String` | `str` (ptr) | 8 | 8 |
| `T?` (value) | in-memory `{i64 flag, T payload}` via `ptr + gep` | 8 + sizeof(T) | 8 |
| `T?` (ref) | `ptr` | 8 | 8 |
| `value {...}` | flat memory | sum of fields | max field align |
| `entity` | `ptr` to heap | 8 | 8 |

### A.2 String Interpolation

```viper
String msg = "Hello, ${name}! You are ${age} years old.";
```

Lowering:

```
; Build via concatenation
%s1 = const.str "Hello, "
%s2 = call str @Viper.String.Concat(%s1, %name)
%s3 = call str @Viper.String.Concat(%s2, "! You are ")
%age_str = call str @Viper.Strings.FromInt(%age)
%s4 = call str @Viper.String.Concat(%s3, %age_str)
%msg = call str @Viper.String.Concat(%s4, " years old.")
```

### A.3 Reference Counting

All entities use ARC:

```
; Retain on assignment
%u2 = %u1
call void @rt_obj_retain_maybe(%u2)

; Release on scope exit or reassignment
%should_free = call i1 @rt_obj_release_check0(%old_value)
; if %should_free: run optional dtor, then free:
cbr %should_free, @free, @cont
@free:
    call void @rt_obj_free(%old_value)
    br @cont
@cont:

; Weak references (SAFE: zeroing + weak-load returns retained strong ref)
call void @rt_weak_store(%weak_slot_addr, %strong_ref)  ; store without retain
%maybe_strong = call ptr @rt_weak_load(%weak_slot_addr) ; returns retained strong ref or null
```

### A.4 Entry Point

The ViperLang entry point is `func start()`:

```viper
func start() {
    print("Hello, ViperLang!");
}
```

IL:

```
func @start() -> void {
    call void @Viper.IO.Print("Hello, ViperLang!")
    ret
}
```

---

## Appendix: Potential Oversights & Risks

### Known Gaps to Address

| Gap | Impact | Mitigation |
|-----|--------|------------|
| **No i8 in IL** | `Byte` type must use i32 | Document clearly; may revisit IL spec |
| **Bag is string-only** | `Set[Integer]` blocked | Phase 0.4 or accept limitation |
| **List pointer-identity** | `List[Integer].has(42)` wrong | Phase 0.2 value-equality wrappers |
| **No struct returns** | Can't return values by value | Return via out-param or heap |
| **No async runtime** | async/await deferred | Thread-first model for v0.1 |
| **Weak refs must be zeroing** | Dangling pointer risk | Phase 0.6 safe weak references |

### Risk Mitigation

1. **Feature creep:** Stick to v0.1 spec; defer v0.2 features
2. **Performance:** Profile early; buffered rendering for demos
3. **Memory leaks:** Implement ARC correctly from Phase 2
4. **Regression:** Run all examples after every phase

### Testing Escape Hatches

If a feature proves too difficult:
1. Document the limitation clearly
2. Provide workaround in documentation
3. Add to "v0.2 deferred" list
4. Do NOT ship broken functionality

---

## Document History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Nov 2024 | Initial draft |
| 2.0 | Dec 2024 | Verified against actual IL/runtime capabilities; Added Phase 0 prerequisites; Changed entry point to start(); Updated to Java/C#-like syntax; Added progress tracking; Corrected IL type constraints (no i8, no structs); Added collection semantic limitations |
| 2.1 | Dec 2024 | Added Testing Philosophy section; Added Available Runtime APIs; Added Phase 10 Demo Applications (Frogger, vTRIS); Added testing guidance and example programs for each phase; Added Example Programs Status tracking; Added Potential Oversights & Risks appendix |
| 2.2 | Dec 2024 | Synchronized with spec review fixes: Added Unit type, `let`/`is` keywords, Ptr type; Updated TokenKind enum; Clarified Result[Unit] usage |
| 2.3 | Dec 2024 | Phase 1 complete: viperlang CLI tool created, hello.viper runs successfully, 9 compiler unit tests passing |
| 2.4 | Dec 2024 | Phase 2 progress: Value types (2.1), Entity types (2.2), and Optionals (2.3) complete with tests |
| 2.5 | Dec 2024 | Phases 3 & 4 progress: Functions (3.1), Methods (3.2), If/Else (4.1), and Loops (4.3) complete. For-in loops with ranges now use slot-based SSA for mutable variables. 12 compiler tests passing |
| 2.6 | Dec 2024 | Phase 7.1 (List) complete with boxing/unboxing. Terminal functions added (SetColor, SetPosition, SetCursorVisible, SetAltScreen, BeginBatch, EndBatch, GetKeyTimeout) and Timer functions (Sleep, Millis). Fixed i64/i32 signature mismatches. Known bug: string comparison RHS const_str not emitted |
| 2.7 | Dec 19, 2024 | Fixed string comparison to use Viper.Strings.Equals runtime call; Fixed entity field assignment offset; Parser now handles binary ops in call args; Created 7 demo applications in /demos/viperlang/; All 907 tests passing |

---

**Status:** Phases 1-4, 7.1 Complete - Working on Phase 10 (Demo Apps)
**Known Bug:** FIXED - String comparison now uses `Viper.Strings.Equals` runtime call
**Final Goal:** Frogger and vTRIS demos running in ViperLang
**Next Step:** Continue with Map/Set collections and standard library wrappers

**Recent Fixes (Dec 19, 2024):**
- String comparison (`==`, `!=`) now correctly calls `Viper.Strings.Equals` runtime function
- Entity field assignment uses correct offset (field.offset without extra header addition)
- Parser now handles binary operators in function call arguments (e.g., `foo(x + 1, y)`)
- 7 demo applications created in `/demos/viperlang/` that all compile successfully
