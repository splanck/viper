# ViperOS C++ Profile

This document defines the **ViperOS C++ Profile** — a minimal, well-defined subset of C++20 required to compile Viper OS kernel and userspace. This profile enables staged compiler development while ensuring the OS can be built incrementally.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Profile Tiers](#2-profile-tiers)
3. [Tier 1: Freestanding Kernel Core](#3-tier-1-freestanding-kernel-core)
4. [Tier 2: Full Kernel Support](#4-tier-2-full-kernel-support)
5. [Tier 3: Userspace Support](#5-tier-3-userspace-support)
6. [Tier 4: Full C++20](#6-tier-4-full-c20)
7. [Explicitly Excluded Features](#7-explicitly-excluded-features)
8. [Required Compiler Flags](#8-required-compiler-flags)
9. [ABI Requirements](#9-abi-requirements)
10. [Implementation Priority](#10-implementation-priority)

---

## 1. Overview

### Purpose

The ViperOS C++ Profile defines:
- **Minimum viable C++** needed for Viper OS kernel and userspace
- **Staged implementation targets** for the vcpp compiler
- **Explicit exclusions** of features not needed for embedded/OS development
- **Compiler flags** required for freestanding/OS environments

### Design Principles

1. **Freestanding First**: Core kernel can be built with minimal runtime
2. **No Exceptions Required**: Kernel uses `-fno-exceptions`
3. **No RTTI Required**: Kernel uses `-fno-rtti`
4. **Minimal Standard Library**: Only freestanding headers required initially
5. **Incremental Capability**: Each tier adds features without breaking previous

---

## 2. Profile Tiers

| Tier | Name | Target | Features | Milestone |
|------|------|--------|----------|-----------|
| 1 | Freestanding Kernel Core | Kernel boot, HAL | Classes, templates, constexpr | Early C++ |
| 2 | Full Kernel Support | Complete kernel | Virtual functions, RAII, lambdas | Mid C++ |
| 3 | Userspace Support | User applications | Hosted std::, exceptions (optional) | Late C++ |
| 4 | Full C++20 | Complete support | Concepts, modules, coroutines | Complete |

### Tier Validation Tests

Each tier is considered complete when it passes these validation criteria:

| Tier | Validation Criteria | Test |
|------|---------------------|------|
| **Tier 1** | Compiles kernel boot code | `kernel/boot/`, `kernel/arch/`, `kernel/lib/` compile successfully |
| **Tier 2** | Compiles entire kernel; boots | Full kernel compiles; OS boots to shell prompt |
| **Tier 3** | Compiles userspace programs | `user/` programs compile; shell commands execute |
| **Tier 4** | Self-hosting compiler | vcpp compiles vcpp (three-stage bootstrap validates) |

---

## 3. Tier 1: Freestanding Kernel Core

**Goal:** Compile kernel boot code, HAL, and core data structures.

### 3.1 Required Language Features

#### Basic C++ Features
- [x] Classes and structs (with access specifiers)
- [x] Member functions (including const members)
- [x] Constructors and destructors
- [x] Copy constructors and copy assignment
- [x] Move constructors and move assignment
- [x] `explicit` keyword
- [x] `friend` declarations
- [x] Nested classes
- [x] Operator overloading

#### Type Deduction
- [x] `auto` variable declarations
- [x] `auto` return types
- [x] `decltype(expr)`

#### Constants and Compile-Time
- [x] `constexpr` variables
- [x] `constexpr` functions (basic)
- [x] `static_assert(condition, message)`

#### References
- [x] Lvalue references (`T&`)
- [x] Rvalue references (`T&&`)
- [x] `std::move` semantics (as library function)
- [x] `std::forward` semantics

#### Initialization
- [x] Uniform initialization (`Type{args}`)
- [x] Initializer lists
- [x] In-class member initializers
- [x] Designated initializers (C++20)

#### Namespaces
- [x] Namespace declarations
- [x] Nested namespaces (`namespace a::b::c`)
- [x] Using declarations
- [x] Anonymous namespaces

#### Enumerations
- [x] Scoped enums (`enum class`)
- [x] Underlying type specification

#### Templates (Basic)
- [x] Class templates
- [x] Function templates
- [x] Non-type template parameters
- [x] Template specialization (full)

#### Control Flow
- [x] Range-based for loops
- [x] `if` with initializer (C++17)

#### Miscellaneous
- [x] `nullptr` keyword
- [x] `noexcept` specifier
- [x] `[[nodiscard]]` attribute
- [x] `[[maybe_unused]]` attribute
- [x] `extern "C"` linkage
- [x] Inline variables

### 3.2 Required Headers (Freestanding)

| Header | Purpose |
|--------|---------|
| `<cstdint>` | Fixed-width integer types |
| `<cstddef>` | `size_t`, `nullptr_t`, `ptrdiff_t` |
| `<climits>` | Integer limits |
| `<cfloat>` | Floating-point limits |
| `<limits>` | `std::numeric_limits` |
| `<type_traits>` | Type introspection |
| `<initializer_list>` | `std::initializer_list` |
| `<utility>` | `std::move`, `std::forward`, `std::pair` |
| `<new>` | Placement new (no allocating `new`) |
| `<cstdarg>` | Variadic argument macros |

### 3.3 Required Builtins

| Builtin | Purpose |
|---------|---------|
| `__builtin_va_*` | Variadic argument handling |
| `__builtin_offsetof` | Structure member offset |
| `__builtin_expect` | Branch prediction hints |
| `__builtin_unreachable` | Unreachable code marker |

### 3.4 NOT Required in Tier 1

- Exceptions (`throw`, `try`, `catch`)
- RTTI (`dynamic_cast`, `typeid`)
- Virtual functions
- Lambdas
- Template parameter packs
- Standard library containers

---

## 4. Tier 2: Full Kernel Support

**Goal:** Complete kernel with memory management, scheduling, and device drivers.

### 4.1 Additional Language Features

#### Virtual Functions
- [x] `virtual` functions
- [x] Pure virtual functions (`= 0`)
- [x] `override` specifier
- [x] `final` specifier
- [x] Virtual destructors
- [x] Multiple inheritance
- [x] Virtual inheritance

#### Templates (Advanced)
- [x] Partial template specialization
- [x] Template parameter packs (`typename... Ts`)
- [x] Pack expansion (`Ts...`)
- [x] Fold expressions (`(args + ...)`)
- [x] Alias templates (`template<class T> using X = Y<T>`)
- [x] Variable templates
- [x] `if constexpr`
- [x] SFINAE

#### Lambdas
- [x] Basic lambdas (`[]() {}`)
- [x] Capture by value/reference
- [x] Generic lambdas (`[](auto x)`)
- [x] Init captures (`[x = std::move(y)]`)
- [x] `mutable` lambdas

#### Other Features
- [x] Structured bindings (`auto [a, b] = expr`)
- [x] `constexpr if`
- [x] `inline` variables
- [x] Defaulted/deleted functions (`= default`, `= delete`)
- [x] Conversion operators

### 4.2 Required Type Traits

```cpp
// Tier 2 requires these type traits to function
std::is_same, std::is_same_v
std::is_integral, std::is_integral_v
std::is_floating_point
std::is_pointer
std::is_reference
std::is_const
std::is_trivially_copyable
std::is_trivially_destructible
std::remove_cv, std::remove_cv_t
std::remove_reference, std::remove_reference_t
std::remove_pointer
std::decay, std::decay_t
std::enable_if, std::enable_if_t
std::conditional, std::conditional_t
std::underlying_type
std::void_t
```

**Note:** These type traits are implemented in library headers (`<type_traits>`), but some require compiler intrinsics for correctness:

### 4.2.1 Compiler Intrinsics for Type Traits

| Type Trait | Compiler Intrinsic Required |
|------------|----------------------------|
| `is_trivially_copyable<T>` | `__is_trivially_copyable(T)` |
| `is_trivially_destructible<T>` | `__is_trivially_destructible(T)` |
| `is_trivially_constructible<T>` | `__is_trivially_constructible(T, Args...)` |
| `is_standard_layout<T>` | `__is_standard_layout(T)` |
| `is_pod<T>` | `__is_pod(T)` |
| `is_empty<T>` | `__is_empty(T)` |
| `is_polymorphic<T>` | `__is_polymorphic(T)` |
| `is_abstract<T>` | `__is_abstract(T)` |
| `is_final<T>` | `__is_final(T)` |
| `is_enum<T>` | `__is_enum(T)` |
| `is_union<T>` | `__is_union(T)` |
| `is_class<T>` | `__is_class(T)` |
| `is_base_of<B, D>` | `__is_base_of(B, D)` |
| `is_convertible<From, To>` | `__is_convertible_to(From, To)` |
| `underlying_type<E>` | `__underlying_type(E)` |
| `is_aggregate<T>` | `__is_aggregate(T)` |

**Implementation Note:** vcpp compiles against custom libc++ headers. The compiler must provide these intrinsics; the library headers wrap them into standard type traits.

### 4.2.2 SFINAE Implementation Note

Tier 2 requires SFINAE (Substitution Failure Is Not An Error), which is one of the hardest C++ features to implement correctly.

**Implementation Strategy:**
1. Start with hard errors on substitution failure (Tier 1 behavior)
2. Add "soft" failure mode that discards candidates (Tier 2)
3. Track substitution context to enable proper error recovery
4. Implement proper candidate set filtering for overload resolution

**Test Cases:**
- `std::enable_if` usage in kernel `Result<T>` class
- Function overload selection with template constraints
- Partial specialization matching

### 4.3 C++ ABI Requirements

| Requirement | Specification |
|-------------|---------------|
| Name mangling | Itanium C++ ABI |
| vtable layout | Itanium C++ ABI |
| RTTI (minimal) | Type info for virtual classes |
| `__cxa_pure_virtual` | Pure virtual call handler |
| `__cxa_deleted_virtual` | Deleted virtual call handler |
| Guard variables | `__cxa_guard_*` for static init |

---

## 5. Tier 3: Userspace Support

**Goal:** Full userspace application support with hosted standard library.

### 5.1 Additional Language Features

#### Exceptions (Optional)
- [ ] `throw` expressions
- [ ] `try`/`catch` blocks
- [ ] Exception specifications
- [ ] Stack unwinding
- [ ] `std::exception` hierarchy

Note: Userspace may use exceptions, but they are optional. Many Viper userspace programs use `-fno-exceptions`.

#### Additional C++17 Features
- [x] Class template argument deduction (CTAD)
- [x] Deduction guides
- [x] `std::optional`, `std::variant`, `std::any`
- [x] `std::string_view`

### 5.2 Required Standard Library Headers

| Category | Headers |
|----------|---------|
| Containers | `<vector>`, `<array>`, `<string>`, `<map>`, `<set>`, `<unordered_map>`, `<unordered_set>` |
| Utilities | `<optional>`, `<variant>`, `<tuple>`, `<functional>` |
| Memory | `<memory>` (`unique_ptr`, `shared_ptr`) |
| Algorithms | `<algorithm>`, `<numeric>` |
| I/O | `<iostream>`, `<fstream>`, `<sstream>` |
| Strings | `<string>`, `<string_view>`, `<charconv>` |
| Time | `<chrono>` |

### 5.3 C++ Runtime Requirements

| Component | Purpose |
|-----------|---------|
| `__cxa_atexit` | Static destructor registration |
| `__cxa_finalize` | Static destructor execution |
| Global constructors | `.init_array` section handling |
| Global destructors | `.fini_array` section handling |
| Operator new/delete | Dynamic memory allocation |

---

## 6. Tier 4: Full C++20

**Goal:** Complete C++20 compliance for future-proofing.

### 6.1 C++20 Features

#### Concepts
- [ ] Concept definitions
- [ ] Requires expressions
- [ ] Constrained templates
- [ ] Abbreviated function templates

#### Three-Way Comparison
- [ ] Spaceship operator (`<=>`)
- [ ] `std::strong_ordering`, `std::weak_ordering`, `std::partial_ordering`

#### Other C++20 Features
- [ ] `consteval` functions
- [ ] `constinit` variables
- [ ] `char8_t` type
- [ ] Lambda improvements (template params, `[=, this]`)
- [ ] `[[likely]]` / `[[unlikely]]` attributes
- [ ] `[[no_unique_address]]` attribute
- [ ] Range-based for with initializer
- [ ] Designated initializers improvements
- [ ] Feature test macros

#### Modules (Deferred)
- [ ] Module declarations
- [ ] Export declarations
- [ ] Module partitions

#### Coroutines (Deferred)
- [ ] `co_await`, `co_yield`, `co_return`
- [ ] Coroutine transformation

---

## 7. Explicitly Excluded Features

These features are **NOT required** for ViperOS and may be omitted or implemented last:

### 7.1 Never Required

| Feature | Reason |
|---------|--------|
| Digraphs/Trigraphs | Legacy compatibility only |
| `export` (C++98) | Never widely implemented, removed |
| `register` keyword | Deprecated, hint only |
| K&R function declarations | Legacy C compatibility |

### 7.2 Deferred Indefinitely

| Feature | Reason |
|---------|--------|
| C++20 Modules | Complex implementation, not used in OS |
| C++20 Coroutines | Can use stackful coroutines instead |
| `std::regex` | Heavy, not used in kernel/core userspace |
| `std::locale` | Minimal i18n in OS currently |
| Wide string literals | Not used in codebase |

---

## 8. Required Compiler Flags

### 8.1 Kernel Compilation

```bash
# Mandatory kernel flags
-std=c++20
-ffreestanding
-fno-exceptions
-fno-rtti
-fno-threadsafe-statics
-fno-stack-protector
-mgeneral-regs-only        # AArch64: no FPU in kernel
-mcpu=cortex-a72           # Target CPU

# Linker flags
-nostdlib
-nostartfiles
```

### 8.2 Userspace Compilation

```bash
# Standard userspace flags
-std=c++20
-fno-exceptions            # Optional: can be omitted for exception support
-fno-rtti                  # Optional: can be omitted for RTTI support
```

### 8.3 vcpp-Specific Defines

| Define | Value | Purpose |
|--------|-------|---------|
| `__viperos__` | 1 | Viper OS target detection |
| `__aarch64__` | 1 | AArch64 architecture |
| `__LP64__` | 1 | LP64 data model |
| `__STDC_HOSTED__` | 0 (kernel), 1 (user) | Freestanding/hosted |

---

## 9. ABI Requirements

### 9.1 Calling Convention

| Aspect | Specification |
|--------|---------------|
| Base ABI | AAPCS64 (ARM 64-bit) |
| Integer args | X0-X7 |
| FP/SIMD args | V0-V7 (userspace only) |
| Return | X0, X1 (or V0-V3 for FP) |
| Stack alignment | 16 bytes |
| Frame pointer | X29 (optional) |

### 9.2 C++ ABI

| Aspect | Specification |
|--------|---------------|
| Name mangling | Itanium C++ ABI |
| Exception handling | DWARF-based (when enabled) |
| RTTI layout | Itanium C++ ABI |
| vtable layout | Itanium C++ ABI |

### 9.3 Object Layout

| Type | Size | Alignment |
|------|------|-----------|
| `bool` | 1 | 1 |
| `char` | 1 | 1 (unsigned) |
| `short` | 2 | 2 |
| `int` | 4 | 4 |
| `long` | 8 | 8 |
| `long long` | 8 | 8 |
| `float` | 4 | 4 |
| `double` | 8 | 8 |
| `long double` | 16 | 16 |
| pointers | 8 | 8 |
| `wchar_t` | 4 | 4 (signed) |

---

## 10. Implementation Priority

### Phase Mapping to Compiler Development

| vcpp Phase | ViperOS Tier | Deliverable |
|------------|--------------|-------------|
| Phase 4 (C++ Frontend) | Tier 1 | Kernel boot, basic drivers |
| Phase 6 (Advanced C++) | Tier 2 | Full kernel, all drivers |
| Phase 7 (Standard Library) | Tier 3 | User applications |
| Phase 9 (Self-Hosting) | Tier 4 | Complete C++20 (for self-hosting) |

### Feature Implementation Order

**Critical Path (implement first):**
1. Classes with constructors/destructors
2. Templates (class and function)
3. `constexpr` functions
4. Move semantics
5. Operator overloading
6. Namespaces
7. `auto` type deduction

**High Priority:**
8. Virtual functions and vtables
9. Multiple inheritance
10. Lambdas
11. Template parameter packs
12. `if constexpr`
13. Structured bindings

**Medium Priority:**
14. SFINAE
15. CTAD
16. `std::optional`, `std::variant`
17. Standard library containers

**Low Priority:**
18. Exceptions (for userspace)
19. Full RTTI
20. Concepts
21. Three-way comparison

---

## Appendix A: Kernel Code Patterns

### A.1 Typical Kernel Class

```cpp
// Tier 1 compatible
class SpinLock {
public:
    SpinLock() = default;
    ~SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept;
    void unlock() noexcept;
    [[nodiscard]] bool try_lock() noexcept;

private:
    std::atomic<bool> m_locked{false};
};
```

### A.2 Typical Template Usage

```cpp
// Tier 1 compatible
template<typename T, size_t Capacity>
class StaticVector {
public:
    constexpr StaticVector() = default;

    void push_back(const T& value);
    void push_back(T&& value);

    T& operator[](size_t index) noexcept;
    const T& operator[](size_t index) const noexcept;

    [[nodiscard]] constexpr size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

private:
    alignas(T) std::byte m_storage[sizeof(T) * Capacity];
    size_t m_size{0};
};
```

### A.3 RAII Pattern

```cpp
// Tier 2 compatible (requires lambdas or virtual functions)
template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) : m_func(std::forward<F>(f)), m_active(true) {}
    ~ScopeGuard() { if (m_active) m_func(); }

    void dismiss() noexcept { m_active = false; }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F m_func;
    bool m_active;
};
```

---

*ViperOS C++ Profile v1.0*
