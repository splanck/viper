---
status: active
audience: public
last-verified: 2025-11-13
---

# Viper IL — Complete Guide

Comprehensive guide to Viper's Intermediate Language (IL), covering everything from quickstart to advanced topics. This document consolidates the quickstart, normative reference, BASIC lowering rules, optimization passes, and worked examples for IL v0.1.

> **Note:** IL v0.1.2 features are documented where they differ from v0.1.

---

## Table of Contents

1. [Quickstart](#quickstart)
2. [Program Structure](#program-structure)
3. [Values and Types](#values-and-types)
4. [Control Flow](#control-flow)
5. [Functions and Calls](#functions-and-calls)
6. [Memory Operations](#memory-operations)
7. [Exception Handling](#exception-handling)
8. [BASIC Lowering Rules](#basic-lowering-rules)
9. [Optimization Passes](#optimization-passes)
10. [Advanced Topics](#advanced-topics)

---

<a id="quickstart"></a>
## Quickstart

Welcome! This guide is for developers from languages like C#, Java, TypeScript, or Python who want a hands-on tour of Viper's intermediate language. **No prior compiler experience is required.**

### What is Viper IL?

Viper IL is the **"thin waist"** of the Viper toolchain — a versioned, textual intermediate representation that decouples frontends from backends:

- **Frontends** (BASIC, etc.) compile to IL
- **VM** executes IL deterministically
- **Verifier** ensures type safety and correctness
- **Transforms** optimize IL (SimplifyCFG, LICM, SCCP)
- **Backends** compile IL to native code

**Why a separate IL?**

- **Decoupling**: Frontends evolve independently from the VM
- **Stability**: IL version headers (`il 0.1`) ensure compatibility
- **Inspectability**: Textual format is easy to read and debug
- **Optimization**: Centralized place for analysis and transforms
- **Multi-language**: Multiple frontends share one runtime

### Program structure

An IL module is plain text. Its top‑level layout is:

1. **Version line** – `il 0.1` (or `il 0.1.2` for experimental features) pins the expected IL grammar.
2. **Extern declarations** – `extern @name(signature) -> ret` describes functions provided by the runtime or other modules.
3. **Global constants** – `global const str @.msg = "hi"` defines immutable data.
4. **Functions** – `func @main() -> i64 { ... }` contains basic blocks and instructions.

Inside a function:

* Each basic block starts with a label like `entry:`. There is no fall‑through; control transfers with a terminator such as `ret`, `br`, or `cbr`.
* Instructions assign results to SSA registers (`%v0`, `%tmp1`). A register is defined once and used many times.
* Comments begin with `#` and run to the end of the line.

These pieces mirror what a compiler would normally keep in its internal IR and make it explicit for learning and debugging.

### Your first IL program
Create a file `first.il` with the contents:

```il
# Print the number 4 and exit.
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  call @Viper.Console.PrintI64(4)    # runtime prints `4\n`
  ret 0                    # zero exit code
}
```

Run it with `ilc`:

```bash
ilc -run first.il
```

Expected output:

```text
4
```

**Line by line**

- `# Print the number 4 and exit.` – comments start with `#` and are ignored by the VM.
- `il 0.1` – required version header (use `il 0.1.2` for experimental features).
- `extern @Viper.Console.PrintI64(i64) -> void` – declare a runtime function taking an `i64` and returning `void`.
- `func @main() -> i64 {` – define the `@main` function that returns an `i64` exit code.
- `entry:` – the initial basic block label.
- `call @Viper.Console.PrintI64(4)` – invoke the extern with the literal `4`.
- `ret 0` – terminate the function and supply the process exit status.
- `}` – close the function body.

Compatibility:
- When built with `-DVIPER_RUNTIME_NS_DUAL=ON`, legacy `@rt_*` externs are accepted as aliases of `@Viper.*`.
- New code should emit `@Viper.*`.
 - Planned: Starting in v0.3.0, legacy `@rt_*` aliases will be OFF by default. Enable with `-DVIPER_RUNTIME_NS_DUAL=ON` if you need to load legacy IL.

**What just happened?** `Viper.Console.PrintI64` is supplied by the runtime and prints its argument. Every function ends with a terminator such as `ret` giving the program's exit code.

**Gotcha:** Every module must start with a version line (e.g., `il 0.1`).

### Values and types
IL is statically typed and uses SSA-style virtual registers (`%v0`, `%t1`, ...). Primitive types include `i1` (bool), `i16`, `i32`, `i64`, `f64`, `ptr`, and `str`, plus specialized types `error` and `resumetok` for exception handling.

```il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  %p = alloca 8            # reserve 8 bytes on the stack
  store i64, %p, 10        # write constant 10 to memory
  %v0 = load i64, %p       # read it back
  call @Viper.Console.PrintI64(%v0)  # prints 10
  ret 0
}
```

**Line by line**

- `%p = alloca 8` – allocate eight bytes of stack memory and bind its address to `%p` (type `ptr`).
- `store i64, %p, 10` – store the 64‑bit constant `10` into the memory pointed to by `%p`.
- `%v0 = load i64, %p` – load an `i64` from `%p` into `%v0`.
- `call @Viper.Console.PrintI64(%v0)` – pass the loaded value to the runtime print routine.
- `ret 0` – return from `main` with exit code 0.

**What just happened?** `alloca` creates a stack slot, `store` writes to it, and `load` reads from it.

**Gotcha:** All integers are 64-bit; mixing `i64` and `f64` requires explicit casts.

### Locals, params, and calls
Functions declare typed parameters. Values are passed and returned explicitly.

```il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @add(i64 %a, i64 %b) -> i64 {
entry:
  %sum = add %a, %b        # compute a + b
  ret %sum
}
func @main() -> i64 {
entry:
  %v0 = call @add(2, 3)    # call with constants
  call @Viper.Console.PrintI64(%v0)  # prints 5
  ret 0
}
```

**Line by line**

- `func @add(i64 %a, i64 %b) -> i64` – declare a function with two `i64` parameters and an `i64` return type.
- `%sum = add %a, %b` – add the two parameters and bind the sum.
- `ret %sum` – return the computed sum.
- `func @main() -> i64 { ... }` – define the entry point.
- `%v0 = call @add(2, 3)` – call `@add` with literal arguments; result stored in `%v0`.
- `call @Viper.Console.PrintI64(%v0)` – print the returned value.
- `ret 0` – exit with status 0.

**What just happened?** `call` pushes arguments and receives a result. Each function has one entry block.

**Gotcha:** Arguments are immutable; use `alloca` + `store` if you need a mutable local.

### Arithmetic and comparisons

```il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  %v0 = add 2, 2           # 4
  %v1 = scmp_gt %v0, 3     # 1 (true)
  call @Viper.Console.PrintI64(%v1)  # prints 1
  ret 0
}
```

**Line by line**

- `%v0 = add 2, 2` – compute the constant expression `2 + 2`.
- `%v1 = scmp_gt %v0, 3` – signed compare‑greater; result is `1` because 4 > 3.
- `call @Viper.Console.PrintI64(%v1)` – zero‑extend the `i1` result and print it.
- `ret 0` – terminate `main` with success.

**What just happened?** `scmp_gt` compares signed integers and yields an `i1` (0 or 1).

**Gotcha:** Comparison results are `i1`; printing them with `Viper.Console.PrintI64` zero-extends to `i64`.

### Control flow
Blocks end with a terminator. `cbr` chooses a target based on an `i1` value.

```il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  %flag = scmp_gt 5, 3     # 1 means take then
  cbr %flag, then, else    # conditional branch
then:
  call @Viper.Console.PrintI64(1)    # prints 1 if flag != 0
  br done                  # jump to exit
else:
  call @Viper.Console.PrintI64(0)    # prints 0 otherwise
  br done
done:
  ret 0
}
```

```text
 entry ──cbr──▶ then ──▶ done
   │             │
   └──────▶ else ┘
```

**Line by line**

- `%flag = scmp_gt 5, 3` – compare constants; `%flag` holds `1`.
- `cbr %flag, then, else` – branch to `then` when `%flag` is non‑zero, otherwise `else`.
- `then:` / `else:` – block labels.
- `br done` – unconditionally jump to the block `done`.
- `ret 0` – final terminator of the `done` block.

**What just happened?** Labels (`then`, `else`, `done`) mark basic blocks. `br` is an unconditional jump.

**Gotcha:** There is no fall-through; every block must end with a terminator.

### Strings and text
Strings live in globals and use `Viper.Console.PrintStr` for output.

```il
il 0.1
extern @Viper.Console.PrintStr(str) -> void
global const str @.msg = "hello"  # immutable global
func @main() -> i64 {
entry:
  %s = const_str @.msg     # load pointer to string
  call @Viper.Console.PrintStr(%s)   # prints hello
  ret 0
}
```

**Line by line**

- `extern @Viper.Console.PrintStr(str) -> void` – declare the runtime string printer.
- `global const str @.msg = "hello"` – create an immutable global string named `@.msg`.
- `%s = const_str @.msg` – get a pointer to the string constant.
- `call @Viper.Console.PrintStr(%s)` – pass that pointer to the runtime for printing.
- `ret 0` – exit normally.

**What just happened?** `const_str` loads the address of a global string constant.

**Gotcha:** Strings are reference-counted; do not `alloca` them manually.

### Errors and exit codes
Returning a non-zero `i64` sets the process exit code. `trap` reports an error and aborts.

```il
il 0.1
func @main() -> i64 {
entry:
  trap "boom"             # aborts with message
}
```

Running the above produces a non-zero exit and prints the message.

**Line by line**

- `trap "boom"` – raise a runtime trap with the message `boom`.

`trap` aborts execution immediately with a non‑zero status; no `ret` is needed.

**Gotcha:** After a `trap` the VM stops; no `ret` is required.

### From high-level code to IL
A tiny BASIC program:

```basic
10 PRINT 2 + 2
20 END
```

Lowered IL:

```il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 2, 2
  call @Viper.Console.PrintI64(%t0)
  ret 0
}
```

**Line by line**

- `%t0 = add 2, 2` – compute the arithmetic expression from the BASIC code.
- `call @Viper.Console.PrintI64(%t0)` – print the result.
- `ret 0` – exit with success.

**What just happened?** The front end evaluated the expression, emitted an `add`, and called the print routine.

### Debugging IL
- `ilc -run --trace foo.il` prints each instruction as it executes.
- `il-verify foo.il` checks structural rules without running.
- Common errors like "type mismatch" or "undefined block" point to the offending line.

### Tips & best practices
- Keep functions small and testable.
- Use meaningful block labels and value names with `@name` and `%v0` hints.
- Prefer deterministic behaviour; avoid relying on undefined order.

### Next steps
- Read the full [IL reference](#reference) for all instructions.
- Explore the `examples/` and `tests/golden/` directories for more programs.
- Try adding your own IL file and running it with `ilc`.

### Common mistakes
- Forgetting the version line (`il 0.1.2`).
- Missing terminators at the end of blocks.
- Mismatched types in instructions or extern calls.

Happy hacking!

<a id="reference"></a>
## Reference

### Normative scope

The archived IL v0.1.2 specification established the design principles still in force today: IL acts as the "thin waist" between front ends and execution engines, enforces explicit control flow with one terminator per block, and keeps the type system intentionally small (`i1`, `i64`, `f64`, `ptr`, `str`, `void`). The material below supersedes earlier drafts (including v0.1.1) while remaining source-compatible with modules written for those versions. Numeric promotion semantics are specified in [devdocs/specs/numerics.md](devdocs/specs/numerics.md) and the unified trap/handler model is defined in [devdocs/specs/errors.md](devdocs/specs/errors.md); both documents are normative for all front ends and the VM.

### IL Reference (v0.1.2)

> Start here: [IL Quickstart](#quickstart) for a hands-on introduction.

#### Overview
Viper IL is the project’s "thin waist" intermediate language designed to sit between diverse front ends and back ends. Its goals are:

* **Determinism** – VM and native back ends must produce identical observable behaviour.
* **Explicit control flow** – each basic block ends with exactly one terminator; no fallthrough.
* **Static types** – a minimal set of primitive types (`i1`, `i64`, `f64`, `ptr`, `str`, `void`).

Execution is organized as functions consisting of labelled basic blocks.  Modules may execute either under the IL virtual machine interpreter or after lowering to native code through a C runtime.  Front ends such as BASIC first lower into IL patterns described in [BASIC lowering](#lowering).

#### Module & Function Syntax
An IL module is a set of declarations and function definitions.  It starts with a version line:

```text
il 0.1
```

An optional `target "..."` metadata line may follow.  The VM ignores it, but native back ends can use it as advisory information.

```text
il 0.1
target "x86_64-sysv"
```

See [examples/il](../examples/il/) for complete programs.

Each function has the form:

```
func @name(param_list?) -> ret_type {
entry:
  ...
}
```

##### Minimal Function
```text
il 0.1
func @main() -> i64 {
entry:
  ret 0
}
```

##### Extern Declarations
External functions are declared with `extern` and may be called like normal functions.

```text
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  call @Viper.Console.PrintI64(42)
  ret 0
}
```

##### Global Constants
Module-level constants bind a symbol to immutable data such as strings.

```text
il 0.1
global const str @.msg = "hi"
func @main() -> i64 {
entry:
  %s = const_str @.msg
  ret 0
}
```

#### Types
| Type | Meaning | Alignment | Notes |
|------|---------|-----------|-------|
| `void` | no value | — | function return only |
| `i1` | boolean | 1 | produced by comparisons and `trunc1` |
| `i16` | 16-bit signed int | 2 | wrap on overflow |
| `i32` | 32-bit signed int | 4 | wrap on overflow |
| `i64` | 64-bit signed int | 8 | wrap on overflow |
| `f64` | 64-bit IEEE float | 8 | NaN/Inf propagate |
| `ptr` | untyped pointer | 8 | byte-addressed |
| `str` | opaque string handle | 8 | managed by runtime |
| `error` | error value | 8 | exception handling only |
| `resumetok` | resume token | 8 | exception handling only |

#### Constants & Literals
Integers use decimal notation (`-?[0-9]+`).  Floats use decimal with optional fraction (`-?[0-9]+(\.[0-9]+)?`) and permit `NaN`, `Inf`, and `-Inf`.  Booleans `true`/`false` sugar to `i1` values `1`/`0`.  Strings appear in quotes with escapes `\"`, `\\`, `\n`, `\t`, `\xNN`.  `const_null` yields a `ptr` null.

#### Basic Blocks
Functions contain one or more labelled blocks.  Labels end in `:` and the first block is `entry`.  A block may declare parameters; each predecessor must supply matching arguments.  Omitting the argument list is shorthand for passing no values (for example, `br next` is the same as `br next()`).

```text
il 0.1
func @loop(%n: i64) -> i64 {
entry(%n: i64):
  br body(%n, 0)
body(%i: i64, %acc: i64):
  %cmp = scmp_ge %i, %n
  cbr %cmp, done(%acc), body(%i, %acc) ; illustrative loop
  trap ; unreachable
 done(%r: i64):
  ret %r
}
```

#### Control Flow
##### `br`
Unconditional branch to a block with optional arguments.

```text
br next(%v)
```

##### `cbr`
Conditional branch on an `i1` value.

```text
cbr %cond, then, else
```

##### `switch.i32`
Multi-way branch on an `i32` scrutinee with an explicit default.

```text
switch.i32 %scrutinee, ^default, 1 -> ^case_one, 2 -> ^case_two
```

The first operand is the `i32` value to test. The first label after the operand
is the mandatory default target, written using the caret form (e.g.
`^default(args?)`). Subsequent entries pair a distinct 32-bit integer constant
with a branch target using `value -> ^label(args?)`. When no case matches, the
default label is taken. Each target may optionally supply block arguments.

##### `ret`
Return from the current function.

##### `trap`
Abort execution with an unconditional runtime trap.

```text
func @oops() -> void {
entry:
  trap
}
```

#### Instructions
Each non-terminator instruction optionally assigns to a `%temp` and produces a result.  Below, `x` and `y` denote operands.

> _Opcode table note:_ An `InstrType` sentinel means the result or operand type
> is taken from the instruction's declared type.

##### Integer Arithmetic
| Instr | Form | Result |
|-------|------|--------|
| `add` | `add x, y` | `i64` |
| `sub` | `sub x, y` | `i64` |
| `mul` | `mul x, y` | `i64` |
| `sdiv` | `sdiv x, y` | `i64` (trap on divide-by-zero or `INT64_MIN / -1`) |
| `udiv` | `udiv x, y` | `i64` (trap on divide-by-zero) |
| `srem` | `srem x, y` | `i64` (trap on divide-by-zero) |
| `urem` | `urem x, y` | `i64` (trap on divide-by-zero) |

`sdiv` and `srem` follow C semantics: the quotient is truncated toward zero and the remainder keeps the dividend's sign.  Front ends such as BASIC map `\` to `sdiv` and `MOD` to `srem`.

```text
il 0.1
func @main() -> i64 {
entry:
  %t0 = add 2, 3
  ret %t0
}
```

##### Checked Integer Arithmetic
| Instr | Form | Result | Notes |
|-------|------|--------|-------|
| `iadd.ovf` | `iadd.ovf x, y` | `i64` | trap on signed overflow |
| `isub.ovf` | `isub.ovf x, y` | `i64` | trap on signed overflow |
| `imul.ovf` | `imul.ovf x, y` | `i64` | trap on signed overflow |
| `sdiv.chk0` | `sdiv.chk0 x, y` | `i64` | trap on divide-by-zero or overflow |
| `udiv.chk0` | `udiv.chk0 x, y` | `i64` | trap on divide-by-zero |
| `srem.chk0` | `srem.chk0 x, y` | `i64` | trap on divide-by-zero |
| `urem.chk0` | `urem.chk0 x, y` | `i64` | trap on divide-by-zero |

The `.ovf` variants detect signed overflow and trap before producing a wrapped result. The `.chk0` variants explicitly check for zero divisors.

##### Bitwise and Shifts
| Instr | Form | Result |
|-------|------|--------|
| `and` | `and x, y` | `i64` |
| `or`  | `or x, y`  | `i64` |
| `xor` | `xor x, y` | `i64` |
| `shl` | `shl x, y` | `i64` |
| `lshr`| `lshr x, y`| `i64` |
| `ashr`| `ashr x, y`| `i64` |

Shift counts are masked modulo 64, matching the behaviour of x86-64 shifts.

```text
%r = xor 0b1010, 0b1100
```

##### Floating-Point Arithmetic
| Instr | Form | Result |
|-------|------|--------|
| `fadd` | `fadd x, y` | `f64` |
| `fsub` | `fsub x, y` | `f64` |
| `fmul` | `fmul x, y` | `f64` |
| `fdiv` | `fdiv x, y` | `f64` |

```text
%f = fmul 2.0, 4.0
```

##### Comparisons
| Instrs | Form | Result |
|--------|------|--------|
| `icmp_eq`, `icmp_ne` | `icmp_eq x, y` | `i1` |
| `scmp_lt`, `scmp_le`, `scmp_gt`, `scmp_ge` | `scmp_lt x, y` | `i1` signed compare |
| `ucmp_lt`, `ucmp_le`, `ucmp_gt`, `ucmp_ge` | `ucmp_lt x, y` | `i1` unsigned compare |
| `fcmp_lt`, `fcmp_le`, `fcmp_gt`, `fcmp_ge`, `fcmp_eq`, `fcmp_ne` | `fcmp_eq x, y` | `i1` (`NaN` makes `fcmp_eq` false and `fcmp_ne` true) |

```text
%cond = scmp_lt %a, %b
```

##### Conversions
| Instr | Form | Result | Notes |
|-------|------|--------|-------|
| `sitofp` | `sitofp x` | `f64` | signed int to float |
| `fptosi` | `fptosi x` | `i64` | trap on NaN or overflow |
| `cast.si_to_fp` | `cast.si_to_fp i16 x` | `f64` | signed int (any size) to float |
| `cast.ui_to_fp` | `cast.ui_to_fp i16 x` | `f64` | unsigned int to float |
| `cast.fp_to_si.rte.chk` | `cast.fp_to_si.rte.chk i32 x` | `i32` | float to signed int (round-to-even, trap on overflow) |
| `cast.fp_to_ui.rte.chk` | `cast.fp_to_ui.rte.chk i32 x` | `i32` | float to unsigned int (round-to-even, trap on overflow) |
| `cast.si_narrow.chk` | `cast.si_narrow.chk i16 x` | `i16` | narrow signed int, trap on overflow |
| `cast.ui_narrow.chk` | `cast.ui_narrow.chk i16 x` | `i16` | narrow unsigned int, trap on overflow |
| `zext1`  | `zext1 x`  | `i64` | zero-extend i1 to i64 |
| `trunc1` | `trunc1 x` | `i1` | truncate i64 to i1 |

The `cast.*` family provides type-aware conversions with explicit overflow and rounding behavior. The `.rte` suffix denotes round-to-even (IEEE 754 default). The `.chk` suffix indicates trap-on-overflow.

```text
%f = sitofp 42
%i = cast.fp_to_si.rte.chk i32 3.7
```

##### Memory Operations
| Instr | Form | Result |
|-------|------|--------|
| `alloca` | `alloca size` | `ptr` (size < 0 traps; memory zero-initialized) |
| `gep`    | `gep ptr, offs` | `ptr` (no bounds checks) |
| `idx.chk` | `idx.chk idx, bound` | — (trap if idx < 0 or idx >= bound) |
| `load`   | `load type, ptr` | `type` (null or misaligned trap) |
| `store`  | `store type, ptr, value` | — (null or misaligned trap) |
| `addr_of`| `addr_of @global` | `ptr` |
| `const_str` | `const_str @label` | `str` |
| `const_null`| `const_null` | `ptr` |

`idx.chk` performs bounds checking for array accesses, trapping if the index is out of range.

`i64`, `f64`, `ptr`, and `str` loads and stores require 8-byte alignment; misaligned or null accesses trap.  Stack allocations created by `alloca` are zero-initialized and live until the function returns.

```text
func @main() -> i64 {
entry:
  %p = alloca 8
  store i64, %p, 7
  %v = load i64, %p
  ret %v
}
```

##### Calls
| Instr | Form | Result |
|-------|------|--------|
| `call` | `call @f(%x, %y)` | return type of `@f` |
| `call.indirect` | `call.indirect %fn_ptr(%x, %y)` | return type from pointer |

Direct calls use `@symbol` references. Indirect calls use a function pointer as the first operand, followed by arguments. The verifier checks arity and types for both forms.

```text
call @f(%x, %y)
%result = call.indirect %fn_ptr(%arg)
```

##### Error Handling

IL provides a structured error handling system with error values, handler stacks, and resumption points.

**Error Types:**
- `error` — Opaque error value containing kind, code, IP, and line number
- `resumetok` — Token identifying a resumption point in the error handler stack

**Handler Stack Operations:**
| Instr | Form | Notes |
|-------|------|-------|
| `eh.push` | `eh.push ^handler` | Push error handler block onto stack |
| `eh.pop` | `eh.pop` | Pop error handler from stack |
| `eh.entry` | `eh.entry` | Mark entry to error handler block |

**Trap Operations:**
| Instr | Form | Notes |
|-------|------|-------|
| `trap` | `trap` | Unconditional trap (abort) |
| `trap.kind` | `trap.kind kind_code` | Trap with specific kind |
| `trap.err` | `trap.err %error_val` | Trap with error value |
| `trap.from_err` | `trap.from_err type value` | Create and trap with new error |

**Resume Operations:**
| Instr | Form | Notes |
|-------|------|-------|
| `resume.same` | `resume.same %tok` | Resume at same instruction |
| `resume.next` | `resume.next %tok` | Resume at next instruction |
| `resume.label` | `resume.label %tok, ^label` | Resume at specific label |

**Error Value Accessors:**
| Instr | Form | Result |
|-------|------|--------|
| `err.get_kind` | `err.get_kind %err` | `i32` |
| `err.get_code` | `err.get_code %err` | `i32` |
| `err.get_ip` | `err.get_ip %err` | `ptr` |
| `err.get_line` | `err.get_line %err` | `i32` |

**Example:**
```il
func @divide(a: i64, b: i64) -> i64 {
entry:
  eh.push ^handler
  %result = sdiv.chk0 %a, %b
  eh.pop
  ret %result
handler:
  eh.entry
  %err = err.get_kind %error
  ; handle error...
  trap
}
```

#### Runtime ABI
The IL runtime provides helper functions used by front ends and tests. All functions use canonical `Viper.*` namespace names. Legacy `@rt_*` aliases are maintained for compatibility when built with `-DVIPER_RUNTIME_NS_DUAL=ON`.

##### Console I/O

| Function | Signature | Notes |
|----------|-----------|-------|
| `@Viper.Console.PrintStr` | `str -> void` | write string to stdout |
| `@Viper.Console.PrintI64` | `i64 -> void` | write integer to stdout |
| `@Viper.Console.PrintF64` | `f64 -> void` | write float to stdout |
| `@Viper.Console.ReadLine` | `-> str` | read line from stdin, newline stripped |

##### String Operations

| Function | Signature | Notes |
|----------|-----------|-------|
| `@Viper.Strings.Len` | `str -> i64` | length in bytes |
| `@Viper.Strings.Concat` | `str × str -> str` | concatenate strings |
| `@Viper.Strings.Mid` | `str × i64 × i64 -> str` | substring; indices clamp; negative bounds trap |
| `@Viper.Strings.FromInt` | `i64 -> str` | convert integer to string |
| `@Viper.Strings.FromDouble` | `f64 -> str` | convert double to string |

##### Type Conversion

| Function | Signature | Notes |
|----------|-----------|-------|
| `@Viper.Convert.ToInt` | `str -> i64` | convert string to integer; traps on invalid numeric |
| `@Viper.Convert.ToDouble` | `str -> f64` | convert string to double; traps on invalid numeric |

##### Memory Management

| Function | Signature | Notes |
|----------|-----------|-------|
| `@rt_alloc` | `i64 -> ptr` | allocate bytes; negative size traps |
| `@rt_free` | `ptr -> void` | deallocate buffer (optional in v0.1.2) |
| `@rt_str_eq` | `str × str -> i1` | string equality (internal runtime helper) |

#### Terminal & Keyboard Features

RuntimeFeature → Symbol

- `TermCls`    → `rt_term_cls`
- `TermColor`  → `rt_term_color_i32`
- `TermLocate` → `rt_term_locate_i32`
- `GetKey`     → `rt_getkey_str`
- `InKey`      → `rt_inkey_str`

These helpers are gated by feature requests during lowering rather than being emitted unconditionally.

Strings are reference-counted by the runtime implementation.  See [src/runtime/](../src/runtime/) for additional details.

#### Memory Model
IL v0.1.2 is single-threaded.  Pointers are plain addresses with no aliasing rules beyond the type requirements of `load` and `store`.  Memory obtained through `alloca` or the runtime follows the alignment rules above, and invalid accesses (null or misaligned) trap deterministically.

#### Source Locations
`.loc file line col` annotates instructions with source information.  It has no semantic effect.

```text
.loc 1 3 4
%v = add 1, 2
```

#### Verifier Rules
Operand and result type checks are now table-driven from `OpcodeInfo`. The
verifier still has bespoke handlers for `idxchk`, calls, and the handful of ops
wired directly to runtime contracts. Passes can also observe the
`hasSideEffects` flag directly via the shared verifier table.
* First block is `entry` and every block ends with exactly one terminator.
* All referenced labels exist in the same function.
* Operand and result types match instruction signatures.
* Calls match callee arity and types.
* `load`/`store` use `ptr` operands and non-void element types.
* `alloca` sizes are `i64`; constant operands must be non-negative.
* Temporaries are defined before use within a block (cross-block dominance checks may be deferred).
* Block parameters have unique names, non-void types, and each predecessor passes matching arguments.
* `cbr` takes an `i1` condition.

Example diagnostics:

```text
L(%x: i64, %x: i64):
              ^ duplicate param %x

br L(1.0)
       ^ arg type mismatch: expected i64, got f64
```

#### Unknown opcodes

Modules may only contain opcodes listed in this reference. When the VM decoder
encounters an unknown opcode tag it raises an `InvalidOperation` trap, marking
the failing instruction with its source location. Diagnostics include the raw
mnemonic rendered as `opcode#<value>` to make mismatches obvious even when the
module was hand-written or generated by an outdated toolchain. The behaviour is
validated by [`src/tests/vm/UnknownOpcodeTests.cpp`](../src/tests/vm/UnknownOpcodeTests.cpp).

#### Text Grammar (EBNF)
```ebnf
module      ::= "il" VERSION (target_decl)? decl*
VERSION     ::= NUMBER "." NUMBER ("." NUMBER)?
target_decl ::= "target" STRING
decl        ::= extern | global | func
extern      ::= "extern" SYMBOL "(" type_list? ")" "->" type
global      ::= "global" ("const")? type SYMBOL "=" ginit
ginit       ::= STRING | INT | FLOAT | "null" | SYMBOL
func        ::= "func" SYMBOL "(" params? ")" "->" type "{" block+ "}"
params      ::= param ("," param)*
param       ::= IDENT ":" type
type_list   ::= type ("," type)*
block       ::= LABEL ("(" params? ")")? ":" instr* term
instr       ::= TEMP "=" op | op
term        ::= "ret" value? | "br" LABEL ("(" value_list? ")")? | "cbr" value "," LABEL ("(" value_list? ")")?," LABEL ("(" value_list? ")")? | "trap"
op          ::= "add" value "," value | "sub" value "," value | "mul" value "," value | "sdiv" value "," value |
                "udiv" value "," value | "srem" value "," value | "urem" value "," value |
                "and" value "," value | "or" value "," value | "xor" value "," value |
                "shl" value "," value | "lshr" value "," value | "ashr" value "," value |
                "fadd" value "," value | "fsub" value "," value | "fmul" value "," value | "fdiv" value "," value |
                "icmp_eq" value "," value | "icmp_ne" value "," value |
                "scmp_lt" value "," value | "scmp_le" value "," value |
                "scmp_gt" value "," value | "scmp_ge" value "," value |
                "ucmp_lt" value "," value | "ucmp_le" value "," value |
                "ucmp_gt" value "," value | "ucmp_ge" value "," value |
                "fcmp_lt" value "," value | "fcmp_le" value "," value |
                "fcmp_gt" value "," value | "fcmp_ge" value "," value |
                "fcmp_eq" value "," value | "fcmp_ne" value "," value |
                "sitofp" value | "fptosi" value | "zext1" value | "trunc1" value |
                "alloca" value | "gep" value "," value |
                "load" type "," value | "store" type "," value "," value |
                "addr_of" SYMBOL | "const_str" SYMBOL | "const_null" |
                "call" SYMBOL "(" args? ")"
args        ::= value ("," value)*
value_list  ::= value ("," value)*
value       ::= TEMP | SYMBOL | literal
literal     ::= INT | FLOAT | STRING | "true" | "false" | "null"
type        ::= "void" | "i1" | "i16" | "i32" | "i64" | "f64" | "ptr" | "str" | "error" | "resumetok"
```

#### Calling Convention (SysV x64)
Native back ends target the System V x86-64 ABI:

* Integer and pointer arguments: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`.
* Floating-point arguments: `xmm0`–`xmm7`.
* Return values: integers/pointers in `rax`, floats in `xmm0`.
* Call sites maintain 16-byte stack alignment; `i1` arguments are zero-extended to 32 bits.

#### Versioning & Conformance
Modules must begin with `il 0.1.2`.  A conforming implementation accepts this grammar, obeys the semantics above, and traps on the conditions listed for each instruction.  Implementations are validated against the sample suite under [examples/il](../examples/il/).

<a id="lowering"></a>
## Lowering

### BASIC to IL Lowering Reference

**Note:** Built-in math functions use runtime helpers. These are internal helpers that have not yet been migrated to the `Viper.*` namespace and still use `@rt_*` names.

| BASIC         | IL runtime call |
|---------------|-----------------|
| `SQR(x)`      | `call @rt_sqrt(x)` |
| `ABS(i)`      | `call @rt_abs_i64(i)` |
| `ABS(x#)`     | `call @rt_abs_f64(x)` |
| `FLOOR(x)`    | `call @rt_floor(x)` |
| `CEIL(x)`     | `call @rt_ceil(x)` |
| `F(x)`        | `call <retTy> @F(args…)` |
| `S(x$, a())`  | `call void @S(str, ptr)` |

Integer arguments to `SQR`, `FLOOR`, and `CEIL` are first widened to `f64`.

> **Arrays today.** BASIC array declarations and accesses lower to runtime
> helpers such as `@rt_arr_i32_new`, `@rt_arr_i32_len`, `@rt_arr_i32_get`, and
> `@rt_arr_i32_set`. The front end emits bounds checks in IL; failing a check
> calls `@rt_arr_oob_panic` before touching the array storage.

### Procedure calls

User-defined `FUNCTION` and `SUB` calls lower to direct `call` instructions.
Arguments are evaluated left-to-right and converted to the callee's expected
types:

| BASIC form | Lowered IL | Notes |
|------------|------------|-------|
| `F(1)` (callee expects `f64`) | `%t0 = sitofp 1`<br>`%r = call f64 @F(%t0)` | Widen integer arguments with `sitofp` before the call. |
| `CALL P(T$)` | `call void @P(str %T)` | String parameters pass runtime-managed handles directly. |
| `CALL P(A())` | `call void @P(ptr %A)` | Arrays pass by reference without copying. |
| `CALL S()` | `call void @S()` | `SUB` invocation used as a statement. |
| `X = F()` | `%x = call i64 @F()` | `FUNCTION` invocation used as an expression. |

Recursive calls lower the same way; see [factorial.bas](#example-4-read-input-and-compute-factorial)
for a recursion sanity check.

### Compilation unit lowering

BASIC programs lower procedures first, then wrap remaining top-level statements
into a synthetic `@main` function:

```
Program
├─ procs[] → @<name>
└─ main[]  → @main
```

This ordering guarantees functions are listed before `@main`.

### Procedure Definitions

| BASIC                                | IL                                                      |
|--------------------------------------|----------------------------------------------------------|
| `FUNCTION f(...) ... END FUNCTION`   | `func @f(<params>) -> <retTy>`<br>`entry_f`/`ret_f` blocks |
| `SUB s(...) ... END SUB`             | `func @s(<params>)`<br>`entry_s`/`ret_s` blocks            |

Return type is derived from the name suffix (`$` → `str`, `#` → `f64`, none →
`i64`). Parameters lower by scalar type or as array handles.

Each BASIC `FUNCTION` or `SUB` becomes an IL function named `@<name>`. The
entry block label is deterministically `entry_<name>` and a closing block
`ret_<name>` carries the fallthrough `ret`. Scalar parameters are materialized
by allocating stack slots and storing the incoming values. Array parameters
(`i64[]` or `str[]`) are passed as pointers/handles and stored directly without
copying.

```il
func @F(i64 %X) -> i64 {
  entry_F:
    br ret_F
  ret_F:
    ret %X
}
```

```il
func @S(i64 %X) {
  entry_S:
    br ret_S
  ret_S:
    ret void
}
```

#### Deterministic label naming (procedures)

Blocks created while lowering a procedure use predictable labels so goldens
remain consistent. Within a procedure `proc`, block names follow these patterns:

* `entry_proc` and `ret_proc` for the entry and synthetic return blocks.
* `if_then_k_proc`, `if_else_k_proc`, `if_end_k_proc` for `IF` constructs.
* `while_head_k_proc`, `while_body_k_proc`, `while_end_k_proc` for `WHILE`.
* `do_head_k_proc`, `do_body_k_proc`, `do_end_k_proc` for `DO` / `LOOP`.
* `for_head_k_proc`, `for_body_k_proc`, `for_inc_k_proc`, `for_end_k_proc` for `FOR`.
* `exit_end_k_proc` is not used; `EXIT` statements branch directly to the
  surrounding loop's `*_end_k_proc` block.
* `call_cont_k_proc` for call continuations.

The counter `k` is monotonic **per procedure**; labels never depend on container
iteration order. Rebuilding the same source therefore yields identical label
names across runs.

Example BASIC and corresponding IL excerpt:

```
10 FUNCTION F(X)
20 FOR I = 0 TO 1
30   WHILE X < 10
40     IF X THEN CALL P
50     X = X + 1
60   WEND
70 NEXT I
80 F = X
90 RETURN
100 END FUNCTION
```

```
func @F(i64 %X) -> i64 {
  entry_F:
    br for_head_0_F
  for_head_0_F:
    ...
    cbr %t0 for_body_0_F for_end_0_F
  for_body_0_F:
    br while_head_0_F
  while_head_0_F:
    ...
    cbr %t1 while_body_0_F while_end_0_F
  while_body_0_F:
    cbr %t2 if_then_0_F if_end_0_F
  if_then_0_F:
    call @P()
    br call_cont_0_F
  call_cont_0_F:
    ...
    br while_head_0_F
  if_end_0_F:
    ...
    br while_head_0_F
  while_end_0_F:
    br for_inc_0_F
  for_inc_0_F:
    ...
    br for_head_0_F
  for_end_0_F:
    br ret_F
  ret_F:
    ret %X
}
```

### Return statements

`RETURN` lowers directly to an IL `ret` terminator (or `ret void` for `SUB`).
Once emitted, the current block is considered closed and no further statements
from the same BASIC block are lowered. This prevents generating dead
instructions after a `RETURN` and ensures each block has exactly one
terminator.

### Boolean expressions

Relational operators (`=`, `<>`, `<`, `>`, and friends) emit `i1` values that
can feed `cbr` directly. When a boolean expression produces a value that must be
reused, the front end materializes it in a temporary stack slot sized for an
`i1` and fills that slot by branching.

#### `NOT`

`NOT expr` flips the operand and stores the inverted constant into the `i1`
slot:

```il
  %cond = ...            ; operand lowered earlier, yields i1
  %not_slot = alloca 1   ; temporary for the result (i1)
  cbr %cond, label not_true_0, label not_false_0
not_true_0:
  store i1, %not_slot, 0
  br label not_join_0
not_false_0:
  store i1, %not_slot, 1
  br label not_join_0
not_join_0:
  %result = load i1, %not_slot
```

#### `ANDALSO`

`A ANDALSO B` only evaluates `B` when `A` is true. The slot defaults to `FALSE`
and updates with `B`'s value when the right-hand side is visited:

```il
  %lhs = ...               ; first operand (i1)
  %and_slot = alloca 1
  store i1, %and_slot, 0
  cbr %lhs, label and_rhs_0, label and_join_0
and_rhs_0:
  %rhs = ...               ; second operand lowered here
  store i1, %and_slot, %rhs
  br label and_join_0
and_join_0:
  %result = load i1, %and_slot
```

#### `ORELSE`

`A ORELSE B` skips `B` when `A` is true. The `TRUE` branch writes `1` and the
`FALSE` branch lowers `B` and stores that value:

```il
  %lhs = ...               ; first operand (i1)
  %or_slot = alloca 1
  cbr %lhs, label or_true_0, label or_rhs_0
or_true_0:
  store i1, %or_slot, 1
  br label or_join_0
or_rhs_0:
  %rhs = ...               ; second operand lowered here
  store i1, %or_slot, %rhs
  br label or_join_0
or_join_0:
  %result = load i1, %or_slot
```

<a id="passes"></a>
## Passes

### Passes overview

The optimisation passes below operate on IL after front-end lowering.

<a id="constfold"></a>
### constfold (v1)

Folds literal computations at the IL level.

#### Supported folds

| Pattern | Result |
|--------|--------|
| `ABS(i64 lit)` | absolute value as i64 |
| `ABS(f64 lit)` | absolute value as f64 |
| `FLOOR(f64 lit)` | `floor` result |
| `CEIL(f64 lit)` | `ceil` result |
| `SQR(f64 lit ≥ 0)` | `sqrt` result |
| `POW(f64 lit, i64 lit)` *(\|exp\| ≤ 16)* | `pow` result |
| `SIN(0)` | `0` |
| `COS(0)` | `1` |

All floating-point folds use C math semantics and emit exact `f64` literals in the
optimized IL.

#### Caveats

* Only the patterns above are folded.
* No general trigonometric folding beyond `SIN(0)` and `COS(0)`.
* `POW` folds only for small integer exponents and `SQR` requires non‑negative inputs.

<a id="mem2reg"></a>
### mem2reg (v3)

Promotes stack slots to SSA registers across branches and loops by introducing
block parameters and passing values along edges.

#### Scope

* Handles integer (`i64`), float (`f64`), and boolean (`i1`) slots.
* The address of the alloca must not escape (only used by `load`/`store`).

#### Algorithm (seal & rename)

1. Collect promotable allocas.
2. Walk blocks in depth-first order, maintaining the current SSA value for each
   variable per block.
3. Loads are replaced with the current value; stores update it.
4. If a block reads a variable before all predecessors are seen, create a
   placeholder block parameter. When the block becomes *sealed* (all preds
   known), resolve placeholders to real parameters with the correct incoming
   arguments.
5. Values are propagated along edges by updating predecessor terminators with
   branch arguments.
6. After processing, remove the original allocas and stores.

#### Example (diamond)

Input IL:

```il
il 0.1
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  store i64 %t0, 2
  br Join
F:
  store i64 %t0, 3
  br Join
Join:
  %t2 = load i64 %t0
  ret %t2
}
```

After `mem2reg`:

```il
il 0.1
func @main() -> i64 {
entry:
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  br Join(2)
F:
  br Join(3)
Join(%a0:i64):
  ret %a0
}
```

The alloca, loads, and stores are removed. A block parameter on `Join` receives
the value from each predecessor via branch arguments.

#### Example (loop)

Input IL:

```il
il 0.1
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store i64 %t0, 0
  br L1
L1:
  %t1 = load i64 %t0
  %t2 = add %t1, 1
  store i64 %t0, %t2
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1, Exit
Exit:
  %t4 = load i64 %t0
  ret %t4
}
```

After `mem2reg`:

```il
il 0.1
func @main() -> i64 {
entry:
  br L1(0)
L1(%a0:i64):
  %t2 = add %a0, 1
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1(%t2), Exit(%t2)
Exit(%a1:i64):
  ret %a1
}
```

The loop header `L1` has a block parameter `%a0` representing the running value,
fed by both the entry edge and the back-edge. The exit block receives the final
value via parameter `%a1`.

#### Stats

`ilc il-opt --mem2reg-stats` prints the number of promoted variables and the
removed loads/stores when the pass runs.

## Examples

### BASIC to IL Examples

The archived BASIC to IL gallery collected six small BASIC programs (≈10–20 lines each) with their fully lowered IL modules. The examples below update that material to IL v0.1.2 while preserving the original teaching intent.

**Legacy Notation:** The examples in this section use legacy `@rt_*` function names for compatibility. These work when the runtime is built with `-DVIPER_RUNTIME_NS_DUAL=ON` (the current default). New code should use the canonical `@Viper.*` names documented in the Runtime ABI section above. For example:
- `@rt_print_str` → `@Viper.Console.PrintStr`
- `@rt_print_i64` → `@Viper.Console.PrintI64`
- `@rt_len` → `@Viper.Strings.Len`

#### Example 1 — Hello, arithmetic, and a conditional branch

**BASIC**

```basic
10 PRINT "HELLO"
20 LET X = 2 + 3
30 LET Y = X * 2
35 PRINT "READY"
40 PRINT Y
50 IF Y > 8 THEN GOTO 80
60 PRINT 4
70 GOTO 90
80 PRINT Y
90 END
```

**IL**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "READY"
func @main() -> i64 {
entry:
  %x_slot = alloca 8
  %y_slot = alloca 8
  %t0 = add 2, 3
  store i64, %x_slot, %t0
  %xv = load i64, %x_slot
  %t1 = mul %xv, 2
  store i64, %y_slot, %t1
  %s0 = const_str @.L0
  call @rt_print_str(%s0)
  %s1 = const_str @.L1
  call @rt_print_str(%s1)
  %yv0 = load i64, %y_slot
  call @rt_print_i64(%yv0)
  %cond = scmp_gt %yv0, 8
  cbr %cond, then80, else60
then80:
  %yv1 = load i64, %y_slot
  call @rt_print_i64(%yv1)
  br done
else60:
  call @rt_print_i64(4)
  br done
done:
  ret 0
}
```

*Notes:* locals lower to stack slots, branches are explicit, and runtime calls handle I/O.

#### Example 2 — Sum 1..10 with a WHILE loop

**BASIC**

```basic
10 PRINT "SUM 1..10"
20 LET I = 1
30 LET S = 0
40 WHILE I <= 10
50 LET S = S + I
60 LET I = I + 1
70 WEND
80 PRINT S
90 PRINT "DONE"
100 END
```

**IL**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "SUM 1..10"
global const str @.L1 = "DONE"
func @main() -> i64 {
entry:
  %i_slot = alloca 8
  %s_slot = alloca 8
  %h = const_str @.L0
  call @rt_print_str(%h)
  store i64, %i_slot, 1
  store i64, %s_slot, 0
  br loop_head
loop_head:
  %i0 = load i64, %i_slot
  %cond = scmp_le %i0, 10
  cbr %cond, loop_body, done
loop_body:
  %s0 = load i64, %s_slot
  %s1 = add %s0, %i0
  store i64, %s_slot, %s1
  %i1 = add %i0, 1
  store i64, %i_slot, %i1
  br loop_head
done:
  %s2 = load i64, %s_slot
  call @rt_print_i64(%s2)
  %d = const_str @.L1
  call @rt_print_str(%d)
  ret 0
}
```

*Notes:* the canonical while-loop lowering uses explicit head/body/done blocks.

#### Example 3 — Nested loops printing a multiplication table

**BASIC**

```basic
10 PRINT "TABLE 5x5"
20 LET N = 5
30 LET I = 1
40 WHILE I <= N
50 LET J = 1
60 WHILE J <= N
70 PRINT I * J
80 LET J = J + 1
90 WEND
100 LET I = I + 1
110 WEND
120 END
```

**IL**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "TABLE 5x5"
func @main() -> i64 {
entry:
  %n_slot = alloca 8
  %i_slot = alloca 8
  %j_slot = alloca 8
  %h = const_str @.L0
  call @rt_print_str(%h)
  store i64, %n_slot, 5
  store i64, %i_slot, 1
  br outer_head
outer_head:
  %i0 = load i64, %i_slot
  %n0 = load i64, %n_slot
  %oc = scmp_le %i0, %n0
  cbr %oc, outer_body, outer_done
outer_body:
  store i64, %j_slot, 1
  br inner_head
inner_head:
  %j0 = load i64, %j_slot
  %n1 = load i64, %n_slot
  %ic = scmp_le %j0, %n1
  cbr %ic, inner_body, inner_done
inner_body:
  %i1 = load i64, %i_slot
  %prod = mul %i1, %j0
  call @rt_print_i64(%prod)
  %j1 = add %j0, 1
  store i64, %j_slot, %j1
  br inner_head
inner_done:
  %i2 = add %i0, 1
  store i64, %i_slot, %i2
  br outer_head
outer_done:
  ret 0
}
```

*Notes:* nested control flow forms two explicit loop nests.

#### Example 4 — Read input and compute factorial

**BASIC**

```basic
10 PRINT "FACTORIAL"
20 PRINT "ENTER N:"
30 LET S$ = INPUT$
40 LET N = VAL(S$)
50 LET R = 1
60 WHILE N > 1
70 LET R = R * N
80 LET N = N - 1
90 WEND
100 PRINT R
110 END
```

**IL**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
global const str @.L0 = "FACTORIAL"
global const str @.L1 = "ENTER N:"
func @main() -> i64 {
entry:
  %s_slot = alloca 8
  %n_slot = alloca 8
  %r_slot = alloca 8
  %h0 = const_str @.L0
  call @rt_print_str(%h0)
  %h1 = const_str @.L1
  call @rt_print_str(%h1)
  %line = call @rt_input_line()
  store str, %s_slot, %line
  %sval = load str, %s_slot
  %n0 = call @rt_to_int(%sval)
  store i64, %n_slot, %n0
  store i64, %r_slot, 1
  br loop_head
loop_head:
  %n1 = load i64, %n_slot
  %cond = scmp_gt %n1, 1
  cbr %cond, loop_body, done
loop_body:
  %r0 = load i64, %r_slot
  %r1 = mul %r0, %n1
  store i64, %r_slot, %r1
  %n2 = sub %n1, 1
  store i64, %n_slot, %n2
  br loop_head
done:
  %r2 = load i64, %r_slot
  call @rt_print_i64(%r2)
  ret 0
}
```

*Notes:* the runtime provides string input and numeric conversion helpers; errors surface as traps.

#### Example 5 — String concat, length, substring, equality

**BASIC**

```basic
10 LET A$ = "JOHN"
20 LET B$ = "DOE"
30 LET C$ = A$ + " "
40 LET C$ = C$ + B$
50 PRINT C$
60 LET L = LEN(C$)
70 PRINT L
80 PRINT MID$(C$, 1, 1)
90 IF C$ = "JOHN DOE" THEN PRINT 1 ELSE PRINT 0
100 END
```

**IL**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_concat(str, str) -> str
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
global const str @.L2 = " "
global const str @.L3 = "JOHN DOE"
func @main() -> i64 {
entry:
  %a_slot = alloca 8
  %b_slot = alloca 8
  %c_slot = alloca 8
  %l_slot = alloca 8
  %sA = const_str @.L0
  store str, %a_slot, %sA
  %sB = const_str @.L1
  store str, %b_slot, %sB
  %sSpace = const_str @.L2
  %a0 = load str, %a_slot
  %c0 = call @rt_concat(%a0, %sSpace)
  store str, %c_slot, %c0
  %b0 = load str, %b_slot
  %c1 = call @rt_concat(%c0, %b0)
  store str, %c_slot, %c1
  call @rt_print_str(%c1)
  %len = call @rt_len(%c1)
  store i64, %l_slot, %len
  call @rt_print_i64(%len)
  %first = call @rt_substr(%c1, 0, 1)
  call @rt_print_str(%first)
  %target = const_str @.L3
  %eq = call @rt_str_eq(%c1, %target)
  cbr %eq, then1, else0
then1:
  call @rt_print_i64(1)
  br exit
else0:
  call @rt_print_i64(0)
  br exit
exit:
  ret 0
}
```

*Notes:* all string work is delegated to runtime helpers and equality returns an `i1` flag.

#### Example 6 — Heap array via `rt_alloc`, squares, and floating average

**BASIC**

```basic
10 LET N = 5
20 DIM A(N)
30 LET I = 0
40 LET SUM = 0
50 WHILE I < N
60 LET A(I) = I * I
70 LET SUM = SUM + A(I)
80 LET I = I + 1
90 WEND
100 LET AVG = SUM / N
110 PRINT AVG
120 PRINT "DONE"
130 END
```

**IL**

```il
il 0.1
extern @rt_alloc(i64) -> ptr
extern @rt_print_f64(f64) -> void
extern @rt_print_str(str) -> void
global const str @.L0 = "DONE"
func @main() -> i64 {
entry:
  %n_slot = alloca 8
  %i_slot = alloca 8
  %sum_slot = alloca 8
  %a_slot = alloca 8
  store i64, %n_slot, 5
  store i64, %i_slot, 0
  store i64, %sum_slot, 0
  %n0 = load i64, %n_slot
  %bytes = mul %n0, 8
  %base = call @rt_alloc(%bytes)
  store ptr, %a_slot, %base
  br loop_head
loop_head:
  %i0 = load i64, %i_slot
  %n1 = load i64, %n_slot
  %cond = scmp_lt %i0, %n1
  cbr %cond, loop_body, done
loop_body:
  %a0 = load ptr, %a_slot
  %offset = shl %i0, 3
  %elem_ptr = gep %a0, %offset
  %sq = mul %i0, %i0
  store i64, %elem_ptr, %sq
  %sum0 = load i64, %sum_slot
  %sum1 = add %sum0, %sq
  store i64, %sum_slot, %sum1
  %i1 = add %i0, 1
  store i64, %i_slot, %i1
  br loop_head
done:
  %sum2 = load i64, %sum_slot
  %n2 = load i64, %n_slot
  %fsum = sitofp %sum2
  %fn = sitofp %n2
  %avg = fdiv %fsum, %fn
  call @rt_print_f64(%avg)
  %msg = const_str @.L0
  call @rt_print_str(%msg)
  ret 0
}
```

*Notes:* heap allocation models BASIC arrays and floating arithmetic uses `sitofp`/`fdiv`.

#### How to use these examples

* Golden tests: keep each BASIC + IL pair in sync when validating lowering.
* VM vs native: run the IL through both execution modes and compare output.
* Coverage: together they exercise arithmetic, branching, loops, input, strings, heap, and floating-point operations.

Sources:
- docs/il-guide.md#quickstart
- docs/il-guide.md#reference
- docs/il-guide.md#reference
- docs/il-guide.md#lowering
- docs/il-guide.md#constfold
- docs/il-guide.md#mem2reg
- archive/docs/basic-to-il-examples.md
