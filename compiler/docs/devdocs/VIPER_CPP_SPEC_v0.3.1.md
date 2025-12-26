# Viper C++ Specification v0.3.1

## Purpose

Define a bounded, project-specific subset of C++ sufficient to:

1. Compile the Viper toolchain (frontends, IL, passes, codegen, VM, runtime) in a hosted environment.
2. Eventually compile the Vana kernel and core runtime in a freestanding environment.

Viper C++ is **not** a general-purpose C++ implementation. It is a self-hosting dialect tuned specifically to the Viper
codebase.

## Derived From

Static analysis of the Viper repository (≈150k LOC) plus iterative design feedback.  
Feature counts and examples are **descriptive snapshots**, not normative requirements.

## Tracks

Viper commit as of 2024-11-24.

## Conformance Language

This specification uses RFC 2119 terminology:

- **MUST** / **REQUIRED** — absolute requirement
- **SHOULD** / **RECOMMENDED** — best practice with valid exceptions
- **MAY** / **OPTIONAL** — truly optional

---

## 0. Profiles & Scope

Viper C++ defines three profiles corresponding to how the code is used:

| Profile      | Scope (directories)                                                                               | Purpose                               |
|--------------|---------------------------------------------------------------------------------------------------|---------------------------------------|
| **Core**     | `include/viper`, `src/{codegen, frontends, il, parse, pass, runtime, support, vm, tools, common}` | Self-hosting compiler + VM            |
| **Extended** | Core + `src/tui`, `src/tests`                                                                     | TUI, tests, dev tools                 |
| **Kernel**   | Subset of Core                                                                                    | Vana kernel; freestanding, no EH/RTTI |

Every feature is tagged with the profile(s) that MUST support it:

- **Core** — required for the self-hosting compiler
- **Extended** — used only in tests / TUI / auxiliary tools
- **Kernel** — subset that MUST work under `-fno-exceptions -fno-rtti` with a freestanding standard library

Unless otherwise noted, a rule applies to all three profiles.

---

## 1. Lexical Elements

### 1.1 Included (all profiles)

| Element                      | Notes                                                      |
|------------------------------|------------------------------------------------------------|
| C-style comments `/* ... */` | MUST support                                               |
| C++ comments `// ...`        | MUST support                                               |
| Integer literals             | Decimal, hex (`0x`), binary (`0b`), digit separators (`'`) |
| Floating literals            | Standard + scientific notation                             |
| String literals              | Normal and raw (`R"(...)"`), standard escape sequences     |
| Character literals           | Normal + escapes                                           |
| Boolean literals             | `true`, `false`                                            |
| Null pointer literal         | `nullptr`                                                  |

### 1.2 Excluded (all profiles)

| Element                                             | Rationale         |
|-----------------------------------------------------|-------------------|
| User-defined literals                               | Not used in Viper |
| Wide/Unicode literals (`L""`, `u""`, `U""`, `u8""`) | Not used          |

---

## 2. Types

### 2.1 Fundamental Types (all profiles)

**REQUIRED:**

- `bool`
- `char`, `signed char`, `unsigned char`
- `short`, `unsigned short`
- `int`, `unsigned int`
- `long`, `unsigned long`
- `long long`, `unsigned long long`
- `float`, `double`
- `void`
- `std::size_t`, `std::ptrdiff_t`
- `std::intN_t`, `std::uintN_t` for N ∈ {8, 16, 32, 64}

**EXCLUDED:**

- `long double`
- `wchar_t`, `char8_t`, `char16_t`, `char32_t`

### 2.2 Compound Types (all profiles)

**REQUIRED:**

- Pointers: `T*`, `const T*`
- References: `T&`, `const T&`
- Rvalue references: `T&&` (for move semantics)
- Arrays: `T[N]` and `T[]` (non-owning)
- Enums: `enum class` STRONGLY RECOMMENDED over unscoped `enum`
- Classes and structs (see §4)

**Unions:**

- Core/Extended: Plain `union` is permitted ONLY as a local implementation detail for trivial bit reinterpretation.
  Public tagged unions MUST use `std::variant`.
- Kernel: Same rules; in practice, union usage is limited to low-level code.

### 2.3 Type Qualifiers

