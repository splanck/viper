# ViperLang V1 — Comprehensive Syntax & Usage Guide

**Status:** Matches the V1 “Frozen” specification.  
**Goal:** Teach every *supported* syntax form with runnable‑style examples and a brief narrative for each.  
**Style:** C/Java‑like blocks, **semicolons required**.

> **How to read this:** Each section introduces a construct, shows a small, focused snippet, then explains *exactly* what the code does and which rules apply.

---

## Table of Contents

1. [Files, Modules, Imports](#1-files-modules-imports)  
2. [Comments](#2-comments)  
3. [Identifiers, Literals & Semicolons](#3-identifiers-literals--semicolons)  
4. [Bindings & Mutability (`var`, `final`)](#4-bindings--mutability-var-final)  
5. [Types: Values vs Entities](#5-types-values-vs-entities)  
   - 5.1 [Value Declarations](#51-value-declarations)  
   - 5.2 [Entity Declarations](#52-entity-declarations)  
   - 5.3 [Attributes (Stored vs Computed)](#53-attributes-stored-vs-computed)  
   - 5.4 [Actions](#54-actions)  
6. [Visibility (Access Control)](#6-visibility-access-control)  
7. [Shared Members](#7-shared-members)  
8. [Construction & Initializers](#8-construction--initializers)  
9. [Value Updates: Copy‑Modify (`with`) & Projections (`inout`)](#9-value-updates-copymodify-with--projections-inout)  
10. [Closures & Function Types](#10-closures--function-types)  
11. [Contracts & Traits](#11-contracts--traits)  
12. [Single Inheritance (`expandable`, `expands`, `super`, `override`)](#12-single-inheritance-expandable-expands-super-override)  
13. [Nullability & Optionals (`T?`, `nothing`, `?.`, `??`, `exists`)](#13-nullability--optionals-t-nothing---)  
14. [Errors: `Error` object, `raise`, `run/mediate/handle`, `rethrow`](#14-errors-error-object-raise-runmediatehandle-rethrow)  
15. [Sum Types & `match` (Exhaustive)](#15-sum-types--match-exhaustive)  
16. [Generics & Constraints](#16-generics--constraints)  
17. [Numerics: Types, Literals, Conversions, Overflow, Decimal Context, `~=`](#17-numerics-types-literals-conversions-overflow-decimal-context-)  
18. [Expressions & Operators (incl. Contracts)](#18-expressions--operators-incl-contracts)  
19. [Equality, Identity & Hashing (`==`, `===`, `@frozen`)](#19-equality-identity--hashing---frozen)  
20. [Collections, Iteration & Fail‑Fast](#20-collections-iteration--failfast)  
21. [Concurrency: Tasks, Channels, Locks, Atomics, Memory Model](#21-concurrency-tasks-channels-locks-atomics-memory-model)  
22. [Text & Strings](#22-text--strings)  
23. [Testing & Tooling](#23-testing--tooling)  
24. [Annotations](#24-annotations)  
25. [Control Flow Essentials](#25-control-flow-essentials)  
26. [Cheat Sheet (Syntax at a Glance)](#26-cheat-sheet-syntax-at-a-glance)

---

## 1) Files, Modules, Imports

```viper
module MyApp.Banking;
import Viper.Data.Json;
import MyApp.Models.Customer;
```

**Narrative.**  
Every file starts with a `module` declaration (it’s the namespace and visibility boundary). `import` pulls in other modules. Only declarations appear at top level—no executable statements outside entities/values.

---

## 2) Comments

```viper
// Line comment.
 /* Block comments can nest:
   /* nested! */
 */
```

**Narrative.**  
Use `//` for single lines and `/* ... */` for blocks. Blocks can nest.

---

## 3) Identifiers, Literals & Semicolons

```viper
var answer = 42;              // Integer literal.
var rate   = 0.08;            // Number (float) literal.
var price  = 19.99d;          // Decimal literal (suffix d).
var hello  = "Hello, 世界! 👋"; // Text literal (UTF‑8).

// Semicolons are required at the end of statements.
```

**Narrative.**  
Literals: `Integer`, `Number` (float), and `Decimal` (`d` suffix). `Text` is UTF‑8. **Every** statement ends with a semicolon; the formatter enforces it.

---

## 4) Bindings & Mutability (`var`, `final`)

```viper
var x = 10;     // Mutable local; type inferred.
x = 20;         // OK.

final y = 10;   // Immutable local.
 // y = 20;     // ERROR: cannot assign to final binding.
```

**Narrative.**  
`var` creates mutable bindings; `final` creates immutable ones. Local types are inferred unless you annotate explicitly.

---

## 5) Types: Values vs Entities

ViperLang has two primary kinds: **values** (copy semantics) and **entities** (reference semantics).

### 5.1 Value Declarations

```viper
value Point {
  exposed attribute x: Number;
  exposed attribute y: Number;
}
```

**Narrative.**  
`value` types are small data aggregates. Assigning `Point` copies it. In V1, value fields themselves must be value types (“pure values”).

### 5.2 Entity Declarations

```viper
entity Customer {
  exposed attribute name: Text;
  internal attribute id: Integer;
}
```

**Narrative.**  
Entities are identity‑bearing reference types. Assigning a `Customer` variable copies the reference. Members are `final` by default unless you opt into overrides (see §12).

### 5.3 Attributes (Stored vs Computed)

```viper
entity Example {
  // Stored attribute (eligible for inout).
  exposed attribute position: Point;

  // Computed attribute (get/set blocks; not eligible for inout).
  exposed attribute center: Point {
    get { return calculateCenter(); }
    set { recenterFromCenter(value); }
  }
}
```

**Narrative.**  
Stored attributes hold data directly. Computed attributes have `get`/`set` bodies and cannot be used with `inout`.

### 5.4 Actions

```viper
entity Account {
  internal attribute balance: Decimal;

  exposed action deposit(amount: Decimal) {
    balance = balance + amount;
  }

  exposed action getBalance() -> Decimal {
    return balance;
  }
}
```

**Narrative.**  
Actions are methods. Return types use `->`. Without `->`, the action returns `Void`.

---

## 6) Visibility (Access Control)

```viper
expandable entity BankAccount {
  internal  attribute id: Text;            // This entity only.
  module    attribute audit: List[Text];   // Same module (e.g., tests).
  inherited attribute balance: Decimal;    // This + expanders (only on expandable).
  exposed   attribute accountNumber: Text; // Public.
}
```

**Narrative.**  
Four levels: `internal` (default), `module`, `inherited` (on expandable entities), and `exposed` (public API).

---

## 7) Shared Members

```viper
entity Math {
  shared final attribute PI: Number = 3.14159;
  shared action max(a: Number, b: Number) -> Number { return a > b ? a : b; }
}
```

**Narrative.**  
`shared` members belong to the type, not instances. Initialization is lazy and thread‑safe. **No** implicit synchronization for shared state—use atomics/locks.

---

## 8) Construction & Initializers

```viper
entity Person {
  exposed attribute name: Text;
  exposed attribute age: Integer;

  initializer(required name: Text, age: Integer = 0) {
    this.name = name;
    this.age  = age;
  }
}

var p = new Person(name: "Ava");          // age defaults to 0.
var q = new Person(name: "Bo", age: 41);  // named argument; any order after positionals.
```

**Narrative.**  
Use `initializer(...)` to set required state. Parameters evaluate left‑to‑right; defaults may reference earlier params. You construct instances with `new Type(...)`.

---

## 9) Value Updates: Copy‑Modify (`with`) & Projections (`inout`)

### 9.1 The `with { }` Update Sugar

```viper
value Point { exposed attribute x: Number; exposed attribute y: Number; }

var p1 = new Point(x: 10, y: 20);
var p2 = p1.with { x += 5; y -= 2; };  // copy p1 → mutate block → assign result to p2;
```

**Narrative.**  
`with` copies the receiver, runs the block to mutate the copy, and returns it. It’s the idiomatic way to update values without aliasing.

> **Guardrails:** Mutating **temporaries** or **accessor returns** is illegal:
> - `getPoint().x = 10;` // ERROR  
> - `box.corner.x = 10;` // ERROR  
> Use `with` or `inout`.

### 9.2 `inout` Projections (Exclusive Borrow)

```viper
action translate(inout p: Point, dx: Number, dy: Number) {
  p.x = p.x + dx;
  p.y = p.y + dy;
}

entity Box { exposed attribute corner: Point; }

var b = new Box();
translate(inout b.corner, 5, 10);  // projects b.corner with exclusivity for call duration;
```

**Narrative.**  
`inout` projects assignable storage (locals/parameters/stored attributes) into a callee **with exclusivity**. The call behaves as copy‑in → mutate → write‑back. Overlapping accesses are rejected or trap. Across tasks, this behaves like a write lock for the call window.

---

## 10) Closures & Function Types

```viper
entity Handler {
  internal attribute h: (Text) -> Void;

  exposed action setHandler(@escaping f: (Text) -> Void) {
    h = f;                          // storing requires @escaping on the parameter;
  }

  exposed action forEach(f: (Text) -> Void) {
    f("tick");                      // non‑escaping parameters can be called but not stored;
  }
}
```

**Narrative.**  
Function parameters are non‑escaping by default; mark `@escaping` to store/return them. Values capture by copy; entities capture by reference.

---

## 11) Contracts & Traits

```viper
contract Drawable { action draw(); action getBounds() -> Rectangle; }
contract Printable { action print(); }

// Traits provide defaults and may introduce new actions (no state).
trait DrawableDefaults {
  default action render(self: Drawable) { self.draw(); }
}

trait PrintableDefaults {
  default action render(self: Printable) { self.print(); }
}

entity Widget implements Drawable, Printable
                with DrawableDefaults, PrintableDefaults {

  exposed action draw() { /* ... */ }
  exposed action getBounds() -> Rectangle { /* ... */ }
  exposed action print() { /* ... */ }

  // Two traits define render(); must resolve:
  exposed override action render() { DrawableDefaults.render(self); }
}
```

**Narrative.**  
Contracts are pure requirements. Traits provide default implementations and may add actions, but can only call contract‑declared members. If multiple traits provide the same action, the entity must choose via an override.

---

## 12) Single Inheritance (`expandable`, `expands`, `super`, `override`)

```viper
expandable entity Account {
  inherited attribute balance: Decimal;

  inherited overridable action calculateFees() -> Decimal {
    return 0.00d;
  }
}

entity SavingsAccount expands Account {
  internal attribute rate: Decimal;

  inherited override action calculateFees() -> Decimal {
    var base = super.calculateFees();
    return base + 5.00d;
  }
}
```

**Narrative.**  
Entities are sealed unless `expandable`. To override, the base action must be `overridable` and the derived must be marked `override`. `super.m()` calls the immediate base implementation. Using `inherited/overridable` on non‑expandable types is an error.

---

## 13) Nullability & Optionals (`T?`, `nothing`, `?.`, `??`, `exists`)

```viper
entity Order {
  exposed attribute customer: Customer;       // non‑nullable
  exposed attribute backup: Customer?;        // optional

  exposed action notify() {
    customer.sendEmail();                     // safe: not optional

    if (backup exists) {
      backup.sendEmail();                     // flow‑checked safe region
    }

    var name = backup?.getName() ?? "Unknown"; // safe navigation + coalescing
  }
}
```

**Narrative.**  
References are non‑nullable by default. Use `T?` for optionals and `nothing` for absence. `x?.y` skips if absent; `a ?? b` supplies a default. `x exists` flow‑narrows to non‑optional within the branch.

---

## 14) Errors: `Error` object, `raise`, `run/mediate/handle`, `rethrow`

```viper
entity Error {
  shared enum Type { Network, File, Invalid, Permission, Timeout, Cancelled, Custom }
  exposed attribute type: Type;
  exposed attribute message: Text;
  exposed attribute data: Map[Text, Any]?;
  exposed attribute cause: Error?;
  exposed attribute stack: StackTrace;
}

// Structured handling:
run {
  file = File.open(path);
  data = file.read();
  process(data);
} mediate e where e.type == Error.Type.File {
  log("File error: " + e.message);
  useDefaultData();
} mediate {
  log("Unexpected: " + error.message);
  rethrow();                      // preserves stack & cause
} handle {
  file?.dispose();                // runs exactly once in all cases
}
```

**Narrative.**  
`raise Error(...)` unwinds to the nearest matching `mediate`, by source order. `handle` always runs, success or failure. `rethrow()` propagates the current error without losing its stack/cause. Unmediated errors propagate to the caller.

---

## 15) Sum Types & `match` (Exhaustive)

```viper
value Result[T] = Ok(value: T) | Err(error: Error);
value Option[T] = Some(value: T) | None;

action divide(a: Number, b: Number) -> Result[Number] {
  if (b == 0) { return Err(Error(type: Error.Type.Invalid, message: "division by zero")); }
  return Ok(a / b);
}

var r = divide(10, 2);
match (r) {
  Ok(v)  => print("Value: " + v);
  Err(e) => log("Error: " + e.message);
} // exhaustive by construction

// Wildcard arm:
match (lookup()) {
  Some(v) => use(v);
  _       => fallback();
}
```

**Narrative.**  
Variants are namespaced to their ADT. `match` must be exhaustive unless you include `_ => ...;`. Great for explicit success/error flow without exceptions.

---

## 16) Generics & Constraints

```viper
entity List[T] {
  exposed action add(item: T) { /* ... */ }
  exposed action get(index: Integer) -> T? { /* ... */ }
}

action sort[T](xs: List[T]) where T implements Comparable {
  // ...
}
```

**Narrative.**  
Type parameters are **invariant** in V1. Use `where T implements ...` to constrain. Values are monomorphized (zero‑cost); entities are reified (type available at runtime).

---

## 17) Numerics: Types, Literals, Conversions, Overflow, Decimal Context, `~=`

```viper
var i: Integer = 42;
var n: Number  = 0.08;      // float literal
var d: Decimal = 19.99d;    // decimal literal

// Only implicit conversion: Integer → Number (exact up to 2^53−1).
n = i;                      // may warn if i can exceed 2^53−1;
n = Number.from(i);         // explicit, no warning;

// Overflow:
var a = Integer.MAX;
// var b = a + 1;           // trap on overflow
var wrap = a +% 1;          // wrapping add
var sat  = a +^ 1;          // saturating add

// Decimal context:
Decimal.withContext(scale: 34, rounding: HALF_EVEN) {
  var q = new Decimal("10") / new Decimal("3");
};

// Approximate equality (floats/decimals):
if (0.1 + 0.2 ~= 0.3) { log("close enough"); }
if (approxEquals(a, b, within: 1e-9)) { /* custom tolerance */ }
```

**Narrative.**  
Numeric literals pick `Integer`, `Number`, or `Decimal` (`d`). Only `Integer → Number` is implicitly allowed (with precision warning above 2^53−1). Integer arithmetic traps by default; `%` uses dividend sign; `Decimal` respects context. `~=` compares within relative/absolute tolerances.

---

## 18) Expressions & Operators (incl. Contracts)

```viper
// Conditional:
var max = a > b ? a : b;

// Short‑circuit:
if (isReady() && doWork()) { /* ... */ }

// Operator contracts (types opt‑in):
contract Additive       { action add(other: Self) -> Self; }       // +
contract Subtractive    { action subtract(other: Self) -> Self; }  // -
contract Multiplicative { action multiply(other: Self) -> Self; }  // *
contract Divisible      { action divide(other: Self) -> Self; }    // /
contract Remainder      { action remainder(other: Self) -> Self; } // %

value Point implements Additive, Subtractive {
  exposed attribute x: Number; exposed attribute y: Number;
  exposed action add(other: Point) -> Point { return new Point(x: x + other.x, y: y + other.y); }
  exposed action subtract(other: Point) -> Point { return new Point(x: x - other.x, y: y - other.y); }
}
var p3 = p1 + p2;  // calls add
var p4 = p3 - p1;  // calls subtract
```

**Narrative.**  
V1 uses familiar precedence (C/Java). Ternary `? :` is right‑associative. Operators are available for built‑ins and for types that implement the corresponding math contracts—no custom symbols or precedence changes.

---

## 19) Equality, Identity & Hashing (`==`, `===`, `@frozen`)

```viper
@frozen value Currency { exposed attribute code: Text; }

var p1 = new Point(x: 10, y: 20);
var p2 = new Point(x: 10, y: 20);
if (p1 == p2) { /* structural equality for values */ }

var c1 = new Customer(name: "Alice");
var c2 = new Customer(name: "Alice");
if (c1 === c2) { /* identity only */ }
// if (c1 == c2) { ... }        // == on entities uses Equatable.equals if implemented; else identity (linter warns)
```

**Narrative.**  
Values compare structurally with `==`; entities compare by identity unless they implement `Equatable`. `===` is always identity and is illegal on values. Use `@frozen` to mark a value safe as a `Map` key (consistent hashing).

---

## 20) Collections, Iteration & Fail‑Fast

```viper
var xs = new List[Integer]();
xs.add(1);
xs.add(2);

// Fail‑fast: structural changes invalidate active iterators.
for (item in xs) {
  if (item == 1) {
    xs.add(99);             // next iterator step raises Error.Invalid
  }
}

// Updating a stored value (for value types) via update:
value Point { exposed attribute x: Number; exposed attribute y: Number; }
var points = new List[Point]();
points.add(new Point(x: 1, y: 2));
points.update(0, p -> { p.x = 10; return p; });
```

**Narrative.**  
Containers are entities; `copy()` is shallow. Iterators are fail‑fast: any structural change invalidates active iterators and triggers an error on the next step. Use `update` helpers to modify stored **values** safely.

---

## 21) Concurrency: Tasks, Channels, Locks, Atomics, Memory Model

### 21.1 Tasks & Timeouts

```viper
var t = Viper.spawn { return fetchData(); };

run {
  var result = t.await(timeout: 2.seconds);
} mediate e where e.type == Error.Type.Timeout {
  t.cancel();                                // timeout does not auto‑cancel
}
```

**Narrative.**  
`spawn` creates a task. `await(timeout:)` raises `Timeout` if the deadline passes and **doesn’t** cancel the task. Cancelling a parent cancels its children. `await()` re‑raises the child’s error (stack preserved).

### 21.2 Channels

```viper
value TryReceive[T] = Received(value: T) | Empty | Closed;
value SendBlocking  = Ok | Closed;
value SendTry       = Ok | Full | Closed;

var ch = new Channel[Text](capacity: 10);

var sb: SendBlocking = ch.send("hi");   // Ok | Closed
var got: Option[Text] = ch.receive();   // Some | None (never Empty)

var tr: TryReceive[Text] = ch.tryReceive(); // Received | Empty | Closed
var ts: SendTry = ch.trySend("bye");        // Ok | Full | Closed

ch.close();
ch.close();                                 // idempotent
```

**Narrative.**  
Blocking `send()` never returns `Full` and blocking `receive()` never returns `Empty`. Non‑blocking calls use tri‑state results. No fairness guarantees among producers/consumers.

### 21.3 Locks as Resources

```viper
contract Disposable { action close(); }

entity Lock {
  exposed action acquire() -> LockGuard;
}

entity LockGuard implements Disposable {
  exposed action close() { /* release */ }
}

var lock = new Lock();
using (guard = lock.acquire()) {
  // critical section
}
```

**Narrative.**  
Locks integrate with `using`, so you get deterministic release even when errors occur.

### 21.4 Atomics (Scalars)

```viper
entity Stats {
  shared atomic attribute hits: Integer = 0;

  exposed action inc() { hits.fetchAdd(1); }
  exposed action snap() -> Integer { return hits.load(); }
}
```

**Narrative.**  
Atomics are for `Integer`, `Number`, and `Boolean` scalar fields. Use `load`, `store`, `fetchAdd/Sub`, and `compareExchange`. They establish happens‑before edges as per the memory model.

### 21.5 Memory Model (Safe Code)

- No data races in safe code.  
- HB via: lock acquire/release, channel send/receive, task `await`, and atomic ops.

**Narrative.**  
Concurrent mutation without synchronization is undefined. Stick to the primitives above for correctness.

---

## 22) Text & Strings

```viper
var text = "Hello, 世界! 👋";
var len  = text.length();                // grapheme‑cluster count
var norm = text.normalize(NFC);
var eq   = text.equalsNormalized(norm, NFC);
```

**Narrative.**  
`Text` is UTF‑8. `length()` counts graphemes; equality compares code points (no implicit normalization). Use normalization helpers or locale collation APIs when needed.

---

## 23) Testing & Tooling

```viper
entity Calculator {
  exposed action add(a: Integer, b: Integer) -> Integer { return a + b; }
}

entity CalculatorTests {
  @test
  action addition_works() {
    var c = new Calculator();
    assert c.add(2, 3) == 5;
  }
}
```

**Narrative.**  
The test runner discovers `@test` actions inside entities. Tests compile into the same module to see `module`‑visible members. The formatter and linter are normative and enforced in CI.

---

## 24) Annotations

- `@test` — marks test actions discovered by the runner.  
- `@frozen` — marks a value as logically immutable (safe map/set key).  
- `@deprecated` — marks APIs slated for removal.  
- `@escaping` — marks function parameters that are stored/returned.

**Narrative.**  
These are the core annotations recognized by the V1 toolchain and type system.

---

## 25) Control Flow Essentials

```viper
// If / else
if (condition()) { doA(); } else { doB(); }

// For‑in iteration
for (item in list) { use(item); }

// While
while (check()) { step(); }

// Break / Continue
for (i in range) {
  if (skip(i)) { continue; }
  if (done(i)) { break; }
}
```

**Narrative.**  
Standard structured control flow with the usual short‑circuiting (`&&`, `||`) and a right‑associative `?:` operator for expressions.

---

## 26) Cheat Sheet (Syntax at a Glance)

- **Module/import:**  
  `module A.B; import X.Y;`
- **Values & entities:**  
  `value V { exposed attribute f: T; }`  
  `entity E { internal attribute g: U; exposed action m() { ... } }`
- **Visibility:** `internal` (default), `module`, `inherited`, `exposed`.
- **Shared:** `shared attribute X: T = ...;` / `shared action f(...) -> R { ... }`
- **Initializer:**  
  `initializer(required p: T, q: U = default) { this.p = p; this.q = q; }`
- **Inheritance:**  
  `expandable entity A { inherited overridable action m() { ... } }`  
  `entity B expands A { inherited override action m() { super.m(); } }`
- **Contracts/traits:**  
  `contract C { action r() -> R; }`  
  `trait T { default action r(self: C) { ... } }`  
  `entity X implements C with T { ... }`
- **Optionals:** `var x: T? = nothing; if (x exists) { ... } y = x?.m() ?? d;`
- **Errors:**  
  `raise Error(type: Error.Type.Invalid, message: "...");`  
  `run { ... } mediate e where e.type == Error.Type.File { ... } handle { ... }`
- **Sum types & match:**  
  `value R[T] = Ok(value: T) | Err(error: Error);`  
  `match (r) { Ok(v) => ..., Err(e) => ..., _ => ... }`
- **Generics:**  
  `entity Box[T] { ... }`  
  `action sort[T](xs: List[T]) where T implements Comparable { ... }`
- **Numerics:**  
  `0.5` → `Number`; `0.5d` → `Decimal`; `n = Number.from(i);`  
  Overflow: `+%`, `-%`, `*%`, `+^`, `-^`, `*^`; `a ~= b within: 1e-9;`
- **Operators via contracts:** `Additive`, `Subtractive`, `Multiplicative`, `Divisible`, `Remainder`.
- **Equality/identity:** `==` (values structural; entities → Equatable or identity), `===` (identity only), `@frozen` for map keys.
- **Collections:** fail‑fast iterators on structural change; `update(i, p -> { ...; return p; });`
- **Concurrency:**  
  Tasks: `spawn/await/cancel`, parent‑child lifetime, timeout doesn’t cancel;  
  Channels: blocking `{send→Ok|Closed, receive→Some|None}`, non‑blocking tri‑state;  
  Locks: `using (guard = lock.acquire()) { ... }`;  
  Atomics: scalar ops; memory model defines HB.
- **Text:** grapheme length, normalization helpers.
- **Testing:** `@test` actions; module‑scoped fixtures; formatter/linter are normative.

---

**End of Guide.**  
This document covers the full V1 surface with runnable‑style code and narrative for each construct, aligned with the frozen spec.
