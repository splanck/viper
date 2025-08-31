IL v0.1.1 — Specification
Status: Normative for VM + codegen MVP
Compatibility: Supersedes v0.1 (tightened typing & traps; no breaking syntax changes)
1. Design Goals
Thin waist between multiple front ends and multiple back ends.
Deterministic semantics so VM and native agree.
Simple, explicit control flow (no fallthrough; one terminator per block).
Small type system (i64, f64, i1, ptr, str).
Non-semantic optimization: transformation passes must not alter program behaviour.
2. Module Structure (text)
il 0.1.1
target "x86_64-sysv"      ;
optional;
VM ignores

    extern @rt_print_str(str)
        ->void global const str @.L0 = "HELLO"

                                       func @main()
                                           ->i32 {
entry:
  % s = const_str @.L0 call @rt_print_str(% s) ret 0
}
Symbols: functions/globals @name, temporaries %tN, labels label:.
Visibility: extern, global [const], func (public by default), internal (optional attribute).
3. Types
Kind | Meaning | Notes
---- | ------- | -----
void | no value | only as function return
i1 | boolean | produced by comparisons & trunc1
i64 | 64-bit signed int | wrap-on-overflow in add/sub/mul
f64 | 64-bit IEEE | NaN/Inf propagate
ptr | untyped pointer | byte-addressed
str | opaque string handle | managed by runtime
4. Constants & Literals
Integers: -?[0-9]+
Floats: -?[0-9]+(\.[0-9]+)? plus NaN, Inf, -Inf
Bools: true/false (sugar for i1 1/0)
Strings: "..." with escapes \" \\ \n \t \xNN
Null pointer: const_null (ptr)
5. Functions & Basic Blocks
A function has parameters, return type, 1+ blocks.
Exactly one terminator per block and none elsewhere.
No implicit fallthrough; control flow uses br/cbr/ret.
6. Instruction Set (typing & traps)
Legend: ⟶ result type, Trap: runtime error condition.
6.1 Integer arithmetic (wrap on overflow)
%d = add %a:i64, %b:i64 ⟶ i64
%d = sub %a:i64, %b:i64 ⟶ i64
%d = mul %a:i64, %b:i64 ⟶ i64
%d = sdiv %a:i64, %b:i64 ⟶ i64 Trap: b=0 or a=INT64_MIN, b=-1
%d = udiv %a:i64, %b:i64 ⟶ i64 Trap: b=0
%d = srem %a:i64, %b:i64 ⟶ i64 Trap: b=0
%d = urem %a:i64, %b:i64 ⟶ i64 Trap: b=0
Bitwise & shifts
and/or/xor: i64 × i64 ⟶ i64
shl/lshr/ashr: i64 × i64 ⟶ i64 (shift count masked mod 64)
6.2 Floating arithmetic (IEEE-754)
fadd/fsub/fmul/fdiv: f64 × f64 ⟶ f64 (NaN/Inf per IEEE)
6.3 Comparisons (result i1)
Integer equality: icmp_eq/icmp_ne (i64,i64)
Signed: scmp_lt/le/gt/ge (i64,i64)
Unsigned: ucmp_lt/le/gt/ge (i64,i64)
Float: fcmp_lt/le/gt/ge/eq/ne (f64,f64) (NaN: eq false, ne true)
6.4 Conversions
sitofp (i64) ⟶ f64
fptosi (f64) ⟶ i64 Trap: NaN or out-of-range
zext1 (i1) ⟶ i64 (0/1 → 0/1)
trunc1 (i64) ⟶ i1 (0 → 0, non-zero → 1)
6.5 Memory & pointers
%p = alloca %n:i64 ⟶ ptr (n ≥ 0; zero-initialized; frame-local)
%q = gep %base:ptr, %off:i64 ⟶ ptr (q = base + off, no bounds checks)
%v = load ty, %p:ptr ⟶ ty Trap: %p=null or misaligned for ty
store ty, %p:ptr, %v:ty Trap: %p=null or misaligned
%p = addr_of @global ⟶ ptr
%p = const_null ⟶ ptr
Alignment: natural alignment: i64/f64 → 8 bytes; str/ptr → pointer-size (8). Misaligned load/store trap.
6.6 Calls & returns
%r = call @f(arg1,…): (T1,…)->Tr
ret (for void), ret %v:Tr otherwise.
Arity and types must match the callee’s signature (verified).
6.7 Control flow (terminators)
br label % dst cbr % cond : i1, label % t,
    label % f ret[val] 6.8 String &misc % s =
        const_str @.Lk ⟶ str trap — unconditional runtime error with diagnostic;