| Qualifier   | Status                                               |
|-------------|------------------------------------------------------|
| `const`     | REQUIRED                                             |
| `constexpr` | REQUIRED                                             |
| `mutable`   | ALLOWED                                              |
| `volatile`  | FORBIDDEN in Core/Extended — Viper C++ MAY reject it |

**Kernel exception:** Code that genuinely needs `volatile` (e.g., MMIO) SHOULD isolate those uses in small, audited
headers. A future revision may carve out a tiny allowed subset of `volatile` for this purpose.

### 2.4 Type Inference (all profiles)

**REQUIRED:**

- `auto` (variables and return types)
- `decltype`, `decltype(auto)`
- Structured bindings: `auto [a, b] = expr;`

### 2.5 Concepts & Requires-Expressions (Core/Extended)

Viper C++ supports a very small subset of C++20 concepts for detection idioms.

**ALLOWED:**

```cpp
// Named concepts (simple form)
template <class T>
concept CursorPredicate = requires(T pred, char ch) {
    { pred(ch) } -> std::convertible_to<bool>;
};

// Requires-expressions inside if constexpr (detection idiom)
if constexpr (requires(Derived& d, const Node& n) { d.before(n); }) {
    static_cast<Derived*>(this)->before(node);
}
```

**NOT ALLOWED:**

- Requires clauses on function or class templates
- Complex boolean algebra of constraints
- Concept hierarchies beyond shallow re-use
- Subsumption-dependent overloading

**Kernel profile:** SHOULD NOT depend on concepts; use traits and `static_assert` instead.

---

## 3. Declarations & Namespaces

### 3.1 Variable Declarations (all profiles)

```cpp
int x = 42;
int y{42};
auto value = compute();
const auto& ref = container[i];
static int counter = 0;
thread_local int tls;  // Kernel: allowed, but no std::thread
```

### 3.2 Function Declarations (all profiles)

**SUPPORTED:**

- Normal functions, `noexcept`, `constexpr`, `inline`, `static`
- Trailing return types: `auto f() -> ReturnType;`
- Attributes: `[[nodiscard]]`, `[[maybe_unused]]`, etc.

**NOT SUPPORTED:**

- `consteval` functions

### 3.3 Namespaces

**SUPPORTED:**

- Nested namespaces: `namespace il::core { ... }`
- Traditional: `namespace il { namespace core { ... } }`
- `using namespace std;` — ALLOWED but DISCOURAGED
- `using std::string;` — RECOMMENDED

**Inline namespaces:** Syntax MAY be accepted but MUST NOT be used in Core; they have no special semantics in Viper C++.
A conforming Viper C++ compiler MAY diagnose inline namespaces as ill-formed.

---

## 4. Classes, Inheritance & RTTI

### 4.1 Class Structure (all profiles)

**FULLY SUPPORTED:**

- All usual members: constructors, destructors, copy/move, static members
- Defaulted / deleted special members
- `explicit` constructors

### 4.2 Inheritance

**ALLOWED:**

- Single inheritance: `class Derived : public Base { ... };`
- "Interface composition": multiple inheritance of pure abstract base classes with no data members

**FORBIDDEN:**

- Multiple inheritance involving any base with data members
- Virtual inheritance (`virtual Base`)

Viper C++ MUST diagnose forbidden inheritance forms as hard errors.

### 4.3 Special Members

All standard special member functions are REQUIRED:

- Default / copy / move constructors
- Copy / move assignment
- Destructor (virtual where needed)

**Guidelines:**

- Use `= default` and `= delete` liberally
- Use `override` for all overriding virtual functions
- Viper C++ SHOULD warn on missing `override` in Core code

### 4.4 RTTI by Profile

| Feature        | Core        | Extended | Kernel    |
|----------------|-------------|----------|-----------|
| `dynamic_cast` | DISCOURAGED | ALLOWED  | FORBIDDEN |
| `typeid`       | DISCOURAGED | ALLOWED  | FORBIDDEN |

**Core policy:** Existing `dynamic_cast` uses SHOULD be migrated to explicit discriminators and helper functions (
`is<T>()`, `as<T>()` style). New Core code MUST NOT introduce RTTI.

**Kernel:** Compiled with `-fno-rtti`. RTTI APIs are not available.

