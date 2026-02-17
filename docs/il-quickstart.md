---
status: active
audience: public
last-updated: 2026-02-02
---

# Viper IL — Quickstart

Get started with Viper's Intermediate Language (IL) in minutes. This guide shows you how to write, verify, and execute
IL programs.

---

## What is Viper IL?

Viper IL is a **typed, readable intermediate language** that serves as the core of the Viper toolchain:

- **Frontends** (Zia, BASIC, etc.) compile to IL
- **VM** executes IL directly via bytecode interpretation
- **Verifier** checks IL for safety and correctness
- **Transforms** (SimplifyCFG, Liveness, etc.) optimize IL
- **Backends** compile IL to native code

IL is designed to be **human-readable** and **easy to inspect**, making it ideal for learning compiler concepts and
debugging.

---

## Quick Start: Your First IL Program

### 1. Write a Simple Function

Create a file `hello.il`:

```il
il 0.1

extern @Viper.Terminal.PrintStr(str) -> void
global const str @.msg = "Hello, Viper IL!"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Viper.Terminal.PrintStr(%msg)
  ret 0
}
```

Compatibility:

- When built with `-DVIPER_RUNTIME_NS_DUAL=ON`, legacy `@rt_*` externs are accepted as aliases of `@Viper.*`.
- New code should emit `@Viper.*`.

### 2. Verify the IL

```sh
il-verify hello.il
```

The verifier checks types, control flow, and instruction well-formedness. No output means success!

### 3. Run the Program

```sh
viper -run hello.il
```

Output:

```
Hello, Viper IL!
```

---

## IL Structure Explained

### Version Header

Every IL file starts with a version declaration:

```il
il 0.1
```

Use `il 0.1.2` for experimental features.

### Extern Declarations

Declare runtime functions you'll call:

```il
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.String.Concat(str, str) -> str
```

### Global Constants

Define immutable data:

```il
global const str @.hello = "Hello!"
global const i64 @.answer = 42
```

### Functions

Functions contain basic blocks and instructions:

```il
func @square(i64 %x) -> i64 {
entry:
  %result = mul %x, %x
  ret %result
}
```

**Key points:**

- `@name` — Function symbol
- `%var` — SSA register (assigned once, used many times)
- `entry:` — First basic block label
- `ret` — Return terminator (required at end of block)

---

## Basic IL Examples

### Arithmetic

```il
func @add(i64 %a, i64 %b) -> i64 {
entry:
  %sum = add %a, %b
  ret %sum
}
```

### Control Flow

```il
func @max(i64 %a, i64 %b) -> i64 {
entry:
  %cond = scmp_gt %a, %b
  cbr %cond, greater, less_or_equal

greater:
  ret %a

less_or_equal:
  ret %b
}
```

### Calling Functions

```il
func @square(i64 %x) -> i64 {
entry:
  %result = mul %x, %x
  ret %result
}

func @call_square(i64 %y) -> i64 {
entry:
  %result = call @square(%y)
  ret %result
}
```

### Switch Statements

```il
func @classify(i64 %n) -> i64 {
entry:
  %n32:i32 = cast.si_narrow.chk %n
  switch.i32 %n32, ^default, 0 -> ^zero, 1 -> ^one, 2 -> ^two

default:
  ret 99

zero:
  ret 0

one:
  ret 10

two:
  ret 20
}
```

---

## IL Toolchain

### Verify IL

Check IL files for correctness:

```sh
il-verify program.il
```

The verifier catches:

- Type mismatches
- Missing terminators
- Invalid control flow
- Undefined references

### Disassemble IL

Pretty-print IL files:

```sh
il-dis program.il
```

### Execute IL

Run IL programs on the VM:

```sh
viper -run program.il
```

### Transform IL

Apply optimization passes:

```sh
viper il-opt program.il --passes "simplify-cfg" -o optimized.il
```

Preset pipelines via `viper il-opt`:

```sh
# O1 (default): simplify-cfg + mem2reg + SCCP + LICM + peephole + DCE
viper il-opt program.il --pipeline O1 -o program.o1.il

# O2: adds loop-simplify + loop-unroll + indvars + inline + GVN + EarlyCSE + DSE + late-cleanup
viper il-opt program.il --pipeline O2 -o program.o2.il

# Custom sequence
viper il-opt program.il --passes "simplify-cfg,mem2reg,sccp,dce" -o out.il
```

Available passes: `check-opt`, `dce`, `dse`, `earlycse`, `gvn`, `indvars`, `inline`, `late-cleanup`, `licm`, `loop-simplify`, `loop-unroll`, `mem2reg`, `peephole`, `sccp`, `simplify-cfg`

---

## Next Steps

**Learn More:**

- **[IL Reference](il-reference.md)** — Complete instruction catalog
- **[IL Guide](il-guide.md)** — Comprehensive guide with examples
- **[Frontend How-To](frontend-howto.md)** — Build your own frontend

**Explore:**

- Check `src/tests/golden/il/` for more IL examples
- Run `viper --help` for all available options
- Experiment with optimization passes
