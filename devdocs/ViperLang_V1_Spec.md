# ViperLang — Final V1 Specification

**Version:** 1.0 (Frozen)  
**Scope:** Language semantics, core constructs, and normative tooling for V1.  
**Audience:** Compiler/runtime implementers, library authors, and developers adopting ViperLang.

> **Design Intent (non-normative):** ViperLang is a modern, statically typed language with clear rules, safe defaults, and a compact surface area. The goal is to be simple for everyday work and predictable for tools, while leaving room to evolve without breaking core laws.

---

## Table of Contents

1. [Design Goals (Non-Normative)](#1-design-goals-non-normative)
2. [Surface Syntax](#2-surface-syntax)  
   2.1. [Files, Modules, Imports](#21-files-modules-imports)  
   2.2. [Blocks, Braces, Semicolons](#22-blocks-braces-semicolons)  
   2.3. [Comments](#23-comments)
3. [Types & Values](#3-types--values)  
   3.1. [Kinds of Types](#31-kinds-of-types)  
   3.2. [Special Types](#32-special-types)  
   3.3. [Nullability](#33-nullability)  
   3.4. [Values](#34-values)  
   3.5. [Entities](#35-entities)  
   3.6. [`inout` Parameters & Projections](#36-inout-parameters--projections)  
   3.7. [Functions & Closures](#37-functions--closures)  
   3.8. [Named Arguments](#38-named-arguments)
4. [Objects, Inheritance, Contracts & Traits](#4-objects-inheritance-contracts--traits)  
   4.1. [Inheritance](#41-inheritance)  
   4.2. [Contracts (Interfaces)](#42-contracts-interfaces)  
   4.3. [Traits (Default Implementations)](#43-traits-default-implementations)  
   4.4. [Annotations](#44-annotations)
5. [Visibility & Shared Members](#5-visibility--shared-members)  
   5.1. [Access Control Levels](#51-access-control-levels)  
   5.2. [Shared Members](#52-shared-members)
6. [Errors & Control Flow](#6-errors--control-flow)  
   6.1. [Error Model](#61-error-model)  
   6.2. [`run / mediate / handle`](#62-run--mediate--handle)
7. [Numerics](#7-numerics)  
   7.1. [Built-in Numeric Types](#71-built-in-numeric-types)  
   7.2. [Literals & Conversions](#72-literals--conversions)  
   7.3. [Overflow & Operators](#73-overflow--operators)  
   7.4. [Division & Remainder](#74-division--remainder)  
   7.5. [Decimal Context](#75-decimal-context)  
   7.6. [Approximate Equality](#76-approximate-equality)
8. [Expressions & Operators](#8-expressions--operators)  
   8.1. [Operators via Contracts](#81-operators-via-contracts)
9. [Equality, Identity & Hashing](#9-equality-identity--hashing)
10. [Collections & Iteration](#10-collections--iteration)
11. [Generics](#11-generics)
12. [Sum Types & Pattern Matching](#12-sum-types--pattern-matching)
13. [Concurrency](#13-concurrency)  
    13.1. [Memory Model (Safe Code)](#131-memory-model-safe-code)  
    13.2. [Tasks](#132-tasks)  
    13.3. [Locks (Resources)](#133-locks-resources)  
    13.4. [Atomics](#134-atomics)  
    13.5. [Channels](#135-channels)
14. [Resources & `using`](#14-resources--using)
15. [Text & Strings](#15-text--strings)
16. [Operators, Contracts & Comparable](#16-operators-contracts--comparable)
17. [Testing & Tooling](#17-testing--tooling)
18. [Standard Library Boundaries](#18-standard-library-boundaries)
19. [Not in V1](#19-not-in-v1)
20. [Core Semantics Appendix (Authoritative Summary)](#20-core-semantics-appendix-authoritative-summary)
21. [Representative Examples](#21-representative-examples)

---

## 1. Design Goals (Non-Normative)

- **Simplicity first.** The common case is trivial; the hard case is possible.
- **Radical consistency.** One way to do a thing; patterns beat exceptions.
- **Readable by humans, predictable for tools.** Formatter/linter are normative.
- **AI-friendly.** No exotic corners; semantics are regular.
- **Modern by default.** No legacy baggage in the core runtime.

---

## 2. Surface Syntax

### 2.1 Files, Modules, Imports

- Source files are UTF-8.
- A file may contain:
    - One `module` declaration (**required**).
    - Any number of `import` declarations.
    - Declarations: `entity`, `value`, `contract`, `trait`.
    - **No top-level executable statements.**

```viper
module MyApp.Banking;

import Viper.Data.Json;
import MyApp.Models.Customer;
```

- **Module = namespace = visibility boundary.**
- **Import cycles** are compile-time errors. The compiler reports the shortest cycle with module names and file locations (up to 5 cycles).

### 2.2 Blocks, Braces, Semicolons

- Blocks are delimited by `{}`.
- **Semicolons are required** to terminate statements. The formatter enforces this.
- Indentation and spacing are formatter-controlled; the formatter is normative.
- **Note:** All code examples in this document are intended to be valid V1 code; any missing semicolons are considered errata. The formatter enforces semicolons uniformly.

### 2.3 Comments

- `//` line comments; `/* … */` block comments; block comments may nest.

---

## 3. Types & Values

### 3.1 Kinds of Types

- **Values** (copy semantics): small data; copy on assignment and call.
- **Entities** (reference semantics): identity-bearing objects.
- **Sum types** (algebraic variants): modeled via `value` unions.
- **Function types**: `(A, B) -> R`.

### 3.2 Special Types

- `Any`: top type; all values conform.
- `Boolean`: logical truth type with literals `true` and `false`.
- `Void`: unit result type of actions with no explicit `->`.
- `StackTrace`: opaque runtime type representing a captured call stack.

### 3.3 Nullability

- References are **non-nullable by default**.
- Optional references use `T?`. The unique absent value is `nothing`.
- `nothing` can only inhabit `T?`, never `T`.
- Presence test: `x exists` is true iff `x != nothing`.

Safe navigation and coalescing:

```viper
file?.dispose();
name = user?.getName() ?? "Unknown";
```

Use `T?` for lightweight presence checks. Use `Option[T] = Some | None` when explicit pattern matching is desired.

### 3.4 Values

```viper
value Point {
    exposed attribute x: Number;
    exposed attribute y: Number;
}
```

- Values are **mutable locally** but **copy on assignment and call**.
- Mutating a **temporary** or an **accessor return** is **illegal**:
    - `getPoint().x = 10;`  // compile error
    - `box.corner.x = 10;`  // compile error

Use one of the following:

1. **Copy-modify-assign back**
```viper
temp = box.corner;
temp.x = 10;
box.corner = temp;
```

2. **`with` sugar** (desugars to copy + block + assign back)
```viper
box.corner = box.corner.with { 
    x += 5; 
    y -= 2; 
};
```

3. **`inout` projection** with exclusivity (see §3.6)
```viper
translate(inout box.corner, 5, 10);
```

**`with {}` sugar:** `x = x.with { /* mutate fields */ };` copies `x` to a temporary, executes the block (no `return` value; `raise` propagates), and assigns the mutated copy back to `x`. The block sees the copy's fields as ordinary mutable locals.

**Equality for values** is **structural**; `hash` is derived from attributes.  
**Pure values (V1 rule):** Attributes of a `value` must be **value types** only.

### 3.5 Entities

```viper
entity Customer {
    exposed attribute name: Text;
    internal attribute id: Number;
}
```

- **Reference semantics** and **identity**.
- Entities are **sealed by default**; mark `expandable` to allow inheritance.
- **Members are `final` by default**; mark `overridable` explicitly.
- **Definite assignment:** all non-optional attributes must be assigned by the end of the initializer (or declared with defaults).

**Stored vs Computed Attributes:**

```viper
entity Example {
    // Stored attribute (can be used with inout)
    exposed attribute position: Point;
    
    // Computed attribute (cannot be used with inout)
    exposed attribute center: Point {
        get { return calculateCenter(); }
        set { recalculateFromCenter(value); }
    }
}
```

- Only **stored attributes** can be passed to `inout` parameters.
- Computed attributes use explicit `get`/`set` blocks.

#### Initializers

- Entities may define an `initializer(parameters) { ... }` block.
- All non-optional attributes must be definitely assigned by the end of the initializer.
- Parameter evaluation order is left-to-right. Default values may reference earlier parameters but not later ones.
- If no initializer is declared, a synthesized initializer is available when all attributes have defaults or are optional.

```viper
entity Person {
    exposed attribute name: Text;
    exposed attribute age: Number;
    
    initializer(required name: Text, age: Number = 0) {
        this.name = name;
        this.age = age;
    }
}
```

### 3.6 `inout` Parameters & Projections

```viper
action translate(inout p: Point, dx: Number, dy: Number) {
    p.x = p.x + dx;
    p.y = p.y + dy;
}
```

- `inout` accepts **only l-values**: locals, parameters, and **stored** attributes.
- Computed accessors, calls, and indexers are illegal as `inout` targets.
- Passing `inout box.corner` is legal **only** if `corner` is a stored attribute.
- **Exclusivity:** the call establishes an **exclusive borrow** of the containing storage for the call's duration (copy-in, mutate, write-back). Overlapping reads/writes are rejected statically or trap in safe code at runtime.
- **Concurrency & inout:** `inout` guarantees exclusive access within the calling task (aliasing & write-back). It does not synchronize across tasks; use locks/channels/atomics for inter-task coordination.

### 3.7 Functions & Closures

- Function parameters are **non-escaping** by default; mark `@escaping` to store or return them.
- Stored function attributes are inherently escaping (no annotation).
- Closures capture **values by copy**, **entities by reference**. Use `entity.copy()` to snapshot an entity.

```viper
entity Handler {
    internal attribute h: (Event) -> Void;

    exposed action set(@escaping f: (Event) -> Void) { 
        this.h = f;
    }

    exposed action forEach(f: (Event) -> Void) {
        f(current());
    }
}
```

### 3.8 Named Arguments

Arguments may be passed by name (`f(x: 1, y: 2);`). Names are optional unless specified as required by the declaration. When names are used, they may appear in any order after positional arguments. Defaults are applied after evaluating earlier parameters (left-to-right).

---

## 4. Objects, Inheritance, Contracts & Traits

### 4.1 Inheritance

```viper
expandable entity Account {
    inherited attribute balance: Decimal;
    inherited overridable action calculateFees() -> Decimal { 
        return 0.00d;
    }
}

entity SavingsAccount expands Account {
    internal attribute interestRate: Decimal;
    inherited override action calculateFees() -> Decimal {
        baseFees = super.calculateFees();
        return baseFees + 5.00d;
    }
}
```

- `super.m()` is valid only inside an `override` of `m()` and calls the **immediate base** implementation.
- Using `inherited`/`overridable` on non-`expandable` entities is a compile error.
- Forgetting `override` when overriding is a compile error.

### 4.2 Contracts (Interfaces)

```viper
contract Drawable {
    action draw();
    action getBounds() -> Rectangle;
}
```

- Contracts declare **requirements only** (no state, no storage).
- **Values may implement contracts. Only entities may `with` traits.**

### 4.3 Traits (Default Implementations)

```viper
contract Printable { 
    action print(); 
}

trait DrawableDefaults {
    default action render(self: Drawable) { 
        self.draw();
    }
}

trait PrintableDefaults {
    default action render(self: Printable) { 
        self.print();
    }
}

entity Widget implements Drawable, Printable
                with DrawableDefaults, PrintableDefaults {

    exposed action draw() { /* ... */ }
    exposed action getBounds() -> Rectangle { /* ... */ }
    exposed action print() { /* ... */ }

    // Conflicting defaults: must resolve/qualify
    exposed override action render() { 
        DrawableDefaults.render(self);
    }
}
```

- Traits provide **default implementations** and **may introduce new actions** to the implementing entity.
- Trait code may only call members **declared by its contracts**.
- If multiple traits provide the **same action**, the entity must **`override`** or **qualify** the chosen default.
- **Values may implement contracts. Only entities may `with` traits.**

### 4.4 Annotations

```viper
// @frozen: marks a value type as semantically immutable
// Suppresses warnings when used as Map keys
@frozen value Currency { 
    exposed attribute code: Text;
}

// @test: marks test actions
entity CalculatorTests {
    @test
    action addition_works() {
        calc = new Calculator();
        assert calc.add(2, 3) == 5;
    }
}

// @deprecated: marks deprecated members
@deprecated
exposed action oldMethod() { /* ... */ }

// @escaping: marks escaping function parameters (shown in §3.7)
```

---

## 5. Visibility & Shared Members

### 5.1 Access Control Levels

- `internal` (default): only this entity.
- `module`: any code in the same module.
- `inherited`: this entity and its expanders (**only** on `expandable` entities).
- `exposed`: public API.

### 5.2 Shared Members

```viper
entity Math {
    shared final attribute PI: Number = 3.14159;
    shared action max(a: Number, b: Number) -> Number { 
        return a > b ? a : b;
    }
}
```

- Shared members are **not** implicitly synchronized.
- **Shared initialization** is **lazy** and **thread-safe**. Order across modules is unspecified except for explicit dependencies. Cross-module dependency cycles are compile errors.

---

## 6. Errors & Control Flow

### 6.1 Error Model

```viper
entity Error {
    shared enum Type { 
        Network, File, Invalid, Permission, Timeout, Cancelled, Custom 
    }
    exposed attribute type: Type;
    exposed attribute message: Text;
    exposed attribute data: Map[Text, Any]?;
    exposed attribute cause: Error?;
    exposed attribute stack: StackTrace;
}
```

- Error stack collection is controlled by `Viper.Errors.captureStacks`:
    - `true` in dev/test; `lazy` in optimized builds.

**Raising Errors:**  
`raise <ErrorExpr>;` unwinds to the nearest matching `mediate`. Unhandled errors propagate across action boundaries. `rethrow();` preserves original `stack` and `cause`.

### 6.2 `run / mediate / handle`

```viper
run {
    file = File.open(path);
    data = file.read();
    process(data);
} mediate e where e.type == Error.Type.File {
    log("File error: " + e.message);
    useDefaultData();
} mediate {
    log("Unexpected error: " + error.message);
    rethrow();
} handle {
    file?.dispose();
}
```

- `mediate` blocks are tested **in source order**; first match wins.
- `handle` runs **exactly once** on all exit paths (success, `raise`, `Timeout`, `Cancelled`).

---

## 7. Numerics

### 7.1 Built-in Numeric Types

- `Integer`: 64-bit two's complement.
- `Number`: 64-bit IEEE-754 float.
- `Decimal`: arbitrary-precision decimal, bounded by **context**.

### 7.2 Literals & Conversions

- `0.08` → `Number`, `0.08d` → `Decimal`.
- The **only implicit** numeric conversion is **`Integer → Number`**, **exact up to 2^53−1**. The compiler **warns** when a value may exceed that bound. All others are explicit (`Number.from(i)`, `Decimal.from(n)`).

```viper
i: Integer = 9_007_199_254_740_992;
n: Number = i;                        // Warning: potential precision loss
n2: Number = Number.from(i);          // Explicit, no warning
```

### 7.3 Overflow & Operators

- Default: `Integer` overflow **traps**.
- Wrapping: `+%`, `-%`, `*%`.
- Saturating: `+^`, `-^`, `*^`.
- No wrap/saturate variants for `%`.

### 7.4 Division & Remainder

- **Integer**: `/` truncates toward zero; `%` has the **sign of the dividend**. Law: `a == (a/b)*b + (a % b)`.
- **Decimal**: `%` uses truncating division: `a % b = a - truncate(a/b) * b` under current decimal context.
- `%` on `Number` is **not provided** in V1.

### 7.5 Decimal Context

- Default context: rounding **HALF_EVEN**, max scale **28**. Operations exceeding context raise `Invalid`.
- Scoped override:
```viper
Decimal.withContext(scale: 34, rounding: HALF_EVEN) {
    result = new Decimal("10") / new Decimal("3");
};
```

### 7.6 Approximate Equality

- `a ~= b` uses default tolerances: relative `1e−12`, absolute `1e−12`.
- Override with `a ~= b within: ε`.

---

## 8. Expressions & Operators

- Evaluation order: **left-to-right**.
- `&&` and `||` are **short-circuiting** (left-to-right).
- Operator precedence & associativity follow mainstream C/Java rules. Parentheses always win. No user-defined operators or precedence changes in V1.
- **Conditional operator:** `cond ? a : b` is right-associative and follows C/Java precedence.

### 8.1 Operators via Contracts

```viper
contract Additive       { action add(other: Self) -> Self; }        // +
contract Subtractive    { action subtract(other: Self) -> Self; }   // -
contract Multiplicative { action multiply(other: Self) -> Self; }   // *
contract Divisible      { action divide(other: Self) -> Self; }     // /
contract Remainder      { action remainder(other: Self) -> Self; }  // %
```

- Operators are available for built-ins and types implementing these contracts.
- No new operator symbols; no precedence changes.

---

## 9. Equality, Identity & Hashing

```viper
==   // equality
===  // identity (entities only; illegal on values)
~=   // approximate equality (numbers/decimals)
```

- **Values:** `==` is structural; `hash` derived from attributes; `===` illegal.
- **Entities:** `==` calls `Equatable.equals` if implemented; otherwise `==` compares identity. `===` always compares identity.
- **Default for entities:** `==` is identity unless `Equatable` is implemented. Use `===` when you mean identity. The linter warns on `==` when it resolves to identity.
- **Comparable law:** `(a.compareTo(b) == 0) ⇔ a.equals(b)` must hold.
- **Maps/Sets:** keys must implement `Equatable` + `Hashable`. Using a **mutable value** as key emits a warning; mark logically immutable values **`@frozen`** to silence.

```viper
@frozen value Currency { 
    exposed attribute code: Text;
}
```

---

## 10. Collections & Iteration

- **Indexing:** V1 uses method calls (`get`, `set`, `update`) rather than `[]` syntax.
- **Iterator invalidation:** `add`, `remove`, `clear`, and any rehash/resize invalidate active iterators; mutating contained elements does not.
- **Fail-fast iteration:** Invalidation is detected at the next iterator operation on that iterator and raises `Error.Type.Invalid`.
- **Shallow copy:** `.copy()` duplicates the container and contained **values**; entity references remain shared.

---

## 11. Generics

- **Invariant** in V1 (no use-site variance).
- Constraints with `where T implements …`.
- **Library variance annotations** (declaration-site only):

```viper
// Library types may use variance annotations
// + for covariant (output positions only)
// - for contravariant (input positions only)  
entity ReadOnlyList[+T] {  // Covariant - T only in output
    exposed action get(index: Integer) -> T?;
}

entity WriteOnlyStream[-T] {  // Contravariant - T only in input
    exposed action write(value: T);
}
```

- User code cannot declare variance; only standard library types may.
- **Implementation:** values are **monomorphized** (zero-cost), entities are **reified** (runtime type available).

---

## 12. Sum Types & Pattern Matching

```viper
value Result[T] = Ok(value: T) | Err(error: Error);
value Option[T] = Some(value: T) | None;
```

- `match` on `value` sum types is **exhaustive**; a missing arm is a compile error unless a wildcard arm is present.
- **Wildcard arm syntax:** `_ => ...;`
- Variant names are **namespaced to their ADT**; disambiguation is by scrutinee type.

---

## 13. Concurrency

### 13.1 Memory Model (Safe Code)

- **No data races** in safe code.
- **Happens-before** is established by:
    1. Lock acquire/release.
    2. Channel send/receive.
    3. Task `await`.
    4. Atomic operations (acquire/release/seq_cst).

Unsynchronized access to the same non-atomic attribute from multiple tasks is **undefined**.

### 13.2 Tasks

```viper
task = Viper.spawn { 
    return fetchData();
};

run {
    data = task.await(timeout: 2.seconds);
} mediate e where e.type == Error.Type.Timeout {
    task.cancel();
}
```

- `await(timeout:)` raises `Timeout` and **does not cancel** the task.
- `cancel()` is explicit; `await()` on a failed task re-raises the task's error with preserved `stack`/`cause`.
- **Task hierarchy:** tasks form a parent–child tree. Exiting a scope cancels child tasks. Cancelling a parent cancels all children.
- **Structured lifetime:** Child tasks are attached to the lexical block that created them (i.e., where the `Viper.spawn` result is bound). When that block exits normally or by unwinding, unattained children are cancelled.

**Parent-child cancellation example:**

```viper
parent = Viper.spawn {
    child1 = Viper.spawn { longWork1(); };
    child2 = Viper.spawn { longWork2(); };
    return combineResults(child1.await(), child2.await());
};
// Cancelling parent cancels both children
parent.cancel();
```

### 13.3 Locks (Resources)

```viper
entity Lock { 
    exposed action acquire() -> LockGuard;
}

entity LockGuard implements Disposable { 
    exposed action dispose();
}

using (guard = lock.acquire()) {
    // critical section
};
```

- Default `Lock` is **non-reentrant**; `ReentrantLock` exists in `Viper.concurrent`.

### 13.4 Atomics

**Declaration (modifier form):**

```viper
// Atomics are a modifier on numeric and boolean types only
shared atomic attribute counter: Integer = 0;
shared atomic attribute flag: Boolean = false;
// NOT allowed: shared atomic attribute obj: Customer;

entity Stats {
    shared atomic attribute hits: Integer = 0;

    action inc() { 
        hits.fetchAdd(1);
    }
    
    action snapshot() -> Integer { 
        return hits.load();
    }
}
```

- Atomics apply only to `Integer`, `Number`, and `Boolean` types.
- Default memory orders: `load` = acquire, `store` = release, RMW ops = acq_rel (sequentially consistent by default).

**Memory Order Enumeration:**
```viper
enum MemoryOrder { Relaxed, Acquire, Release, AcqRel, SeqCst }
```

**Atomic Operation Signatures:**
- `load(order: MemoryOrder = Acquire) -> T;`
- `store(value: T, order: MemoryOrder = Release) -> Void;`
- `fetchAdd(delta: T, order: MemoryOrder = AcqRel) -> T;`
- `fetchSub(delta: T, order: MemoryOrder = AcqRel) -> T;`
- `compareExchange(inout expected: T, desired: T, order: MemoryOrder = SeqCst) -> Boolean;`
    - On success, returns `true`. On failure, stores the current value into `expected` and returns `false`.

**Floating-point equality in CAS:** For `Number`, `compareExchange` uses **bit-equality** (IEEE-754 payloads included). NaN never equals NaN by value, but bitwise-identical NaNs compare equal for CAS.

### 13.5 Channels

```viper
value TryReceive[T] = Received(value: T) | Empty | Closed;
value SendBlocking = Ok | Closed;
value SendTry = Ok | Full | Closed;

res: SendBlocking = channel.send("message");
val: Option[Text] = channel.receive();
tr: TryReceive[Text] = channel.tryReceive();
ts: SendTry = channel.trySend("msg");
channel.close();
channel.close();  // idempotent
```

**Invariants**

- **Blocking** `receive()` yields `Some(T)` or `None` (closed & empty), **never** `Empty`.
- **Blocking** `send()` yields `Ok` or `Closed`, **never** `Full`.
- **Non-blocking** `tryReceive()` yields `Received`/`Empty`/`Closed`; `trySend()` yields `Ok`/`Full`/`Closed`.
- **No fairness guarantees** among competing senders/receivers.

---

## 14. Resources & `using`

### 14.1 Disposable Protocol

```viper
contract Disposable { 
    action dispose();
}
```

- `using` calls `dispose()` **exactly once** on **all** exit paths (return, `raise`, `Timeout`, `Cancelled`).
- Disposable vs finalization: Finalizers are not part of V1. `dispose()` must be explicit (typically via `using`).

```viper
using (conn = Database.connect()) {
    if (conn.isStale()) {
        raise Error(type: Error.Type.Network, message: "Stale");
    }
    result = conn.query("SELECT * FROM users");
};
```

---

## 15. Text & Strings

- `Text` is UTF-8; `length()` counts **grapheme clusters**.
- Equality compares **code points** (no implicit normalization).
- `normalize(form)` and `equalsNormalized(other, form)` are provided.
- Locale-aware collation lives in `Viper.Text.Collation`.

---

## 16. Operators, Contracts & Comparable

```viper
contract Equatable   { action equals(other: Self) -> Boolean; }
contract Hashable    { action hash() -> Integer; }
contract Comparable  expands Equatable { action compareTo(other: Self) -> Integer; }
```

- Laws:
    - If `Comparable` is implemented: `(a.compareTo(b) == 0) ⇔ a.equals(b)`.
    - `hash` must be consistent with `equals`.

---

## 17. Testing & Tooling

- **Testing:** The runner discovers `@test` actions inside entities and compiles test fixtures into the **same module** as the code under test, enabling `module` visibility.
- **Formatter & linter are normative**; code is formatted on save and enforced in CI.

```viper
entity CalculatorTests {
    @test 
    action addition_works() {
        calc = new Calculator();
        assert calc.add(2, 3) == 5;
    }
}
```

**Key Linter Warnings:**
- `VIPER001`: `==` on entities that resolves to identity (prefer `===` for clarity)
- `VIPER002`: Mutable value type used as `Map` key (prefer `@frozen`)
- `VIPER003`: Implicit `Integer → Number` where value may exceed 2^53−1
- `VIPER004`: Unreachable channel cases (e.g., handling `Full` after blocking `send()`)

---

## 18. Standard Library Boundaries

- **Modern only** in `Viper.*`: HTTP, JSON, crypto, time, text, concurrency, AI.
- Legacy protocols live in opt-in adapter libraries, not in `Viper.*`.
- AI access is versioned and cancellable (`Viper.AI.vN`), supports deterministic runs via seeds.

```viper
var t = Viper.spawn {
    return Viper.AI.v1.complete(
        prompt: "Explain quantum computing", 
        seed: 12345, 
        model: "v1"
    );
};
var resp = t.await(timeout: 5.seconds);
```

---

## 19. Not in V1

- Async/await (use `spawn/await/cancel`).
- Ownership/lifetimes.
- Macros/metaprogramming.
- User-defined operators or precedence changes.
- Multiple inheritance (use single inheritance + contracts/traits).
- Implicit conversions (except `Integer → Number` with warnings).
- Top-level executable code.
- Bitwise/shift operators (may be added in V2).
- `%` for `Number`.

---

## 20. Core Semantics Appendix (Authoritative Summary)

- **Statements:** semicolons required.
- **Values:** copy on assignment/call; no mutating temporaries/accessor returns; `with {}` copies, mutates in block, assigns back; `inout` accepted only on l-values; exclusivity enforced. Pure values only. Values may implement contracts.
- **Entities:** reference semantics; sealed unless `expandable`; members final by default; overriding requires `override`, base requires `overridable`; `super` only inside overrides, calls immediate base. Only entities may `with` traits.
- **Stored vs computed attributes:** only stored attributes can be passed to `inout`; computed attributes use explicit `get`/`set`.
- **Initializers:** all non-optional attributes must be assigned; left-to-right evaluation; defaults may reference earlier parameters.
- **Nullability:** non-nullable by default; `T?` optionals; `nothing` inhabits `T?` only; `x exists` tests presence.
- **Equality/Identity:** `==` equality; `===` identity (entities only). Values: structural; Entities: `==` uses `Equatable` else identity; `===` always identity. `Comparable` law holds. Default for entities: `==` is identity unless `Equatable`.
- **Hashing:** derived for values; `Hashable` required for map/set keys; warn on mutable value keys; `@frozen` silences warning when appropriate.
- **Numbers:** `Integer(64)`, `Number(64-bit IEEE)`, `Decimal(context)`. Only implicit `Integer → Number` (warn > 2^53−1). Overflow traps; wrap/saturate for `+ - *`. Integer `/` truncates; `%` has dividend sign. Decimal `%` uses truncating division. `%` for `Number` not in V1.
- **Decimal context:** default HALF_EVEN, scale 28; `withContext` overrides; exceeding context raises `Invalid`.
- **Expressions:** left-to-right evaluation; `&&`/`||` short-circuit L-to-R; precedence ~ C/Java; parentheses win; `? :` is right-associative.
- **Operators via contracts:** Additive/Subtractive/Multiplicative/Divisible/Remainder; no custom symbols/precedence.
- **Errors:** `raise` unwinds to nearest matching `mediate`; unhandled propagate; `rethrow()` preserves stack/cause; `handle` runs exactly once.
- **Generics:** invariant; constraints with `where T implements …`; values monomorphized; entities reified; library types may declare variance.
- **Sum types & `match`:** exhaustive unless wildcard (`_`); variants namespaced to their ADT.
- **Functions:** parameters non-escaping unless `@escaping`; stored function attributes inherently escaping; captures: values by copy, entities by reference. Named arguments optional unless required.
- **Concurrency:** no data races in safe code; HB via locks, channels, `await`, atomics. Task hierarchy with structured lifetime; `await(timeout:)` raises `Timeout` and does not cancel; explicit `cancel()`. `inout` doesn't synchronize across tasks.
- **Locks & resources:** `Disposable.dispose()`; `using` calls `dispose()` exactly once on all exits. No finalizers in V1.
- **Atomics:** `atomic` attribute modifier on `Integer`, `Number`, `Boolean` only; standard operations with memory orders; CAS uses bit-equality for `Number`.
- **Channels:** blocking `receive → Some/None`, blocking `send → Ok/Closed`; non-blocking `tryReceive → Received/Empty/Closed`, `trySend → Ok/Full/Closed`; `close()` idempotent; no fairness guarantees.
- **Iteration:** fail-fast on structural modification; raises `Invalid` on next operation.
- **Text:** grapheme-aware length; code-point equality; normalization helpers; locale collation in `Viper.Text.Collation`.
- **Modules & tests:** module = namespace = visibility boundary; import cycles forbidden with diagnostics; tests compiled into the same module.
- **Tooling:** formatter & linter are normative; CI enforces style.
- **Annotations:** `@frozen`, `@test`, `@deprecated`, `@escaping` defined; others may be added via tooling.

---

## 21. Representative Examples

### 21.1 Value Updates

```viper
entity Example {
    exposed attribute corner: Point;

    exposed action demo() {
        temp = this.corner;
        temp.x = 10;
        this.corner = temp;

        this.corner = this.corner.with { 
            x += 5;
            y -= 2;
        };

        translate(inout this.corner, 5, 10);
    }
}
```

### 21.2 Error Handling

```viper
run {
    file = File.open(path);
    data = file.read();
    process(data);
} mediate e where e.type == Error.Type.File {
    log("File error: " + e.message);
    useDefaultData();
} mediate {
    log("Unexpected error: " + error.message);
    rethrow();
} handle {
    file?.dispose();
}
```

### 21.3 Channels

```viper
res: SendBlocking = ch.send("msg");
val: Option[Text] = ch.receive();
tr: TryReceive[Text] = ch.tryReceive();
ts: SendTry = ch.trySend("msg2");
ch.close();
```

### 21.4 Tasks

```viper
task = Viper.spawn { 
    return fetchData();
};

run {
    data = task.await(timeout: 2.seconds);
} mediate e where e.type == Error.Type.Timeout {
    task.cancel();
}
```

### 21.5 Generics & Match

```viper
value Result[T] = Ok(value: T) | Err(error: Error);

action divide(a: Number, b: Number) -> Result[Number] {
    if (b == 0) {
        return Err(Error(type: Error.Type.Invalid, message: "Division by zero"));
    }
    return Ok(a / b);
}

action use() {
    r = divide(10, 2);
    match (r) {
        Ok(v)  => print(v);
        Err(e) => print(e.message);
        _ => print("Unexpected");  // Wildcard example
    }
}
```

---

**End of ViperLang V1 Specification.**

**This specification is frozen for V1 implementation.**