# IL v0.1.2 Specification

Status: Normative for VM and code generator MVP
Compatibility: Supersedes v0.1.1; older modules parse unchanged

## Goals & design principles
- Thin waist between multiple front ends and back ends
- Deterministic semantics so VM and native agree
- Explicit control flow (no fallthrough; one terminator per block)
- Small type system (`i64`, `f64`, `i1`, `ptr`, `str`)
- Non-semantic optimizations only

## Module structure & example
Every module begins with a version line and may specify a target triple. Symbols are `@` for globals/functions and `%` for temporaries. Labels end with `:`.

```text
il 0.1.2
target "x86_64-sysv" ; optional, ignored by the VM
```

Example:

```il
il 0.1.2

extern @rt_print_str(str) -> void

global const str @.L0 = "HELLO"

func @main() -> i32 {
entry:
  %s = const_str @.L0
  call @rt_print_str(%s)
  ret 0
}
```

See [examples/il](examples/il/) for complete programs.

## Types
| Type | Meaning | Alignment | Notes |
|------|---------|-----------|-------|
| `void` | no value | — | only as function return |
| `i1` | boolean | 1 | produced by comparisons and `trunc1` |
| `i64` | 64-bit signed int | 8 | wrap on overflow |
| `f64` | 64-bit IEEE float | 8 | NaN/Inf propagate |
| `ptr` | untyped pointer | 8 | byte-addressed |
| `str` | opaque string handle | 8 | managed by runtime |

## Constants & literals
- Integers: `-?[0-9]+`
- Floats: `-?[0-9]+(\.[0-9]+)?`, plus `NaN`, `Inf`, `-Inf`
- Bools: `true` / `false` (sugar for `i1` `1`/`0`)
- Strings: `"..."` with escapes `\" \\ \n \t \xNN`
- Null pointer: `const_null` (type `ptr`)

## Functions & basic blocks
Functions declare parameters and a single return type. Bodies contain one or more basic blocks. The first block is the entry. Each block:
- is labeled (`label:`)
- defines temporaries (`%name =`)
- ends with exactly one terminator (`br`, `cbr`, or `ret`)
No implicit fallthrough between blocks.

Blocks may declare parameters in their header:

```il
L(%x: i64, %p: ptr):
  ...
```

Terminators supply arguments for the next block's parameters:

```il
br L(%a, %b)
cbr %cond, T(%a), F(%b)
```

The 0-argument shorthand forms `br L` and `cbr %cond, T, F` remain valid. On entry, block parameters are bound to the passed values; each predecessor must provide the exact number and types of parameters declared.
Example:

```il
br L ; equivalent to br L()
```

## Instruction set
Legend: `→` result type. Traps denote runtime errors.

### Integer arithmetic
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `add` | `i64, i64` | `i64` | — |
| `sub` | `i64, i64` | `i64` | — |
| `mul` | `i64, i64` | `i64` | — |
| `sdiv` | `i64, i64` | `i64` | divisor = 0 or (`INT64_MIN`, `-1`) |
| `udiv` | `i64, i64` | `i64` | divisor = 0 |
| `srem` | `i64, i64` | `i64` | divisor = 0 |
| `urem` | `i64, i64` | `i64` | divisor = 0 |

`sdiv` and `srem` follow C semantics: the quotient is truncated toward zero and
the remainder has the sign of the dividend. Front ends like BASIC map `\` to
`sdiv` and `MOD` to `srem`.

### Bitwise and shifts
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `and` / `or` / `xor` | `i64, i64` | `i64` | — |
| `shl` / `lshr` / `ashr` | `i64, i64` | `i64` | shift count masked mod 64 |

### Floating-point arithmetic
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `fadd` / `fsub` / `fmul` / `fdiv` | `f64, f64` | `f64` | — |

### Comparisons
| Instruction | Operands | Result | Notes/Traps |
|-------------|----------|--------|-------------|
| `icmp_eq` / `icmp_ne` | `i64, i64` | `i1` | — |
| `scmp_lt` / `scmp_le` / `scmp_gt` / `scmp_ge` | `i64, i64` | `i1` | signed |
| `ucmp_lt` / `ucmp_le` / `ucmp_gt` / `ucmp_ge` | `i64, i64` | `i1` | unsigned |
| `fcmp_lt` / `fcmp_le` / `fcmp_gt` / `fcmp_ge` / `fcmp_eq` / `fcmp_ne` | `f64, f64` | `i1` | `fcmp_eq` false and `fcmp_ne` true on NaN |

### Conversions
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `sitofp` | `i64` | `f64` | — |
| `fptosi` | `f64` | `i64` | NaN or out-of-range |
| `zext1` | `i1` | `i64` | — |
| `trunc1` | `i64` | `i1` | — |

### Memory & pointers
| Instruction | Operands | Result | Traps/Notes |
|-------------|----------|--------|-------------|
| `alloca` | `i64` size | `ptr` | size < 0 (if constant); zero-initialized, frame-local |
| `gep` | `ptr, i64` | `ptr` | pointer + offset; no bounds checks |
| `load` | `type, ptr` | `type` | null or misaligned pointer |
| `store` | `type, ptr, type` | — | null or misaligned pointer |
| `addr_of` | `@global` | `ptr` | — |
| `const_null` | — | `ptr` | — |
| `const_str` | `@label` | `str` | label refers to constant string |

Natural alignment: `i64`, `f64`, `ptr`, and `str` require 8-byte alignment. Misaligned `load`/`store` trap.

### Calls & returns
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `call` | `@f(args...)` | function return type | arity or type mismatch (verified) |
| `ret` | `[value]` | — | — |

### Control flow (terminators)
| Instruction | Operands | Result | Traps |
|-------------|----------|--------|-------|
| `br` | `label` | — | — |
| `cbr` | `i1, label, label` | — | — |
| `ret` | `[value]` | — | — |
| `trap` | — | — | unconditional runtime error |

## Runtime ABI
| Function | Signature | Notes |
|----------|-----------|-------|
| `@rt_print_str` | `str -> void` | write string to stdout |
| `@rt_print_i64` | `i64 -> void` | write integer to stdout |
| `@rt_print_f64` | `f64 -> void` | write float to stdout |
| `@rt_input_line` | `-> str` | read line, newline stripped |
| `@rt_len` | `str -> i64` | length in bytes |
| `@rt_concat` | `str × str -> str` | concatenate strings |
| `@rt_substr` | `str × i64 × i64 -> str` | indices clamp; negative params trap |
| `@rt_to_int` | `str -> i64` | traps on invalid numeric |
| `@rt_to_float` | `str -> f64` | traps on invalid numeric |
| `@rt_str_eq` | `str × str -> i1` | string equality |
| `@rt_alloc` | `i64 -> ptr` | allocate bytes; negative size traps |
| `@rt_free` | `ptr -> void` | deallocate; optional in v0.1.2 |

Strings are ref-counted (implementation detail).

## Memory model
IL has no concurrency in v0.1.2. Pointers are plain addresses; no aliasing rules beyond load/store types. `alloca` memory is zero-initialized and lives until the function returns. Loads and stores to `null` or misaligned addresses trap.

## Verifier obligations
- First block is entry; every block ends with one terminator
- All referenced labels exist in the same function
- Operand and result types match instruction signatures
- Calls match callee arity and types
- `load`/`store` use `ptr` operands and non-void types
- `alloca` size is `i64` (non-negative if constant)
- Temporaries are defined before use within a block (dominance across blocks deferred)
- Block parameters have unique names per block, non-void types, and are visible only within their block
- `br`/`cbr` pass arguments matching target block parameter counts and types; `%c` in `cbr` is `i1`

Example diagnostics:

```text
L(%x: i64, %x: i64):
              ^ duplicate param %x