---

## 5. Templates

### 5.1 Supported Forms (all profiles)

**REQUIRED in Core:**

- Class templates with a small number of type parameters
- Function templates
- Variadic templates ONLY for forwarding (`Args&&...`)
- Non-type template parameters for small integers (`std::size_t N`)
- Explicit specializations and partial specializations

### 5.2 Lightweight SFINAE (Core)

Viper C++ supports simple `std::enable_if_t` patterns.

**ALLOWED:**

```cpp
// Partial specializations with single enable_if_t
template <typename T, typename Enable = void>
struct SlotTraits;

template <typename T>
struct SlotTraits<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    // ...
};

// Function templates with single enable_if_t parameter
template <class U = T, class = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Diag>>>
Expected(U&& value);
```

**Conditions MUST be built from:**

- Standard type traits (`std::is_integral_v`, `std::is_same_v`, etc.)
- `&&`, `||`, `!` over those traits

**FORBIDDEN:**

- Nested or recursive `enable_if` games
- Expression-SFINAE (`decltype(...)` tricks)
- Custom trait stacks that become a meta-language

### 5.3 Excluded Template Features (all profiles)

- Template template parameters
- Expression templates
- Heavy meta-programming patterns
- Deep template recursion

---

## 6. Lambdas

### 6.1 Basic Lambdas (all profiles)

**ALLOWED:**

- Normal lambdas: `[] {}`, `[](int x) { ... }`
- Captures: `[=]`, `[&]`, `[x]`, `[&x]`, `[this]`, init-captures
- `mutable` lambdas
- `noexcept` lambdas

### 6.2 Restricted Generic Lambdas (Core/Extended)

Viper C++ MUST support restricted generic lambdas:

```cpp
[](auto lhs, auto rhs, auto* out) { ... }
[&](auto& backend) { ... }
[](const auto& a, const auto& b) { return a < b; }
```

**Restrictions:**

- Parameters MUST be `auto`, `auto&`, `const auto&`, or `auto*`
- No parameter packs in lambdas
- No `template<...>` on the lambda
- No explicit trailing return type for generic lambdas

**Kernel profile:** Generic lambdas are ALLOWED syntactically but SHOULD be minimized.

### 6.3 Excluded Lambda Features

- `constexpr` lambdas (beyond what stdlib requires internally)
- Template lambdas (`[]<typename T>(T x) { ... }`)
- Lambda parameter packs (`[](auto... xs) { ... }`)

---

## 7. Operators

### 7.1 Built-in Operators (all profiles)

All standard operators on fundamental types MUST be supported.

### 7.2 Operator Overloading

**ALLOWED (Core):**

- Equality and ordering: `operator==`, `operator!=`, `operator<`
- Assignment operators
- Stream insertion (`operator<<`) for diagnostics
- Container-style `operator[]`
- Smart pointer `operator*` / `operator->`

**FORBIDDEN (Core):**

- User-defined arithmetic (`operator+`, `operator*`, etc.) unless carefully justified
- Overloading `operator new` / `operator delete` (global or per-class)

### 7.3 Spaceship Operator

`operator<=>` with `= default` is ALLOWED where it simplifies comparisons.

---

## 8. Control Flow

**SUPPORTED (all profiles):**

- `if`, `if constexpr`, `switch`
- `for` (classic), range-for, range-for with structured bindings
- `while`, `do-while`
- `break`, `continue`, `return`
- `goto` — ALLOWED but DISCOURAGED; see §15.2

**Statement attributes:**

- `[[likely]]`, `[[unlikely]]`
- `[[fallthrough]]` in switch statements

---

## 9. Exception Handling

### 9.1 Core & Extended

Exceptions are SUPPORTED but intentionally limited.

**ALLOWED:**

- `throw` of types derived from `std::exception`
- `try` / `catch` with `catch (const std::exception& e)` and `catch (...)`
- `noexcept` on functions

**RECOMMENDED:**

- Prefer `Result<T>` / expected-style APIs for ordinary error handling
- Reserve exceptions for "non-local abort" paths
- Always catch by `const&`

**Cultural rule:** Core code MAY use exceptions; new Core APIs SHOULD default to `Result<T>`-style error reporting
unless there is a strong reason to prefer exceptions.

