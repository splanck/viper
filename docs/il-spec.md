# IL Specification

---
content merged from ilspecs.md
---

IL v0.1 — Specification
Status: Draft (feature‑complete for a small but usable language)
Scope: Suitable for BASIC v1 front end, an IL interpreter, and an IL→x86‑64 backend.

1) Design Goals and Non‑Goals
Goals
	• Thin waist: Multiple source languages → IL; two execution engines consume IL (VM and native codegen).
	• Small but expressive: Enough to implement a BASIC‑like language (scalars, strings, control flow, calls, simple memory).
	• Deterministic & portable semantics: Interpreter and compiled code must agree.
	• Toolable: Text format is human‑readable and stable; a verifier can enforce well‑formedness.
	• Solo‑friendly: Avoid complex analyses (no mandatory SSA φ). Prefer simple, explicit constructs.
Non‑Goals (v0.1)
	• No exceptions or unwinding.
	• No first‑class closures.
	• No garbage collector in the IL (strings/heap go through the runtime).
	• No varargs; no vector types; no concurrency.
Rationale: Keep the core small; let the runtime provide higher‑level features.

2) Core Concepts
An IL module contains:
	• Target metadata (optional)
	• External declarations (runtime and cross-module symbols)
	• Global definitions (mutable or constant)
	• Function definitions (bodies formed by basic blocks)
	• String literals (interned, read‑only)
Execution starts at a function (e.g., @main).
All control flow is explicit: basic blocks end with exactly one terminator (br, cbr, or ret). There is no fallthrough.
Values in IL are:
	• Temporaries (named with %…) produced by instructions (single assignment per producing instruction)
	• Constants (i64, f64, i1, null, strlit)
	• Addresses (ptr) obtained via alloca, gep, globals, or const_null
	SSA policy: IL does not require global SSA with φ. Use stack slots (alloca) and loads/stores to carry values across merges. Temporaries (%tN) are single‑assignment per instruction but not globally SSA.

3) Types
Primitive and opaque types:
Type	Meaning	Notes
void	no value	return type only
i1	boolean (0/1)	produced by comparisons and logical ops
i64	64‑bit signed integer	two’s complement; arithmetic wraps
f64	64‑bit IEEE 754 float	NaN/Inf allowed
ptr	untyped pointer	size matches target word size
str	opaque string handle	managed by runtime
Rationale: One integer width (i64) and one float (f64) keep front ends simple; str defers string complexity to the runtime.

4) Constants & Literals
	• Integer: 0, -1, 42 (base 10 only in v0.1).
	• Float: 0.0, 1.5, -2.0, NaN, Inf, -Inf.
	• Bool: true, false (sugar for i1 1/0).
	• Null pointer: null (of type ptr).
	• String literal: "HELLO\n" (UTF‑8; escapes \n \t \\ \" \xNN).
Typed constants use context; explicit casts are required when ambiguous.

5) Symbols, Linkage, Visibility
	• Symbol names: functions and globals start with @ (e.g., @main, @rt_print_i64).
	• Temporaries: start with % (e.g., %t1).
	• Linkage:
		○ extern — declared but not defined here.
		○ global — defined here; optionally const for read‑only.
	• Visibility:
		○ public (default) — exported from module.
		○ internal — not exported (backend may give it local/hidden visibility).

6) Module Structure (Text Format)

il 0.1
target "generic"            ; optional, e.g., "x86_64-sysv"
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
global const str @.L0 = "HELLO"
func @main() -> i32 {
entry:
  %s = const_str @.L0
  call @rt_print_str(%s)
  ret 0
}
Comments: ; to end of line.
Whitespace: insignificant except inside strings.

7) Functions & Basic Blocks

func @name(param_list?) -> ret_type [attributes]? {
label0:
  ...
  terminator
label1:
  ...
  terminator
}
	• Parameters: name: type pairs, comma separated.
	• Attributes (optional, v0.1 understood by verifier/tools): noreturn, pure, readonly. (Advisory; backends may ignore in v0.1.)
	• Blocks: label: followed by non‑empty sequence of non‑terminator instructions and exactly one terminator.

