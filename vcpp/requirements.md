# Viper C++ Compiler Requirements Specification

This document specifies the complete set of C and C++ language features, standard library
components, compiler extensions, and runtime requirements necessary to build the entire
Viper project. It serves as the definitive specification for the Viper C++ system compiler.

---

## Table of Contents

1. [Language Standard Requirements](#1-language-standard-requirements)
2. [C++ Core Language Features](#2-c-core-language-features)
3. [C Core Language Features](#3-c-core-language-features)
4. [C++ Standard Library Requirements](#4-c-standard-library-requirements)
5. [C Standard Library Requirements](#5-c-standard-library-requirements)
6. [Compiler Builtins and Extensions](#6-compiler-builtins-and-extensions)
7. [Preprocessor Requirements](#7-preprocessor-requirements)
8. [Platform and ABI Requirements](#8-platform-and-abi-requirements)
9. [Freestanding Mode Requirements](#9-freestanding-mode-requirements)
10. [Compilation Modes and Flags](#10-compilation-modes-and-flags)

---

## 1. Language Standard Requirements

### Minimum Standards

| Component | Minimum Standard | Notes |
|-----------|------------------|-------|
| C++ | **C++17** | Required for structured bindings, `std::optional`, `std::variant`, `std::string_view` |
| C | **C11** | Required for `_Static_assert`, anonymous structs/unions, `_Alignas`/`_Alignof` |

### Optional C++20 Features (Nice-to-Have)

- `std::remove_cvref_t` (used in `function_ref.hpp`, can be emulated with C++17)
- `[[likely]]` / `[[unlikely]]` attributes
- Concepts (not currently used but may be adopted)

---

## 2. C++ Core Language Features

### 2.1 Classes and Object-Oriented Programming

| Feature | Example | Files Using |
|---------|---------|-------------|
| Class definitions | `class Module { ... };` | All `.hpp` files |
| Public/private/protected access | Member visibility | All classes |
| Constructors/destructors | `Module(); ~Module();` | All classes |
| Copy/move constructors | `Module(Module&&)` | `Module.cpp`, `Function.cpp` |
| Copy/move assignment | `operator=(Module&&)` | `Module.cpp`, `Value.hpp` |
| `= default` specifier | `~Module() = default;` | Many classes |
| `= delete` specifier | `operator=(const X&) = delete;` | `VMContext.hpp` |
| Member initializer lists | `: member_(value)` | All constructors |
| In-class member initializers | `int x = 0;` | `VMContext.hpp`, `SimplifyCFG.hpp` |
| `explicit` constructors | `explicit Module(StringRef)` | Many classes |
| Virtual functions | `virtual void run()` | Transform passes |
| Pure virtual functions | `= 0` | Abstract interfaces |
| Inheritance (single/multiple) | `: public Base` | Pass framework |
| `final` specifier | `class Derived final` | Various |
| `override` specifier | `void run() override` | Virtual overrides |

### 2.2 Templates

| Feature | Example | Files Using |
|---------|---------|-------------|
| Class templates | `template<typename T> class X` | `function_ref.hpp`, containers |
| Function templates | `template<typename T> T func()` | Many utilities |
| Full template specialization | `template<> class X<int>` | Type traits |
| Partial template specialization | `template<typename T> class X<T*>` | Type traits |
| Variadic templates | `template<typename... Args>` | `function_ref.hpp`, tuples |
| Parameter pack expansion | `Args...` | Variadic functions |
| Template alias (`using`) | `using type = T;` | Type traits |
| Non-type template parameters | `template<size_t N>` | `std::array`, constants |
| Template argument deduction | Class template argument deduction | C++17 |
| SFINAE | `std::enable_if_t<...>` | `function_ref.hpp` |

### 2.3 Modern C++11 Features

| Feature | Example | Files Using |
|---------|---------|-------------|
| `auto` type deduction | `auto x = func();` | Throughout codebase |
| `decltype` | `decltype(expr)` | Type inference |
| Range-based for loops | `for (auto& x : container)` | Throughout codebase |
| Initializer lists | `{1, 2, 3}` | Container initialization |
| Uniform initialization | `Type{args}` | Object construction |
| Lambda expressions | `[&](int x) { return x; }` | Algorithms, callbacks |
| Lambda captures | `[=]`, `[&]`, `[this]`, `[x=move(y)]` | Many files |
| Move semantics | `std::move(x)` | Smart pointers, containers |
| Rvalue references | `T&&` | Move constructors |
| Perfect forwarding | `std::forward<T>(x)` | Template utilities |
| `nullptr` | `nullptr` instead of `NULL` | Throughout |
| Scoped enums | `enum class Opcode : uint8_t` | `Instr.hpp`, many enums |
| `noexcept` specifier | `void func() noexcept` | `VMContext.hpp`, many |
| `constexpr` functions | `constexpr int f()` | Constants, compile-time |
| `static_assert` | `static_assert(cond, "msg")` | Type validation |
| `override`/`final` | Virtual function specifiers | OOP hierarchies |
| Default/deleted functions | `= default`, `= delete` | Rule of 5/0 |
| User-defined literals | `operator"" _suffix` | Potential use |
| `thread_local` storage | `thread_local VM* vm;` | VM context, traps |
| Alias templates | `template<class T> using X = Y<T>;` | Type utilities |
| Variadic templates | `template<typename... Ts>` | Tuple, variant |

### 2.4 Modern C++14 Features

| Feature | Example | Files Using |
|---------|---------|-------------|
| Generic lambdas | `[](auto x) { ... }` | Algorithm callbacks |
| Lambda init-captures | `[x = std::move(y)]` | Resource transfer |
| `decltype(auto)` | Return type deduction | Template utilities |
| Variable templates | `template<class T> constexpr T pi` | Constants |
| `[[deprecated]]` attribute | Function deprecation | API evolution |
| Binary literals | `0b1010` | Bit manipulation |
| Digit separators | `1'000'000` | Large numbers |
| `std::make_unique` | `std::make_unique<T>()` | Smart pointer creation |

### 2.5 Modern C++17 Features (Required)

| Feature | Example | Files Using |
|---------|---------|-------------|
| Structured bindings | `auto [a, b] = pair;` | `Lowerer_Stmt.cpp`, TUI |
| `if` with initializer | `if (auto x = f(); x > 0)` | Control flow |
| `std::optional<T>` | `std::optional<Slot>` | `VMContext.hpp`, returns |
| `std::variant<Ts...>` | Discriminated unions | Value types |
| `std::string_view` | Non-owning string ref | `SimplifyCFG.hpp`, I/O |
| `[[nodiscard]]` attribute | Return value warning | Memory allocators |
| `[[maybe_unused]]` attribute | Suppress warnings | Padding, debug |
| `[[noreturn]]` attribute | No-return functions | `exit()`, traps |
| `inline` variables | `inline constexpr int x = 5;` | Header constants |
| Nested namespace definition | `namespace a::b::c { }` | Potential use |
| `constexpr if` | `if constexpr (cond)` | Template metaprogramming |
| Fold expressions | `(args + ...)` | Variadic templates |
| Class template argument deduction | `std::vector v{1,2,3};` | Convenience |
| `std::byte` | Byte manipulation | Binary data |

### 2.6 Namespaces

| Feature | Example | Files Using |
|---------|---------|-------------|
| Namespace declaration | `namespace il::core { }` | All C++ headers |
| Nested namespaces | `namespace il::core::vm { }` | Module organization |
| Anonymous namespaces | `namespace { }` | File-local symbols |
| `using` declarations | `using std::string;` | Convenience |
| `using namespace` | `using namespace std;` | Limited use |
| Inline namespaces | `inline namespace v1 { }` | ABI versioning |

### 2.7 Exception Handling

| Feature | Example | Files Using |
|---------|---------|-------------|
| `try`/`catch`/`throw` | Exception handling | Error paths |
| `noexcept` specifier | `void f() noexcept` | Performance-critical |
| `noexcept(expr)` | Conditional noexcept | Move operations |
| Standard exception types | `std::runtime_error` | Error reporting |

---

## 3. C Core Language Features

### 3.1 C99 Features

| Feature | Example | Files Using |
|---------|---------|-------------|
| Fixed-width integers | `int32_t`, `uint64_t` | All C files |
| `bool` type | `stdbool.h` / `_Bool` | Logic operations |
| Designated initializers | `.field = value` | Struct initialization |
| Compound literals | `(type){values}` | Inline struct creation |
| `inline` functions | `inline int f()` | Performance-critical |
| `restrict` keyword | `void* restrict p` | Aliasing hints |
| `//` comments | Single-line comments | Throughout |
| Variable declarations anywhere | Not just block start | Modern style |
| `snprintf` | Safe string formatting | `stdio.c` |
| `long long` type | 64-bit integers | Large values |
| Variadic macros | `#define LOG(...)` | Debugging |
| `__func__` predefined | Current function name | Debugging |

### 3.2 C11 Features

| Feature | Example | Files Using |
|---------|---------|-------------|
| `_Static_assert` | Compile-time assertions | Structure validation |
| Anonymous structs/unions | Nested without name | Compact layout |
| `_Alignas`/`_Alignof` | Alignment control | Memory layout |
| `_Generic` | Type-generic expressions | Optional |
| `_Noreturn` | Functions that don't return | `abort()`, `exit()` |
| `<stdatomic.h>` concepts | Atomic operations | Concurrency |

### 3.3 Type System

| Category | Types Required |
|----------|----------------|
| Integers | `char`, `short`, `int`, `long`, `long long` (signed/unsigned) |
| Fixed-width | `int8_t`, `int16_t`, `int32_t`, `int64_t`, `uint*_t` variants |
| Size types | `size_t`, `ssize_t`, `ptrdiff_t`, `intptr_t`, `uintptr_t` |
| Floating point | `float`, `double`, `long double` |
| Character | `char`, `unsigned char`, `signed char` |
| Pointer | Pointer types, pointer arithmetic, function pointers |
| Aggregate | `struct`, `union`, arrays |
| Enum | Enumeration types |

### 3.4 Operators and Expressions

| Category | Operators |
|----------|-----------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Bitwise | `&`, `|`, `^`, `~`, `<<`, `>>` |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logical | `&&`, `||`, `!` |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` |
| Increment/Decrement | `++`, `--` (prefix and postfix) |
| Pointer | `*` (dereference), `&` (address-of), `->`, `.` |
| Ternary | `?:` |
| Comma | `,` |
| Sizeof | `sizeof(type)`, `sizeof expr` |
| Cast | `(type)expr` |

---

## 4. C++ Standard Library Requirements

### 4.1 Containers

| Header | Components | Usage |
|--------|------------|-------|
| `<vector>` | `std::vector<T>` | Dynamic arrays throughout |
| `<array>` | `std::array<T, N>` | Fixed-size arrays |
| `<string>` | `std::string` | String manipulation |
| `<map>` | `std::map<K, V>` | Ordered key-value |
| `<set>` | `std::set<T>` | Ordered unique elements |
| `<unordered_map>` | `std::unordered_map<K, V>` | Hash tables |
| `<unordered_set>` | `std::unordered_set<T>` | Hash sets |
| `<deque>` | `std::deque<T>` | Double-ended queue |
| `<list>` | `std::list<T>` | Linked lists |
| `<forward_list>` | `std::forward_list<T>` | Singly-linked list |
| `<queue>` | `std::queue<T>`, `std::priority_queue<T>` | FIFO queues |
| `<stack>` | `std::stack<T>` | LIFO stack |

### 4.2 Utilities

| Header | Components | Usage |
|--------|------------|-------|
| `<optional>` | `std::optional<T>` | Nullable values |
| `<variant>` | `std::variant<Ts...>`, `std::get`, `std::get_if`, `std::holds_alternative` | Tagged unions |
| `<tuple>` | `std::tuple<Ts...>`, `std::get<I>`, `std::make_tuple` | Heterogeneous collections |
| `<pair>` / `<utility>` | `std::pair<T, U>`, `std::make_pair` | Two-element tuples |
| `<memory>` | `std::unique_ptr<T>`, `std::make_unique`, `std::shared_ptr<T>` | Smart pointers |
| `<functional>` | `std::function<Sig>`, `std::invoke`, `std::reference_wrapper` | Callables |
| `<any>` | `std::any` | Type-erased storage |
| `<bitset>` | `std::bitset<N>` | Fixed-size bit arrays |

### 4.3 Algorithms and Iterators

| Header | Components | Usage |
|--------|------------|-------|
| `<algorithm>` | `std::sort`, `std::find`, `std::copy`, `std::transform`, `std::for_each`, `std::remove_if`, etc. | Container algorithms |
| `<numeric>` | `std::accumulate`, `std::iota`, `std::gcd`, `std::lcm` | Numeric algorithms |
| `<iterator>` | Iterator traits, `std::begin`, `std::end`, `std::back_inserter` | Iterator utilities |

### 4.4 Strings and I/O

| Header | Components | Usage |
|--------|------------|-------|
| `<string>` | `std::string`, `std::to_string` | String handling |
| `<string_view>` | `std::string_view` | Non-owning string refs |
| `<sstream>` | `std::stringstream`, `std::ostringstream` | String streams |
| `<iostream>` | `std::cout`, `std::cerr`, `std::cin` | Standard I/O |
| `<fstream>` | `std::ifstream`, `std::ofstream` | File I/O |
| `<iomanip>` | `std::setw`, `std::setfill`, `std::hex` | I/O formatting |
| `<charconv>` | `std::to_chars`, `std::from_chars` | Fast number conversion |

### 4.5 Type Traits and Metaprogramming

| Header | Components | Usage |
|--------|------------|-------|
| `<type_traits>` | `std::enable_if_t`, `std::is_same_v`, `std::remove_cv_t`, `std::remove_reference_t`, `std::decay_t`, `std::is_integral_v`, `std::is_trivially_copyable_v`, etc. | Template metaprogramming |
| `<limits>` | `std::numeric_limits<T>` | Type limits |

### 4.6 Concurrency

| Header | Components | Usage |
|--------|------------|-------|
| `<thread>` | `std::thread` | Thread creation |
| `<mutex>` | `std::mutex`, `std::lock_guard`, `std::unique_lock` | Synchronization |
| `<condition_variable>` | `std::condition_variable` | Thread signaling |
| `<atomic>` | `std::atomic<T>`, `std::atomic_flag` | Atomic operations |
| `<future>` | `std::future`, `std::promise`, `std::async` | Async operations |

### 4.7 Time and Chrono

| Header | Components | Usage |
|--------|------------|-------|
| `<chrono>` | `std::chrono::duration`, `std::chrono::time_point`, `std::chrono::steady_clock` | Time measurement |
| `<ctime>` | `std::time`, `std::clock` | C-style time |

### 4.8 Error Handling

| Header | Components | Usage |
|--------|------------|-------|
| `<exception>` | `std::exception`, `std::terminate` | Exception base |
| `<stdexcept>` | `std::runtime_error`, `std::logic_error`, `std::out_of_range` | Standard exceptions |
| `<system_error>` | `std::error_code`, `std::system_error` | System errors |

### 4.9 C Compatibility Headers

| Header | C Header | Purpose |
|--------|----------|---------|
| `<cstdint>` | `<stdint.h>` | Fixed-width integers |
| `<cstddef>` | `<stddef.h>` | `size_t`, `nullptr_t`, `offsetof` |
| `<cstdlib>` | `<stdlib.h>` | General utilities |
| `<cstdio>` | `<stdio.h>` | C-style I/O |
| `<cstring>` | `<string.h>` | C string functions |
| `<cmath>` | `<math.h>` | Math functions |
| `<cassert>` | `<assert.h>` | Assertions |
| `<climits>` | `<limits.h>` | Integer limits |
| `<cerrno>` | `<errno.h>` | Error numbers |
| `<cctype>` | `<ctype.h>` | Character classification |

---

## 5. C Standard Library Requirements

### 5.1 Core Headers

| Header | Required Functions |
|--------|-------------------|
| `<stdio.h>` | `printf`, `fprintf`, `sprintf`, `snprintf`, `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf`, `fopen`, `fclose`, `fread`, `fwrite`, `fgets`, `fputs`, `fputc`, `fgetc`, `fseek`, `ftell`, `rewind`, `fflush`, `feof`, `ferror`, `perror`, `sscanf`, `getchar`, `putchar`, `puts`, `remove`, `rename`, `tmpfile`, `tmpnam`, `fileno`, `fdopen`, `freopen` |
| `<stdlib.h>` | `malloc`, `calloc`, `realloc`, `free`, `atoi`, `atol`, `atoll`, `atof`, `strtol`, `strtoul`, `strtoll`, `strtoull`, `strtod`, `strtof`, `strtold`, `abs`, `labs`, `llabs`, `div`, `ldiv`, `lldiv`, `exit`, `_Exit`, `abort`, `atexit`, `getenv`, `setenv`, `unsetenv`, `putenv`, `system`, `qsort`, `bsearch`, `rand`, `srand` |
| `<string.h>` | `strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`, `strrchr`, `strstr`, `strpbrk`, `strspn`, `strcspn`, `strtok`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`, `strerror` |
| `<stdint.h>` | All fixed-width types (`int8_t` through `int64_t`, `uint8_t` through `uint64_t`), `intptr_t`, `uintptr_t`, `intmax_t`, `uintmax_t`, `INT*_MIN/MAX`, `UINT*_MAX`, `SIZE_MAX`, `INTPTR_MIN/MAX` |
| `<stddef.h>` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `<stdbool.h>` | `bool`, `true`, `false` |
| `<stdarg.h>` | `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` |

### 5.2 Additional Headers

| Header | Required Functions/Macros |
|--------|--------------------------|
| `<limits.h>` | `CHAR_BIT`, `CHAR_MIN/MAX`, `SCHAR_MIN/MAX`, `UCHAR_MAX`, `SHRT_MIN/MAX`, `USHRT_MAX`, `INT_MIN/MAX`, `UINT_MAX`, `LONG_MIN/MAX`, `ULONG_MAX`, `LLONG_MIN/MAX`, `ULLONG_MAX` |
| `<errno.h>` | `errno`, `ENOENT`, `EINVAL`, `ENOMEM`, `EBADF`, `EACCES`, `EEXIST`, `ETIMEDOUT`, `EINTR`, etc. |
| `<assert.h>` | `assert()` macro |
| `<ctype.h>` | `isalpha`, `isdigit`, `isalnum`, `isspace`, `isupper`, `islower`, `isprint`, `ispunct`, `iscntrl`, `isxdigit`, `toupper`, `tolower` |
| `<math.h>` | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `log2`, `pow`, `sqrt`, `cbrt`, `hypot`, `ceil`, `floor`, `round`, `trunc`, `fmod`, `remainder`, `fabs`, `fmax`, `fmin`, `NAN`, `INFINITY`, `isnan`, `isinf`, `isfinite` |
| `<time.h>` | `time`, `clock`, `difftime`, `mktime`, `localtime`, `gmtime`, `strftime`, `clock_gettime` (POSIX), `CLOCK_REALTIME`, `CLOCK_MONOTONIC` |
| `<signal.h>` | `signal`, `raise`, `SIGINT`, `SIGTERM`, `SIGSEGV`, `SIGABRT` |
| `<setjmp.h>` | `setjmp`, `longjmp`, `jmp_buf` |
| `<inttypes.h>` | `PRId32`, `PRIu64`, `PRIx64`, `SCNd32`, etc. (format specifiers) |
| `<float.h>` | `FLT_MIN`, `FLT_MAX`, `FLT_EPSILON`, `DBL_MIN`, `DBL_MAX`, `DBL_EPSILON`, etc. |

### 5.3 POSIX Extensions (Required for Runtime)

| Header | Required Functions |
|--------|-------------------|
| `<unistd.h>` | `read`, `write`, `close`, `lseek`, `unlink`, `access`, `getcwd`, `chdir`, `sleep`, `usleep`, `sbrk` |
| `<fcntl.h>` | `open`, `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND` |
| `<sys/types.h>` | `off_t`, `ssize_t`, `mode_t`, `pid_t` |
| `<sys/stat.h>` | `stat`, `fstat`, `mkdir`, `S_ISREG`, `S_ISDIR` |
| `<pthread.h>` | `pthread_create`, `pthread_join`, `pthread_detach`, `pthread_self`, `pthread_equal`, `pthread_mutex_init/lock/unlock/destroy`, `pthread_cond_init/wait/timedwait/signal/broadcast/destroy` |
| `<sched.h>` | `sched_yield` |

---

## 6. Compiler Builtins and Extensions

### 6.1 Required GCC/Clang Builtins

| Builtin | Purpose | Usage |
|---------|---------|-------|
| `__builtin_va_list` | Variadic argument list type | `stdarg.h` implementation |
| `__builtin_va_start(ap, last)` | Start variadic processing | Variadic functions |
| `__builtin_va_arg(ap, type)` | Get next variadic argument | Variadic functions |
| `__builtin_va_end(ap)` | End variadic processing | Variadic functions |
| `__builtin_va_copy(dest, src)` | Copy va_list | Variadic forwarding |
| `__builtin_inf()` | Positive infinity | Float special values |
| `__builtin_nan("")` | Quiet NaN | Float special values |
| `__builtin_huge_val()` | HUGE_VAL | Overflow handling |
| `__builtin_expect(expr, val)` | Branch prediction | Performance hints |
| `__builtin_unreachable()` | Unreachable code marker | Optimization |
| `__builtin_assume(cond)` | Assumption for optimizer | Optimization |
| `__builtin_clz(x)` | Count leading zeros | Bit manipulation |
| `__builtin_ctz(x)` | Count trailing zeros | Bit manipulation |
| `__builtin_popcount(x)` | Population count | Bit counting |
| `__builtin_bswap16/32/64(x)` | Byte swap | Endian conversion |
| `__builtin_add_overflow(a, b, res)` | Checked addition | Safe arithmetic |
| `__builtin_sub_overflow(a, b, res)` | Checked subtraction | Safe arithmetic |
| `__builtin_mul_overflow(a, b, res)` | Checked multiplication | Safe arithmetic |
| `__builtin_memcpy(d, s, n)` | Optimized memcpy | Memory operations |
| `__builtin_memset(s, c, n)` | Optimized memset | Memory operations |
| `__builtin_memmove(d, s, n)` | Optimized memmove | Memory operations |

### 6.2 Atomic Builtins (GCC-style)

| Builtin | Purpose |
|---------|---------|
| `__atomic_load_n(ptr, order)` | Atomic load |
| `__atomic_store_n(ptr, val, order)` | Atomic store |
| `__atomic_exchange_n(ptr, val, order)` | Atomic exchange |
| `__atomic_compare_exchange_n(ptr, expected, desired, weak, succ, fail)` | CAS operation |
| `__atomic_fetch_add(ptr, val, order)` | Atomic fetch-and-add |
| `__atomic_fetch_sub(ptr, val, order)` | Atomic fetch-and-subtract |
| `__atomic_fetch_and(ptr, val, order)` | Atomic fetch-and-AND |
| `__atomic_fetch_or(ptr, val, order)` | Atomic fetch-and-OR |
| `__atomic_fetch_xor(ptr, val, order)` | Atomic fetch-and-XOR |

Memory order constants:
- `__ATOMIC_RELAXED`
- `__ATOMIC_CONSUME`
- `__ATOMIC_ACQUIRE`
- `__ATOMIC_RELEASE`
- `__ATOMIC_ACQ_REL`
- `__ATOMIC_SEQ_CST`

### 6.3 Type Attributes

| Attribute | Purpose | Example |
|-----------|---------|---------|
| `__attribute__((packed))` | Remove padding | Compact structs |
| `__attribute__((aligned(N)))` | Alignment control | Memory layout |
| `__attribute__((unused))` | Suppress unused warnings | Optional params |
| `__attribute__((noreturn))` | No-return function | `abort()`, `exit()` |
| `__attribute__((noinline))` | Prevent inlining | Debug support |
| `__attribute__((always_inline))` | Force inlining | Critical paths |
| `__attribute__((visibility("default")))` | Symbol visibility | Shared libraries |
| `__attribute__((weak))` | Weak symbol | Optional implementations |
| `__attribute__((constructor))` | Run before main | Initialization |
| `__attribute__((destructor))` | Run after main | Cleanup |
| `__attribute__((format(printf, N, M)))` | Format string checking | Printf-like |

---

## 7. Preprocessor Requirements

### 7.1 Standard Directives

| Directive | Description |
|-----------|-------------|
| `#include "file"` | Include local header |
| `#include <file>` | Include system header |
| `#define NAME value` | Simple macro |
| `#define FUNC(args)` | Function-like macro |
| `#define VARIADIC(...)` | Variadic macro |
| `#undef NAME` | Undefine macro |
| `#ifdef NAME` | Conditional on definition |
| `#ifndef NAME` | Conditional on non-definition |
| `#if expression` | Conditional on expression |
| `#elif expression` | Else-if branch |
| `#else` | Else branch |
| `#endif` | End conditional |
| `#pragma once` | Include guard (preferred) |
| `#pragma pack(push/pop)` | Packing control |
| `#error "message"` | Compilation error |
| `#warning "message"` | Compilation warning |
| `#line` | Line number override |

### 7.2 Predefined Macros

| Macro | Purpose |
|-------|---------|
| `__FILE__` | Current filename |
| `__LINE__` | Current line number |
| `__func__` / `__FUNCTION__` | Current function name |
| `__DATE__` | Compilation date |
| `__TIME__` | Compilation time |
| `__cplusplus` | C++ standard version |
| `__STDC_VERSION__` | C standard version |
| `__GNUC__` | GCC major version |
| `__clang__` | Clang compiler |
| `NDEBUG` | Release mode |

### 7.3 Platform Detection Macros

| Macro | Platform |
|-------|----------|
| `_WIN32` | Windows (32 or 64-bit) |
| `_WIN64` | Windows 64-bit |
| `__APPLE__` | macOS/iOS |
| `__linux__` | Linux |
| `__unix__` | Unix-like systems |
| `__ANDROID__` | Android |
| `__viperos__` | Viper OS (custom) |

### 7.4 Architecture Detection

| Macro | Architecture |
|-------|--------------|
| `__x86_64__` / `_M_X64` | x86-64 |
| `__i386__` / `_M_IX86` | x86 32-bit |
| `__aarch64__` / `_M_ARM64` | ARM64 |
| `__arm__` / `_M_ARM` | ARM 32-bit |
| `__riscv` | RISC-V |

---

## 8. Platform and ABI Requirements

### 8.1 Calling Conventions

| Platform | Convention | Requirements |
|----------|------------|--------------|
| x86-64 Linux/macOS | System V AMD64 | 6 integer regs (RDI, RSI, RDX, RCX, R8, R9), 8 XMM regs for FP, 128-byte red zone |
| x86-64 Windows | Microsoft x64 | 4 regs (RCX, RDX, R8, R9), shadow space, different struct passing |
| ARM64 | AAPCS64 | 8 integer regs (X0-X7), 8 FP regs (V0-V7), no red zone |

### 8.2 Data Type Requirements

| Type | Size | Alignment | Notes |
|------|------|-----------|-------|
| `char` | 1 byte | 1 byte | May be signed or unsigned |
| `short` | 2 bytes | 2 bytes | |
| `int` | 4 bytes | 4 bytes | |
| `long` | 8 bytes (LP64) / 4 bytes (LLP64) | Same as size | Linux/macOS: 8, Windows: 4 |
| `long long` | 8 bytes | 8 bytes | |
| `float` | 4 bytes | 4 bytes | IEEE 754 single |
| `double` | 8 bytes | 8 bytes | IEEE 754 double |
| `long double` | 16 bytes (x86) / 8 bytes (ARM) | 16 bytes | Platform-dependent |
| Pointers | 8 bytes | 8 bytes | 64-bit systems |
| `size_t` | 8 bytes | 8 bytes | 64-bit systems |

### 8.3 Object File Format

| Platform | Format |
|----------|--------|
| Linux | ELF64 |
| macOS | Mach-O 64-bit |
| Windows | PE/COFF 64-bit |
| Viper OS | ELF64 (custom) |

---

## 9. Freestanding Mode Requirements

The Viper OS libc operates in freestanding mode with these requirements:

### 9.1 No Standard Library Dependencies

The freestanding environment must NOT require:
- Exception handling runtime (`libcxxabi`, `libgcc_eh`)
- RTTI support
- Global constructors/destructors (unless explicitly used)
- Thread-local storage runtime (must use compiler builtins)
- Dynamic linking infrastructure

### 9.2 Required Freestanding Headers

| Header | Purpose |
|--------|---------|
| `<stddef.h>` | `size_t`, `NULL`, `offsetof` |
| `<stdint.h>` | Fixed-width integers |
| `<stdarg.h>` | Variadic arguments (via builtins) |
| `<stdbool.h>` | Boolean type |
| `<limits.h>` | Integer limits |
| `<float.h>` | Floating-point limits |
| `<stdnoreturn.h>` | `noreturn` macro |
| `<stdalign.h>` | Alignment macros |

### 9.3 Compiler Flags for Freestanding

```
-ffreestanding
-fno-exceptions
-fno-rtti
-fno-unwind-tables
-fno-asynchronous-unwind-tables
-fno-stack-protector
-mno-red-zone (x86-64 kernel)
-nostdlib
-nostdinc (when providing custom headers)
```

---

## 10. Compilation Modes and Flags

### 10.1 Required Compiler Flags

```bash
# C++ Standard
-std=c++17

# C Standard
-std=c11

# Warnings (recommended)
-Wall
-Wextra
-Wpedantic
-Werror=return-type
-Werror=uninitialized

# Debug Mode
-g
-O0
-DDEBUG

# Release Mode
-O2 or -O3
-DNDEBUG

# Position Independent Code (for shared libs)
-fPIC

# Sanitizers (development)
-fsanitize=address
-fsanitize=undefined
-fsanitize=thread
```

### 10.2 Linker Requirements

| Feature | Requirement |
|---------|-------------|
| Static linking | Full static library support |
| Dynamic linking | Shared library support (`.so`, `.dylib`) |
| LTO | Link-Time Optimization support |
| Dead code elimination | `-gc-sections` / `--dead-strip` |
| Symbol visibility | Hidden by default, explicit exports |
| Debug info | DWARF format support |

### 10.3 Build System Integration

The compiler must support:
- CMake toolchain files
- Custom sysroot specification
- Cross-compilation
- Multiple output formats (object, assembly, LLVM IR/BC)

---

## Appendix A: Feature Test Matrix

| Feature | GCC | Clang | MSVC | Notes |
|---------|-----|-------|------|-------|
| C++17 | 7+ | 5+ | 19.14+ | Structured bindings, optional, variant |
| C11 | 4.6+ | 3.1+ | N/A | MSVC has limited C11 support |
| Atomics | 4.7+ | 3.1+ | 19.0+ | GCC-style builtins |
| Thread-local | 4.8+ | 3.3+ | 19.0+ | `thread_local` keyword |
| constexpr | 4.6+ | 3.1+ | 19.0+ | Full C++14 constexpr: GCC 5+, Clang 3.4+ |

---

## Appendix B: Minimum Compiler Versions

| Compiler | Minimum Version | Recommended |
|----------|-----------------|-------------|
| GCC | 8.0 | 11+ |
| Clang | 7.0 | 15+ |
| Apple Clang | 10.0 | 14+ |

---

## Appendix C: Test Program

The following program can be used to verify compiler compliance:

```cpp
// vcpp_test.cpp - Compiler compliance test
#include <optional>
#include <variant>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>

// C++17 structured bindings
std::pair<int, int> get_pair() { return {1, 2}; }

// Templates with SFINAE
template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T double_value(T x) { return x * 2; }

// Lambda with captures
auto make_adder(int x) {
    return [x](int y) { return x + y; };
}

// constexpr
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

// thread_local
thread_local int tls_value = 0;

// noexcept and nodiscard
[[nodiscard]] int get_value() noexcept { return 42; }

int main() {
    // Structured binding
    auto [a, b] = get_pair();

    // Optional
    std::optional<int> opt = 42;

    // Variant
    std::variant<int, double> var = 3.14;

    // String view
    std::string_view sv = "hello";

    // Smart pointers
    auto ptr = std::make_unique<int>(42);

    // constexpr
    static_assert(factorial(5) == 120);

    // Lambda
    auto add5 = make_adder(5);

    return add5(a) + *opt + static_cast<int>(std::get<double>(var));
}
```

Compile with:
```bash
clang++ -std=c++17 -Wall -Wextra vcpp_test.cpp -o vcpp_test
```

---

*Document Version: 1.0*
*Generated for Viper Project System Compiler Specification*