### 9.2 Kernel

**NO C++ EXCEPTIONS.** Builds MUST succeed with `-fno-exceptions`.

- `throw`, `try`, `catch` are FORBIDDEN
- `noexcept` is effectively the default semantic
- Error handling uses `Result<T>` types or kernel-specific facilities

---

## 10. Standard Library Subset

### 10.1 Core Profile — Required Headers

| Header                                                         | Key Types                                                 |
|----------------------------------------------------------------|-----------------------------------------------------------|
| `<string>`                                                     | `std::string`                                             |
| `<string_view>`                                                | `std::string_view`                                        |
| `<vector>`                                                     | `std::vector`                                             |
| `<array>`                                                      | `std::array`                                              |
| `<optional>`                                                   | `std::optional`, `std::nullopt`                           |
| `<variant>`                                                    | `std::variant`, `std::get_if`, `std::holds_alternative`   |
| `<any>`                                                        | `std::any`, `std::any_cast`                               |
| `<memory>`                                                     | `std::unique_ptr`, `std::make_unique`                     |
| `<utility>`                                                    | `std::move`, `std::forward`, `std::pair`, `std::exchange` |
| `<functional>`                                                 | `std::function`, `std::hash`                              |
| `<algorithm>`                                                  | `std::find`, `std::sort`, `std::any_of`, etc.             |
| `<numeric>`                                                    | `std::accumulate`                                         |
| `<unordered_map>`, `<unordered_set>`, `<map>`, `<set>`         | Associative containers                                    |
| `<sstream>`                                                    | `std::ostringstream`, `std::istringstream`                |
| `<fstream>`                                                    | Basic file I/O                                            |
| `<iostream>`                                                   | `std::cout`, `std::cerr`                                  |
| `<filesystem>`                                                 | `std::filesystem::path`, basic operations                 |
| `<limits>`                                                     | `std::numeric_limits`                                     |
| `<cstdint>`, `<cstddef>`, `<cstring>`, `<cstdio>`, `<cassert>` | C compatibility                                           |
| `<initializer_list>`                                           | `std::initializer_list`                                   |
| `<span>`                                                       | `std::span`                                               |
| `<type_traits>`                                                | Core traits, `std::enable_if`                             |
| `<mutex>`                                                      | `std::mutex`, `std::lock_guard`, `std::unique_lock`       |
| `<concepts>`                                                   | `std::convertible_to`                                     |

### 10.2 Extended Profile — Additional Headers

Extended code (TUI, tests) uses or may introduce the following headers:

| Header                 | Current Usage                                 |
|------------------------|-----------------------------------------------|
| `<regex>`              | **Used** — TUI syntax highlighter (`src/tui`) |
| `<thread>`             | Tests only (`src/tests`)                      |
| `<atomic>`             | Tests only (`src/tests`)                      |
| `<condition_variable>` | Reserved for future use                       |

### 10.3 Kernel Profile — Freestanding Subset

**ALLOWED:**

- `<cstdint>`, `<cstddef>`, `<limits>`, `<type_traits>`
- `<utility>` (`std::move`, `std::forward`)
- `<array>`, `<span>`
- `<optional>`, `<variant>` (if freestanding impl available)

**NOT ALLOWED:**

- `<iostream>`, `<fstream>`, `<sstream>`
- `<string>` (kernel uses its own string type)
- `<mutex>`, `<thread>`, `<atomic>`, `<condition_variable>` — all synchronization comes from kernel-provided primitives
- `<filesystem>`
- Any header requiring general-purpose heap

### 10.4 Excluded from All Profiles

- `<shared_ptr>`, `<weak_ptr>` — prefer `unique_ptr`
- `<coroutine>` — no coroutines
- `<ranges>` — not needed
- `<format>` — not used
- `<expected>` — Viper has its own `Expected` implementation
- Localization / i18n headers

---

## 11. Attributes

**SUPPORTED (all profiles):**

- `[[nodiscard]]`
- `[[maybe_unused]]`
- `[[noreturn]]`
- `[[likely]]`, `[[unlikely]]`
- `[[fallthrough]]`
- `[[deprecated]]`

**EXCLUDED:**