8) Instruction Set (v0.1)
Notation:
	• dst = op ... means instruction yields a value.
	• Operands must match the operand type constraints below.
	• On a trap condition, execution halts with a runtime error (diagnostic encouraged) in both VM and native execution.
8.1 Arithmetic (integers)
	• %d = add %a:i64, %b:i64 — two’s complement wrap.
	• %d = sub %a:i64, %b:i64
	• %d = mul %a:i64, %b:i64
	• %d = sdiv %a:i64, %b:i64 — trap if %b == 0 or %a == INT64_MIN && %b == -1 (overflow).
	• %d = udiv %a:i64, %b:i64 — treats operands as unsigned; trap if %b == 0.
	• %d = srem %a:i64, %b:i64 — trap if %b == 0.
	• %d = urem %a:i64, %b:i64 — trap if %b == 0.
Bitwise & shifts
	• %d = and %a:i64, %b:i64
	• %d = or %a:i64, %b:i64
	• %d = xor %a:i64, %b:i64
	• %d = shl %a:i64, %b:i64 — shift count mod 64.
	• %d = lshr %a:i64, %b:i64 — logical right shift; count mod 64.
	• %d = ashr %a:i64, %b:i64 — arithmetic right shift; count mod 64.
Rationale: Explicit signed/unsigned division/rem ensures well‑defined behavior.
8.2 Arithmetic (floats, IEEE‑754)
	• %d = fadd %a:f64, %b:f64
	• %d = fsub %a:f64, %b:f64
	• %d = fmul %a:f64, %b:f64
	• %d = fdiv %a:f64, %b:f64 — follows IEEE (divide by zero yields ±Inf; NaNs propagate).
8.3 Comparisons
	• Integer, signed: %b = scmp_lt|le|gt|ge %a:i64, %c:i64 -> i1
	• Integer, unsigned: %b = ucmp_lt|le|gt|ge %a:i64, %c:i64 -> i1
	• Integer equality: %b = icmp_eq|ne %a:i64, %c:i64 -> i1
	• Float: %b = fcmp_lt|le|gt|ge|eq|ne %a:f64, %c:f64 -> i1 (IEEE total order not assumed; eq is false if either is NaN except ne which is true if NaN involved)
8.4 Conversions
	• %d = sitofp %a:i64 -> f64
	• %d = fptosi %a:f64 -> i64 (trap on overflow/NaN)
	• %d = zext1 %a:i1 -> i64 (0/1 to 0/1 in i64)
	• %d = trunc1 %a:i64 -> i1 (0 → 0, non‑zero → 1)
8.5 Memory & Pointers
	• %p = alloca size:i64 → ptr
Allocates size bytes in the current function’s stack frame. Lifetime: the whole call (no dynamic free).
	• %q = gep %base:ptr, %offset:i64 → ptr
Pointer arithmetic: q = base + offset. No bounds checks.
	• %v = load ty, %p:ptr → ty
Reads ty from address %p. Trap if %p is null or misaligned (see §10).
	• store ty, %p:ptr, %v:ty
Writes v to %p. Trap if %p is null or misaligned.
	• %p = addr_of @global → ptr
Address of a global.
	• %p = const_null → ptr
Equivalent to null.
Rationale: Untyped ptr keeps IL simpler; the load ty encodes the type of access.
8.6 Calls & Returns
	• %r = call @func(arg1, …)
Callee must be declared; arguments must match signature. Return type may be void (in which case the destination is omitted: call @func(...)).
	• ret (when function return type is void)
	• ret %v (when function returns a value)
Intra‑module calls are direct. Cross‑module/runtime calls are resolved by the loader or OS linker in native builds.
8.7 Control Flow
	• br label %dst
Unconditional branch.
	• cbr %cond:i1, label %then, label %else
Conditional branch on %cond != 0.
	• Terminator rules: Each basic block must end with exactly one terminator and contain none elsewhere.
8.8 Constants / String
	• %s = const_str @.Lk → str
