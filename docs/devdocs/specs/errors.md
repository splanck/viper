---
status: draft
last-verified: 2025-09-23
audience: public
---

# Global Error and Trap Semantics

This document specifies the cross-layer error and trap model used by VIPER. It covers
checked IL instructions, runtime bridges, handler control flow, and BASIC surface
syntax lowering rules. The material here is normative for the VM, the BASIC front end,
and any future languages that target the IL trap mechanism.

## Trap Kinds

The VM recognizes the following trap kinds. They are defined and shared between
checked IL operations, the runtime C ABI, and BASIC surface semantics.

| Kind | Description |
|------|-------------|
| `DivideByZero` | Integer divide/remainder with a zero divisor. |
| `Overflow` | Checked arithmetic overflow (e.g., `INT64_MIN / -1`, `i64` abs). |
| `InvalidCast` | Invalid numeric or pointer cast. |
| `DomainError` | Invalid mathematical domain (e.g., sqrt of negative). |
| `Bounds` | Bounds check failure (arrays, strings). |
| `FileNotFound` | File system open on a path that does not exist. |
| `EOF` | End-of-file reached while input still expected. |
| `IOError` | Other I/O failure (permissions, device errors). |
| `InvalidOperation` | Operation outside the allowed state machine. |
| `RuntimeError` | Catch-all for unexpected runtime failures. |

## IL Error-Handling Primitives

The IL exposes structured primitives for raising and handling traps:

- `trap.kind <Kind>` raises a trap with the given kind and default error
  metadata.
- `trap.err %err` raises with a fully populated `Error` record.
- `eh.push ^handler` activates the handler block referenced by label.
- `eh.pop` removes the most recently pushed handler.
- `resume.same %tok`, `resume.next %tok`, and `resume.label %tok, ^L` resume
  execution after a trap using the supplied resume token.

Handlers receive their parameters in SSA form and must not forge resume tokens.

## Trap Flow and Handler Control

Handlers are installed with `eh.push ^handler` and removed with `eh.pop`. When a
trap is raised, the VM unwinds stack frames until it finds the most recent
handler. Control then jumps into the handler block with two parameters:
`(%err: Error, %tok: ResumeTok)`.

### Trap Dispatch Sequence

```
[Trap raised]
      |
      v
[Unwind frames] -- no handler --> [Terminate with diagnostic]
      |
      v
[Nearest eh.push]
      |
      v
[Enter handler block with (%err, %tok)]
```

The `Error` record contains `{ kind:i32, code:i32, ip:u64, line:i32 }` where
`line` is `-1` when no source line is available. The resume token is opaque and
must be passed unchanged to `resume.*` instructions.

### Resume Modes

Handlers may resume execution in three different ways. Each is shown as a
control-flow schematic; `%tok` is the resume token received by the handler.

#### `resume.same %tok`

```
[Handler]
    |
    | resume.same %tok
    v
[Faulting instruction re-executes]
```

Use this when the handler has corrected the underlying issue (for example, by
setting a divisor to a non-zero default) and wants to retry the failing
instruction.

#### `resume.next %tok`

```
[Handler]
    |
    | resume.next %tok
    v
[Instruction immediately after the fault]
```

Choose this mode when the handler wants to skip the faulting instruction but
continue within the same block (mirrors BASIC `RESUME NEXT`).

#### `resume.label %tok, ^L`

```
[Handler]
    |
    | resume.label %tok, ^L
    v
[Branch to label/block L]
```

This mode performs a non-local branch to another block in the current function.
It matches BASIC `RESUME <label>`.

A handler may also decide not to resume; in that case it can `trap` again or
simply fall off the end (which behaves like re-raising the same trap).

## Mapping Tables

### Checked IL Instruction → Trap Kind