- `[[no_unique_address]]`
- `[[carries_dependency]]`
- Vendor-specific or custom attributes

---

## 12. Preprocessor

**SUPPORTED:**

- `#pragma once` (PREFERRED header guard)
- Traditional include guards: `#ifndef` / `#define` / `#endif`
- `#include` of system and project headers
- `#define` / `#undef` for simple macros
- Conditional compilation: `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`
- `#error` (and `#warning` if supported)

**Predefined macros:**

- `__FILE__`, `__LINE__`, `__func__`, `__cplusplus`
- `__has_include`, `__has_cpp_attribute` where available

**Guidelines:**

- MINIMIZE macro usage
- PREFER `constexpr` for constants
- PREFER templates for function-like behavior

---

## 13. Memory Model & Low-Level Operations

### 13.1 Ownership Patterns

**Core rules:**

- Heap-allocated objects MUST be owned by `std::unique_ptr<T>` or containers
- Shared ownership (`std::shared_ptr`) is effectively FORBIDDEN in Core
- Manual `new` / `delete` are FORBIDDEN

### 13.2 Banned Ownership Patterns

**FORBIDDEN (Core/Kernel):**

- Raw owning pointers (`T* p = new T; delete p;`)
- Owning C-style arrays (`new T[N]` / `delete[]`)
- Custom `operator new` / `operator delete`

### 13.3 Whitelisted Low-Level Casts

`reinterpret_cast` is ALLOWED ONLY in specific patterns:

**Pattern 1: Pointer ↔ void* round-trip**

```cpp
void* p = reinterpret_cast<void*>(objPtr);
auto* obj = reinterpret_cast<MyType*>(p);
```

**Pattern 2: Pointer ↔ integer for tagging/hashing**

```cpp
std::uintptr_t bits = reinterpret_cast<std::uintptr_t>(ptr);
auto* ptr2 = reinterpret_cast<void*>(bits);
```

**Pattern 3: Opaque handle types at subsystem boundaries**

```cpp
auto* impl = reinterpret_cast<rt_string_impl*>(handle);
```

**Pattern 4: Memory-mapped type access (alignment/layout controlled)**

```cpp
out.i64 = *reinterpret_cast<const std::int64_t*>(ptr);
```

**Pattern 5: Function pointer casts for runtime ABI**

**FORBIDDEN patterns:**

- Casting between unrelated object types (`Foo*` → `Bar*`)
- Cases where `static_cast` or `const_cast` would suffice
- Structural punning between polymorphic classes

**Union for bit reinterpretation:** Plain `union` MAY be used as a local helper ONLY for trivial bit reinterpretation
between fundamental types (e.g., `double` ↔ `uint64_t`), with explicit discriminator handling.

---

## 14. Concurrency

### 14.1 Core (Host)

**REQUIRED:**

- `<mutex>` with `std::mutex`, `std::lock_guard`, `std::unique_lock`

**NOT REQUIRED for Core:**

- `<thread>` — no thread creation in Core
- `<atomic>`, `<condition_variable>` — only in tests

### 14.2 Extended

Tests MAY use `std::thread`, `std::atomic`, `std::condition_variable`.

### 14.3 Kernel

**NO standard concurrency library.** Kernel builds MUST NOT include `<thread>`, `<mutex>`, `<atomic>`, or
`<condition_variable>`. All synchronization comes from kernel-provided primitives.

- `thread_local` keyword ALLOWED
- Standard threading/synchronization headers FORBIDDEN

---

## 15. Forbidden & Discouraged Constructs

### 15.1 Hard Errors (Compiler MUST Reject in Core/Kernel)

| Construct                                        | Rationale               |
|--------------------------------------------------|-------------------------|
| Coroutines (`co_await`, `co_yield`, `co_return`) | Complexity, not used    |
| Modules (`module`, `export module`)              | Not mature, not used    |
| Variable-length arrays; `alloca`                 | Stack safety            |
| User-defined literals                            | Not used                |
| `operator new` / `operator delete` overloads     | Memory safety           |
| Multiple inheritance with data members           | Complexity              |
| Virtual inheritance                              | Complexity              |
| Template template parameters                     | Complexity              |
| Inline assembly (`asm`, `__asm__`)               | Use separate `.s` files |

