---
status: draft
audience: internal
last-verified: 2025-09-24
---

# Error and Trap Semantics

This specification defines the shared trap model for checked IL instructions, the
virtual machine (VM), the C runtime bridge, and the BASIC surface language. It is
authoritative for how traps are raised, routed to handlers, resumed, and reported
when unhandled.

## Trap Kinds

The platform recognises the following trap kinds. They remain stable across front
ends, the IL, and the VM:

| Trap kind | Meaning |
|-----------|---------|
| `DivideByZero` | Integral division or modulus attempted with a zero divisor. |
| `Overflow` | Arithmetic or conversion exceeded the representable range. |
| `InvalidCast` | A checked cast rejected the source/target combination. |
| `DomainError` | Math argument outside the function domain (e.g., negative base to non-integer exponent). |
| `Bounds` | Array or string access outside the declared range. |
| `FileNotFound` | Runtime could not locate the requested file. |
| `EOF` | Runtime attempted to read past the end of a stream. |
| `IOError` | Runtime I/O failed for reasons other than `FileNotFound`/`EOF`. |
| `InvalidOperation` | Operation invoked in an unsupported state (e.g., resume token reused). |
| `RuntimeError` | Fallback for unexpected runtime failures. |

## Error Records and Resume Tokens

When a trap is raised, the VM constructs an `Error` record and pairs it with an
opaque `ResumeTok` before transferring control to a handler.

```
struct Error {
  i32 kind;   // Trap kind enumerator, see table above.
  i32 code;   // Runtime-provided error code (0 when not supplied).
  u64 ip;     // Instruction pointer within the IL function.
  i32 line;   // Source line number or -1 if unknown.
};
```

Handler blocks must accept two block parameters in this order: `%err: Error`
and `%tok: ResumeTok`. The resume token captures the suspended continuation and
may be consumed by exactly one `resume.*` instruction.

## Trap Propagation Flow

### Trap → Handler Transfer

```mermaid
flowchart TD
  start[Execute instruction] --> trap{Trap raised?}
  trap -- No --> next[Continue normally]
  trap -- Yes --> unwind[Unwind frames to nearest eh.push]
  unwind --> handler{Handler found?}
  handler -- No --> fatal[Terminate with unhandled trap diagnostic]
  handler -- Yes --> enter[Enter handler block with (%err, %tok)]
```

### `resume.same` (RESUME)

```mermaid
flowchart TD
  H[Handler receives (%err, %tok)] --> resume{Issue resume.same?}
  resume -- Yes --> reenter[Re-execute faulting instruction]
  reenter --> trapOutcome{Trap cleared?}
  trapOutcome -- Yes --> continue[Continue execution]
  trapOutcome -- No --> repeat[Repeat trap search (unwind, handler lookup)]
  resume -- No --> handlerFlow[Handler completes normally]
  handlerFlow --> retResume[Return to caller of handler block]
```

### `resume.next` (RESUME NEXT)

```mermaid
flowchart TD
  H[Handler receives (%err, %tok)] --> resume{Issue resume.next?}
  resume -- Yes --> advance[Resume after faulting instruction]
  advance --> continue[Continue execution in original block]
  resume -- No --> handlerFlow[Handler completes or re-raises]
```

### `resume.label` (RESUME <label>)

```mermaid
flowchart TD
  H[Handler receives (%err, %tok)] --> resume{Issue resume.label?}
  resume -- Yes --> branch[Branch to specified target block]
  branch --> continue[Continue execution at label with its parameters]
  resume -- No --> handlerFlow[Handler completes or re-raises]
```

## Checked Sources → Trap Kind Mapping

The tables below list every checked IL primitive and runtime error code together
with the trap kind the VM must raise. If a condition is not listed the runtime
must map it to `RuntimeError` to keep diagnostics deterministic.

### Checked IL Instructions

| Source | Trap kind | Notes |
|--------|-----------|-------|
| `sdiv.chk0` (divisor = 0) | `DivideByZero` | Applies to integer division `a \ b`. |
| `sdiv.chk0` (min-int / -1) | `Overflow` | Detects two's-complement overflow. |
| `srem.chk0` (divisor = 0) | `DivideByZero` | Applies to integer remainder `a MOD b`. |
| `idx.chk` | `Bounds` | Any out-of-range array or string index. |
| `load.chk` / `store.chk` | `Bounds` | Checked memory operations share the same bounds trap. |
| `cast.int_to_smaller.chk` | `Overflow` | Narrowing integer cast that discards significant bits. |
| `cast.float_to_int.chk` | `Overflow` | Result does not fit in destination integer range. |
| `cast.ptr_to_int.chk` | `InvalidCast` | Disallowed pointer conversion. |
| `cast.int_to_enum.chk` | `InvalidCast` | Integer value not representable in target enum. |
| `trap.kind <Kind>` | `<Kind>` | Immediate trap with declared kind. |
| `trap.err` | `err.kind` | Uses the kind embedded in the `Error` operand. |

