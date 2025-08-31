# Project Overview

Here’s a concrete, end-to-end design you can use as a blueprint. It assumes a two-layer
architecture with a “thin waist” Intermediate Language (IL) that everything revolves
around. You’ll start with a BASIC front end, run IL via an interpreter, and add an
IL→assembly backend as a separate module.

0. Project Goals

   1. Multi-language front ends (start with BASIC; later add others) that all lower to a common IL.
   1. Backend interpreter that executes IL directly (great for fast bring‑up, tests, and debugging).
   1. Backend compiler that translates IL to assembly (x86‑64 SysV first), assembled and linked into native binaries.
   1. Small, solo‑friendly codebase with clear module boundaries, strong tests, and an extensible runtime ABI.

1. High-Level Architecture

   ```
    +-----------------------+      +-----------------------+
    |   Frontends (N)       |      |   Tools               |
    |  - BASIC (v1)         |      |  - CLI (driver)       |
    |  - Future: Tiny C,    |      |  - IL verifier        |
    |    Pascal, …          |      |  - Disassembler       |
    +-----------+-----------+      +-----------+-----------+
                |                              |
                v                              |
    +-----------------------+                  |
    |   IL Builder          |                  |
    |  (in-memory IR)       |                  |
    +-----------+-----------+                  |
                |                              |
       +--------v---------+            +-------v----------+
       |   IL Optimizer   |   (opt)    |   IL Serializer  |
       |  (optional)      | ---------->|  .il text/bc     |
       +--------+---------+            +-------+----------+
                |                              |
                +------------------------------+  
                |
   ```

   +--------------+--------------------+
   | |
   v v
   +------------+ +--------------------+
   | IL VM | | Codegen Backend |
   | (Interpreter) | IL -> Assembly |
   | run .il in-process | emit .s , assemble |
   +------------+ +--------------------+
   | |
   v v
   Program Native Binary
   Thin waist: All languages produce the same IL; both the VM and codegen consume the same IL.

1. Components & Responsibilities
   2.1 Front Ends (start with BASIC)
   • Purpose: Translate source to IL while enforcing language rules (syntax + semantics).
   • Subcomponents:
   ○ Lexer: tokens (identifiers, numbers, strings, keywords).
   ○ Parser: builds an AST for a subset of BASIC (e.g., LET, PRINT, IF/THEN/ELSE, WHILE/WEND, GOTO, GOSUB/RETURN, function calls if present).
   ○ Semantic Analysis: symbol tables (variables, functions), simple type handling (integer, float, string), constant folding for literals.
   ○ Desugaring: normalize constructs (e.g., ELSEIF → nested IF).
   ○ Lowering to IL: walk the AST and emit IL instructions via an IR Builder.
   • Output: an in-memory IL Module (functions, global variables, string constants).
   Why this separation: It keeps language-specific work isolated. To add a new language later, you reuse IR Builder and runtime ABI.

2.2 Intermediate Language (IL)
• Purpose: A simple, SSA-optional, three-address style IR that’s easy to interpret and easy to lower to assembly.
• Form: In-memory graph of Functions → BasicBlocks → Instructions; optionally serializable to a human-readable text format (.il) and to a compact bytecode (.ilbc) later.
• Types (minimal v1):
○ i1 (bool), i32, i64, f64, ptr, str (handle to runtime-managed string).
• Values:
○ Virtual registers (temporaries), constants, globals, function symbols.
• Instructions (v1 set, intentionally small):
○ Arithmetic: add, sub, mul, div, mod (integers), fadd, fsub, fmul, fdiv (floats).
○ Bitwise: and, or, xor, shl, shr.
○ Compare: cmp_eq, cmp_ne, cmp_lt, cmp_le, cmp_gt, cmp_ge (int/float variants).
○ Control: br label, cbr cond, tlabel, flabel, ret [val].
○ Memory: load ptr, store ptr, val, alloca size (stack slot), gep base, offset (simple pointer arithmetic).
○ Call: call func_symbol, arg1, ... (conforms to runtime ABI).
○ Const: const_i64, const_f64, const_str, const_ptr.
○ Cast (minimal): sitofp, fptosi, zext/trunc as needed.
• Metadata:
○ Source locations (file:line:col) attached to instructions for diagnostics and debugging.
○ Function attributes (e.g., noreturn, pure) later.
• Control Flow: Basic blocks end with exactly one terminator (br, cbr, or ret).
• Calling convention (IL-level):
○ Simple: by-value scalars; pointers for strings/arrays; return via register or implicit out‑param if larger than a machine word.
SSA? Start without SSA to keep it simple. You can add an SSA pass later (with Φ nodes) if you want classic dataflow optimizations.

