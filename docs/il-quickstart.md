---
status: draft
audience: newcomer
last-verified: 2025-09-12
---
# IL Quickstart

Welcome! This guide is for developers coming from languages like C#, Java, TypeScript, or Python who want a hands-on tour of Viper's intermediate language (IL). No prior compiler experience is required.

## What is the IL?
Viper IL is the "thin waist" between high-level front ends and the virtual machine (VM). Front ends emit IL, and back ends like the VM execute it deterministically. Keeping the IL small and explicit makes it easy to inspect and test. IL acts as a stable contract: front ends can evolve independently while the VM only needs to understand the IL version printed at the top of the file. Because IL is textual and minimal, you can open any module in a regular editor, reason about every step, and run it in a reproducible way.

Why bother with a separate IL at all? Directly interpreting a high-level language couples the VM to that language's semantics. An IL gives Viper a neutral core that multiple front ends can target (BASIC today, others tomorrow) while sharing a single runtime. It also provides a convenient place for verification passes and optimisations before execution.

## Program structure

An IL module is plain text. Its top‑level layout is:

1. **Version line** – `il 0.1.2` pins the expected IL grammar.
2. **Extern declarations** – `extern @name(signature) -> ret` describes functions provided by the runtime or other modules.
3. **Global constants** – `global const str @.msg = "hi"` defines immutable data.
4. **Functions** – `func @main() -> i64 { ... }` contains basic blocks and instructions.

Inside a function:

* Each basic block starts with a label like `entry:`. There is no fall‑through; control transfers with a terminator such as `ret`, `br`, or `cbr`.
* Instructions assign results to SSA registers (`%v0`, `%tmp1`). A register is defined once and used many times.
* Comments begin with `#` and run to the end of the line.

These pieces mirror what a compiler would normally keep in its internal IR and make it explicit for learning and debugging.

## Your first IL program
Create a file `first.il` with the contents:

```il
# Print the number 4 and exit.
il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  call @rt_print_i64(4)    # runtime prints `4\n`
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
- `il 0.1.2` – required version header.
- `extern @rt_print_i64(i64) -> void` – declare a runtime function taking an `i64` and returning `void`.
- `func @main() -> i64 {` – define the `@main` function that returns an `i64` exit code.
- `entry:` – the initial basic block label.
- `call @rt_print_i64(4)` – invoke the extern with the literal `4`.
- `ret 0` – terminate the function and supply the process exit status.
- `}` – close the function body.

**What just happened?** `rt_print_i64` is supplied by the runtime and prints its argument. Every function ends with a terminator such as `ret` giving the program's exit code.

**Gotcha:** Every module must start with a version line (`il 0.1.2`).

## Values and types
IL is statically typed and uses SSA-style virtual registers (`%v0`, `%t1`, ...). Primitive types include `i1` (bool), `i64`, `f64`, `ptr`, and `str`.

```il
il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %p = alloca 8            # reserve 8 bytes on the stack
  store i64, %p, 10        # write constant 10 to memory
  %v0 = load i64, %p       # read it back
  call @rt_print_i64(%v0)  # prints 10
  ret 0
}
```

**Line by line**

- `%p = alloca 8` – allocate eight bytes of stack memory and bind its address to `%p` (type `ptr`).
- `store i64, %p, 10` – store the 64‑bit constant `10` into the memory pointed to by `%p`.
- `%v0 = load i64, %p` – load an `i64` from `%p` into `%v0`.
- `call @rt_print_i64(%v0)` – pass the loaded value to the runtime print routine.
- `ret 0` – return from `main` with exit code 0.

**What just happened?** `alloca` creates a stack slot, `store` writes to it, and `load` reads from it.

**Gotcha:** All integers are 64-bit; mixing `i64` and `f64` requires explicit casts.

## Locals, params, and calls
Functions declare typed parameters. Values are passed and returned explicitly.

```il
il 0.1.2
extern @rt_print_i64(i64) -> void
func @add(i64 %a, i64 %b) -> i64 {
entry:
  %sum = add %a, %b        # compute a + b
  ret %sum
}
func @main() -> i64 {
entry:
  %v0 = call @add(2, 3)    # call with constants
  call @rt_print_i64(%v0)  # prints 5
  ret 0
}
```

**Line by line**

- `func @add(i64 %a, i64 %b) -> i64` – declare a function with two `i64` parameters and an `i64` return type.
- `%sum = add %a, %b` – add the two parameters and bind the sum.
- `ret %sum` – return the computed sum.
- `func @main() -> i64 { ... }` – define the entry point.
- `%v0 = call @add(2, 3)` – call `@add` with literal arguments; result stored in `%v0`.
- `call @rt_print_i64(%v0)` – print the returned value.
- `ret 0` – exit with status 0.

**What just happened?** `call` pushes arguments and receives a result. Each function has one entry block.

**Gotcha:** Arguments are immutable; use `alloca` + `store` if you need a mutable local.

## Arithmetic and comparisons

```il
il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %v0 = add 2, 2           # 4
  %v1 = scmp_gt %v0, 3     # 1 (true)
  call @rt_print_i64(%v1)  # prints 1
  ret 0
}
```

**Line by line**

- `%v0 = add 2, 2` – compute the constant expression `2 + 2`.
- `%v1 = scmp_gt %v0, 3` – signed compare‑greater; result is `1` because 4 > 3.
- `call @rt_print_i64(%v1)` – zero‑extend the `i1` result and print it.
- `ret 0` – terminate `main` with success.

**What just happened?** `scmp_gt` compares signed integers and yields an `i1` (0 or 1).

**Gotcha:** Comparison results are `i1`; printing them with `rt_print_i64` zero-extends to `i64`.

## Control flow
Blocks end with a terminator. `cbr` chooses a target based on an `i1` value.

```il
il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %flag = scmp_gt 5, 3     # 1 means take then
  cbr %flag, then, else    # conditional branch
