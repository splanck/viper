---
status: active
audience: public
last-verified: 2025-11-25
---

# Contributor Guide

<a id="contributing"></a>

## Contributing

- The repository includes a lightweight `Analysis` library in `src/il/analysis`.
  Run the tests under `src/tests/analysis` when modifying these utilities.

<a id="style-guide"></a>

## Style Guide

See [CLAUDE.md](../../CLAUDE.md) for project-wide policies. This guide defines
comment conventions and file headers for the Viper codebase. All contributors
must follow these rules when adding new code or updating existing files.

### File naming

Use `.cpp` for implementation files and `.hpp` for headers. Do not use `.cc`. Use `.hpp` for C++ headers and `.h` for C runtime headers.

### File headers

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

#### Notes

- Keep lines concise and factual.
- Omit fields that do not apply rather than leaving placeholders.

#### Example

```cpp
// foo/Bar.hpp
// Purpose: Declares the Bar helper.
// Key invariants: ID remains unique.
// Ownership/Lifetime: Caller frees instances.
// Links: docs/codemap.md
```

### Doxygen API comments

Public classes, functions, and members use Doxygen comments with triple
slashes. Common tags:

- `@brief` – one line summary.
- `@param` – describe each parameter.
- `@return` – state what the function yields.
- `@note` – extra context or links.
- `@invariant` – conditions that always hold.

Keep comments under ~100 characters per line.

### Members and attributes

Member variables have short trailing comments focusing on meaning
or units:

```cpp
int count; ///< Number of active users.
```

Avoid repeating type information or restating obvious details.

### Naming and tone

- Be direct and neutral; avoid marketing phrases.
- Link to related docs or ADRs with Markdown links when relevant.
- Prefer verbs for functions, nouns for classes, and short
  `snake_case` names for variables.

### Spacing and indentation

Use 4-space indentation, Allman braces, and blank lines to keep code readable.

#### Good

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

#### Bad

```cpp
//foo/Calc.cpp
#include "Calc.hpp"
namespace il{int add(int a,int b){return a+b;}}
```

The bad example lacks blank lines, uses cramped K&R braces, and omits the standard
4-space indentation, making it difficult to read.

### Examples

#### Class

```cpp
// foo/Bar.hpp
/// @brief Manages frobnications.
/// @invariant ID is unique per instance.
/// @ownership Caller owns instances.
/// @note See [IL spec](il-reference.md).
class Bar {
public:
  /// @brief Perform one step.
  void step();

private:
  int id; ///< Unique identifier.
};
```

#### Function

```cpp
/// @brief Adds two integers.
/// @param a First operand.
/// @param b Second operand.
/// @return Sum of `a` and `b`.
int add(int a, int b);
```

#### Enum

```cpp
/// @brief Token categories.
/// @note Must align with the IL lexer.
enum class TokenKind {
  Identifier, ///< Alphanumeric symbol.
  Number,     ///< Numeric literal.
  EndOfFile,  ///< Sentinel.
};
```

<a id="testing"></a>

## Testing

### Tolerance checks for float e2e

Floating point outputs in end-to-end tests can vary slightly. To keep tests stable, expectations are
stored in files containing lines such as:

```
EXPECT≈ 3.14 0.05
```

The `tests/e2e/support/FloatOut` helper reads program output and these expectation files and fails
if any absolute difference exceeds the specified tolerance. The CMake test driver runs this helper
after executing the sample.

<a id="frontend-internals"></a>

## Frontend Internals

<a id="frontend-internals-parser"></a>

### Parser

The lexer recognizes single-line comments beginning with an apostrophe `'`
or the keyword `REM`. `REM` is case-insensitive and only treated as a
comment when it appears at the start of a line or after whitespace.
Characters are skipped until the end of the line, and no comment tokens
are produced. Line and column counters continue to advance so subsequent
tokens report accurate locations.

<a id="frontend-internals-semantics"></a>

### Semantics

The semantic analyzer walks the BASIC AST to perform checks and annotate
structures for later phases.

#### ProcTable

A `ProcTable` maps each declared FUNCTION or SUB to its signature: kind,
return type inferred from the name suffix, and parameter types along with array
flags. This table enables call checking in later passes.

#### Two-pass Analysis

Semantic analysis runs in two phases. First all procedure declarations are
registered in the `ProcTable`. In the second pass each procedure body is
checked followed by the top-level statements. This enables calls to functions
that are defined later in the file and supports recursion.

Example diagnostics emitted by the analyzer:

```
10 FUNCTION F(A, A)
              ^
error[B1005]: duplicate parameter 'A'

30 SUB S(X#())
        ^
error[B2004]: array parameter must be i64[] or str[]

50 FUNCTION S()
            ^
error[B1004]: duplicate procedure 'S'
```

#### Scope stack

Each `FUNCTION` or `SUB` is analyzed with a stack of lexical scopes. A new
scope is pushed when entering a nested block and popped on exit. `DIM` adds a
symbol to the current scope; the name may shadow one from an outer scope but
redeclaring within the same scope is rejected.

#### Return-path analysis

Functions must return a value along every control-flow path. The analyzer
implements a simple structural check:

- `RETURN` with an expression marks a returning path.
- `IF`/`ELSEIF`/`ELSE` returns only when all arms return.
- `WHILE` and `FOR` are assumed to possibly skip execution or loop forever and
  therefore do not guarantee a return.
- For a sequence of statements, only the final statement is considered.

If analysis fails, `missing return in FUNCTION <name>` is reported at the
`END FUNCTION` keyword. This pass is conservative and does not attempt deep
flow analysis (e.g., constant conditions or loop bounds).

