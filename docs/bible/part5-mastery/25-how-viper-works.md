# Chapter 25: How Viper Works

You've learned to write programs. Now let's look under the hood. Understanding how your code becomes a running program helps you write better code and debug mysterious problems.

This chapter demystifies the Viper toolchain.

---

## The Journey of Your Code

When you run a Viper program, several transformations happen:

```
Source Code → Frontend → IL → VM/Native → Results
     ↑            ↑        ↑       ↑
  You write   Parser   Intermediate  Execution
     this    & checks   Language
```

Let's follow a simple program through this journey.

---

## Step 1: Source Code

You write:

```viper
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func start() {
    let result = add(3, 4);
    Viper.Terminal.Say(result);
}
```

This is just text — characters in a file. The computer doesn't understand it yet.

---

## Step 2: Lexical Analysis (Tokenization)

The *lexer* reads characters and groups them into *tokens*:

```
func   → TOKEN_FUNC
add    → TOKEN_IDENTIFIER("add")
(      → TOKEN_LPAREN
a      → TOKEN_IDENTIFIER("a")
:      → TOKEN_COLON
i64    → TOKEN_TYPE("i64")
,      → TOKEN_COMMA
...
```

This is like breaking a sentence into words. The lexer also catches simple errors:

```viper
let x = 3.14.15;  // Error: invalid number literal
let @invalid = 5;  // Error: unexpected character '@'
```

---

## Step 3: Parsing (Syntax Analysis)

The *parser* takes tokens and builds an *Abstract Syntax Tree* (AST):

```
FunctionDecl
├── name: "add"
├── params:
│   ├── Param(name: "a", type: i64)
│   └── Param(name: "b", type: i64)
├── returnType: i64
└── body:
    └── ReturnStmt
        └── BinaryExpr
            ├── op: ADD
            ├── left: Identifier("a")
            └── right: Identifier("b")
```

The tree represents the structure of your program. Parsing catches syntax errors:

```viper
func broken( {  // Error: expected parameter or ')'
let x = ;        // Error: expected expression after '='
```

---

## Step 4: Semantic Analysis

The *semantic analyzer* checks that your program makes sense:

**Type checking:**
```viper
let x: i64 = "hello";  // Error: cannot assign string to i64
let y = add("a", "b"); // Error: expected i64, got string
```

**Scope checking:**
```viper
func test() {
    Viper.Terminal.Say(x);  // Error: 'x' not defined
    let x = 5;
}
```

**Return checking:**
```viper
func getValue() -> i64 {
    let x = 5;
    // Error: function must return a value
}
```

---

## Step 5: IL Generation

Your program becomes *Intermediate Language* — a lower-level representation:

```
.func add(i64, i64) -> i64
    param.get 0          ; get first parameter (a)
    param.get 1          ; get second parameter (b)
    iadd                 ; add them
    ret                  ; return result

.func start() -> void
    iconst 3             ; push 3
    iconst 4             ; push 4
    call add             ; call add(3, 4)
    call Viper.Terminal.Say  ; print result
    ret
```

IL is like assembly language but machine-independent. It works on any platform.

### Why IL?

Multiple frontends (ViperLang, BASIC, Pascal) all generate the same IL. Then one VM or code generator handles execution for all of them:

```
ViperLang ─┐
           ├──► IL ───► VM/Native
BASIC ─────┤
           │
Pascal ────┘
```

This is why you can mix languages in Viper projects!

---

## Step 6: Execution (VM or Native)

### The Virtual Machine

The VM interprets IL instruction by instruction:

```
Instruction: iconst 3
VM Action:   Push 3 onto stack
Stack:       [3]

Instruction: iconst 4
VM Action:   Push 4 onto stack
Stack:       [3, 4]

Instruction: iadd
VM Action:   Pop two values, add, push result
Stack:       [7]

Instruction: call Viper.Terminal.Say
VM Action:   Pop value, print it
Output:      7
Stack:       []
```

The VM is like a computer inside your computer, running IL instead of machine code.

### Native Code Generation

For performance, Viper can compile IL to native machine code:

```
IL:                     x86-64:
iadd                    pop rbx
                        pop rax
                        add rax, rbx
                        push rax
```

Native code runs directly on the CPU — no interpreter overhead.

---

## The Three Frontends

### ViperLang

Modern syntax, designed for Viper:

```viper
func square(x: i64) -> i64 {
    return x * x;
}
```

### BASIC

Classic syntax, familiar to many:

```basic
FUNCTION Square(x AS INTEGER) AS INTEGER
    Square = x * x
END FUNCTION
```

### Pascal

Structured, educational heritage:

```pascal
function Square(x: Integer): Integer;
begin
    Square := x * x;
end;
```

All three generate the same IL:

```
.func square(i64) -> i64
    param.get 0
    param.get 0
    imul
    ret
```

---

## The Stack Machine

Viper's IL uses a *stack-based* model. Operations push and pop values:

```viper
let result = (3 + 4) * 2;
```

Becomes:

```
iconst 3     ; stack: [3]
iconst 4     ; stack: [3, 4]
iadd         ; stack: [7]
iconst 2     ; stack: [7, 2]
imul         ; stack: [14]
store result ; stack: []
```

