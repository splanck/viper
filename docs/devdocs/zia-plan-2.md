# Zia Implementation Plan v2

## Version 0.2 - Complete Language Implementation

**Status:** Active Development
**Last Updated:** December 2024
**Goal:** 100% Fully Mature Zia Implementation

---

## Executive Summary

Zia is currently ~45% complete with a working core that supports basic programs, entities, value types, and Viper.* runtime integration. This plan takes us from the current state to a production-ready language with full type system, concurrency, modules, and all planned features.

### Current State (Phase 1 Complete)
- Basic lexer, parser, AST, and lowerer
- Core types: Integer, Number, Text, Boolean
- Entity and value types with methods
- Control flow: if/else, while, for-in, match, guard
- Collections via Viper.Collections.*
- Terminal I/O via Viper.Terminal.*
- Demo applications: Frogger, entities

### Target State (Phase 5 Complete)
- Full type system with generics specialization
- Complete async/await concurrency model
- Multi-file module system with imports
- Result/Option types with comprehensive error handling
- Virtual dispatch and proper inheritance
- Complete standard library integration

---

## Implementation Phases

### Legend
- [x] Complete
- [~] Partial (works but needs refinement)
- [ ] Not started

---

## Phase 1: Core Language Foundation [COMPLETE]

### 1.1 Lexer & Token System [x]
- [x] Token kinds (80+ types)
- [x] Keywords (30+ reserved words)
- [x] Operators (arithmetic, comparison, logical, bitwise)
- [x] Literals (integers, floats, strings, booleans)
- [x] Identifiers with Unicode support
- [x] Comments (line //, block /* */)
- [x] String escape sequences
- [x] Source location tracking

### 1.2 Parser & AST [x]
- [x] Module declarations
- [x] Import statements (path and dotted)
- [x] Function declarations with parameters
- [x] Return types and type annotations
- [x] Variable declarations (Java-style: `Type name = expr;`)
- [x] Expression parsing with precedence climbing
- [x] Statement parsing
- [x] Block expressions

### 1.3 Basic Types [x]
- [x] Integer (64-bit signed)
- [x] Number (64-bit float)
- [x] Text (UTF-8 strings)
- [x] Boolean (true/false)
- [x] Null literal
- [x] Unit type ()

### 1.4 Operators [x]
- [x] Arithmetic: + - * / %
- [x] Comparison: == != < > <= >=
- [x] Logical: && || !
- [x] Bitwise: & | ^ ~ << >>
- [x] Assignment: = += -= *= /=
- [x] Ternary: condition ? then : else
- [x] Null coalescing: ??

### 1.5 Control Flow [x]
- [x] If/else statements
- [x] If expressions (with required else)
- [x] While loops
- [x] For-in loops with ranges
- [x] Break and continue
- [x] Guard statements (early exit)
- [x] Match statements

### 1.6 Functions [x]
- [x] Global function declarations
- [x] Function calls with positional args
- [x] Named argument syntax
- [x] Return statements
- [x] Implicit return
- [x] Void functions

### 1.7 Entity Types [x]
- [x] Entity declaration
- [x] Field declarations (Java-style)
- [x] Method declarations
- [x] Self/this access
- [x] Constructor via `new`
- [x] Field access and assignment
- [x] Method calls
- [x] Heap allocation via rt_alloc

### 1.8 Value Types [x]
- [x] Value declaration
- [x] Field declarations
- [x] Methods
- [x] Copy semantics
- [x] Stack allocation

### 1.9 Viper.* Runtime Integration [x]
- [x] Viper.Terminal (Say, Print, GetKey, etc.)
- [x] Viper.Collections.List (add, get, Count)
- [x] Viper.Time (SleepMs via Clock.Sleep)
- [x] Runtime function lookup
- [x] Automatic boxing for collections

---

## Phase 2: Type System & Inheritance [IN PROGRESS]

### 2.1 Inheritance [~]
- [x] Extends clause parsing
- [x] Basic super.method() calls
- [ ] **Virtual method dispatch (vtable)**
- [ ] **Method override verification**
- [ ] **Constructor chaining**
- [ ] **Proper field inheritance layout**

### 2.2 Interfaces [~]
- [x] Interface declaration parsing
- [x] Implements clause parsing
- [x] Method signature validation
- [ ] **Interface method dispatch**
- [ ] **Multiple interface implementation**
- [ ] **Default method implementations**

### 2.3 Optional Types [~]
- [x] T? syntax parsing
- [x] Null coalescing (??)
- [x] Optional type construction
- [ ] **Optional chaining (?.) implementation**
- [ ] **If-let syntax**
- [ ] **While-let syntax**
- [ ] **Safe unwrapping patterns**

### 2.4 Generics [~]
- [x] Generic type parameter syntax
- [x] Generic type parsing (List[T], Map[K,V])
- [ ] **Generic specialization**
- [ ] **Type constraint syntax (where clauses)**
- [ ] **Variance annotations**
- [ ] **Generic method instantiation**

### 2.5 Type Inference
- [x] Basic type inference from literals
- [x] Type inference from function returns
- [ ] **Bidirectional type inference**
- [ ] **Flow-sensitive typing**
- [ ] **Smart casts after type checks**

### 2.6 Type Checking
- [x] Basic type compatibility
- [x] Is/as operators
- [ ] **Exhaustive match checking**
- [ ] **Nullable type safety enforcement**
- [ ] **Assignment compatibility checks**
- [ ] **Function signature matching**

---

## Phase 3: Advanced Language Features

### 3.1 Result Types & Error Handling
- [ ] Result[T, E] type definition
- [ ] Ok() and Error() constructors
- [ ] Try operator (?) for Result propagation
- [ ] Error type hierarchy
- [ ] Match on Result variants
- [ ] Panic semantics

### 3.2 Pattern Matching [~]
- [x] Literal patterns (int, string, bool)
- [x] Binding patterns (name)
- [x] Wildcard patterns (_)
- [ ] **Constructor patterns (Some(x), None)**
- [ ] **Tuple patterns ((a, b, c))**
- [ ] **Guard expressions (where clause)**
- [ ] **Exhaustiveness checking**
- [ ] **Or patterns (a | b)**

### 3.3 Closures & Lambdas [~]
- [x] Lambda expression syntax
- [x] Lambda parameter types
- [x] Basic lambda compilation
- [ ] **Closure environment capture**
- [ ] **Closure type representation**
- [ ] **Higher-order functions**
- [ ] **Function references**

### 3.4 Tuples
- [ ] Tuple type syntax ((A, B, C))
- [ ] Tuple literal syntax ((a, b, c))
- [ ] Tuple element access (.0, .1, .2)
- [ ] Tuple destructuring
- [ ] Unit tuple ()

### 3.5 String Features [~]
- [x] String literals
- [x] Escape sequences
- [x] String concatenation (+)
- [x] String interpolation (parsed, desugared)
- [ ] **Multi-line strings**
- [ ] **Raw strings**
- [ ] **String formatting API**

### 3.6 Collections Integration [~]
- [x] List via Viper.Collections.List
- [x] new List() constructor
- [x] add(), get(), Count
- [ ] **List literal syntax [1, 2, 3]**
- [ ] **Map via Viper.Collections.Map**
- [ ] **Set via Viper.Collections.Bag**
- [ ] **Collection iteration (for x in list)**
- [ ] **Indexer syntax list[i]**

### 3.7 Range Expressions [~]
- [x] Exclusive ranges (0..10)
- [x] Inclusive ranges (0..=10)
- [x] For-in with ranges
- [ ] **Range as first-class type**
- [ ] **Range slicing for collections**

---

## Phase 4: Module System & Visibility

### 4.1 Module System
- [x] Module declaration parsing
- [x] Import statement parsing
- [ ] **Module file resolution**
- [ ] **Multi-file compilation**
- [ ] **Circular dependency detection**
- [ ] **Module caching**
- [ ] **Precompiled module format**

### 4.2 Symbol Resolution
- [x] Local symbol lookup
- [x] Function lookup in current module
- [ ] **Cross-module symbol resolution**
- [ ] **Qualified name resolution (Mod.Type.method)**
- [ ] **Import aliasing (import X as Y)**
- [ ] **Selective imports (import X.{a, b})**

### 4.3 Visibility [~]
- [x] expose/hide keywords parsed
- [x] Visibility enum in AST
- [ ] **Public/private enforcement**
- [ ] **Module-level visibility**
- [ ] **Protected visibility for inheritance**
- [ ] **Internal visibility within package**

### 4.4 Name Resolution
- [x] Local variables
- [x] Function parameters
- [x] Entity fields
- [ ] **Shadowing rules**
- [ ] **Capture analysis for closures**
- [ ] **Forward reference handling**

---

## Phase 5: Concurrency

### 5.1 Thread Support
- [ ] Thread creation syntax
- [ ] Thread.Start() integration
- [ ] Thread.Join() support
- [ ] Thread.Sleep() support
- [ ] Thread-local storage

### 5.2 Synchronization
- [ ] Monitor.Enter/Exit
- [ ] Synchronized blocks
- [ ] Safe atomic operations
- [ ] Lock statement sugar

### 5.3 Async/Await [Future]
- [ ] Async function declarations
- [ ] Await expressions
- [ ] Task type integration
- [ ] Parallel execution primitives
- [ ] Structured concurrency

### 5.4 Channels [Future]
- [ ] Channel type syntax
- [ ] Send/receive operations
- [ ] Buffered channels
- [ ] Select statement

---

## Phase 6: Standard Library Integration

### 6.1 Core Runtime [~]
- [x] Viper.Terminal - Console I/O
- [x] Viper.Collections.List - Dynamic arrays
- [ ] Viper.Collections.Map - Hash maps
- [ ] Viper.Collections.Bag - String sets
- [ ] Viper.Collections.Queue - FIFO queues
- [ ] Viper.Collections.Stack - LIFO stacks
- [ ] Viper.Collections.Seq - General sequences

### 6.2 Math & Utilities [~]
- [ ] Viper.Math - Math functions
- [ ] Viper.Random - Random numbers
- [ ] Viper.Convert - Type conversions
- [ ] Viper.Fmt - Formatting

### 6.3 File I/O
- [ ] Viper.IO.File - File operations
- [ ] Viper.IO.Dir - Directory operations
- [ ] Viper.IO.Path - Path manipulation
- [ ] Viper.IO.LineReader/Writer

### 6.4 Date/Time
- [ ] Viper.DateTime - Date/time operations
- [ ] Viper.Time.Clock - Timing
- [ ] Viper.Diagnostics.Stopwatch

### 6.5 Text Processing
- [ ] Viper.String methods
- [ ] Viper.Text.StringBuilder
- [ ] Viper.Text.Codec - Encoding
- [ ] Viper.Text.Csv - CSV parsing

### 6.6 Crypto & Hashing
- [ ] Viper.Crypto.Hash (MD5, SHA1, SHA256)
- [ ] Viper.Text.Guid

### 6.7 System
- [ ] Viper.Environment
- [ ] Viper.Machine
- [ ] Viper.Exec - Process execution
- [ ] Viper.Log - Logging

### 6.8 Graphics [Optional]
- [ ] Viper.Graphics.Canvas
- [ ] Viper.Graphics.Color
- [ ] Viper.Vec2/Vec3

---

## Phase 7: Quality & Polish

### 7.1 Error Messages
- [ ] Source location in errors
- [ ] Contextual error messages
- [ ] Error recovery in parser
- [ ] Suggestion system

### 7.2 Testing Infrastructure
- [ ] Zia test keyword
- [ ] Test runner integration
- [ ] Assert functions
- [ ] Test discovery

### 7.3 Documentation
- [ ] Language specification complete
- [ ] Standard library documentation
- [ ] Tutorial and examples
- [ ] API reference generation

### 7.4 Tooling
- [ ] Syntax highlighting definitions
- [ ] LSP integration basics
- [ ] REPL improvements
- [ ] Debugger support

---

## Detailed Task Breakdown

### Immediate Priority (Next 2 Weeks)

#### Task 1: Virtual Method Dispatch
**Files:** `Lowerer.cpp`, `Compiler.cpp`
**Description:** Implement proper vtable-based virtual dispatch for entity methods.

1. Generate vtable structure for each entity type
2. Store vtable pointer in object header
3. Emit indirect calls through vtable for virtual methods
4. Handle override correctly in inheritance chain
5. Add tests for polymorphic dispatch

#### Task 2: Optional Chaining (?.)
**Files:** `Lowerer.cpp`
**Description:** Implement the optional chaining operator.

1. Lower OptionalChainExpr properly
2. Generate null check before member access
3. Propagate null if receiver is null
4. Handle chained access (a?.b?.c)
5. Add tests for optional chaining

#### Task 3: If-Let and While-Let
**Files:** `Parser.cpp`, `Lowerer.cpp`
**Description:** Pattern matching sugar for optionals.

1. Parse if-let syntax: `if let x = optionalExpr { ... }`
2. Parse while-let syntax: `while let x = optionalExpr { ... }`
3. Lower to null check + binding
4. Add tests

#### Task 4: Collection Indexer Syntax
**Files:** `Parser.cpp`, `Lowerer.cpp`
**Description:** Support list[i] and map[key] syntax.

1. Parse index expressions
2. Lower to get_Item/set_Item calls for List
3. Lower to Get/Set calls for Map
4. Handle assignment: list[i] = value
5. Add tests

#### Task 5: For-In with Collections
**Files:** `Lowerer.cpp`
**Description:** Iterate over collections with for-in.

1. Detect collection type in for-in
2. Generate iterator pattern (index loop for List)
3. Handle Map iteration via Keys()
4. Add tests for collection iteration

### Medium Priority (Next Month)

#### Task 6: Result Type Implementation
1. Define Result[T, E] as built-in type
2. Implement Ok() and Error() constructors
3. Implement ? operator for Result propagation
4. Add match support for Result variants
5. Document error handling patterns

#### Task 7: Constructor Patterns
1. Parse constructor patterns: `Some(x)`, `Ok(value)`
2. Implement pattern matching for ADT variants
3. Add exhaustiveness checking foundation
4. Add tests for constructor patterns

#### Task 8: Multi-File Module System
1. Implement module file resolution
2. Build module dependency graph
3. Compile modules in dependency order
4. Cache compiled modules
5. Handle circular dependencies

#### Task 9: Generic Specialization
1. Track generic type parameters through lowering
2. Generate specialized code for concrete types
3. Handle generic method calls
4. Add tests for generics

#### Task 10: Visibility Enforcement
1. Track visibility in semantic analysis
2. Report errors for private access violations
3. Handle expose/hide properly
4. Add cross-module visibility tests

### Lower Priority (Future)

#### Task 11: Closure Environment Capture
- Implement proper closure capture analysis
- Generate environment struct for captured variables
- Handle mutable capture correctly

#### Task 12: Async/Await
- Design async function representation
- Implement Task type
- Generate state machine for async functions
- Implement await lowering

#### Task 13: Thread Integration
- Thread creation API
- Synchronization primitives
- Thread-safe collections

#### Task 14: Test Framework
- Test keyword and discovery
- Assertion helpers
- Test runner

---

## Architecture Notes

### Current Lowering Strategy
- Slot-based allocation for mutable variables
- SSA form with block parameters
- Direct runtime function calls
- Boxing for generic collections

### Type System Architecture
- SemanticType for resolved types
- TypePtr in AST for parsed types
- Type inference during semantic analysis
- No dependent types planned

### Memory Model
- Value types: stack allocated, copied on assignment
- Entity types: heap allocated, reference counted
- Weak references: tracked separately
- No manual memory management exposed

---

## Testing Requirements

Each feature must have:
1. Unit tests for parser (if applicable)
2. Unit tests for lowerer
3. Integration tests (compile + run)
4. Golden tests for IL output stability

---

## Success Criteria

### Phase 2 Complete When:
- Virtual dispatch works for all entity methods
- Optional chaining compiles and runs correctly
- Generic types can be instantiated
- All Phase 2 tests pass

### Phase 3 Complete When:
- Result types work end-to-end
- Pattern matching supports constructors
- Closures capture variables correctly
- Collection syntax works idiomatically

### Phase 4 Complete When:
- Multi-file programs compile correctly
- Module imports resolve properly
- Visibility is enforced at compile time

### Phase 5 Complete When:
- Thread creation works
- Synchronization primitives work
- Programs can use multiple threads safely

### Full Completion When:
- All Viper.* runtime APIs accessible from Zia
- Demo applications demonstrate all features
- Documentation complete
- No known critical bugs

---

## Appendix: File Map

### Core Frontend Files
- `src/frontends/zia/Lexer.cpp` - Tokenization
- `src/frontends/zia/Parser.cpp` - Parsing to AST
- `src/frontends/zia/AST.hpp` - AST node definitions
- `src/frontends/zia/Lowerer.cpp` - AST to IL lowering
- `src/frontends/zia/Compiler.cpp` - Entry point

### Test Files
- `src/tests/frontends/zia/ZiaCompilerTests.cpp`

### Demo Files
- `demos/zia/entities.zia` - Entity definitions
- `demos/zia/frogger.zia` - Game demo

### Documentation
- `docs/devdocs/Zia_v0.1_RC1_Specification.md`
- `docs/devdocs/Zia_v0.1_RC1_Quick_Reference.md`
- `docs/devdocs/Zia_Complete_Implementation_Plan.md` (previous)
- `docs/devdocs/zia-plan-2.md` (this file)

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| Dec 2024 | 2.0 | Initial v2 plan with comprehensive status audit |