### Runtime Error Codes

| Runtime `Err` | Trap kind | Notes |
|---------------|-----------|-------|
| `Err::FileNotFound` | `FileNotFound` | File could not be opened. |
| `Err::EOF` | `EOF` | End-of-file reached before satisfying request. |
| `Err::IOError` | `IOError` | Any other I/O failure (permissions, device errors). |
| `Err::Overflow` | `Overflow` | Runtime arithmetic helpers overflowed. |
| `Err::InvalidCast` | `InvalidCast` | Runtime rejected conversion. |
| `Err::DomainError` | `DomainError` | Runtime math domain violation. |
| `Err::Bounds` | `Bounds` | Runtime container access out of range. |
| `Err::InvalidOperation` | `InvalidOperation` | API misuse such as double-resume. |
| `Err::RuntimeError` | `RuntimeError` | Catch-all for unexpected failures. |
| `Err::None` | — | Indicates success; no trap is raised. |

## BASIC ↔ IL Handler Mapping

The BASIC surface installs and clears handlers using `ON ERROR` directives. The
front end must lower these statements to the IL `eh.push` and `eh.pop` primitives
within the current procedure scope.

### Installing a Handler (`ON ERROR GOTO label`)

**BASIC**

```basic
ON ERROR GOTO Handler
Y = 0
X = 10 \ Y
Handler:
PRINT "Recovered"
RESUME NEXT
```

**Lowered IL**

```il
func @main() -> i64 {
entry:
  eh.push ^handler
  # previous lowering stored 0 into %Y.addr
  %Y = load i64, %Y.addr
  %tmp = sdiv.chk0 10, %Y
  eh.pop
  ret 0
handler(%err: Error, %tok: ResumeTok):
  call @rt_print_str("Recovered")
  resume.next %tok
}
```

### Clearing a Handler (`ON ERROR GOTO 0`)

```basic
ON ERROR GOTO 0
```

```il
  eh.pop
```

### `RESUME`

```basic
RESUME
```

```il
  resume.same %tok
```

### `RESUME NEXT`

```basic
RESUME NEXT
```

```il
  resume.next %tok
```

### `RESUME label`

```basic
RESUME HandlerExit
```

```il
  resume.label %tok, ^HandlerExit
```

## Worked Trap Examples

### Divide-by-zero in BASIC

```basic
ON ERROR GOTO Handler
X = 4 \ 0
PRINT "unreachable"
Handler:
PRINT ERR, ERL
RESUME NEXT
```

* IL emits `sdiv.chk0 4, 0`, which traps with kind `DivideByZero`.
* The handler receives `%err.kind = DivideByZero`, `%err.line` holding the BASIC
  line number, and resumes after the faulting assignment.

### Array Bounds Violation (`idx.chk`)

```basic
DIM A(9)
ON ERROR GOTO Bounds
PRINT A(10)
Bounds:
PRINT "Index", ERR
RESUME NEXT
```

* IL lowers the read to `idx.chk %arr, 10` and `load` from the checked pointer.
* When the index is outside `0…9`, the VM raises `Bounds`. The handler prints the
  diagnostic then continues with the next statement.

### Opening a Missing File

```basic
ON ERROR GOTO Missing
OPEN "missing.txt" FOR INPUT AS #1
PRINT "opened"
Missing:
PRINT ERR
RESUME NextStep
NextStep:
PRINT "done"
```

* The runtime returns `Err::FileNotFound`. The VM maps it to trap kind
  `FileNotFound` and raises via `trap.err`.
* The handler prints the BASIC error code (implementation-defined) and resumes at
  the labelled block `NextStep`.

## Unhandled Trap Diagnostics

If a trap propagates without a matching handler, the VM must terminate execution
with a stable diagnostic comprising these fields:

| Field | Description |
|-------|-------------|
| `Kind` | Trap kind name (`DivideByZero`, `Bounds`, …). |
| `Function` | Fully qualified IL function symbol where the trap occurred. |
| `IL Location` | Instruction pointer (`ip`) or textual offset within the IL module. |
| `Source Line` | Original source line when available; `unknown` when `line = -1`. |

The diagnostic is printed exactly in the order above, one field per line, to
ensure deterministic tooling output.

## Determinism Guarantees

* Checked IL instructions and runtime helpers must emit the trap kind listed in
  the mapping tables.
* Runtime error codes must map 1:1 to trap kinds; introducing a new code requires
  updating this specification.
* Resume tokens may be consumed at most once. Issuing a second resume with the
  same token must raise `InvalidOperation`.
* Handlers that fall through without resuming propagate the original trap after
  unwinding the `eh.push` frame.