Yields a handle to a module‑internal, immutable string literal.
	• %b = strlen %s:str -> i64 (optional intrinsic)
Rationale: This can be a runtime call instead; including an intrinsic is optional. In v0.1 prefer runtime: call @rt_len(%s).
8.9 Diagnostics / Trap
	• trap — unconditionally triggers a runtime error and aborts current function (and program by default).
Use sparingly; front ends should prefer generating valid IL.

9) Runtime ABI (Minimum Surface)
IL treats the runtime as a set of extern functions with C ABI. Initial set:
Console I/O
	• extern @rt_print_str(str) -> void
	• extern @rt_print_i64(i64) -> void
	• extern @rt_print_f64(f64) -> void
	• extern @rt_input_line() -> str
Strings
	• extern @rt_len(str) -> i64
	• extern @rt_concat(str, str) -> str
	• extern @rt_substr(str, i64, i64) -> str
	• extern @rt_to_int(str) -> i64 (trap on invalid)
	• extern @rt_to_float(str) -> f64 (trap on invalid)
	• extern @rt_str_eq(str, str) -> i1
Memory (optional for v0.1)
	• extern @rt_alloc(i64) -> ptr
	• extern @rt_free(ptr) -> void
Rationale: Keep IL small; move complex behaviors (I/O, text) to runtime. The VM and native backends call the same ABI.

10) Memory Model, Alignment, Endianness
	• Addressing: ptr represents a byte address in the process address space (VM and native).
	• Alignment requirement: All load/store must be naturally aligned to the size of ty (8 bytes for i64/f64, 1 for byte loads if added later). Misaligned access traps.
	• Endianness: Module text is endian‑agnostic; backends handle target endianness. VM uses the host’s endianness internally but exposes no byte reinterpretation operations in v0.1, avoiding observable differences.
	• Stack allocations: alloca memory is zero‑initialized in v0.1 for determinism.
	• Null dereference: load/store on null trap.
Rationale: Stricter rules simplify the interpreter and verifier; native backends can rely on them.

11) Verifier Rules (Well‑Formedness)
A module MUST satisfy:
Module
	• All referenced symbols (functions/globals) are declared or defined exactly once.
	• String labels @.Lk are defined exactly once; read‑only.
Functions
	• Parameter and return types are valid IL types (no void params).
	• Every function has at least one basic block; the entry block is the first.
	• Control‑flow graph is closed: all labels used are defined in the same function.
Blocks
	• Exactly one terminator at the end; none before the last instruction.
	• All temporaries used are defined in dominator order within the function or are constants/globals.
	• No fallthrough between blocks; only br, cbr, or ret.
Typing & Ops
	• Operand types match instruction requirements.
	• call argument arity and types match the callee signature; destination must match return type (or be omitted if void).
	• load ty, %p / store ty, %p, %v types are consistent.
Memory
	• All alloca sizes are non‑negative; excessively large sizes may be implementation‑defined error (VM may trap with “stack overflow”).
The verifier should produce clear diagnostics with source location metadata when available.

12) Textual IL Grammar (EBNF, v0.1)

module      ::= "il" version (target_decl)? (decl_or_def)*
version     ::= number "." number
target_decl ::= "target" string
decl_or_def ::= extern_decl | global_decl | func_def
extern_decl ::= "extern" symbol "(" param_list? ")" "->" type
global_decl ::= "global" ("const")? type symbol "=" global_init
global_init ::= int_lit | float_lit | "null" | string_lit | symbol
func_def    ::= "func" symbol "(" params? ")" "->" type attr_list? "{" block+ "}"
params      ::= param ("," param)*
param       ::= ident ":" type
param_list  ::= type ("," type)*
attr_list   ::= /* optional attributes list, space separated */
block       ::= label ":" instr* term
label       ::= ident
instr       ::= named_instr | bare_instr
named_instr ::= temp "=" op
bare_instr  ::= term | "store" type "," value "," value | "call" symbol "(" args? ")"
term        ::= "ret" | "ret" value
             | "br" "label" value
             | "cbr" value "," "label" value "," "label" value
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
             | "scmp_lt" value "," value | "scmp_le" value "," value
             | "scmp_gt" value "," value | "scmp_ge" value "," value
             | "ucmp_lt" value "," value | "ucmp_le" value "," value
             | "ucmp_gt" value "," value | "ucmp_ge" value "," value
             | "fcmp_lt" value "," value | "fcmp_le" value "," value
             | "fcmp_gt" value "," value | "fcmp_ge" value "," value
             | "fcmp_eq" value "," value | "fcmp_ne" value "," value
             | "sitofp" value
             | "fptosi" value
             | "zext1"  value
             | "trunc1" value
             | "alloca" value
             | "gep" value "," value
             | "load" type "," value
             | "addr_of" symbol
             | "const_str" symbol
             | "const_null"
             | "call" symbol "(" args? ")"
             | "trap"
