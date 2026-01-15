# Viper C++ Compiler Implementation Plan

This document outlines the comprehensive implementation plan for the Viper C++ (vcpp) compiler, a self-hosted C/C++ compiler targeting the Viper project.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture](#2-architecture)
3. [Development Phases](#3-development-phases)
4. [Phase 1: Foundation](#4-phase-1-foundation)
5. [Phase 2: C Frontend](#5-phase-2-c-frontend)
6. [Phase 3: C Code Generation](#6-phase-3-c-code-generation)
7. [Phase 4: C++ Frontend](#7-phase-4-c-frontend)
8. [Phase 5: Optimization](#8-phase-5-optimization)
9. [Phase 6: Advanced C++](#9-phase-6-advanced-c)
10. [Phase 7: Standard Library](#10-phase-7-standard-library)
11. [Phase 8: Tooling](#11-phase-8-tooling)
12. [Phase 9: Self-Hosting](#12-phase-9-self-hosting)
13. [Testing Strategy](#13-testing-strategy)
14. [Project Structure](#14-project-structure)
15. [Dependencies](#15-dependencies)
16. [Milestones](#16-milestones)

---

## 1. Project Overview

### 1.1 Goals

1. **Primary Goal**: Build a C++20/C17 compiler capable of compiling the Viper OS kernel and userland
2. **Secondary Goal**: Achieve self-hosting (vcpp can compile itself)
3. **Tertiary Goal**: Provide a clean, maintainable codebase suitable for educational purposes

### 1.2 Host and Target Platforms

| Platform Type | Platforms | Description |
|---------------|-----------|-------------|
| **Host** (vcpp runs here) | macOS (ARM64, x86-64), Linux (x86-64, ARM64) | Development environments |
| **Target** (vcpp generates code for) | **AArch64 only** | Viper OS target (bare-metal) |

**Note:** vcpp is a cross-compiler. It runs on macOS/Linux development machines but only generates code for AArch64 targets.

### 1.3 Language Support

| Language | Standard | Priority |
|----------|----------|----------|
| C | C17 | Phase 2-3 |
| C++ | C++20 | Phase 4-6 |

### 1.4 Design Principles

1. **Correctness First**: Prioritize correct code generation over optimization
2. **Incremental Development**: Each phase produces a usable compiler
3. **Test-Driven**: Comprehensive test suite at every stage
4. **Clean Architecture**: Modular design with clear interfaces
5. **Documentation**: Well-documented code and design decisions

---

## 2. Architecture

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        vcpp Driver                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Preprocessor │  │    Lexer     │  │       Parser         │  │
│  │              │─▶│              │─▶│   (C/C++ Frontend)   │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Abstract Syntax Tree                   │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                   Semantic Analysis                       │  │
│  │         (Type Checking, Name Resolution, etc.)            │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                Intermediate Representation                │  │
│  │                      (Viper IR)                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Optimization Passes                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    AArch64 Backend                        │  │
│  │              (Target: Viper OS only)                      │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                  ELF64 Assembly Output                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Component Overview

| Component | Description | Language |
|-----------|-------------|----------|
| Driver | Command-line interface, orchestration | C++ |
| Preprocessor | Macro expansion, includes, conditionals | C++ |
| Lexer | Tokenization | C++ |
| Parser | Syntax analysis, AST construction | C++ |
| Sema | Semantic analysis, type checking | C++ |
| IR | Intermediate representation | C++ |
| Optimizer | IR transformation passes | C++ |
| CodeGen | Target-specific code generation | C++ |
| Assembler | Assembly to object code (optional) | C++ |

### 2.3 Intermediate Representation

Design a custom IR (Viper IR) with the following properties:

- **SSA Form**: Static Single Assignment
- **Typed**: All values have explicit types
- **Low-Level**: Close to machine operations
- **Target-Independent**: Separate from machine specifics

```
; Example Viper IR
define i32 @factorial(i32 %n) {
entry:
    %cmp = icmp sle i32 %n, 1
    br i1 %cmp, label %base, label %recurse

base:
    ret i32 1

recurse:
    %n1 = sub i32 %n, 1
    %r = call i32 @factorial(i32 %n1)
    %result = mul i32 %n, %r
    ret i32 %result
}
```

---

## 3. Development Phases

### 3.1 Phase Summary

**Note:** Timelines are for full-time development. Realistic estimates account for debugging, testing, and edge cases.

| Phase | Name | Duration | Deliverable |
|-------|------|----------|-------------|
| 1 | Foundation | 2-3 months | Project infrastructure, utilities |
| 2 | C Frontend | 4-6 months | C17 parser and semantic analysis |
| 3 | C Code Generation | 4-6 months | Working C compiler |
| 4 | C++ Frontend (Tier 1-2) | 6-9 months | ViperDOS C++ Profile Tier 1-2 |
| 5 | Optimization | 3-4 months | Basic optimization passes |
| 6 | Advanced C++ (Tier 3-4) | 6-9 months | Full C++20 support |
| 7 | Standard Library | 2-3 months | Compiler-rt, libc++ support |
| 8 | Tooling | 2-3 months | Debugger support, IDE integration |
| 9 | Self-Hosting | 4-6 months | vcpp compiles vcpp |

**Total Estimated Timeline:** 33-49 months (approximately 3-4 years)

### 3.2 Alternative: LLVM Backend Option

Instead of building a custom backend, vcpp could emit LLVM IR and use LLVM for optimization and code generation:

```
┌─────────────────────────────────────────────────────────────────┐
│  OPTION A: Custom Backend          OPTION B: LLVM Backend       │
├─────────────────────────────────────────────────────────────────┤
│  vcpp Frontend (C/C++)             vcpp Frontend (C/C++)        │
│         │                                   │                    │
│         ▼                                   ▼                    │
│    Viper IR                            LLVM IR Emission          │
│         │                                   │                    │
│         ▼                                   ▼                    │
│  Custom Optimizer                    LLVM Optimizer (opt)        │
│         │                                   │                    │
│         ▼                                   ▼                    │
│  AArch64 Backend                     LLVM AArch64 Backend        │
│         │                                   │                    │
│         ▼                                   ▼                    │
│    ELF64 Output                        ELF64 Output              │
└─────────────────────────────────────────────────────────────────┘
```

| Aspect | Custom Backend | LLVM Backend |
|--------|---------------|--------------|
| Development time | +12-18 months | Uses existing LLVM |
| Code quality | Must build optimization | World-class optimization |
| Binary size | Smaller compiler | Larger (LLVM dependency) |
| Self-hosting | Possible | Requires LLVM for bootstrap |
| Educational value | High | Lower (black box) |
| Maintenance | Full ownership | LLVM updates required |

**Recommendation:** Consider LLVM backend for faster time-to-working-compiler, then optionally develop custom backend for educational/self-hosting goals.

### 3.3 Feature Staging (ViperDOS C++ Profile)

C++ features are staged according to the ViperDOS C++ Profile (see `viperdos_cpp_profile.md`):

| Tier | Phase | Key Features | ViperDOS Target |
|------|-------|--------------|----------------|
| **Tier 1** | Phase 4a | Classes, templates (basic), constexpr, move semantics | Kernel boot, HAL |
| **Tier 2** | Phase 4b | Virtual functions, lambdas, advanced templates | Full kernel |
| **Tier 3** | Phase 6a | Standard containers, optional exceptions | Userspace apps |
| **Tier 4** | Phase 6b | Concepts, three-way comparison, C++20 complete | Self-hosting |

This staging allows incremental capability:
- After **Tier 1**: Compile kernel boot code and core data structures
- After **Tier 2**: Compile complete Viper OS kernel
- After **Tier 3**: Compile user applications with full standard library
- After **Tier 4**: Full C++20 for self-hosting and future code

### 3.4 Phase to ViperDOS Tier Mapping

| Phase | ViperDOS Tier | Validation Criteria |
|-------|--------------|---------------------|
| Phase 3 | — | C compiler works; can build Viper libc |
| Phase 4 (early) | Tier 1 | Compiles `kernel/boot/`, `kernel/arch/`, `kernel/lib/` |
| Phase 4 (late) | Tier 2 | Compiles entire kernel; boots to shell |
| Phase 6 | Tier 3 | Compiles `user/` programs; shell commands work |
| Phase 9 | Tier 4 | vcpp compiles vcpp (three-stage bootstrap) |

### 3.5 Dependencies

```
Phase 1 ──▶ Phase 2 ──▶ Phase 3 ──┬──▶ Phase 5 ──▶ Phase 9
                                   │
                                   └──▶ Phase 4 ──▶ Phase 6 ──▶ Phase 7
                                                              │
                                                              └──▶ Phase 8
```

---

## 4. Phase 1: Foundation

### 4.1 Objectives

- Establish project structure
- Implement core utilities
- Set up build system and testing infrastructure

### 4.2 Tasks

#### 4.2.1 Project Setup

- [ ] Create directory structure
- [ ] Set up CMake build system
- [ ] Configure CI/CD pipeline
- [ ] Set up code formatting (clang-format)
- [ ] Set up linting (clang-tidy)
- [ ] Create documentation framework

#### 4.2.2 Core Utilities

- [ ] **Memory Management**
  - Arena allocator for AST nodes
  - Bump allocator for temporary allocations
  - Pool allocator for fixed-size objects

- [ ] **String Handling**
  - Interned string table
  - String view implementation
  - String builder

- [ ] **Data Structures**
  - Dynamic array (vector)
  - Hash map
  - Hash set
  - Small vector (stack-allocated small sizes)
  - Intrusive linked list

- [ ] **Error Handling**
  - Result type (`Result<T, E>`)
  - Error codes enumeration
  - Diagnostic system foundation

- [ ] **File I/O**
  - Memory-mapped file reading
  - Source file abstraction
  - Include path management

#### 4.2.3 Diagnostic System

- [ ] Source location tracking (file, line, column)
- [ ] Diagnostic message formatting
- [ ] Error/warning/note severity levels
- [ ] Colored terminal output
- [ ] Source code snippet display
- [ ] Fix-it hints

#### 4.2.4 Command-Line Interface

- [ ] Argument parser
- [ ] Configuration structures
- [ ] Help text generation
- [ ] Version information

### 4.3 Deliverables

- Standalone utility library
- Working build system
- Test framework with initial tests
- Driver skeleton

---

## 5. Phase 2: C Frontend

### 5.1 Objectives

- Implement C17-compliant preprocessor
- Implement C17 lexer
- Implement C17 parser
- Implement semantic analysis for C

### 5.2 Tasks

#### 5.2.1 Preprocessor

- [ ] **Tokenization**
  - Preprocessing tokens
  - Header name tokens
  - Identifier tokens
  - Preprocessing numbers

- [ ] **Directives**
  - `#include` (local and system)
  - `#define` (object-like macros)
  - `#define` (function-like macros)
  - `#undef`
  - `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`
  - `#error`, `#warning`
  - `#pragma` (including `#pragma once`)
  - `#line`
  - Null directive (`#`)

- [ ] **Macro Expansion**
  - Argument substitution
  - Stringification (`#`)
  - Token pasting (`##`)
  - `__VA_ARGS__`
  - Recursive macro expansion prevention
  - Predefined macros (`__FILE__`, `__LINE__`, etc.)

- [ ] **Conditional Compilation**
  - Constant expression evaluation
  - `defined` operator
  - `__has_include`
  - `__has_attribute`
  - `__has_builtin`

#### 5.2.2 Lexer

- [ ] **Token Types**
  - Keywords (all C17 keywords)
  - Identifiers
  - Integer literals (decimal, hex, octal, binary)
  - Floating-point literals
  - Character literals (including escape sequences)
  - String literals (including concatenation)
  - Punctuators and operators

- [ ] **Lexer Features**
  - UTF-8 source support
  - Trigraph handling (optional)
  - Line splicing (backslash-newline)
  - Comment removal
  - Source location tracking

#### 5.2.3 Parser

- [ ] **Declarations**
  - Variable declarations
  - Function declarations and definitions
  - Type declarations (struct, union, enum)
  - Typedef declarations
  - Static assertions

- [ ] **Type Specifiers**
  - Basic types (void, char, int, float, double, etc.)
  - Signed/unsigned modifiers
  - Short/long modifiers
  - `_Bool`
  - `_Complex`, `_Imaginary`
  - Struct/union specifiers
  - Enum specifiers
  - Typedef names
  - Type qualifiers (const, volatile, restrict, `_Atomic`)
  - Storage class specifiers (auto, register, static, extern, `_Thread_local`)
  - Function specifiers (inline, `_Noreturn`)
  - Alignment specifiers (`_Alignas`)

- [ ] **Declarators**
  - Pointer declarators
  - Array declarators (fixed, VLA, incomplete)
  - Function declarators (parameters, variadic)
  - Abstract declarators
  - Initializers (scalar, aggregate, designated)

- [ ] **Statements**
  - Compound statements (blocks)
  - Expression statements
  - Selection statements (if, switch)
  - Iteration statements (while, do-while, for)
  - Jump statements (goto, continue, break, return)
  - Labeled statements (identifiers, case, default)

- [ ] **Expressions**
  - Primary expressions (identifiers, constants, strings, parenthesized)
  - Postfix expressions (subscript, call, member access, increment)
  - Unary expressions (prefix increment, address-of, dereference, sizeof, alignof, cast)
  - Binary expressions (arithmetic, relational, bitwise, logical, assignment)
  - Conditional expression
  - Comma expression
  - Generic selection (`_Generic`)

#### 5.2.4 AST Design

- [ ] **Node Hierarchy**
  ```
  ASTNode
  ├── Decl
  │   ├── VarDecl
  │   ├── FunctionDecl
  │   ├── TypedefDecl
  │   ├── StructDecl
  │   ├── UnionDecl
  │   ├── EnumDecl
  │   ├── EnumConstantDecl
  │   └── FieldDecl
  ├── Stmt
  │   ├── CompoundStmt
  │   ├── IfStmt
  │   ├── SwitchStmt
  │   ├── WhileStmt
  │   ├── DoStmt
  │   ├── ForStmt
  │   ├── GotoStmt
  │   ├── ContinueStmt
  │   ├── BreakStmt
  │   ├── ReturnStmt
  │   ├── LabelStmt
  │   ├── CaseStmt
  │   ├── DefaultStmt
  │   └── ExprStmt
  ├── Expr
  │   ├── IntegerLiteral
  │   ├── FloatLiteral
  │   ├── CharLiteral
  │   ├── StringLiteral
  │   ├── DeclRefExpr
  │   ├── UnaryExpr
  │   ├── BinaryExpr
  │   ├── CallExpr
  │   ├── CastExpr
  │   ├── MemberExpr
  │   ├── SubscriptExpr
  │   ├── ConditionalExpr
  │   ├── SizeofExpr
  │   ├── AlignofExpr
  │   └── CompoundLiteralExpr
  └── Type
      ├── BuiltinType
      ├── PointerType
      ├── ArrayType
      ├── FunctionType
      ├── StructType
      ├── UnionType
      ├── EnumType
      └── TypedefType
  ```

#### 5.2.5 Semantic Analysis

- [ ] **Symbol Tables**
  - Scoped symbol table implementation
  - Namespace management
  - Tag namespace (struct/union/enum)
  - Label namespace

- [ ] **Type System**
  - Type representation
  - Type equivalence
  - Type compatibility
  - Composite type computation
  - Integer promotion rules
  - Usual arithmetic conversions
  - Default argument promotions

- [ ] **Declaration Processing**
  - Linkage determination
  - Storage duration
  - Type completion checking
  - Redeclaration validation
  - Tentative definitions

- [ ] **Expression Type Checking**
  - Lvalue/rvalue classification
  - Implicit conversions
  - Operator type checking
  - Function call argument checking
  - Assignment compatibility

- [ ] **Statement Checking**
  - Control flow validation
  - Return statement checking
  - Break/continue context checking
  - Switch statement validation
  - Goto/label resolution

### 5.3 Deliverables

- Complete C17 preprocessor
- Complete C17 lexer
- Complete C17 parser
- Semantic analysis with full type checking
- AST dump utility

---

## 6. Phase 3: C Code Generation

### 6.1 Objectives

- Design and implement Viper IR
- Implement IR generation from C AST
- Implement AArch64 code generation (target-only)
- Produce working ELF64 object files

### 6.2 Tasks

#### 6.2.1 IR Design

- [ ] **Type System**
  - Void type
  - Integer types (i1, i8, i16, i32, i64)
  - Floating-point types (f32, f64)
  - Pointer types
  - Array types
  - Struct types
  - Function types

- [ ] **Instructions**
  - Arithmetic: add, sub, mul, sdiv, udiv, srem, urem
  - Floating-point: fadd, fsub, fmul, fdiv, frem
  - Bitwise: and, or, xor, shl, lshr, ashr
  - Comparison: icmp, fcmp
  - Conversion: trunc, zext, sext, fptrunc, fpext, fptoui, fptosi, uitofp, sitofp, ptrtoint, inttoptr, bitcast
  - Memory: alloca, load, store, getelementptr
  - Control flow: br, switch, ret, unreachable
  - Function: call
  - Other: phi, select

- [ ] **IR Infrastructure**
  - Module representation
  - Function representation
  - Basic block representation
  - Instruction representation
  - Value/Use tracking
  - IR printer (text format)
  - IR parser (text format)

#### 6.2.2 IR Generation

- [ ] **Declaration Lowering**
  - Global variables
  - Function definitions
  - Local variables (alloca)
  - Static local variables

- [ ] **Expression Lowering**
  - Arithmetic expressions
  - Comparison expressions
  - Logical expressions (short-circuit)
  - Bitwise expressions
  - Assignment expressions
  - Pointer arithmetic
  - Array subscript
  - Member access
  - Function calls
  - Type casts
  - Compound literals
  - Initializer lists

- [ ] **Statement Lowering**
  - Compound statements
  - If statements
  - Switch statements (jump table, decision tree)
  - While loops
  - Do-while loops
  - For loops
  - Goto/labels
  - Break/continue
  - Return

- [ ] **ABI Handling**
  - Struct passing/returning
  - Variadic functions
  - Calling convention implementation

#### 6.2.3 AArch64 Backend

- [ ] **Instruction Selection**
  - Pattern matching for IR → machine instructions
  - Addressing mode selection
  - Immediate encoding

- [ ] **Register Allocation**
  - Virtual registers
  - Live range analysis
  - Linear scan allocator (initial)
  - Graph coloring allocator (advanced)
  - Spill code generation
  - Callee-saved register handling

- [ ] **Instruction Scheduling**
  - Basic block scheduling
  - Dependency analysis

- [ ] **Assembly Generation**
  - Instruction encoding
  - Directive generation
  - Symbol references
  - Relocation generation

- [ ] **AArch64-Specific Features**
  - AAPCS64 calling convention
  - Atomic instruction generation
  - Memory barrier generation

#### 6.2.4 Inline Assembly Support (Critical Path)

**Note:** Inline assembly is required for Phase 3 because Viper libc (`crt0.c`, syscalls) uses it extensively. This is a critical path item.

- [ ] **Parser**
  - `asm` / `__asm__` statement parsing
  - `volatile` qualifier
  - Extended asm syntax (outputs : inputs : clobbers)
  - Multi-line string templates with `\n`

- [ ] **Operand Handling**
  - Constraint letter parsing (`r`, `m`, `i`, etc.)
  - Constraint modifiers (`=`, `+`, `&`)
  - Matching constraints (`0`-`9`)
  - AArch64-specific constraints (`w`, `x`, `I`, `J`, etc.)

- [ ] **Register Allocation Integration**
  - Explicit register variables (`register x asm("x0")`)
  - Input/output operand allocation
  - Early-clobber handling
  - Clobber list processing

- [ ] **Code Generation**
  - Template substitution (`%0`, `%1`, `%[name]`)
  - Operand modifier application (`%w0`, `%x0`)
  - Inline instruction emission
  - Memory clobber as compiler barrier

**Estimated Effort:** 5-9 weeks (significant complexity in register allocation interaction)

#### 6.2.5 Object File Generation

- [ ] **ELF Support**
  - ELF64 header generation
  - Section creation (.text, .data, .bss, .rodata)
  - Symbol table generation
  - Relocation generation
  - String table management

- [ ] **Debug Information**
  - DWARF generation
  - Line number information
  - Variable location information
  - Type information

### 6.3 Deliverables

- Working C cross-compiler producing AArch64 executables
- AArch64 backend with AAPCS64 calling convention
- ELF64 object file output
- Basic DWARF debug information

---

## 7. Phase 4: C++ Frontend

### 7.1 Objectives

- Extend lexer for C++
- Implement C++ parser
- Implement C++ semantic analysis
- Support C++17 core features

### 7.2 Tasks

#### 7.2.1 Lexer Extensions

- [ ] C++ keywords
- [ ] Operator tokens (::, .*, ->*, <=>)
- [ ] Raw string literals
- [ ] User-defined literals
- [ ] Unicode string literals (u8, u, U)

#### 7.2.2 Parser Extensions

- [ ] **Namespaces**
  - Namespace declarations
  - Nested namespace definitions
  - Using declarations
  - Using directives
  - Namespace aliases
  - Anonymous namespaces
  - Inline namespaces

- [ ] **Classes**
  - Class definitions
  - Access specifiers
  - Member functions
  - Member variables
  - Static members
  - Constructors
  - Destructors
  - Conversion functions
  - Operator overloading
  - Friend declarations
  - Nested classes
  - Local classes

- [ ] **Inheritance**
  - Base class specifiers
  - Virtual inheritance
  - Access control
  - Override/final specifiers

- [ ] **Templates**
  - Class templates
  - Function templates
  - Variable templates
  - Alias templates
  - Template parameters (type, non-type, template)
  - Template argument deduction
  - Explicit instantiation
  - Explicit specialization
  - Partial specialization

- [ ] **Declarations**
  - Auto type deduction
  - Decltype
  - Structured bindings
  - Init-if/init-switch
  - Constexpr variables and functions
  - Inline variables
  - Static_assert

- [ ] **Expressions**
  - New/delete expressions
  - Throw expressions
  - Lambda expressions
  - Fold expressions
  - Requires expressions
  - Type traits

- [ ] **Statements**
  - Try/catch/throw
  - Range-based for
  - Constexpr if

#### 7.2.3 Name Lookup

- [ ] Unqualified lookup
- [ ] Qualified lookup
- [ ] Argument-dependent lookup (ADL)
- [ ] Template name lookup
- [ ] Two-phase lookup

#### 7.2.4 Overload Resolution

- [ ] Candidate function collection
- [ ] Viable function filtering
- [ ] Implicit conversion sequences
- [ ] Best viable function selection
- [ ] Template argument deduction
- [ ] SFINAE

#### 7.2.5 Template Instantiation

- [ ] Implicit instantiation
- [ ] Explicit instantiation
- [ ] Instantiation point determination
- [ ] Template argument substitution
- [ ] Dependent name handling

#### 7.2.6 Semantic Analysis Extensions

- [ ] **Class Semantics**
  - Special member function generation
  - Virtual function table computation
  - Object layout calculation
  - Constructor/destructor semantics
  - Copy/move semantics

- [ ] **Template Semantics**
  - Template parameter validation
  - Constraint checking
  - Concept satisfaction

- [ ] **Exception Semantics**
  - Exception specification checking
  - Noexcept evaluation
  - Try block scoping

### 7.3 Deliverables

- C++17 parser
- C++ semantic analysis
- Template instantiation
- Working C++ compiler (basic features)

---

## 8. Phase 5: Optimization

### 8.1 Objectives

- Implement essential optimization passes
- Achieve reasonable code quality
- Match -O1/-O2 performance levels

### 8.2 Tasks

#### 8.2.1 Analysis Passes

- [ ] **Control Flow Analysis**
  - CFG construction
  - Dominator tree
  - Post-dominator tree
  - Loop detection
  - Loop nesting forest

- [ ] **Data Flow Analysis**
  - Reaching definitions
  - Live variables
  - Available expressions
  - Def-use chains
  - Use-def chains

- [ ] **Alias Analysis**
  - Basic alias analysis
  - Type-based alias analysis (TBAA)
  - Escape analysis

- [ ] **Call Graph Analysis**
  - Call graph construction
  - Strongly connected components

#### 8.2.2 Transformation Passes

- [ ] **Local Optimizations**
  - Constant folding
  - Algebraic simplification
  - Strength reduction
  - Dead code elimination
  - Common subexpression elimination (local)

- [ ] **Global Optimizations**
  - Global common subexpression elimination
  - Global value numbering
  - Partial redundancy elimination
  - Code motion (loop-invariant)
  - Dead store elimination

- [ ] **Control Flow Optimizations**
  - Unreachable code elimination
  - Block merging
  - Jump threading
  - Tail merging
  - Branch simplification
  - Loop simplification

- [ ] **Loop Optimizations**
  - Loop-invariant code motion
  - Induction variable simplification
  - Loop unrolling
  - Loop rotation
  - Loop unswitching

- [ ] **Interprocedural Optimizations**
  - Function inlining
  - Dead argument elimination
  - Return value optimization
  - Tail call optimization
  - Interprocedural constant propagation

- [ ] **Memory Optimizations**
  - Scalar replacement of aggregates (SROA)
  - Memory to register promotion
  - Load/store optimization
  - Memcpy optimization

#### 8.2.3 Target-Specific Optimizations (AArch64)

- [ ] Instruction combining
- [ ] Address mode optimization
- [ ] Conditional select/move instruction selection
- [ ] Load/store pair optimization
- [ ] AArch64 immediate encoding optimization

#### 8.2.4 Optimization Pipeline

- [ ] Pass manager infrastructure
- [ ] Pass ordering optimization
- [ ] Optimization level presets (-O0, -O1, -O2, -O3, -Os)

### 8.3 Deliverables

- Complete optimization pipeline
- Optimization level support
- Performance competitive with -O1/-O2

---

## 9. Phase 6: Advanced C++

### 9.1 Objectives

- Complete C++20 support
- Implement remaining language features
- Full template support

### 9.2 Tasks

#### 9.2.1 C++20 Features

- [ ] **Concepts**
  - Concept definitions
  - Requires expressions
  - Constraint normalization
  - Constraint satisfaction
  - Constrained template parameters

- [ ] **Coroutines** (optional)
  - Coroutine transformation
  - Promise type handling
  - co_await/co_yield/co_return

- [ ] **Modules** (optional)
  - Module declaration
  - Export declarations
  - Module partitions
  - Header units

- [ ] **Other C++20 Features**
  - Three-way comparison (<=>)
  - Designated initializers
  - consteval
  - constinit
  - char8_t
  - Lambda improvements
  - Range-based for with initializer

#### 9.2.2 Advanced Templates

- [ ] Variadic templates
- [ ] Pack expansion contexts
- [ ] Fold expressions
- [ ] Class template argument deduction (CTAD)
- [ ] Template template parameters
- [ ] Dependent templates

#### 9.2.3 Exception Handling

- [ ] Exception object management
- [ ] Stack unwinding
- [ ] Personality routines
- [ ] LSDA generation
- [ ] Cleanup actions

#### 9.2.4 RTTI

- [ ] Type info generation
- [ ] Dynamic_cast implementation
- [ ] Typeid implementation
- [ ] Vtable generation with RTTI

#### 9.2.5 Lambda Implementation

- [ ] Closure type generation
- [ ] Capture handling
- [ ] Generic lambdas
- [ ] Init captures
- [ ] Constexpr lambdas

#### 9.2.6 Virtual Functions

- [ ] Vtable layout
- [ ] Vptr initialization
- [ ] Virtual call generation
- [ ] Multiple inheritance vtables
- [ ] Virtual inheritance vtables
- [ ] Thunks

#### 9.2.7 Name Mangling

- [ ] Itanium C++ ABI mangling
- [ ] Template mangling
- [ ] Lambda mangling
- [ ] Local class mangling

### 9.3 Deliverables

- Complete C++20 compiler
- Full template metaprogramming support
- Exception handling support
- RTTI support

---

## 10. Phase 7: Standard Library

### 10.1 Objectives

- Support standard library compilation
- Implement compiler runtime
- Enable libc++ usage

### 10.2 Tasks

#### 10.2.1 Compiler Builtins

- [ ] **Type Traits Builtins**
  - `__is_class`
  - `__is_enum`
  - `__is_union`
  - `__is_trivially_copyable`
  - `__is_trivially_destructible`
  - `__is_standard_layout`
  - `__is_pod`
  - `__is_abstract`
  - `__is_base_of`
  - `__is_convertible`
  - `__underlying_type`
  - `__is_same`

- [ ] **Memory Builtins**
  - `__builtin_memcpy`
  - `__builtin_memset`
  - `__builtin_memmove`
  - `__builtin_alloca`
  - `__builtin_assume_aligned`

- [ ] **Atomic Builtins**
  - `__atomic_*` family
  - `__sync_*` family (legacy)

- [ ] **Other Builtins**
  - `__builtin_expect`
  - `__builtin_unreachable`
  - `__builtin_constant_p`
  - `__builtin_choose_expr`
  - Overflow builtins
  - Bit manipulation builtins

#### 10.2.2 Compiler Runtime

- [ ] Integer arithmetic routines (64-bit ops on 32-bit)
- [ ] Floating-point routines
- [ ] Exception handling runtime
- [ ] Stack unwinding
- [ ] Guard variable handling
- [ ] Thread-local storage support

#### 10.2.3 Standard Library Compatibility

- [ ] libc compatibility
- [ ] libc++ header compatibility
- [ ] libstdc++ header compatibility (optional)
- [ ] Attribute support for stdlib

### 10.3 Deliverables

- Complete builtin support
- Compiler runtime library
- Ability to compile libc++

---

## 11. Phase 8: Tooling

### 11.1 Objectives

- Enhanced developer experience
- Debug support
- IDE integration

### 11.2 Tasks

#### 11.2.1 Debug Information

- [ ] **DWARF Support**
  - Complete DWARF 4 generation
  - Line number information
  - Variable locations
  - Type information
  - Call frame information

- [ ] **Debug Features**
  - Source-level debugging
  - Optimized code debugging
  - Macro expansion tracking

#### 11.2.2 Diagnostics Enhancement

- [ ] Template instantiation backtraces
- [ ] Macro expansion traces
- [ ] Constraint satisfaction explanations
- [ ] Overload resolution notes
- [ ] Fix-it suggestions
- [ ] Static analysis warnings

#### 11.2.3 IDE Integration

- [ ] **Language Server Protocol (LSP)**
  - Completion
  - Go to definition
  - Find references
  - Hover information
  - Diagnostics
  - Code actions

- [ ] **Compile Commands**
  - compile_commands.json generation
  - Project configuration support

#### 11.2.4 Additional Tools

- [ ] **Preprocessor Output**
  - `-E` flag support
  - Macro expansion visualization

- [ ] **AST Dump**
  - `-ast-dump` flag
  - JSON AST output

- [ ] **IR Dump**
  - `-emit-llvm` equivalent
  - IR visualization

- [ ] **Assembly Output**
  - `-S` flag
  - Assembly annotation

### 11.3 Deliverables

- Complete DWARF debug information
- LSP server
- Developer tools

---

## 12. Phase 9: Self-Hosting

### 12.1 Objectives

- vcpp can compile itself
- Bootstrap validation
- Performance optimization

### 12.2 Tasks

#### 12.2.1 Self-Compilation

- [ ] Identify vcpp-specific constructs
- [ ] Ensure all used features are implemented
- [ ] Fix compilation issues
- [ ] Validate output correctness

#### 12.2.2 Bootstrap Process

```
┌─────────────────────────────────────────────────────────────┐
│  Stage 1: Build vcpp with host compiler (GCC/Clang)         │
│           vcpp.stage1 = GCC(vcpp.source)                    │
├─────────────────────────────────────────────────────────────┤
│  Stage 2: Build vcpp with Stage 1                           │
│           vcpp.stage2 = vcpp.stage1(vcpp.source)            │
├─────────────────────────────────────────────────────────────┤
│  Stage 3: Build vcpp with Stage 2                           │
│           vcpp.stage3 = vcpp.stage2(vcpp.source)            │
├─────────────────────────────────────────────────────────────┤
│  Validation: Compare Stage 2 and Stage 3 binaries           │
│              vcpp.stage2 == vcpp.stage3 (bit-identical)     │
└─────────────────────────────────────────────────────────────┘
```

#### 12.2.3 Performance Validation

- [ ] Compile time benchmarking
- [ ] Generated code benchmarking
- [ ] Memory usage profiling
- [ ] Comparison with GCC/Clang

#### 12.2.4 Viper OS Compilation

- [ ] Compile Viper OS kernel
- [ ] Compile Viper OS userland
- [ ] Compile Viper libc
- [ ] Boot and validate

### 12.3 Deliverables

- Self-hosting compiler
- Bootstrap validation
- Viper OS build capability

---

## 13. Testing Strategy

### 13.1 Test Categories

| Category | Description | Count Target |
|----------|-------------|--------------|
| Unit Tests | Individual component tests | 1000+ |
| Integration Tests | Multi-component tests | 500+ |
| Conformance Tests | Language standard tests | 5000+ |
| Regression Tests | Bug fix verification | Ongoing |
| Performance Tests | Compile time and code quality | 100+ |

### 13.2 Test Sources

- [ ] **Custom Test Suite**
  - Hand-written tests for each feature
  - Edge case coverage
  - Error message tests

- [ ] **External Test Suites**
  - GCC torture tests
  - LLVM test-suite
  - C-testsuite
  - cpp-testsuite (custom)

- [ ] **Real-World Code**
  - Viper OS kernel
  - Viper libc
  - SQLite
  - Lua
  - Various open-source projects

### 13.3 Test Infrastructure

- [ ] Test runner framework
- [ ] Expected output comparison
- [ ] Crash detection
- [ ] Timeout handling
- [ ] Parallel test execution
- [ ] Test result reporting
- [ ] Continuous integration

### 13.4 Coverage Requirements

| Phase | Minimum Coverage |
|-------|------------------|
| Phase 1 | 90% |
| Phase 2 | 85% |
| Phase 3 | 80% |
| Phase 4 | 75% |
| Phase 5 | 80% |
| Phase 6 | 75% |
| Phase 7 | 70% |
| Phase 8 | 70% |
| Phase 9 | N/A |

---

## 14. Project Structure

```
vcpp/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── requirements.md
├── specifications.md
├── implementation_plan.md
│
├── include/
│   └── vcpp/
│       ├── Basic/              # Core utilities
│       │   ├── Allocator.hpp
│       │   ├── StringRef.hpp
│       │   ├── SourceLocation.hpp
│       │   └── Diagnostic.hpp
│       │
│       ├── Lex/                # Lexer
│       │   ├── Token.hpp
│       │   ├── Lexer.hpp
│       │   └── Preprocessor.hpp
│       │
│       ├── Parse/              # Parser
│       │   ├── Parser.hpp
│       │   └── ParseDecl.hpp
│       │
│       ├── AST/                # Abstract Syntax Tree
│       │   ├── ASTNode.hpp
│       │   ├── Decl.hpp
│       │   ├── Stmt.hpp
│       │   ├── Expr.hpp
│       │   ├── Type.hpp
│       │   └── ASTContext.hpp
│       │
│       ├── Sema/               # Semantic Analysis
│       │   ├── Sema.hpp
│       │   ├── Scope.hpp
│       │   ├── Lookup.hpp
│       │   └── Overload.hpp
│       │
│       ├── IR/                 # Intermediate Representation
│       │   ├── Module.hpp
│       │   ├── Function.hpp
│       │   ├── BasicBlock.hpp
│       │   ├── Instruction.hpp
│       │   ├── Value.hpp
│       │   └── IRBuilder.hpp
│       │
│       ├── IRGen/              # IR Generation
│       │   ├── IRGen.hpp
│       │   └── CGDecl.hpp
│       │
│       ├── Opt/                # Optimization
│       │   ├── PassManager.hpp
│       │   ├── Pass.hpp
│       │   └── Passes.hpp
│       │
│       ├── CodeGen/            # Code Generation
│       │   ├── TargetMachine.hpp
│       │   ├── MachineInstr.hpp
│       │   ├── RegisterAlloc.hpp
│       │   └── AsmPrinter.hpp
│       │
│       └── Target/             # Target-specific (AArch64 only)
│           └── AArch64/
│               ├── AArch64Target.hpp
│               ├── AArch64AsmPrinter.hpp
│               └── AArch64InstrInfo.hpp
│       │
│       └── Driver/             # Compiler Driver
│           ├── Driver.hpp
│           └── Options.hpp
│
├── lib/
│   ├── Basic/
│   ├── Lex/
│   ├── Parse/
│   ├── AST/
│   ├── Sema/
│   ├── IR/
│   ├── IRGen/
│   ├── Opt/
│   ├── CodeGen/
│   └── Target/
│       └── AArch64/
│   └── Driver/
│
├── tools/
│   ├── vcpp/                   # Main compiler executable
│   │   └── main.cpp
│   ├── vcpp-pp/                # Standalone preprocessor
│   │   └── main.cpp
│   ├── vcpp-lex/               # Standalone lexer
│   │   └── main.cpp
│   └── vcpp-lsp/               # Language server
│       └── main.cpp
│
├── runtime/
│   ├── builtins/               # Compiler builtins
│   ├── crt/                    # C runtime
│   └── cxxrt/                  # C++ runtime
│
├── test/
│   ├── unit/                   # Unit tests
│   │   ├── Basic/
│   │   ├── Lex/
│   │   ├── Parse/
│   │   ├── AST/
│   │   ├── Sema/
│   │   ├── IR/
│   │   ├── Opt/
│   │   └── CodeGen/
│   │
│   ├── integration/            # Integration tests
│   │   ├── C/
│   │   └── CXX/
│   │
│   ├── conformance/            # Language conformance tests
│   │   ├── C/
│   │   └── CXX/
│   │
│   └── benchmark/              # Performance tests
│
├── docs/
│   ├── Design.md
│   ├── IR.md
│   ├── ABI.md
│   └── Internals/
│
└── cmake/
    ├── VcppConfig.cmake
    └── modules/
```

---

## 15. Dependencies

### 15.1 Build Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| CMake | 3.20+ | Build system |
| C++ Compiler | C++20 | Building vcpp |
| Python | 3.8+ | Test runner, scripts |
| Ninja | 1.10+ | Build tool (optional) |

### 15.2 Runtime Dependencies

| Dependency | Purpose |
|------------|---------|
| GNU Binutils | Assembler and linker |
| LLVM tools | Alternative assembler/linker (optional) |

### 15.3 Test Dependencies

| Dependency | Purpose |
|------------|---------|
| GoogleTest | Unit testing |
| lit | LLVM-style test runner |
| FileCheck | Output verification |

### 15.4 Optional Dependencies

| Dependency | Purpose |
|------------|---------|
| libffi | Foreign function interface |
| zlib | Compressed debug info |
| libxml2 | XML output |

---

## 16. Milestones

### 16.1 Milestone Summary

| Milestone | Target | Description |
|-----------|--------|-------------|
| M1 | Phase 1 Complete | Project infrastructure ready |
| M2 | C Preprocessor | Full C17 preprocessor |
| M3 | C Parser | Full C17 parser with AST |
| M4 | C Compiler | Working C17 compiler |
| M5 | C++ Parser | C++17 parser |
| M6 | C++ Compiler | Working C++17 compiler |
| M7 | Optimizations | -O2 performance level |
| M8 | C++20 | Full C++20 support |
| M9 | Self-Hosting | vcpp compiles vcpp |
| M10 | Viper OS | vcpp builds Viper OS |

### 16.2 Milestone Criteria

#### M1: Project Infrastructure
- [ ] Build system working
- [ ] Test framework working
- [ ] CI/CD pipeline active
- [ ] Core utilities implemented
- [ ] Diagnostic system functional

#### M2: C Preprocessor
- [ ] All directives implemented
- [ ] Macro expansion correct
- [ ] Include handling working
- [ ] Conditional compilation working
- [ ] Preprocessor test suite passing

#### M3: C Parser
- [ ] All C17 syntax parsed
- [ ] AST construction complete
- [ ] Error recovery functional
- [ ] Parser test suite passing

#### M4: C Compiler
- [ ] Semantic analysis complete
- [ ] IR generation working
- [ ] AArch64 backend functional (AAPCS64 ABI)
- [ ] ELF64 object file generation working
- [ ] Can compile simple C programs for AArch64
- [ ] Can compile SQLite (cross-compiled for AArch64)
- [ ] Can compile Viper libc

#### M5: C++ Parser
- [ ] C++17 syntax parsed
- [ ] Template parsing working
- [ ] Name lookup implemented
- [ ] Parser test suite passing

#### M6: C++ Compiler
- [ ] Overload resolution working
- [ ] Template instantiation working
- [ ] Class semantics complete
- [ ] Can compile simple C++ programs
- [ ] Can compile moderately complex templates

#### M7: Optimizations
- [ ] All -O2 passes implemented
- [ ] Performance within 80% of Clang -O2
- [ ] Compile time reasonable

#### M8: C++20 Complete
- [ ] All C++20 features implemented
- [ ] Concepts working
- [ ] consteval/constinit working
- [ ] Designated initializers working

#### M9: Self-Hosting
- [ ] vcpp compiles vcpp successfully
- [ ] Three-stage bootstrap validates
- [ ] Generated compiler passes all tests

#### M10: Viper OS
- [ ] Kernel compiles with vcpp
- [ ] Userland compiles with vcpp
- [ ] libc compiles with vcpp
- [ ] OS boots successfully
- [ ] All OS tests pass

---

## Appendix A: Risk Assessment

### A.1 Technical Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Template complexity | High | Incremental implementation, extensive testing |
| Code generation bugs | High | Differential testing against GCC/Clang |
| Performance issues | Medium | Profile-guided optimization, benchmarking |
| ABI compatibility | Medium | Strict adherence to platform ABI specs |
| Debug info complexity | Medium | Incremental DWARF support |

### A.2 Schedule Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Underestimated complexity | High | Buffer time in estimates, MVP approach |
| Scope creep | Medium | Strict phase boundaries, feature prioritization |
| Testing overhead | Medium | Automated testing, CI/CD |

---

## Appendix B: References

### B.1 Language Standards
- ISO/IEC 9899:2018 (C17)
- ISO/IEC 14882:2020 (C++20)

### B.2 ABI Specifications
- System V AMD64 ABI
- ARM Architecture Procedure Call Standard (AAPCS64)
- Itanium C++ ABI

### B.3 File Format Specifications
- ELF Specification
- DWARF Debugging Standard

### B.4 Related Projects
- LLVM/Clang
- GCC
- TCC (Tiny C Compiler)
- chibicc
- cproc
- 8cc
- lacc

---

*Viper C++ Compiler Implementation Plan v1.0*
