---
status: active
audience: developers
last-updated: 2025-01-19
---

# How to Extend the Viper Runtime

Complete implementation guide for adding new classes, methods, and static functions to the Viper runtime library. This document walks through every step required to expose new functionality to Viper programs.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Architecture Overview](#2-architecture-overview)
3. [Quick Start: Adding a Simple Function](#3-quick-start-adding-a-simple-function)
4. [Adding a New Runtime Class](#4-adding-a-new-runtime-class)
5. [Definition File Syntax](#5-definition-file-syntax)
6. [Code Generation with rtgen](#6-code-generation-with-rtgen)
7. [Frontend Integration](#7-frontend-integration)
8. [CMake Build Integration](#8-cmake-build-integration)
9. [Testing Your Extension](#9-testing-your-extension)
10. [Complete Example: Counter Class](#10-complete-example-counter-class)
11. [Common Patterns](#11-common-patterns)
12. [Troubleshooting](#12-troubleshooting)
13. [Reference Materials](#13-reference-materials)

---

## 1. Introduction

### What You'll Learn

This guide teaches you how to extend the Viper runtime with:

- **Static functions**: Standalone utility functions (like `Viper.Math.Sin`)
- **Classes with methods**: Object-oriented types (like `Viper.Collections.Map`)
- **Properties**: Getter/setter pairs on class instances
- **Constructor functions**: Factory functions for creating class instances

### When to Extend the Runtime

Extend the runtime when you need to:

- Expose platform-specific functionality (file I/O, networking, graphics)
- Provide high-performance operations implemented in C
- Add new data structures or algorithms
- Interface with external C libraries

### Prerequisites

- Familiarity with C programming
- Basic understanding of the Viper build system (CMake)
- Knowledge of Viper IL type system (see [IL Guide](il-guide.md))

---

## 2. Architecture Overview

### The Runtime Extension Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        RUNTIME EXTENSION PIPELINE                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. C Implementation        2. Definition File       3. Code Generation     │
│  ┌─────────────────┐        ┌─────────────────┐      ┌─────────────────┐    │
│  │ rt_counter.c    │        │ runtime.def     │      │ rtgen tool      │    │
│  │ rt_counter.h    │───────▶│ RT_FUNC(...)    │─────▶│ (build time)    │    │
│  │                 │        │ RT_CLASS_BEGIN  │      │                 │    │
│  └─────────────────┘        └─────────────────┘      └────────┬────────┘    │
│                                                                │             │
│                                                                ▼             │
│  4. Generated Headers       5. Frontend Integration  6. Usage in Viper      │
│  ┌─────────────────┐        ┌─────────────────┐      ┌─────────────────┐    │
│  │ RuntimeNameMap  │        │ RuntimeMethod   │      │ Dim c = Counter │    │
│  │ RuntimeClasses  │───────▶│ Index (BASIC)   │─────▶│ c.Increment()   │    │
│  │ RuntimeSigs     │        │ Lowerer (Zia)   │      │ Print c.Value   │    │
│  └─────────────────┘        └─────────────────┘      └─────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Location | Purpose |
|-----------|----------|---------|
| **Runtime Library** | `src/runtime/` | C implementation of runtime functions |
| **Definition File** | `src/il/runtime/runtime.def` | Single source of truth for all runtime metadata |
| **rtgen Tool** | `src/tools/rtgen/` | Build-time code generator |
| **Generated Headers** | `src/il/runtime/generated/` | Auto-generated `.inc` files |
| **RuntimeClasses** | `src/il/runtime/classes/` | C++ wrappers for OOP integration |

### Naming Conventions

| Entity | Convention | Example |
|--------|------------|---------|
| C source file | `rt_<module>.c` | `rt_counter.c` |
| C header file | `rt_<module>.h` | `rt_counter.h` |
| C function | `rt_<module>_<action>` | `rt_counter_new` |
| Canonical name | `Viper.<Namespace>.<Class>.<Method>` | `Viper.Utils.Counter.New` |
| Definition ID | PascalCase unique identifier | `CounterNew` |

---

## 3. Quick Start: Adding a Simple Function

Let's add a simple static function: `Viper.Utils.Square` that squares an integer.

### Step 1: Add the C Implementation

Create or edit `src/runtime/rt_math.c` (or a new file):

```c
/// @brief Squares an integer value.
/// @param n The integer to square.
/// @return n * n
int64_t rt_math_square(int64_t n)
{
    return n * n;
}
```

Add the declaration to `src/runtime/rt_math.h`:

```c
/// @brief Squares an integer value.
int64_t rt_math_square(int64_t n);
```

### Step 2: Add the Definition

Edit `src/il/runtime/runtime.def` and add in the appropriate section:

```c
//=============================================================================
// MATH FUNCTIONS
//=============================================================================

// ... existing functions ...

RT_FUNC(MathSquare, rt_math_square, "Viper.Math.Square", "i64(i64)")
```

The `RT_FUNC` macro parameters are:
1. **id**: Unique C++ identifier (`MathSquare`)
2. **c_symbol**: C function name (`rt_math_square`)
3. **canonical**: Viper namespace path (`Viper.Math.Square`)
4. **signature**: IL type signature (`i64(i64)`)

### Step 3: Regenerate Code

```bash
cmake --build build -j
```

The build system automatically runs `rtgen` when `runtime.def` changes.

### Step 4: Use in Viper

**BASIC:**
```basic
DIM result AS INTEGER
result = Viper.Math.Square(5)
PRINT result  ' Outputs: 25
```

**Zia:**
```zia
var result = Viper.Math.Square(5)
print(result)  // Outputs: 25
```

---

## 4. Adding a New Runtime Class

Classes are more complex than standalone functions. They require:
- A constructor function (or `none` for static utility classes)
- Instance methods (operate on `self`)
- Properties (getters and optional setters)

### Class Categories

| Category | Constructor | Example |
|----------|-------------|---------|
| **Instance Class** | Has constructor | `Viper.Collections.Map` |
| **Static Utility** | `none` | `Viper.DateTime` |

### Internal Structure Pattern

Runtime classes typically use an opaque struct:

```c
// rt_counter.c

typedef struct
{
    int64_t value;      // Current counter value
    int64_t step;       // Increment step size
} ViperCounter;
```

The struct is allocated via `rt_obj_new_i64()` which handles GC registration.

---

## 5. Definition File Syntax

### Location

`src/il/runtime/runtime.def`

### RT_FUNC - Standalone Functions

```c
RT_FUNC(id, c_symbol, "canonical", "signature")
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `id` | Unique C++ identifier | `MathSin` |
| `c_symbol` | C function name | `rt_math_sin` |
| `canonical` | Viper namespace path | `"Viper.Math.Sin"` |
| `signature` | IL type signature | `"f64(f64)"` |

### RT_ALIAS - Function Aliases

```c
RT_ALIAS("canonical_alias", target_id)
```

Creates an alternate name for an existing function.

### RT_CLASS_BEGIN/END - Class Definition

```c
RT_CLASS_BEGIN("name", type_id, "layout", ctor_id)
    // properties and methods go here
RT_CLASS_END()
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `name` | Fully qualified class name | `"Viper.Utils.Counter"` |
| `type_id` | Type identifier suffix | `Counter` |
| `layout` | Memory layout type | `"obj"` |
| `ctor_id` | Constructor function ID or `none` | `CounterNew` |

### RT_PROP - Properties

```c
RT_PROP("name", "type", getter_id, setter_id_or_none)
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `name` | Property name (PascalCase) | `"Value"` |
| `type` | IL type | `"i64"` |
| `getter_id` | Getter function ID | `CounterGetValue` |
| `setter_id_or_none` | Setter function ID or `none` | `CounterSetValue` |

**Getter convention:** `"Viper.Class.get_PropName"` canonical name
**Setter convention:** `"Viper.Class.set_PropName"` canonical name

### RT_METHOD - Methods

```c
RT_METHOD("name", "signature", target_id)
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `name` | Method name (PascalCase) | `"Increment"` |
| `signature` | Signature WITHOUT receiver | `"void()"` |
| `target_id` | Implementation function ID | `CounterIncrement` |

**Important:** The signature omits the implicit `self` (receiver) parameter. The C function must still accept it as the first argument.

### Type Abbreviations

| Abbreviation | IL Type | C Type |
|--------------|---------|--------|
| `void` | No return value | `void` |
| `i1` | Boolean | `int8_t` (0 or 1) |
| `i8` | 8-bit signed | `int8_t` |
| `i16` | 16-bit signed | `int16_t` |
| `i32` | 32-bit signed | `int32_t` |
| `i64` | 64-bit signed | `int64_t` |
| `f32` | 32-bit float | `float` |
| `f64` | 64-bit float | `double` |
| `str` | String | `rt_string` |
| `obj` | Object reference | `void*` |
| `ptr` | Raw pointer | `void*` |

---

## 6. Code Generation with rtgen

### What rtgen Does

The `rtgen` tool reads `runtime.def` and generates three `.inc` files:

| File | Purpose |
|------|---------|
| `RuntimeNameMap.inc` | Maps canonical names to C symbols for native codegen |
| `RuntimeClasses.inc` | Class/method/property catalog for OOP dispatch |
| `RuntimeSignatures.inc` | Function signatures for type checking |

### Generated Output Example

For a function defined as:
```c
RT_FUNC(CounterNew, rt_counter_new, "Viper.Utils.Counter.New", "obj()")
```

`RuntimeNameMap.inc` generates:
```c
RUNTIME_NAME_ALIAS("Viper.Utils.Counter.New", "rt_counter_new")
```

### When rtgen Runs

The build system detects changes to `runtime.def` and reruns rtgen automatically:

```cmake
# In CMakeLists.txt
add_custom_command(
    OUTPUT ${RUNTIME_GENERATED_DIR}/RuntimeNameMap.inc
           ${RUNTIME_GENERATED_DIR}/RuntimeClasses.inc
           ${RUNTIME_GENERATED_DIR}/RuntimeSignatures.inc
    COMMAND rtgen ${RUNTIME_DEF_FILE} ${RUNTIME_GENERATED_DIR}
    DEPENDS rtgen ${RUNTIME_DEF_FILE}
)
```

### Manual Regeneration

```bash
./build/src/rtgen src/il/runtime/runtime.def src/il/runtime/generated/
```

---

## 7. Frontend Integration

### How Frontends Discover Runtime Functions

Both BASIC and Zia frontends use the generated runtime metadata to:

1. **Resolve canonical names** to C symbols
2. **Type-check** method calls against signatures
3. **Emit correct IL** for runtime calls

### BASIC Frontend: RuntimeMethodIndex

The BASIC frontend uses `RuntimeMethodIndex` (`src/frontends/basic/sem/RuntimeMethodIndex.cpp`):

```cpp
// Seeded from generated RuntimeClasses
void RuntimeMethodIndex::seed(const std::vector<il::runtime::RuntimeClass> &classes)
{
    for (const auto &cls : classes)
    {
        for (const auto &m : cls.methods)
        {
            RuntimeMethodInfo info;
            info.target = m.target;
            parseSignature(m.signature, info);
            map_[keyFor(cls.qname, m.name, info.args.size())] = std::move(info);
        }
    }
}
```

### Zia Frontend: Direct Lowering

Zia resolves runtime calls during lowering (`src/frontends/zia/Lowerer.cpp`):

```cpp
// Emit extern declarations for used runtime functions
const auto *desc = il::runtime::findRuntimeDescriptor(externName);
if (desc)
    builder_->addExtern(std::string(desc->name),
                        desc->signature.retType,
                        desc->signature.paramTypes);
```

### Adding Support for New Functions

After adding a function to `runtime.def`:

1. **No additional frontend changes needed** for static functions called by canonical name
2. **For new classes**, ensure frontends can resolve the class type (usually automatic)
3. **For special syntax**, may need parser/lowerer changes (rare)

---

## 8. CMake Build Integration

### Runtime Library Structure

The runtime is organized into component libraries in `src/runtime/CMakeLists.txt`:

```cmake
set(RT_BASE_SOURCES
    rt_context.c
    rt_error.c
    # ... core functions
)

set(RT_COLLECTIONS_SOURCES
    rt_map.c
    rt_list.c
    # ... collection types
)

# Object libraries (for compilation)
add_library(viper_rt_base_obj OBJECT ${RT_BASE_SOURCES})
add_library(viper_rt_collections_obj OBJECT ${RT_COLLECTIONS_SOURCES})

# Static libraries (for linking)
add_library(viper_rt_base STATIC $<TARGET_OBJECTS:viper_rt_base_obj>)
add_library(viper_rt_collections STATIC $<TARGET_OBJECTS:viper_rt_collections_obj>)

# Combined runtime library
add_library(viper_runtime STATIC
    $<TARGET_OBJECTS:viper_rt_base_obj>
    $<TARGET_OBJECTS:viper_rt_collections_obj>
    # ... all component object libraries
)
```

### Adding a New Source File

1. Identify the appropriate component (base, collections, text, io, etc.)
2. Add the source file to the corresponding `RT_*_SOURCES` list:

```cmake
set(RT_BASE_SOURCES
    # ... existing sources ...
    rt_counter.c    # Add your new file
)
```

3. If creating a new component, add new `OBJECT` and `STATIC` library definitions

### Adding a Public Header

Add to `RT_PUBLIC_HEADERS`:

```cmake
set(RT_PUBLIC_HEADERS
    # ... existing headers ...
    ${CMAKE_CURRENT_SOURCE_DIR}/rt_counter.h
)
```

---

## 9. Testing Your Extension

### Unit Tests

Create a unit test in `src/tests/unit/`:

```cpp
// TestRuntimeCounter.cpp
#include <gtest/gtest.h>

extern "C" {
#include "rt_counter.h"
}

TEST(RuntimeCounter, NewCreatesZeroCounter)
{
    void *counter = rt_counter_new();
    EXPECT_EQ(rt_counter_get_value(counter), 0);
}

TEST(RuntimeCounter, IncrementAddsOne)
{
    void *counter = rt_counter_new();
    rt_counter_increment(counter);
    EXPECT_EQ(rt_counter_get_value(counter), 1);
}
```

### Integration Tests (Golden Tests)

Create a golden test in `src/tests/integration/`:

**`counter_test.bas`:**
```basic
DIM c AS Counter
c = Counter.New()
PRINT c.Value      ' Expected: 0
c.Increment()
PRINT c.Value      ' Expected: 1
```

**`counter_test.expected`:**
```
0
1
```

### Running Tests

```bash
# Build and run all tests
cmake --build build -j && ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R counter
```

---

## 10. Complete Example: Counter Class

Let's implement a complete `Viper.Utils.Counter` class with:
- Constructor: `Counter.New()` and `Counter.NewWithStep(step)`
- Properties: `Value` (read-only), `Step` (read/write)
- Methods: `Increment()`, `Decrement()`, `Reset()`

### Step 1: Create rt_counter.h

```c
//===----------------------------------------------------------------------===//
// File: src/runtime/rt_counter.h
// Purpose: Simple counter class for demonstration.
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new counter with default step of 1.
    /// @return Pointer to new counter object.
    void *rt_counter_new(void);

    /// @brief Create a new counter with specified step.
    /// @param step The increment/decrement step size.
    /// @return Pointer to new counter object.
    void *rt_counter_new_with_step(int64_t step);

    /// @brief Get the current counter value.
    /// @param obj Counter pointer.
    /// @return Current value.
    int64_t rt_counter_get_value(void *obj);

    /// @brief Get the step size.
    /// @param obj Counter pointer.
    /// @return Step size.
    int64_t rt_counter_get_step(void *obj);

    /// @brief Set the step size.
    /// @param obj Counter pointer.
    /// @param step New step size.
    void rt_counter_set_step(void *obj, int64_t step);

    /// @brief Increment the counter by step.
    /// @param obj Counter pointer.
    void rt_counter_increment(void *obj);

    /// @brief Decrement the counter by step.
    /// @param obj Counter pointer.
    void rt_counter_decrement(void *obj);

    /// @brief Reset the counter to zero.
    /// @param obj Counter pointer.
    void rt_counter_reset(void *obj);

#ifdef __cplusplus
}
#endif
```

### Step 2: Create rt_counter.c

```c
//===----------------------------------------------------------------------===//
// File: src/runtime/rt_counter.c
// Purpose: Simple counter class implementation.
//===----------------------------------------------------------------------===//

#include "rt_counter.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdint.h>

/// @brief Internal counter structure.
typedef struct
{
    int64_t value; ///< Current counter value.
    int64_t step;  ///< Increment/decrement step size.
} ViperCounter;

void *rt_counter_new(void)
{
    ViperCounter *counter = (ViperCounter *)rt_obj_new_i64(0, (int64_t)sizeof(ViperCounter));
    if (!counter)
    {
        rt_trap("Counter: memory allocation failed");
        return NULL;
    }

    counter->value = 0;
    counter->step = 1;

    return counter;
}

void *rt_counter_new_with_step(int64_t step)
{
    ViperCounter *counter = (ViperCounter *)rt_obj_new_i64(0, (int64_t)sizeof(ViperCounter));
    if (!counter)
    {
        rt_trap("Counter: memory allocation failed");
        return NULL;
    }

    counter->value = 0;
    counter->step = step;

    return counter;
}

int64_t rt_counter_get_value(void *obj)
{
    return ((ViperCounter *)obj)->value;
}

int64_t rt_counter_get_step(void *obj)
{
    return ((ViperCounter *)obj)->step;
}

void rt_counter_set_step(void *obj, int64_t step)
{
    ((ViperCounter *)obj)->step = step;
}

void rt_counter_increment(void *obj)
{
    ViperCounter *counter = (ViperCounter *)obj;
    counter->value += counter->step;
}

void rt_counter_decrement(void *obj)
{
    ViperCounter *counter = (ViperCounter *)obj;
    counter->value -= counter->step;
}

void rt_counter_reset(void *obj)
{
    ((ViperCounter *)obj)->value = 0;
}
```

### Step 3: Add to runtime.def

Add to `src/il/runtime/runtime.def` in an appropriate section:

```c
//=============================================================================
// UTILS - COUNTER
//=============================================================================

RT_FUNC(CounterNew,         rt_counter_new,           "Viper.Utils.Counter.New",           "obj()")
RT_FUNC(CounterNewWithStep, rt_counter_new_with_step, "Viper.Utils.Counter.NewWithStep",   "obj(i64)")
RT_FUNC(CounterGetValue,    rt_counter_get_value,     "Viper.Utils.Counter.get_Value",     "i64(obj)")
RT_FUNC(CounterGetStep,     rt_counter_get_step,      "Viper.Utils.Counter.get_Step",      "i64(obj)")
RT_FUNC(CounterSetStep,     rt_counter_set_step,      "Viper.Utils.Counter.set_Step",      "void(obj,i64)")
RT_FUNC(CounterIncrement,   rt_counter_increment,     "Viper.Utils.Counter.Increment",     "void(obj)")
RT_FUNC(CounterDecrement,   rt_counter_decrement,     "Viper.Utils.Counter.Decrement",     "void(obj)")
RT_FUNC(CounterReset,       rt_counter_reset,         "Viper.Utils.Counter.Reset",         "void(obj)")

// Class definition for OOP dispatch
RT_CLASS_BEGIN("Viper.Utils.Counter", Counter, "obj", CounterNew)
    RT_PROP("Value", "i64", CounterGetValue, none)
    RT_PROP("Step", "i64", CounterGetStep, CounterSetStep)
    RT_METHOD("Increment", "void()", CounterIncrement)
    RT_METHOD("Decrement", "void()", CounterDecrement)
    RT_METHOD("Reset", "void()", CounterReset)
RT_CLASS_END()
```

### Step 4: Update CMakeLists.txt

Edit `src/runtime/CMakeLists.txt`:

```cmake
set(RT_BASE_SOURCES
    # ... existing sources ...
    rt_counter.c
)

set(RT_PUBLIC_HEADERS
    # ... existing headers ...
    ${CMAKE_CURRENT_SOURCE_DIR}/rt_counter.h
)
```

### Step 5: Build and Test

```bash
# Build
cmake --build build -j

# Verify the function is registered
grep -r "Counter" build/src/il/runtime/generated/

# Run tests
ctest --test-dir build --output-on-failure
```

### Step 6: Use in Viper Programs

**BASIC:**
```basic
' Create a counter
DIM c AS Counter
c = Counter.New()

' Use methods
c.Increment()
c.Increment()
PRINT "Value: "; c.Value   ' Output: Value: 2

' Change step
c.Step = 5
c.Increment()
PRINT "Value: "; c.Value   ' Output: Value: 7

' Reset
c.Reset()
PRINT "Value: "; c.Value   ' Output: Value: 0
```

**Zia:**
```zia
// Create a counter
var c = Counter.New()

// Use methods
c.Increment()
c.Increment()
print("Value: " + c.Value.ToString())  // Output: Value: 2

// Change step
c.Step = 5
c.Increment()
print("Value: " + c.Value.ToString())  // Output: Value: 7

// Reset
c.Reset()
print("Value: " + c.Value.ToString())  // Output: Value: 0
```

---

## 11. Common Patterns

### Pattern 1: Static Utility Class

For classes with no instances (all static methods):

```c
// runtime.def
RT_CLASS_BEGIN("Viper.Math", Math, "obj", none)  // note: ctor is 'none'
    RT_METHOD("Sin", "f64(f64)", MathSin)
    RT_METHOD("Cos", "f64(f64)", MathCos)
    RT_METHOD("Sqrt", "f64(f64)", MathSqrt)
RT_CLASS_END()
```

### Pattern 2: Factory Methods

For classes with multiple constructors:

```c
// runtime.def
RT_FUNC(MapNew,      rt_map_new,       "Viper.Collections.Map.New",      "obj()")
RT_FUNC(MapFromList, rt_map_from_list, "Viper.Collections.Map.FromList", "obj(obj)")

RT_CLASS_BEGIN("Viper.Collections.Map", Map, "obj", MapNew)
    // MapNew is the default constructor
    // MapFromList is a static factory method
    RT_METHOD("FromList", "obj(obj)", MapFromList)
    // ... other methods
RT_CLASS_END()
```

### Pattern 3: Read-Only Properties

```c
RT_PROP("Length", "i64", ArrayGetLength, none)  // no setter
```

### Pattern 4: Read-Write Properties

```c
RT_PROP("Capacity", "i64", ArrayGetCapacity, ArraySetCapacity)
```

### Pattern 5: Methods Returning Self (Fluent API)

```c
// C implementation
void *rt_builder_append(void *obj, const char *text)
{
    // ... append logic ...
    return obj;  // Return self for chaining
}

// runtime.def
RT_METHOD("Append", "obj(str)", BuilderAppend)
```

Usage:
```basic
builder.Append("Hello").Append(" ").Append("World")
```

### Pattern 6: Optional Parameters with Overloads

Create multiple functions with different arities:

```c
RT_FUNC(SubstrFrom,   rt_substr_from,   "Viper.String.Substr", "str(str,i64)")
RT_FUNC(SubstrRange,  rt_substr_range,  "Viper.String.Substr", "str(str,i64,i64)")
```

### Pattern 7: Error Handling with Traps

```c
void *rt_list_get(void *obj, int64_t index)
{
    ViperList *list = (ViperList *)obj;
    if (index < 0 || index >= list->count)
    {
        rt_trap("List index out of bounds: %lld (size: %lld)", index, list->count);
        return NULL;  // Unreachable after trap
    }
    return list->items[index];
}
```

---

## 12. Troubleshooting

### "Unknown runtime function" Error

**Symptom:** Compiler reports unknown function when calling `Viper.X.Y`

**Causes:**
1. Function not added to `runtime.def`
2. Typo in canonical name
3. Build not regenerated after editing `runtime.def`

**Solution:**
```bash
# Force regeneration
rm -rf build/src/il/runtime/generated/
cmake --build build -j
```

### "Signature mismatch" Error

**Symptom:** Type error when calling runtime function

**Cause:** Signature in `runtime.def` doesn't match C function

**Solution:** Verify parameter types match exactly:
- `i64` = `int64_t`
- `f64` = `double`
- `str` = `rt_string`
- `obj` = `void*`

### Linker Error: "undefined reference to rt_xxx"

**Symptom:** Link fails with undefined symbol

**Causes:**
1. Source file not added to CMakeLists.txt
2. Function declared but not implemented
3. Missing `extern "C"` in header (for C++ callers)

**Solution:**
1. Verify source file is in appropriate `RT_*_SOURCES` list
2. Check function implementation exists
3. Ensure header has `extern "C"` wrapper

### Class Methods Not Found

**Symptom:** Method calls work for static functions but not on class instances

**Cause:** Missing `RT_CLASS_BEGIN/END` block or method not listed inside

**Solution:** Verify class definition includes all methods:
```c
RT_CLASS_BEGIN("Viper.Utils.Counter", Counter, "obj", CounterNew)
    RT_METHOD("Increment", "void()", CounterIncrement)  // Must be inside the block
RT_CLASS_END()
```

### Property Getter/Setter Not Working

**Symptom:** Property access fails or returns wrong type

**Cause:** Canonical name convention not followed

**Solution:** Use exact conventions:
- Getter: `"Viper.Class.get_PropertyName"`
- Setter: `"Viper.Class.set_PropertyName"`

---

## 13. Reference Materials

### Related Documentation

- **[IL Guide](il-guide.md)** - IL type system and instruction reference
- **[Frontend How-To](frontend-howto.md)** - Building language frontends
- **[Architecture](architecture.md)** - System architecture overview
- **[Codemap](codemap.md)** - Codebase navigation

### Key Source Files

| File | Purpose |
|------|---------|
| `src/il/runtime/runtime.def` | Single source of truth for runtime metadata |
| `src/tools/rtgen/rtgen.cpp` | Code generator implementation |
| `src/runtime/CMakeLists.txt` | Runtime build configuration |
| `src/runtime/rt_internal.h` | Internal runtime utilities |
| `src/runtime/rt_object.h` | Object allocation (GC integration) |
| `src/il/runtime/classes/RuntimeClasses.hpp` | C++ wrapper for class metadata |

### Example Implementations

Study these existing implementations as references:

| Class | Location | Complexity |
|-------|----------|------------|
| `Viper.Text.Guid` | `src/runtime/rt_guid.c` | Simple (static utility) |
| `Viper.Diagnostics.Stopwatch` | `src/runtime/rt_stopwatch.c` | Medium (instance class) |
| `Viper.Collections.Map` | `src/runtime/rt_map.c` | Complex (data structure) |
| `Viper.IO.File` | `src/runtime/rt_file.c` | Complex (OS integration) |

---

## Appendix: Quick Reference Card

### Adding a Static Function

```c
// 1. Implement in rt_module.c
int64_t rt_module_func(int64_t arg) { ... }

// 2. Declare in rt_module.h
int64_t rt_module_func(int64_t arg);

// 3. Add to runtime.def
RT_FUNC(ModuleFunc, rt_module_func, "Viper.Module.Func", "i64(i64)")

// 4. Add source to CMakeLists.txt (if new file)
// 5. Build: cmake --build build -j
```

### Adding a New Class

```c
// 1. Implement constructor and methods
void *rt_myclass_new(void) { ... }
void rt_myclass_do_thing(void *obj) { ... }
int64_t rt_myclass_get_value(void *obj) { ... }

// 2. Add RT_FUNCs for all functions
RT_FUNC(MyClassNew, rt_myclass_new, "Viper.MyClass.New", "obj()")
RT_FUNC(MyClassDoThing, rt_myclass_do_thing, "Viper.MyClass.DoThing", "void(obj)")
RT_FUNC(MyClassGetValue, rt_myclass_get_value, "Viper.MyClass.get_Value", "i64(obj)")

// 3. Add RT_CLASS_BEGIN/END block
RT_CLASS_BEGIN("Viper.MyClass", MyClass, "obj", MyClassNew)
    RT_PROP("Value", "i64", MyClassGetValue, none)
    RT_METHOD("DoThing", "void()", MyClassDoThing)
RT_CLASS_END()
```

### Type Mapping Quick Reference

| IL | C | BASIC | Zia |
|----|---|-------|-----|
| `i64` | `int64_t` | `Integer` / `Long` | `int` |
| `f64` | `double` | `Double` | `float` |
| `i1` | `int8_t` (0/1) | `Boolean` | `bool` |
| `str` | `rt_string` | `String` | `str` |
| `obj` | `void*` | Object type | class instance |
| `void` | `void` | (no return) | (no return) |