### 15.2 Warnings / Lint Violations (ALLOWED but DISCOURAGED)

| Construct                        | Guidance                       |
|----------------------------------|--------------------------------|
| `goto` across scopes             | Prefer structured control flow |
| C-style casts in C++             | Use `static_cast`, etc.        |
| `reinterpret_cast` outside §13.3 | Must justify                   |
| `dynamic_cast` in Core           | Migrate to discriminators      |
| Exceptions for expected errors   | Prefer `Result<T>`             |
| Raw `new` / `delete` in Extended | Avoid normalizing              |

---

## 16. Style Guidelines (Non-Normative but Enforced in Core)

- `override` on all overridden virtual functions
- `[[nodiscard]]` on functions whose return values must not be ignored
- **Naming:**
    - Namespaces: `lower_snake` (`viper::il`)
    - Types: `PascalCase`
    - Functions/variables: `camelCase`
    - Data members: `camelCase_` (trailing underscore)
    - Macros: `UPPER_SNAKE`
- PREFER `enum class` for new enums
- Keep headers self-contained

---

## 17. Implementation Phases

| Phase | Target                                 | Features                                                                                   |
|-------|----------------------------------------|--------------------------------------------------------------------------------------------|
| 1     | `src/runtime`, `src/lib/graphics`      | C99 subset only                                                                            |
| 2     | `src/{il, vm, support, common, parse}` | Classes, single inheritance, basic templates                                               |
| 3     | `src/codegen`, `src/frontends/basic`   | Move semantics, restricted generic lambdas (§6.2), `if constexpr`, limited concepts (§2.5) |
| 4     | All Core except TUI/tests              | `std::any`, `<mutex>`, full `std::variant`                                                 |
| 5     | `src/tui`, `src/tests`                 | `<regex>`, `<thread>`, RTTI in tests                                                       |
| 6     | Vana kernel                            | Freestanding, no EH, no RTTI                                                               |

---

## 18. Verification Levels

| Level | Requirement                                                  |
|-------|--------------------------------------------------------------|
| 1     | Core builds — Phases 1-4 compile with Viper C++              |
| 2     | Core tests — Unit tests and golden tests pass                |
| 3     | Extended builds — `src/tui` and `src/tests` compile          |
| 4     | Full test suite — 100% pass, output equivalence with Clang   |
| 5     | Self-hosting — Viper C++ builds itself and passes Levels 1-4 |

---

## 19. Viper IL Extensions Required for C++

**This section documents IL features that MUST be added or extended to support compiling Viper C++ to Viper IL.**

### 19.1 Current IL Capabilities (Relevant to C++)

| Feature            | Current Support                         | Notes                                     |
|--------------------|-----------------------------------------|-------------------------------------------|
| Direct calls       | `call @func(args)`                      | ✅ Full support                            |
| Indirect calls     | `call.indirect %ptr(args)`              | ✅ Full support — used for vtable dispatch |
| Exception handling | `eh.push`, `eh.pop`, `trap`, `resume.*` | ⚠️ BASIC-style, not C++ semantics         |
| Type registry      | Runtime `rt_type_registry`              | ⚠️ Basic, needs extension for RTTI        |
| Globals            | `global const`, `global var`            | ✅ Full support                            |
| Memory ops         | `load`, `store`, `alloca`               | ✅ Full support                            |

### 19.2 Required IL Extensions

#### 19.2.1 Scope-Based Cleanup (RAII) — REQUIRED

**Problem:** C++ requires destructor calls during stack unwinding. The current `eh.push/pop` model doesn't support
automatic cleanup.

**Primary mechanism: `cleanup.push/pop/done`**

```il
# Register cleanup handler for current scope
cleanup.push ^cleanupBlock

# Normal code that might unwind
call @mightThrow()

# Remove cleanup handler on normal exit
cleanup.pop
br ^continue

# Cleanup block runs on:
# - Normal scope exit (cleanup.pop reached)
# - Exception unwinding through this scope
cleanupBlock:
  call @MyClass_destructor(%obj)
  cleanup.done  # terminates cleanup, continues unwinding or normal flow
```

**IL changes REQUIRED:**