2.3 Runtime Library & ABI
• Purpose: Provide I/O and utilities as normal functions callable from IL (and later, from compiled code), so front ends don’t hardcode behavior.
• Initial surface (prefixed rt\_):
○ Console: rt_print_str(str), rt_print_i64(i64), rt_print_f64(f64), rt_input_line() -> str.
○ Strings: rt_len(str)->i64, rt_concat(str,str)->str, rt_substr(str,i64,i64)->str, rt_to_int(str)->i64, rt_to_float(str)->f64.
○ Math (optional): rt_sin(f64), rt_cos, rt_pow, etc., often thin wrappers on libc.
○ Memory: rt_alloc(size)->ptr, rt_free(ptr). Strings use internal ref-count or arena.
• Implementation: C for portability; expose a stable C ABI.
• Memory/Strings: Start with ref-counted strings (simpler than GC). Later you can swap to a small, precise GC without changing IL.
The compiler lowers BASIC built-ins like PRINT → call rt_print\_\*. The interpreter and native code both call the same functions.

2.4 IL VM (Interpreter)
• Purpose: Execute IL quickly for development, testing, and “script-like” usage.
• Execution model: Register-based interpreter over decoded instructions (no need for a separate bytecode in v1; interpret the in-memory IR).
• State:
○ Frame: local virtual-register array, stack slots (for alloca), a call stack of frames.
○ Heap: managed by runtime (for strings/arrays).
○ Globals: addresses bound at module load time.
• Main loop (sketch):

for (;;) {
switch (instr.opcode) {
case OP_ADD: regs[d] = regs[a] + regs[b]; ip++; break;
case OP_CBR: ip = regs[cond] ? then_bb->first : else_bb->first; break;
case OP_CALL: regs[dst] = call_runtime_or_fn(fsym, args...); ip++; break;
case OP_RET: return regs[retv];
// ...
}
}
• Diagnostics:
○ On runtime error (e.g., wrong type to rt_len), report with source location metadata from the IL and a short backtrace of IL call frames.
• Performance: Plenty for early stages. If you need more speed, add:
○ A compact bytecode encoder (.ilbc) to optimize dispatch.
○ Direct-threaded dispatch (computed gotos) in C/C++.
○ Simple hot-path inliner in the VM.

2.5 Codegen Backend (IL → Assembly)
• Purpose: Produce native binaries from IL.
• Targets: Start with x86‑64 SysV (Linux/macOS); later add Windows x64 and ARM64.
• Pipeline:
1\. Instruction Selection: Greedy mapping of IL ops to asm patterns (no need for DAG selector at first).
2\. Register Allocation: Start with linear-scan over virtual registers; spill to the stack as needed.
3\. Prologue/Epilogue: Set up call frame, preserve callee-saved registers, align stack.
4\. Calling Convention: Map IL calls to native ABI (SysV: integer args in rdi, rsi, rdx, rcx, r8, r9; float args in xmm0..).
5\. Emitting Assembly: Generate .s (AT&T or Intel syntax); invoke the system assembler and linker.
• Files:
○ module.s → assemble → module.o → link with librt.a → a.out.
• Debug Info (optional in v1):
○ Emit comments with source line info, later expand to DWARF if desired.