then:
  call @rt_print_i64(1)    # prints 1 if flag != 0
  br done                  # jump to exit
else:
  call @rt_print_i64(0)    # prints 0 otherwise
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

## Strings and text
Strings live in globals and use `rt_print_str` for output.

```il
il 0.1.2
extern @rt_print_str(str) -> void
global const str @.msg = "hello"  # immutable global
func @main() -> i64 {
entry:
  %s = const_str @.msg     # load pointer to string
  call @rt_print_str(%s)   # prints hello
  ret 0
}
```

**Line by line**

- `extern @rt_print_str(str) -> void` – declare the runtime string printer.
- `global const str @.msg = "hello"` – create an immutable global string named `@.msg`.
- `%s = const_str @.msg` – get a pointer to the string constant.
- `call @rt_print_str(%s)` – pass that pointer to the runtime for printing.
- `ret 0` – exit normally.

**What just happened?** `const_str` loads the address of a global string constant.

**Gotcha:** Strings are reference-counted; do not `alloca` them manually.

## Errors and exit codes
Returning a non-zero `i64` sets the process exit code. `trap` reports an error and aborts.

```il
il 0.1.2
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

## From high-level code to IL
A tiny BASIC program:

```basic
10 PRINT 2 + 2
20 END
```

Lowered IL:

```il
il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 2, 2
  call @rt_print_i64(%t0)
  ret 0
}
```

**Line by line**

- `%t0 = add 2, 2` – compute the arithmetic expression from the BASIC code.
- `call @rt_print_i64(%t0)` – print the result.
- `ret 0` – exit with success.

**What just happened?** The front end evaluated the expression, emitted an `add`, and called the print routine.

## Debugging IL
- `ilc -run --trace foo.il` prints each instruction as it executes.
- `ilc -verify foo.il` checks structural rules without running.
- Common errors like "type mismatch" or "undefined block" point to the offending line.

## Tips & best practices
- Keep functions small and testable.
- Use meaningful block labels and value names with `@name` and `%v0` hints.
- Prefer deterministic behaviour; avoid relying on undefined order.

## Next steps
- Read the full [IL reference](il-reference.md) for all instructions.
- Explore the `examples/` and `tests/golden/` directories for more programs.
- Try adding your own IL file and running it with `ilc`.

## Common mistakes
- Forgetting the version line (`il 0.1.2`).
- Missing terminators at the end of blocks.
- Mismatched types in instructions or extern calls.

Happy hacking!