Advantages:
- Simple to generate code
- Compact representation
- Easy to interpret

---

## Memory Management

Viper manages memory automatically using *garbage collection*:

```viper
func makeList() -> [i64] {
    let list = [1, 2, 3];  // Memory allocated
    return list;
}

func start() {
    let a = makeList();    // list is reachable through 'a'
    let b = makeList();    // another list allocated

    a = null;              // first list no longer reachable
    // Garbage collector will reclaim first list's memory

    Viper.Terminal.Say(b[0]);  // second list still in use
}
```

### How Garbage Collection Works

1. **Mark**: Start from "roots" (global variables, stack), follow all references, mark reachable objects
2. **Sweep**: Free unmarked objects — they're garbage

```
Before GC:
  Roots → [Object A] → [Object B]
          [Object C] (unreachable)
          [Object D] → [Object E]

After Mark:
  [Object A] ✓
  [Object B] ✓
  [Object C] (unmarked)
  [Object D] ✓
  [Object E] ✓

After Sweep:
  [Object C] freed
```

You don't need to manually free memory — but understanding GC helps you write efficient code.

---

## The Runtime Library

When you call `Viper.Terminal.Say`, you're calling into the *runtime* — a library of functions written in C that provide system capabilities:

- **I/O**: Files, console, networking
- **Memory**: Allocation, garbage collection
- **Threads**: Parallelism, synchronization
- **Graphics**: Windows, drawing, sprites
- **Time**: Clocks, timers, delays

The runtime bridges IL code and the operating system.

---

## Type System Details

### Value Types vs Reference Types

**Value types** are stored directly:
```viper
let x: i64 = 42;      // 42 is stored in x
let y = x;            // y gets a copy of 42
x = 100;              // y is still 42
```

**Reference types** store a pointer:
```viper
let a = [1, 2, 3];    // array is on heap, a holds reference
let b = a;            // b points to same array
a[0] = 999;           // b[0] is also 999!
```

### Type Inference

Viper infers types when obvious:

```viper
let x = 42;           // x is i64
let y = 3.14;         // y is f64
let z = "hello";      // z is string
let list = [1, 2, 3]; // list is [i64]
```

But you can be explicit:
```viper
let x: i64 = 42;
let y: f64 = 3.14;
```

---

## Compilation Phases Summary

| Phase | Input | Output | Errors Caught |
|-------|-------|--------|---------------|
| Lexing | Source text | Tokens | Invalid characters, malformed literals |
| Parsing | Tokens | AST | Syntax errors |
| Semantic | AST | Typed AST | Type errors, undefined names |
| IL Gen | Typed AST | IL | (none — types already checked) |
| VM/Codegen | IL | Execution/Native | (none — IL is verified) |

---

## Debugging: Seeing the Internals

### View Tokens

```bash
viper --dump-tokens myprogram.viper
```

### View AST

```bash
viper --dump-ast myprogram.viper
```

### View IL

```bash
viper --dump-il myprogram.viper
```

### View Native Assembly

```bash
viper --dump-asm myprogram.viper
```

These help understand what your code becomes.

---

## The Three Languages (Behind the Scenes)

All three languages share the same:
- IL specification
- Virtual machine
- Runtime library
- Native code generator
- Garbage collector

Only the frontend (lexer, parser, semantic analysis) differs.

This is why you can:
- Import BASIC modules from ViperLang
- Call Pascal functions from BASIC
- Share data structures across languages

---

## Common Misconceptions

**"The VM is slow"**

Modern VMs are highly optimized. For I/O-bound or high-level work, you won't notice. For CPU-intensive code, compile to native.

**"Garbage collection causes pauses"**

Modern GC algorithms minimize pauses. Viper uses incremental collection for responsiveness. For real-time applications, you can tune GC behavior.

**"I need to understand IL to program"**

No! But understanding it helps debug tricky issues and write more efficient code.

---

## Summary

- Source code goes through lexing → parsing → semantic analysis → IL generation → execution
- The AST represents your program's structure
- IL is a platform-independent intermediate representation
- The VM interprets IL; native compilation generates machine code
- Three frontends (ViperLang, BASIC, Pascal) all produce the same IL
- Garbage collection automatically manages memory
- The runtime provides system services (I/O, threads, graphics)

Understanding this pipeline helps you:
- Read error messages better
- Debug more effectively
- Write more efficient code
- Appreciate language design

---

## Exercises

**Exercise 25.1**: Use `--dump-tokens` on a simple program and identify each token type.

**Exercise 25.2**: Use `--dump-ast` to see the tree structure of a function with nested expressions.

**Exercise 25.3**: Write a function in ViperLang and BASIC, use `--dump-il` on both, and compare the output.

**Exercise 25.4**: Deliberately introduce different types of errors (lexical, syntax, semantic) and observe how the compiler reports them.

**Exercise 25.5**: Experiment with the garbage collector: create a program that allocates many objects and see when/how they're collected.

**Exercise 25.6** (Challenge): Read the IL for a recursive function and trace its execution by hand, tracking the stack.

---

*Now that you understand how Viper works, let's make your code faster. Next chapter: Performance optimization.*

*[Continue to Chapter 26: Performance →](26-performance.md)*