args        ::= value ("," value)*
value       ::= temp | symbol | literal
temp        ::= "%" ident
symbol      ::= "@" ident
type        ::= "void" | "i1" | "i64" | "f64" | "ptr" | "str"
ident       ::= [A-Za-z_][A-Za-z0-9_]*   ; not reserved words
string      ::= '"' ... '"' with escapes
Notes: For simplicity, the grammar shows some redundancy between instr and term; your parser can unify this.

13) Examples
13.1 Hello World

il 0.1
extern @rt_print_str(str) -> void
global const str @.L0 = "HELLO, WORLD\n"
func @main() -> i32 {
entry:
  %s = const_str @.L0
  call @rt_print_str(%s)
  ret 0
}
13.2 If / Else

extern @rt_print_i64(i64) -> void
func @main() -> i32 {
entry:
  %x = add 2, 3         ; 5
  %p = scmp_gt %x, 4    ; i1
  cbr %p, label then, label else
then:
  call @rt_print_i64(%x)
  br label exit
else:
  call @rt_print_i64(4)
  br label exit
exit:
  ret 0
}
13.3 While Loop (desugared)

func @main() -> i32 {
entry:
  %sum_slot = alloca 8
  store i64, %sum_slot, 0
  %i_slot = alloca 8
  store i64, %i_slot, 0
  br label loop_head
loop_head:
  %i = load i64, %i_slot
  %c = scmp_lt %i, 10
  cbr %c, label loop_body, label done
loop_body:
  %sum = load i64, %sum_slot
  %sum2 = add %sum, %i
  store i64, %sum_slot, %sum2
  %i2 = add %i, 1
  store i64, %i_slot, %i2
  br label loop_head
done:
  %sum3 = load i64, %sum_slot
  ret %sum3
}

14) Lowering Guidance for Front Ends
	• Variables: Lower source variables to alloca + load/store. This avoids needing φ at block merges.
	• Short‑circuit boolean: Lower A && B to control flow (branch to compute B only if A is true).
	• Strings: Use const_str for literals; call runtime for operations (len, concat, compare).
	• Built‑ins: Prefer runtime calls (e.g., BASIC PRINT 42 → call @rt_print_i64(42)).
	• Type coercions: Be explicit (e.g., i64→f64 uses sitofp).

15) Interpreter (VM) Contract
	• Dispatch: Instruction‑by‑instruction, deterministic.
	• Stack frames: alloca storage per function; zero‑initialized.
	• Errors: On trap conditions (div zero, misaligned/NULL deref, bad cast), VM halts with a message including current function, block label, and instruction index.
	• Tracing: Optional --trace mode that prints executed instructions with temporary values.
Rationale: VM serves as the semantic oracle for codegen validation.

16) Backend (Native) Contract
	• Target ABI: Start with x86‑64 SysV.
	• Registers: Use linear‑scan over virtual registers mapped from IL temporaries; spill to stack slots as needed.
	• Calling convention: Map IL arguments to SysV registers/stack; map str/ptr as pointers; i1 passed as 8‑bit or zero‑extended to 32/64 bits per ABI (pick zero‑extend to 32 in v0.1).
	• Prologue/Epilogue: Maintain 16‑byte stack alignment at call sites.
	• Runtime calls: Link against librt.a; treat as external.