abort program.7. Runtime ABI(minimum surface) C ABI functions(extern)
    : @rt_print_str(str)
          ->void @rt_print_i64(i64)
          ->void @rt_print_f64(f64)
          ->void @rt_input_line()
          ->str @rt_len(str)
          ->i64 @rt_concat(str, str)
          ->str @rt_substr(str, i64, i64)
          ->str @rt_to_int(str)
          ->i64 @rt_to_float(str)
          ->f64 @rt_str_eq(str, str)
          ->i1 @rt_alloc(i64)
          ->ptr @rt_free(ptr)
          ->void(optional in v0.1.1) Strings are ref
    - counted(implementation detail).rt_substr clamps to valid range;
invalid(negative) parameters trap.8. Memory Model IL exposes no data races
    or concurrency primitives in v0.1.1. Pointers are plain addresses;
no aliasing rules beyond type of load / store.alloca memory lives until function returns;
zero - initialized.Null / misaligned load /
           store → trap.9. Verifier Rules Structure : each block ends with one terminator;
entry is first block;
labels referenced are defined in same
        function.Typing : operand and result types must match opcode signature.Calls : arity /
    types match callee signature;
destination present only when return type ≠ void.Memory : load / store use ptr operand;
data type not void.Alloca : size is i64(non - negative if constant).Use - before -
    def(intra - block)
    : a temp must be defined earlier in the same block.(
          Dominance across blocks deferred to later SSA pass.)10. Text Format Grammar(EBNF)
          module ::
    = "il" version(target_decl)
    ? (decl_or_def)*version :: = number "." number target_decl :: = "target" string

          decl_or_def ::
              = extern_decl | global_decl | func_def extern_decl :: = "extern" symbol "(" type_list
      ? ")"
        "->" type global_decl ::
            = "global"("const")
        ? type symbol "=" ginit ginit :: = string | int | float | "null" |
                                           symbol

                                               func_def ::
                                               = "func" symbol "(" params ? ")"
                                                                            "->" type attr_list
          ? "{" block + "}" params :: = param("," param) *param :: = ident ":" type type_list :: =
                type("," type) *

            block ::
                = label ":" instr *term label :: = ident instr :: =
                    named | bare named :: = temp "=" op bare :: =
                        term | "store" type "," value "," value | "call" symbol "(" args
                            ? ")"

                              term ::
                                  = "ret" | "ret" value |
                                    "br"
                                    "label" value |
                                    "cbr" value ","
                                    "label" value ","
                                    "label" value

                                        op ::
                                        = "add" value "," value | "sub" value "," value |
                                                  "mul" value "," value | "sdiv" value "," value |
                                                  "udiv" value "," value | "srem" value "," value |
                                                  "urem" value "," value | "and" value "," value |
                                                  "or" value "," value | "xor" value "," value |
                                                  "shl" value "," value | "lshr" value "," value |
                                                  "ashr" value "," value | "fadd" value "," value |
                                                  "fsub" value "," value | "fmul" value "," value |
                                                  "fdiv" value "," value |
                                                  "icmp_eq" value "," value |
                                                  "icmp_ne" value "," value |
                                                  "scmp_lt" value "," value |
                                                  "scmp_le" value "," value |
                                                  "scmp_gt" value "," value |
                                                  "scmp_ge" value "," value |
                                                  "ucmp_lt" value "," value |
                                                  "ucmp_le" value "," value |
                                                  "ucmp_gt" value "," value |
                                                  "ucmp_ge" value "," value |
                                                  "fcmp_lt" value "," value |
                                                  "fcmp_le" value "," value |
                                                  "fcmp_gt" value "," value |
                                                  "fcmp_ge" value "," value |
                                                  "fcmp_eq" value "," value |
                                                  "fcmp_ne" value "," value | "sitofp" value |
                                                  "fptosi" value | "zext1" value | "trunc1" value |
                                                  "alloca" value | "gep" value "," value |
                                                  "load" type "," value | "addr_of" symbol |
                                                  "const_str" symbol | "const_null" |
                                                  "call" symbol "(" args
                                              ? ")" | "trap"

                                                    args ::
                                                    = value("," value) *value :: =
                                                        temp | symbol | literal temp :: =
                                                            "%" ident symbol :: =
                                                                "@" ident type :: =
                                                                    "void" | "i1" | "i64" | "f64" |
                                                                    "ptr" |
                                                                    "str" 11. Calling Convention(
                                                                        native back end target)
                                                                        Default target
                                              : x86 - 64 System V(Linux / macOS).Args
                            : integers / pointers in rdi,
                            rsi, rdx, rcx, r8, r9;
floats in xmm0..7. Return : integer / pointer in rax;
float in xmm0.Stack : 16 - byte alignment at call sites.i1 passing : zero -
    extend to 32 bits(in line with SysV)
        .12. Versioning Files must start with il 0.1.1. Future versions must
    not change the meaning of existing opcodes;
additions are backwards - compatible.13. Conformance A VM or
    backend is conformant if it : Accepts grammar &passes verifier.Produces identical observable
                                  behavior(stdout, exit code) on the official sample suite under
        / docs / examples / il /.Traps on the listed conditions.