br L(1.0)
       ^ arg type mismatch: expected i64, got f64
```

## Text grammar (EBNF)
```ebnf
module      ::= "il" VERSION (target_decl)? decl_or_def*
VERSION     ::= NUMBER "." NUMBER ("." NUMBER)?
target_decl ::= "target" STRING

decl_or_def ::= extern_decl | global_decl | func_def
extern_decl ::= "extern" SYMBOL "(" type_list? ")" "->" type
global_decl ::= "global" ("const")? type SYMBOL "=" ginit
ginit       ::= STRING | INT | FLOAT | "null" | SYMBOL
func_def    ::= "func" SYMBOL "(" params? ")" "->" type ("internal")? "{" block+ "}"
params      ::= param ("," param)*
param       ::= IDENT ":" type
type_list   ::= type ("," type)*

block       ::= LABEL ("(" params? ")")? ":" instr* term
instr       ::= TEMP "=" op | op
term        ::= "ret" value?
             | "br" LABEL ("(" value_list? ")")?
             | "cbr" value "," LABEL ("(" value_list? ")")? "," LABEL ("(" value_list? ")")?
op          ::= "add" value "," value
             | "sub" value "," value
             | "mul" value "," value
             | "sdiv" value "," value
             | "udiv" value "," value
             | "srem" value "," value
             | "urem" value "," value
             | "and" value "," value
             | "or" value "," value
             | "xor" value "," value
             | "shl" value "," value
             | "lshr" value "," value
             | "ashr" value "," value
             | "fadd" value "," value
             | "fsub" value "," value
             | "fmul" value "," value
             | "fdiv" value "," value
             | "icmp_eq" value "," value
             | "icmp_ne" value "," value
             | "scmp_lt" value "," value
             | "scmp_le" value "," value
             | "scmp_gt" value "," value
             | "scmp_ge" value "," value
             | "ucmp_lt" value "," value
             | "ucmp_le" value "," value
             | "ucmp_gt" value "," value
             | "ucmp_ge" value "," value
             | "fcmp_lt" value "," value
             | "fcmp_le" value "," value
             | "fcmp_gt" value "," value
             | "fcmp_ge" value "," value
             | "fcmp_eq" value "," value
             | "fcmp_ne" value "," value
             | "sitofp" value
             | "fptosi" value
             | "zext1" value
             | "trunc1" value
             | "alloca" value
             | "gep" value "," value
             | "load" type "," value
             | "store" type "," value "," value
             | "addr_of" SYMBOL
             | "const_str" SYMBOL
             | "const_null"
             | "call" SYMBOL "(" args? ")"
             | "trap"
args        ::= value ("," value)*
value_list  ::= value ("," value)*
value       ::= TEMP | SYMBOL | literal
literal     ::= INT | FLOAT | STRING | "true" | "false" | "null"
type        ::= "void" | "i1" | "i64" | "f64" | "ptr" | "str"
```

## Calling convention (SysV x64)
- Integers and pointers: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
- Floats: `xmm0`–`xmm7`
- Return: integer/pointer in `rax`, float in `xmm0`
- Stack aligned to 16 bytes at call sites
- `i1` arguments are zero-extended to 32 bits

## Versioning & conformance
Modules must start with `il 0.1.2`. Future versions must preserve semantics of existing opcodes. A VM or backend is conformant if it:
- accepts this grammar and passes the verifier
- produces identical observable behaviour (stdout, exit code) on the sample suite under [examples/il](examples/il/)
- traps on the conditions listed above