2.6 Tools / Driver
• CLI (ilc as example):
○ ilc front basic file.bas -emit-il (print IL)
○ ilc front basic file.bas -run (front → IL → run on VM)
○ ilc front basic file.bas -S (front → IL → asm)
○ ilc front basic file.bas -o a.out (front → IL → asm → link)
○ ilc il-opt file.il -o file.opt.il (optional)
○ ilc il-verify file.il (structure/type checks)
• Verifier: Ensures each block ends with a terminator, types of operands match, calls match signatures, etc.
• Disassembler: Pretty-print IL from binary form (if you add .ilbc).

3. Implementation Language & Structure
   • Language choice:
   ○ C++20 is a sweet spot for ASTs, IR graph, and RAII management (smart pointers, std::vector, std::unordered_map).
   ○ Keep the runtime in C with a stable ABI.
   • Repo layout:

/runtime/ (C) rt\_*.c, rt\_*.h, build to librt.a
/il/ (C++) IL core: types, module, builder, verifier, serializer
/vm/ (C++) IL interpreter
/codegen/ (C++) x86_64 backend, regalloc, asm emitter
/frontends/
/basic/ (C++) lexer, parser, AST, lowering
/common/ (C++) shared front-end utils
/tools/ (C++) CLI driver, disassembler
/tests/ (mixed) unit + golden + e2e

4. BASIC v1: Source → IL Example
   BASIC (subset):

10 PRINT "HELLO"
20 LET X = 2 + 3
30 IF X > 4 THEN PRINT X ELSE PRINT 4
40 END
Lowered IL (textual sketch):

module {
global str @.L0 = "HELLO"
func @main() -> i32 {
entry:
%t0 = const_str @.L0
call @rt_print_str(%t0)
%t1 = const_i64 2
%t2 = const_i64 3
%t3 = add %t1, %t2
store &X, %t3
%t4 = load &X
%t5 = cmp_gt %t4, const_i64 4
cbr %t5, then, else
then:
call @rt_print_i64(%t4)
br exit
else:
call @rt_print_i64(const_i64 4)
br exit
exit:
ret const_i32 0
}
}
Note: &X can be modeled as a function‑local stack slot created via alloca in entry.

5. Testing Strategy (solo‑friendly)

   1. Golden tests for front ends: source → expected IL text.
   1. VM e2e tests: run IL on interpreter; assert stdout/return code.
   1. Backend e2e tests: same programs compiled to native; compare output to VM.
   1. Differential testing: VM result vs. native result for each sample.
   1. IL verifier unit tests: deliberately malformed IR to ensure verifier catches issues.
   1. Fuzz lite: fuzz front-end lexer/parser (strings, numbers, nesting) with small seeds.

1. Extensibility: Adding New Languages
   • Contract: New front end just needs to produce valid IL and adhere to the runtime ABI for I/O and strings.
   • BASIC specifics:
   ○ Keywords map to simple control flow and runtime calls.
   ○ Dynamic-ish typing can be handled by front-end coercions to a small IL type set (i64, f64, str) plus runtime helpers.
   Later front ends (Tiny C / Pascal):
   • Reuse symbol tables and type checker utilities where possible.
   • Keep desugaring consistent (loops → blocks and branches).
   • Avoid pushing complexity into IL: keep IL small and orthogonal.

1. IL Details Worth Nailing Early
   • Integer width: choose i64 as the canonical integer; i32 only if you need it for external ABIs.
   • Floats: f64 only at first.
   • Strings: opaque str handle; only manipulated via runtime calls.
   • Booleans: i1 for branch conditions.
   • Undefined behavior: define it narrowly—e.g., division by zero triggers a runtime error with a diagnostic.
   • Verifier rules (non-exhaustive):
   ○ All operands dominate their uses.
   ○ Every block has exactly one terminator.
   ○ Types of operands match opcode expectations.
   ○ Calls match callee signatures (arity + types).
   ○ No fallthrough between blocks without a br.

