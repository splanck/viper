---
status: active
audience: public
last-verified: 2025-09-24
---

# BASIC Object-Oriented Programming

Viper's BASIC frontend permanently supports a lightweight object model for
structuring programs around `CLASS` definitions. This guide summarizes the
syntax, runtime behavior, and tooling expectations for the object features now
enabled in every build.

## Overview

- `CLASS` blocks declare fields, methods, constructors, and destructors.
- Instances are reference-counted handles allocated with `NEW` and released with
  `DELETE`.
- The implicit `ME` expression is available inside methods to reference the
  current instance.
- Method calls use dot notation (e.g., `obj.Inc()`), lowering to IL functions
  with mangled names.

## Declaring Classes

A class declaration appears at the top level alongside procedures and the main
statement list. Fields default to 64-bit integers unless explicitly typed.

```basic
10 CLASS Counter
20   value AS INTEGER
30   SUB NEW()
40     LET value = 0
50   END SUB
60   SUB INC()
70     LET value = value + 1
80   END SUB
90   FUNCTION Current() AS INTEGER
100    RETURN value
110  END FUNCTION
120 END CLASS
```

Constructors (`SUB NEW`) and destructors (`DESTRUCTOR`) are optional. Methods may
return values via `FUNCTION` members.

## Working With Instances

Allocate objects with `NEW` and access members using dot syntax. The `ME`
keyword inside a method evaluates to the current receiver.

```basic
10 DIM c
20 LET c = NEW Counter()
30 CALL c.INC()
40 PRINT c.Current()
50 DELETE c
60 END
```

Behind the scenes the frontend lowers these operations to IL helpers provided by
`il/runtime/RuntimeSignatures.hpp`. The runtime retains deterministic field
layouts and emits reference-counting calls when objects flow between scopes.

## Testing & Tooling

All unit, smoke, and integration tests compile with object support enabled. No
feature flags or CMake options are required—running `cmake --build` and `ctest`
exercises the object lowering pipeline by default. When extending OOP behavior,
add tests under `tests/unit` or `tests/basic` without additional configuration.