- New opcode: `cleanup.push ^block` — register cleanup block for current scope
- New opcode: `cleanup.pop` — normal exit, run cleanup then continue
- New opcode: `cleanup.done` — terminates cleanup block
- Verifier: cleanup blocks MUST NOT have normal predecessors
- Verifier: `cleanup.push` and `cleanup.pop` MUST be balanced within a function

**Alternative: `invoke` instruction**

```il
%result = invoke @mayThrow(%arg) to ^continue unwind ^cleanup
```

The `invoke` instruction MAY be implemented as syntactic sugar over explicit `cleanup.push` + `throw.typed` sequences.
It is OPTIONAL; implementors MAY choose to support only `cleanup.*` instructions.

#### 19.2.2 Type-Based Exception Handling — REQUIRED

**Problem:** C++ catches by type (`catch (const MyException& e)`), not by error kind.

**Current IL:** Trap kinds are enum-based (`DivideByZero`, `Overflow`, etc.)

**Proposed extension:**

```il
# Throw with type ID and exception object pointer
throw.typed %typeId, %exceptionObj

# Handler block receives type information
handler(%err: ptr, %typeId: i64, %tok: ResumeTok):
  %isMyEx = icmp.eq %typeId, @MyException_typeId
  cbr %isMyEx, ^handle, ^rethrow
  
handle:
  # ... handle exception ...
  cleanup.done
  
rethrow:
  rethrow %tok  # continue unwinding
```

**IL changes REQUIRED:**

- Extend `Error` record with `type_id: i64` field
- New opcode: `throw.typed %typeId, %obj` — throw typed exception
- New opcode: `rethrow %tok` — continue unwinding after partial handling
- Handler blocks receive type ID parameter

**Exception object lifetime:**

- `%obj` MUST be a `ptr` to a heap-allocated exception object owned by the runtime
- After a matching handler finishes (resumes, rethrows, or returns), the runtime is responsible for destroying and
  freeing the exception object
- Handlers MUST treat `%err: ptr` as borrowed; they MUST NOT free it

#### 19.2.3 RTTI Instructions (Extended Profile Only)

**Problem:** `dynamic_cast` and `typeid` need runtime type introspection.

**Proposed IL additions:**

```il
# Get runtime type ID of object (concrete type, not static type)
%typeId = typeof.obj %obj

# Check if object is instance of TargetType (including base classes)
# Returns true if inherits(runtimeType, targetType)
%isA = isa.dyn %obj, @TargetType_typeId

# Dynamic cast (returns null on failure)
# Equivalent to: isa.dyn ? bitcast : null
%casted = cast.dyn %obj, @TargetType_typeId

# Get type info pointer (for typeid operator)
%info = typeinfo @TargetType
```

**IL changes (Extended profile only):**

- New opcode: `typeof.obj %ptr -> i64` — get concrete runtime type ID
- New opcode: `isa.dyn %ptr, i64 -> i1` — check inheritance chain
- New opcode: `cast.dyn %ptr, i64 -> ptr` — dynamic cast, null on failure
- New opcode: `typeinfo @name -> ptr` — get type info pointer

**Kernel profile:** These opcodes MUST be rejected or stubbed (always return null/false).

#### 19.2.4 Static Initialization — REQUIRED

**Problem:** C++ global objects with constructors must be initialized before `main()`.

**Proposed IL additions:**

```il
# Module-level initialization registration
module.init @MyGlobal_ctor priority=100

# Global with runtime initialization
global var ptr @myGlobal = zeroinit

# Init function called at startup
func @MyGlobal_ctor() -> void {
  %obj = call @MyClass_new()
  store ptr %obj, @myGlobal
  ret void
}
```

**IL changes REQUIRED:**

- New directive: `module.init @func priority=N`
- Module structure: `std::vector<InitEntry> inits` with priority ordering
- VM: call init functions in priority order before `@main`
- Codegen: emit `.init_array` or equivalent

#### 19.2.5 Destructor Convention (No IL Change)

**Approach:** Convention-based, no IL change needed.

```il
# Convention: destructor functions named @TypeName_dtor
func @MyClass_dtor(ptr %this) -> void nothrow {
  # cleanup members, call base destructors
  ret void
}

# Cleanup blocks just call the destructor explicitly
cleanup:
  call @MyClass_dtor(%obj)
  cleanup.done
```