| Instruction | Condition | Trap Kind |
|-------------|-----------|-----------|
| `sdiv.chk0` | Divisor is `0` | `DivideByZero` |
| `sdiv.chk_ov` | Dividend `INT64_MIN` with divisor `-1` | `Overflow` |
| `srem.chk0` | Divisor is `0` | `DivideByZero` |
| `srem.chk_ov` | Dividend `INT64_MIN` with divisor `-1` | `Overflow` |
| `udiv.chk0` | Divisor is `0` | `DivideByZero` |
| `urem.chk0` | Divisor is `0` | `DivideByZero` |
| `cast.si_to_i64.chk` | Source outside `i64` range | `Overflow` |
| `cast.f64_to_i64.chk` | NaN or value outside `i64` | `InvalidCast` |
| `cast.ptr_to_i64.chk` | Pointer cannot be represented | `InvalidCast` |
| `idx.chk` | Index outside `[0, length)` | `Bounds` |
| `load.chk` | Null/misaligned pointer | `InvalidOperation` |
| `store.chk` | Null/misaligned pointer | `InvalidOperation` |
| `pow.chkdom` | Invalid exponent domain | `DomainError` |

### Runtime `Err` Code → Trap Kind

| Runtime `Err` | Trap Kind |
|---------------|-----------|
| `FileNotFound` | `FileNotFound` |
| `EOF` | `EOF` |
| `IOError` | `IOError` |
| `Overflow` | `Overflow` |
| `InvalidCast` | `InvalidCast` |
| `DomainError` | `DomainError` |
| `Bounds` | `Bounds` |
| `InvalidOperation` | `InvalidOperation` |
| `RuntimeError` (any other non-zero) | `RuntimeError` |

The C runtime reports success or failure and fills an `Err` out-parameter. VM
glue converts that code into the listed trap kind and raises `trap.err` with the
complete `Error` payload.

## BASIC ↔ IL Handler Mapping

The BASIC `ON ERROR` directive installs or clears a handler for the current
procedure. Each variant lowers to explicit `eh.push`/`eh.pop` and `resume.*`
operations.

### Installing a Handler

BASIC:

```basic
10 ON ERROR GOTO handler
20 ...
30 handler:
```

Lowered IL (excerpt):

```
entry:
  eh.push ^handler
  br ^body

body:
  ... instructions ...
  eh.pop
  ret

handler(%err: Error, %tok: ResumeTok):
  ...
```

### Clearing the Handler

BASIC `ON ERROR GOTO 0` lowers to a simple `eh.pop` in the current function to
remove the active handler.

### Resume Variants

| BASIC Statement | IL Equivalent |
|-----------------|---------------|
| `RESUME` | `resume.same %tok` |
| `RESUME NEXT` | `resume.next %tok` |
| `RESUME label` | `resume.label %tok, ^label` |

The `%tok` value is always the resume token received by the handler block. Hand
crafted IL must not forge resume tokens.

## Worked Examples

### Divide-by-zero

IL:

```
  %q = sdiv.chk0 %numerator, %denominator
```

- If `%denominator` is `0`, the VM raises `DivideByZero`.
- A handler can set `%denominator` to `1`, then `resume.same %tok` to re-run the
  division safely.

### Out-of-bounds index

IL:

```
  %item = idx.chk %array, %index
```

- If `%index` is outside `[0, len)`, a `Bounds` trap is raised.
- A BASIC program with `ON ERROR` can `RESUME label` that clamps the index.

### Opening a missing file

Runtime call from BASIC:

```
  %ok = call @rt_file_open(%handle, %path, %err_out)
  brnz %ok, ^continue, ^trap

trap:
  %err = load Error, %err_out
  trap.err %err
```

If the runtime stores `Err::FileNotFound` in `%err_out`, the VM glue maps it to
the `FileNotFound` trap kind and enters the handler. A BASIC handler may print a
message and `RESUME NEXT` to continue without the file.

## Unhandled Trap Diagnostics

If no `eh.push` is active when a trap occurs, the VM terminates with a deterministic
multi-field diagnostic:

```
Trap: <Kind>
Function: <function name>
IL: <function>#<block>#<instruction index>
Source line: <line number or -1>
```

For example, an unhandled divide-by-zero might report:

```
Trap: DivideByZero
Function: @main
IL: @main#L2#7
Source line: 42
```

The `Source line` field is `-1` when the IL instruction is not annotated with a
line number.