1. Interpreter vs Codegen: Division of Labor
   • Interpreter is your oracle for semantics and the fastest path to a working system.
   • Backend focuses purely on performance and binary output, not language behavior.
   • When a test fails in native, compare to VM output to isolate codegen bugs.

1. Minimal Code Sketches
   IR Builder (C++-ish)

Value v1 = b.const_i64(2);
Value v2 = b.const_i64(3);
Value sum = b.add(v1, v2);
b.call(sym("rt_print_i64"), {sum});
b.ret(b.const_i32(0));
Interpreter dispatch (C-like)

for (;;) {
Instr \*i = ip++;
switch (i->op) {
case OP_ADD: regs[i->dst] = regs[i->a].i64 + regs[i->b].i64; break;
case OP_CBR: ip = regs[i->cond].i1 ? i->tgt : i->ftgt; break;
case OP_CALL: regs[i->dst] = call_host(i->callee, regs, i->argc); break;
case OP_RET: return regs[i->retv];
// ...
}
}
Assembly emission (x86‑64 SysV, sketch)

# prologue

push %rbp
mov %rsp, %rbp
sub $32, %rsp # spill area

# ... instructions mapped from IL ...

# epilogue

mov %rbp, %rsp
pop %rbp
ret

10. Diagnostics & Developer UX
    • Source mapping: carry file/line/col through AST → IL → VM/native for clear errors.
    • Pretty errors: show source snippet with a caret.
    • Tracing (optional): VM flag --trace-il to print each executed IL instruction with values.
    • \*\* REPL (nice-to-have)\*\*: ilc repl runs a BASIC prompt backed by the VM.

01. Performance (when you’re ready)
    • Interpreter:
    ○ Switch → direct-threaded dispatch (computed goto).
    ○ Intern strings; cache common constant strings.
    • Backend:
    ○ Peephole pass (remove redundant moves, combine cmp+jcc).
    ○ Simple constant folding at IL build time (you’ll get many wins “for free”).
    ○ Linear-scan regalloc with live-interval splitting.

01. Risks & Mitigations
    • Scope creep → Strict v1 feature set; milestone-based roadmap.
    • Type system complexity → Keep IL types minimal; push conversions into front end and runtime helpers.
    • String/heap bugs → Start with refcounted strings and a small test suite around them; add ASAN/UBSAN in CI.
    • Codegen pitfalls → Lean on VM-oracle differential tests; begin with a single platform/ABI.

01. Roadmap (suggested)
    Milestone A (bring-up)
    • BASIC front end for PRINT, LET, IF, GOTO.
    • IL core + verifier + textual serializer.
    • Runtime: rt_print\_\*, basic string support.
    • VM executes small programs; golden tests.
    Milestone B (control & funcs)
    • WHILE/WEND, FOR/NEXT (desugared), simple functions/procedures.
    • Calls in IL + runtime input (rt_input_line).
    • CLI flags: -emit-il, -run.
    Milestone C (codegen v1)
    • x86‑64 SysV codegen, linear-scan regalloc, emit .s, assemble+link with librt.a.
    • Differential tests (VM vs native).
    Milestone D (quality)
    • Peephole optimizer, better diagnostics, small library expansion (strings/math/file I/O).
    • Optional: bytecode encoding for faster VM.

01. What’s “Done” for v1
    • BASIC subset compiles to IL, runs correctly on the VM, and compiles to native with matching outputs.
    • Clear CLI, docs, and tests.
    • Clean separation of: front end ↔ IL ↔ VM/Codegen ↔ runtime.

Final Notes
• This design stays intentionally small and orthogonal so you can build it solo without it turning into another sprawling VC.
• The IL is your stable contract: it keeps language work and machine work independent.
• The interpreter-first approach gives you immediate feedback and a rock-solid oracle for codegen.