The frontend generates explicit destructor calls; the IL does not need special destructor metadata.

### 19.3 IL Extension Summary

| Extension                                       | Priority     | Profiles       | Notes                  |
|-------------------------------------------------|--------------|----------------|------------------------|
| `cleanup.push/pop/done`                         | **REQUIRED** | Core, Extended | Primary RAII mechanism |
| `throw.typed`                                   | **REQUIRED** | Core, Extended | Type-based exceptions  |
| `rethrow`                                       | **REQUIRED** | Core, Extended | Continue unwinding     |
| `module.init`                                   | **REQUIRED** | Core, Extended | Static initialization  |
| `invoke` instruction                            | OPTIONAL     | Core, Extended | Sugar over cleanup.*   |
| `typeof.obj`, `isa.dyn`, `cast.dyn`, `typeinfo` | OPTIONAL     | Extended only  | RTTI support           |

### 19.4 Runtime Extensions

The C runtime (`src/runtime`) needs these additions:

| Function                               | Purpose                                     |
|----------------------------------------|---------------------------------------------|
| `rt_isa_dynamic(obj, typeId)`          | Check if object's type inherits from target |
| `rt_cast_dynamic(obj, typeId)`         | Dynamic cast, returns null on failure       |
| `rt_get_type_id(obj)`                  | Get object's concrete runtime type ID       |
| `rt_type_inherits(typeId, baseTypeId)` | Check inheritance relationship              |
| `rt_exception_alloc(typeId, size)`     | Allocate exception object                   |
| `rt_exception_free(obj)`               | Free exception object after handling        |

The existing `rt_type_registry.c` provides a foundation but needs:

- Inheritance chain traversal
- Type ID → class info lookup (currently O(n), could be O(1) with hashtable)
- Exception object pool or allocator

---

## 20. Appendix — Feature Usage Snapshot

**Counts are approximate and reflect the 2024-11-24 snapshot; they are not conformance criteria.**

| Feature                  | Approximate Count |
|--------------------------|-------------------|
| `std::string`            | ~4,460            |
| `std::move`              | ~1,000            |
| `std::vector`            | ~900              |
| `std::string_view`       | ~800              |
| `[[nodiscard]]`          | ~760              |
| `noexcept`               | ~630              |
| `constexpr`              | ~430              |
| RTTI uses (mostly tests) | ~276              |
| `std::optional`          | ~240              |
| `mutable`                | ~155              |
| `enum class`             | ~130              |
| `reinterpret_cast`       | ~130              |
| `std::unique_ptr`        | ~100              |
| `std::function`          | ~70               |
| Operator overloads       | ~60               |
| `static_assert`          | ~35               |
| `throw`                  | ~30               |
| `if constexpr`           | ~15               |
| Generic lambdas          | ~15               |
| `std::enable_if`         | ~6                |
| `std::any`               | ~3                |
| `std::mutex`             | ~7                |
| Named concepts           | 1                 |
| `requires` expressions   | ~5                |

---

## 21. Document History

| Version | Date       | Changes                                                                                                                                                                                                                                                                                                                                                                                       |
|---------|------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0.1     | 2024-11-24 | Initial draft from static analysis                                                                                                                                                                                                                                                                                                                                                            |
| 0.2     | 2024-11-24 | Added profiles, generic lambdas, concepts, SFINAE, `<any>`, `<mutex>`, RTTI policy, `reinterpret_cast` whitelist                                                                                                                                                                                                                                                                              |
| 0.3     | 2024-11-25 | Refined wording (MUST/SHOULD/MAY), clarified union handling, profile tagging per feature, marked counts as descriptive, added IL extension plan for C++ (RAII, typed exceptions, RTTI, static init)                                                                                                                                                                                           |
| 0.3.1   | 2024-11-25 | Corrections: clarified inline namespace handling, added `volatile` kernel carve-out, added exception cultural rule, clarified Extended header usage (regex=TUI, thread/atomic=tests), picked `cleanup.*` as REQUIRED RAII mechanism (`invoke` now OPTIONAL), added exception object lifetime semantics, Phase 3 now explicitly mentions restricted generic lambdas, expanded document history |

---

**This specification is a living document. It evolves as Viper C++ is implemented.**
