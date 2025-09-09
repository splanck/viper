# Style guide

See [AGENTS.md](../AGENTS.md) for project-wide policies. This guide defines
comment conventions and file headers for the Viper codebase. All contributors
must follow these rules when adding new code or updating existing files.

## File naming

Use `.cpp` for implementation files and `.hpp` for headers. Do not use `.cc` or `.h`.

## File headers

Every source and header file begins with a short block comment that explains
the file's role. Use this template:

```cpp
// File: <path/to/file>
// Purpose: <one line>
// Key invariants: <state what must hold>
// Ownership/Lifetime: <who allocates and frees>
// Perf/Threading notes: <hot paths or concurrency>
// Links: <specs, ADRs, docs>
```

### Notes

- Keep lines concise and factual.
- Omit fields that do not apply rather than leaving placeholders.

### Example

```cpp
// foo/Bar.hpp
// Purpose: Declares the Bar helper.
// Key invariants: ID remains unique.
// Ownership/Lifetime: Caller frees instances.
// Links: docs/class-catalog.md
```

## Doxygen API comments

Public classes, functions, and members use Doxygen comments with triple
slashes. Common tags:

- `@brief` – one line summary.
- `@param` – describe each parameter.
- `@return` – state what the function yields.
- `@note` – extra context or links.
- `@invariant` – conditions that always hold.

Keep comments under ~100 characters per line.

## Members and attributes

Member variables have short trailing comments focusing on meaning
or units:

```cpp
int count; ///< Number of active users.
```

Avoid repeating type information or restating obvious details.

## Naming and tone

- Be direct and neutral; avoid marketing phrases.
- Link to related docs or ADRs with Markdown links when relevant.
- Prefer verbs for functions, nouns for classes, and short
  `snake_case` names for variables.

## Spacing and indentation

Use 4-space indentation, Allman braces, and blank lines to keep code readable.

### Good

```cpp
// foo/Calc.cpp
// Purpose: Example with spacing.

#include "Calc.hpp"

namespace il {

/// @brief Adds two numbers.
int add(int a, int b)
{
    return a + b;
}

} // namespace il
```

### Bad

```cpp
//foo/Calc.cpp
#include "Calc.hpp"
namespace il{int add(int a,int b){return a+b;}}
```

The bad example lacks blank lines, uses cramped K&R braces, and omits the standard
4-space indentation, making it difficult to read.

## Examples

### Class

```cpp
// foo/Bar.hpp
/// @brief Manages frobnications.
/// @invariant ID is unique per instance.
/// @ownership Caller owns instances.
/// @note See [IL spec](references/il.md).
class Bar {
public:
  /// @brief Perform one step.
  void step();

private:
  int id; ///< Unique identifier.
};
```

### Function

```cpp
/// @brief Adds two integers.
/// @param a First operand.
/// @param b Second operand.
/// @return Sum of `a` and `b`.
int add(int a, int b);
```

### Enum

```cpp
/// @brief Token categories.
/// @note Must align with the IL lexer.
enum class TokenKind {
  Identifier, ///< Alphanumeric symbol.
  Number,     ///< Numeric literal.
  EndOfFile,  ///< Sentinel.
};
```