Determinism: Native execution must match VM results for all defined programs.

17) Error Model
	• Static errors (rejected by verifier): type mismatches, wrong arity, malformed CFG, undefined labels/symbols.
	• Dynamic errors (trap at runtime): division by zero, invalid fptosi, null/misaligned memory access, alloca overflow (implementation‑defined threshold).
Diagnostics should include function/block name, instruction text, and (if provided) source file:line:col metadata attached to IL.

18) Metadata (Optional, Non‑Semantic)
Attach metadata to instructions and functions; ignored by execution but used by tools:

!loc(file="prog.bas", line=12, col=5)
%t0 = add 2, 3 !loc(...)
Keys are implementation‑defined; the verifier simply allows them.

19) Extensibility & Versioning
	• File header il 0.1 is required.
	• New opcodes/types must not change semantics of existing ones.
	• Reserve opcode names: copy, select, phi for potential future extensions.
	• Unknown attributes are ignored by v0.1 tools but preserved when possible.

20) Conformance & Testing
A front end, VM, and backend are v0.1‑conformant if:
	1. They accept the grammar and verifier rules.
	2. VM and native outputs match on the provided conformance suite:
		○ arithmetic (edge cases: INT64_MIN/-1), comparisons, branches
		○ loops with alloca‑backed variables
		○ function calls (including nested)
		○ strings: literals, concat, len, equality via runtime
		○ traps (each must be triggered as specified)

21) Frequently Asked Design Questions
Why no φ?
It keeps the pipeline simpler for a solo developer. When values must flow across merges, use alloca + load/store. You can always add an optional SSA pass later.
Why only i64 and f64?
Most languages can coerce to these; it dramatically reduces instruction surface and verifier complexity. Smaller widths can be emulated with masking if needed.
Why strict alignment and traps?
Undefined or host‑dependent behavior would make VM vs native disagree. Traps keep semantics crisp.
How do I do arrays/structs?
Use alloca + rt_alloc for heap blocks and gep offsets. Provide front‑end‑level layout rules and runtime helpers; IL stays untyped at pointer level.

22) Mapping BASIC v1 → IL (Cheat Sheet)
BASIC	IL pattern
PRINT "X"	%s = const_str @.L; call @rt_print_str(%s)
PRINT X	%v = load i64, %slotX; call @rt_print_i64(%v)
LET X = A + B	%a = load i64, %slotA; %b = load i64, %slotB; %c = add %a, %b; store i64, %slotX, %c
IF C THEN S1 ELSE S2	%p = (…cmp…); cbr %p, label then, label else
WHILE C … WEND	Desugar to loop_head/loop_body/done with slot loads/stores

23) Minimal Opcode Reference (Quick Table)
Opcode	Signature	Semantics (trap conditions)
add/sub/mul	i64 × i64 → i64	wrap on overflow
sdiv/udiv	i64 × i64 → i64	div by 0; INT64_MIN/-1 for sdiv
srem/urem	i64 × i64 → i64	div by 0
and/or/xor	i64 × i64 → i64	bitwise
shl/lshr/ashr	i64 × i64 → i64	shift count mod 64
fadd/fsub/fmul/fdiv	f64 × f64 → f64	IEEE
icmp_eq/ne	i64 × i64 → i1	
scmp_* / ucmp_*	i64 × i64 → i1	signed/unsigned
fcmp_*	f64 × f64 → i1	IEEE NaN rules
sitofp/fptosi	see text	fptosi traps on NaN/overflow
zext1/trunc1	i1↔i64	trunc1: nonzero → 1
alloca	i64 → ptr	size ≥ 0; traps on huge
gep	ptr × i64 → ptr	ptr arithmetic
load/store	typed	traps on null/misaligned
addr_of	@global → ptr	
const_str	@.Lk → str	
const_null	→ ptr	
call	(...)->T	callee must exist
br/cbr/ret	terminators	
trap	—	unconditional trap