<a id="frontend-internals-analysis"></a>

### Analysis Utilities

#### CFG

On-demand helpers to query basic block successors and predecessors without
constructing an explicit graph.

```cpp
using namespace viper::analysis;
CFGContext ctx(module);
auto succ = successors(ctx, block);
auto pred = predecessors(ctx, block);
```

#### Orders

Standard depth-first orders are available without materializing a graph.

```cpp
auto po = postOrder(ctx, fn);      // entry last
auto rpo = reversePostOrder(ctx, fn); // entry first
```

#### Acyclicity & Topological Order

Cycle detection and topological sorting are available for DAG-restricted
analyses.

```cpp
bool ok = isAcyclic(ctx, fn);      // false if any cycle exists
auto topo = topoOrder(ctx, fn);    // empty if cyclic
```

These helpers gate passes like the mem2reg v2 prototype, which operates only
on acyclic control-flow graphs.

#### Dominators

Computes immediate dominators using the algorithm of Cooper, Harvey, and
Kennedy ("A Simple, Fast Dominance Algorithm"). The function iterates to a
fixed point over reverse post-order, yielding a tree of parent links and
children for easy traversal. Complexity is linear in practice and worst-case
\(O(V \times E)\).

#### IL utilities

Helpers in `src/il/utils` provide light-weight queries on basic blocks and
instructions, such as checking block membership or retrieving a block's
terminator. They depend only on `il_core`, allowing passes to use them without
pulling in the Analysis layer.

<a id="frontend-internals-ir-builder"></a>

### IR Builder Helpers

The IR builder provides convenience routines for constructing control flow with
block parameters and branch arguments.

#### Creating Blocks

```cpp
Module m;
IRBuilder b(m);
Function &f = b.startFunction("f", Type(Type::Kind::Void), {});
BasicBlock &loop = b.createBlock(f, "loop", {{"i", Type(Type::Kind::I64)}});
```

`createBlock` assigns value identifiers to each parameter automatically.

#### Accessing Block Parameters

```cpp
Value i = b.blockParam(loop, 0);
```

#### Branches with Arguments

```cpp
b.br(loop, {Value::constInt(0)});               // unconditional
b.cbr(i, loop, {i}, loop, {i});                 // conditional
```

Both helpers assert that the number of arguments matches the destination block's
parameter list.

<a id="debugging"></a>

## Debugging

### Breakpoints and Source Tracing

#### Breaking on source lines

`viper` can halt execution before running a specific source line.

#### Flags

- `--break <file:line>`: Generic breakpoint flag. If the argument contains a path separator or dot, it is interpreted as
  a source-line breakpoint.
- `--break-src <file:line>`: Explicit source-line breakpoint.

Paths are normalized before comparison, including platform separators and `.`/`..` segments. When the normalized path
does not match the location recorded in the IL, `viper` falls back to comparing only the basename.

Specifying the same breakpoint more than once coalesces into a single breakpoint. When multiple instructions map to the
same source line, `viper` reports the breakpoint once per line until control transfers to a different basic block.

#### Examples

```sh
# Path normalization: break at the first PRINT in math_basics.bas
viper front basic -run ./examples/basic/../basic/math_basics.bas \
  --break ./examples/basic/../basic/math_basics.bas:4 --trace=src

# Basename fallback with the explicit flag
viper front basic -run examples/basic/sine_cosine.bas \
  --break-src sine_cosine.bas:5 --trace=src
```

### Verifying INKEY$/GETKEY$ Locally

These built-in functions interact with the terminal and require manual testing to verify behavior.

#### Non-blocking read (INKEY$)

```sh
viper front basic -run examples/inkey_smoke.bas
```

Expected: Program polls once, prints "No key" (or the key code if pressed quickly), and exits immediately without
blocking.

#### Blocking read (GETKEY$)

Create a temporary test file and run:

```sh
echo 'COLOR 7,0: PRINT "Press a key": k$ = GETKEY$(): PRINT "Got:"; ASC(k$)' > /tmp/getkey.bas
viper front basic -run /tmp/getkey.bas
```

Expected: Program waits for one keystroke, then prints the ASCII code and exits.

**Note:** INKEY$ never blocks; GETKEY$ waits for input. Both functions require parentheses even though they take zero
arguments.

### Debugging BASIC Recursion Failures

Use the factorial example to inspect recursive calls.

```sh
viper -run src/tests/e2e/factorial.bas --trace=src \
    --break-src src/tests/e2e/factorial.bas:5 \
    --debug-cmds examples/il/debug_script.txt
```

The `--trace=src` flag prints each executed instruction with the originating
file and line. The `--break-src` flag pauses before the recursive call. The
debug script steps twice and then continues so you can watch the call enter
and return.

Check the trace for a `RETURN` at the end of `FACT`; missing returns suggest
the recursion never reaches the base case. At startup the trace should show
`fn=@main` followed by a call to `@fact`. If `@main` is absent or never calls
the function, the program may have been lowered incorrectly.

<a id="migrations"></a>

## Migrations

No active migrations are in flight. Follow release notes and ADRs for future migration plans.

Sources:

- docs/contributor-guide.md#contributing
- docs/contributor-guide.md#style-guide
- docs/contributor-guide.md#testing
- docs/contributor-guide.md#frontend-internals-parser
- docs/contributor-guide.md#frontend-internals-semantics
- docs/contributor-guide.md#frontend-internals-analysis
- docs/contributor-guide.md#frontend-internals-ir-builder
- archive/docs/debugging.md
- archive/docs/dev/debug-recursion.md
