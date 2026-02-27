# Zia vs Viper BASIC: Feature Parity Matrix

## Context

This document is a comprehensive feature parity audit between the two Viper frontends: **Zia** (modern, Swift/Kotlin-influenced) and **Viper BASIC** (classic BASIC with OOP extensions). The goal is to identify gaps, surprising behavioral differences, and IL capabilities that one frontend hasn't wired up — prioritizing issues that would frustrate someone moving from BASIC to Zia.

---

## 1. Feature Parity Matrix

### Legend
- **Full** = fully implemented (parsed, analyzed, lowered to IL, tested)
- **Partial** = parsed and partially lowered, or missing edge cases
- **Parsed** = AST exists but no IL lowering
- **None** = not implemented at all

---

### 1.1 Variable Types & Declarations

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| 64-bit integer | **Full** (`Integer`) | **Full** (`I64`, `%`, `&`) | Same IL type |
| 64-bit float | **Full** (`Number`) | **Full** (`F64`, `#`, `!`) | Same IL type |
| Boolean | **Full** (`Boolean`) | **Full** (`Bool`) | **Zia=1/0, BASIC=-1/0** (see dangerous diffs) |
| String | **Full** (`String`) | **Full** (`Str`, `$`) | Same runtime ref-counted strings |
| Byte | **Full** (`Byte`) | None | Zia only — lowered as i32 |
| Unit type | **Full** (`Unit`) | None | Zia only — for `Result[Unit]` |
| Ptr (opaque) | **Full** | **Full** (`Object`) | Both use IL `ptr` |
| Any (top type) | **Full** | None | Zia only |
| Never (bottom type) | **Full** | None | Zia only |
| Type suffixes (`$`, `%`, `#`) | None | **Full** | BASIC only |
| Type inference | **Full** | **Partial** (from context) | Zia infers from initializer; BASIC from suffix or AS |
| Mutable variable | **Full** (`var`) | **Full** (`DIM`) | |
| Immutable binding | **Full** (`final`) | **Full** (`CONST`) | BASIC CONST is compile-time only |
| Static local | None | **Full** (`STATIC`) | BASIC only — persistent across calls |
| Shared module variable | None | **Full** (`SHARED`) | BASIC only |
| Array resize | None | **Full** (`REDIM PRESERVE`) | BASIC only — Zia uses dynamic List |
| Multi-dim arrays | None | **Full** (`DIM a(m,n)`) | BASIC only — row-major flattened |
| Implicit declaration | None | **Full** (via suffix) | BASIC allows `x% = 5` without DIM |

### 1.2 Control Flow

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| if/else | **Full** (`if/else`) | **Full** (`IF/THEN/ELSE/END IF`) | |
| if expression (ternary) | **Full** (`if (c) a else b`) | None | Zia only |
| Ternary operator | **Full** (`a ? b : c`) | None | Zia only |
| while loop | **Full** (`while`) | **Full** (`WHILE/WEND`) | |
| do-while / do-until | None | **Full** (`DO/LOOP WHILE/UNTIL`) | BASIC only — pre-test and post-test |
| C-style for | **Full** (`for (;;)`) | None | Zia only |
| Counted for | None | **Full** (`FOR i = a TO b STEP s`) | BASIC only |
| for-in (range) | **Full** (`for (i in 0..10)`) | None | Zia only |
| for-in (collection) | **Full** | **Full** (`FOR EACH`) | Both iterate collections |
| for-in (tuple destructure) | **Full** (`for (k,v in map)`) | None | Zia only |
| match/select | **Full** (`match`) | **Full** (`SELECT CASE`) | Both fully featured |
| match expression | **Full** | None | Zia only — returns a value |
| Pattern matching (destructure) | **Full** (Wildcard, Binding, Constructor, Tuple) | None | Zia only |
| Guard statement | **Full** (`guard ... else`) | None | Zia only |
| GoTo | None | **Full** (`GOTO`) | BASIC only |
| GoSub/Return | None | **Full** (`GOSUB/RETURN`) | BASIC only — stack-based |
| break/continue | **Full** | **Full** (`EXIT FOR/WHILE/DO`) | BASIC has no `continue` equivalent |
| Labeled break | None | None | Neither |

### 1.3 Functions & Procedures

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Function declaration | **Full** (`func`) | **Full** (`FUNCTION/END FUNCTION`) | |
| Void procedure | **Full** (returns `Void`) | **Full** (`SUB/END SUB`) | |
| Return type annotation | **Full** (`-> Type` or `: Type`) | **Full** (`AS Type`) | |
| Named arguments | **Parsed** | None | Zia parses but unclear if fully used |
| Default parameters | **Full** | None | Zia pads missing args with lowered default expressions |
| ByRef parameters | None | **Full** (`BYREF`) | BASIC only |
| ByVal parameters | Implicit (all by-value) | **Full** (`BYVAL`, default) | |
| Array parameters | None | **Full** (`arr()` syntax) | BASIC only |
| Variadic parameters | None | None | Neither |
| Function overloading | None | None | Neither |
| Generic functions | **Full** (`func f[T](x: T)`) | None | Zia only |
| Constrained generics | **Full** (`[T: Interface]`) | None | Zia only |
| Function references | **Full** (`&funcName`) | **Full** (`ADDRESSOF`) | Both produce function pointers |
| Lambda / closure | **Full** (`(x) => x + 1`) | None | **Major gap** — Zia only |

### 1.4 Classes, Entities & OOP

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Reference type (heap) | **Full** (`entity`) | **Full** (`CLASS`) | Both use rt_alloc |
| Value type (copy) | **Full** (`value`) | **Full** (`TYPE`) | BASIC TYPE is fields-only (no methods) |
| Constructor | **Full** (`func init()`) | **Full** (`SUB New()`) | |
| Destructor | **Full** (`deinit`) | **Full** (`DESTRUCTOR`) | Both emit `__dtor_TypeName` function |
| DELETE statement | None | **Full** (`DELETE obj`) | **Gap** — BASIC only |
| Single inheritance | **Full** (`extends`) | **Full** (`:` syntax) | |
| Interface declaration | **Full** (`interface`) | **Full** (`INTERFACE`) | |
| Interface implementation | **Full** (itable dispatch) | **Full** (itable dispatch) | Both use runtime itable binding |
| Virtual methods | Implicit (all virtual) | **Full** (`VIRTUAL` modifier) | BASIC has explicit vtable dispatch |
| Override methods | **Full** (`override`) | **Full** (`OVERRIDE`) | |
| Abstract methods | None | **Full** (`ABSTRACT`) | BASIC only |
| Final methods | None | **Full** (`FINAL`) | BASIC only |
| Abstract classes | None | **Full** | BASIC only |
| Properties (get/set) | **Full** (`property`) | **Full** (`PROPERTY/GET/SET`) | Both synthesize `get_X`/`set_X` methods |
| Static fields | **Full** (`static`) | **Full** (`STATIC DIM`) | Zia emits IL globals |
| Static methods | **Full** (`static func`) | **Full** (`STATIC SUB/FUNCTION`) | Both omit self parameter |
| Static constructors | None | **Full** | BASIC only — `$static` thunks |
| Access control | **Full** (`expose`/`hide`) | **Full** (`PUBLIC`/`PRIVATE`) | |
| self/this reference | **Full** (`self`) | **Full** (`ME`) | |
| Base class call | **Full** (`super.method()`) | **Full** (`BASE.Method()`) | |
| Null reference | **Full** (`null`) | **Full** (`NOTHING`) | |
| Struct literal init | **Full** (`Point { x=1, y=2 }`) | None | Zia only |
| Weak references | **Parsed** | None | Zia parses `weak` but doesn't lower |

### 1.5 Collections

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Dynamic list | **Full** (List literal `[1,2,3]`) | Via runtime (`Viper.Collections.List`) | Zia has literal syntax + sugar |
| Map / Dictionary | **Full** (Map literal `{"a":1}`) | Via runtime (`Viper.Collections.Map`) | Zia has literal syntax + sugar |
| Set | **Full** (Set literal `{1,2,3}`) | Via runtime | Zia lowers set literals via `rt_set_new` + `rt_set_add` |
| Seq (lazy sequence) | **Full** | Via runtime | Zia has specific for-in integration |
| Native arrays | **Full** (FixedArray `T[N]`) | **Full** (`DIM arr(size)`) | Different models: Zia=fixed-size inline, BASIC=dynamic |
| Multi-dim arrays | None | **Full** | BASIC only |
| Array bounds query | None | **Full** (`LBOUND`/`UBOUND`) | BASIC only |
| Index syntax sugar | **Full** (`list[i]`, `map[k]`) | **Full** (`arr(i)`) | Zia uses brackets, BASIC uses parens |
| Collection generics | **Full** (`List[Integer]`) | None | Zia only — BASIC uses untyped `Object` |
| Tuple type | **Full** (`(Integer, String)`) | None | Zia only |
| Tuple destructure | **Full** (`var (a, b) = tuple`) | None | Zia only |

### 1.6 String Operations

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| String concatenation | **Full** (`+`) | **Full** (`&` and `+`) | **`&` means bitwise AND in Zia** |
| String interpolation | **Full** (`"Hello ${name}"`) | None | **Gap** — Zia only |
| Auto-coerce to string | **Full** (left must be String) | **Full** (either side) | BASIC is more permissive |
| Comparison operators | **Full** (case-sensitive) | **Full** (case-sensitive) | Same runtime functions |
| LEN / length | Via `Str.Len(s)` | **Full** (`LEN(s)`) | Different syntax |
| Substring | Via `Str.Mid(s,pos,len)` | **Full** (`MID$(s,pos,len)`) | Both use 1-based positions |
| Left/Right | Via runtime | **Full** (`LEFT$`/`RIGHT$`) | BASIC has direct builtins |
| Trim | Via runtime | **Full** (`LTRIM$`/`RTRIM$`/`TRIM$`) | |
| Case conversion | Via runtime | **Full** (`UCASE$`/`LCASE$`) | |
| Search | Via runtime | **Full** (`INSTR()`) | |
| Char code | Via runtime | **Full** (`ASC()`/`CHR$()`) | |
| Number to string | Via `+` auto-coerce | **Full** (`STR$()`) | |
| String to number | Via runtime | **Full** (`VAL()`) | |

### 1.7 Error Handling

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| try/catch/finally | **Full** (`try/catch/finally`) | **Full** (`TRY/CATCH/FINALLY/END TRY`) | Both use IL EhPush/EhPop/EhEntry/ResumeLabel |
| On Error GoTo | None | **Full** (`ON ERROR GOTO`) | BASIC only — classic unstructured EH |
| Resume | None | **Full** (`RESUME/RESUME NEXT/RESUME label`) | BASIC only |
| USING (auto-dispose) | None | **Full** (`USING/END USING`) | BASIC only — desugars to TRY/FINALLY |
| Optional type (T?) | **Full** | None | Zia only — null-safe type system |
| Null coalescing (??) | **Full** | None | Zia only |
| Optional chaining (?.) | **Full** | None | Zia only |
| Try propagation (?) | **Full** | None | Zia only — early return on null |
| Force unwrap (!) | **Full** | None | Zia only — trap on null |
| Result type | **Typed** (exists in type system) | None | Zia has the type but no construction/destructure |

**Analysis**: Both frontends now have structured exception handling. Zia additionally uses null-safe optional types for expected failures. BASIC also has unstructured EH (ON ERROR GOTO) which Zia does not support.

### 1.8 Closures & Lambdas

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Lambda expressions | **Full** (`(x) => x + 1`) | None | |
| Closures (captures) | **Full** (value + reference capture) | None | |
| Higher-order functions | **Full** | None | BASIC has ADDRESSOF but no closures |

### 1.9 Modules & Imports

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Module declaration | **Full** (`module M;`) | **Full** (implicit per-file) | |
| Namespace blocks | **Full** (`namespace N { }`) | **Full** (`NAMESPACE N/END NAMESPACE`) | |
| File imports | **Full** (`bind "./file"`) | **Full** (`ADDFILE "file.bas"`) | BASIC is textual inclusion |
| Namespace imports | **Full** (`bind Viper.Terminal`) | **Full** (`USING Viper.Terminal`) | |
| Selective imports | **Full** (`bind Viper.X { A, B }`) | None | Zia only |
| Alias imports | **Full** (`bind X = Viper.Y`) | **Full** (`USING Viper.Y AS X`) | |
| Import depth limits | **Full** (50 depth, 100 files) | None | Zia only |

### 1.10 Operators

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Arithmetic (`+`,`-`,`*`,`/`) | **Full** | **Full** | **Both do integer `/` on int operands** |
| Integer division | Via `/` (same as regular div) | **Full** (`\`) | BASIC has explicit integer div |
| Modulo | **Full** (`%`) | **Full** (`MOD`) | Both use signed remainder (SRemChk0) |
| Exponentiation | None (use `Math.Pow()`) | **Full** (`^`) | **`^` = bitwise XOR in Zia** |
| Comparison (`==`,`!=`,`<`,etc.) | **Full** | **Full** (`=`,`<>`,`<`,etc.) | **`=` means different things** |
| Logical AND (short-circuit) | **Full** (`&&` or `and`) | **Full** (`ANDALSO`) | |
| Logical OR (short-circuit) | **Full** (`\|\|` or `or`) | **Full** (`ORELSE`) | |
| Logical NOT | **Full** (`!` or `not`) | **Full** (`NOT`) | |
| Eager logical AND/OR | None | **Full** (`AND`/`OR`) | BASIC only — no short-circuit |
| Bitwise AND | **Full** (`&`) | None | **`&` = string concat in BASIC** |
| Bitwise OR | **Full** (`\|`) | None | Zia only |
| Bitwise XOR | **Full** (`^`) | None | **`^` = exponentiation in BASIC** |
| Bitwise NOT | **Full** (`~`) | None | Zia only |
| Compound assignment (`+=`) | **Full** (`+=`, `-=`, `*=`, `/=`, `%=`) | None | Zia desugars `a += b` to `a = a + b` |
| Null coalescing (`??`) | **Full** | None | Zia only |
| Optional chaining (`?.`) | **Full** | None | Zia only |
| Range (`..`, `..=`) | **Full** | None | Zia only |
| Type check (`is`) | **Full** (`is`) | **Full** (`IS`) | Both use `rt_obj_class_id` + comparison |
| Type cast (`as`) | **Full** | **Full** (`AS`) | |
| Operator overloading | None | None | Neither |

### 1.11 Generics

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Generic types | **Full** (`entity Stack[T]`) | None | Zia only |
| Generic functions | **Full** (`func f[T](x: T)`) | None | Zia only |
| Type constraints | **Full** (`[T: Comparable]`) | None | Zia only |
| Generic instantiation | **Full** (name-mangling + cache) | None | Zia only |

### 1.12 I/O & Domain-Specific

| Feature | Zia | BASIC | Notes |
|---------|-----|-------|-------|
| Console print | Via `bind Viper.Terminal` | **Full** (`PRINT`) | BASIC has built-in syntax |
| Console input | Via runtime | **Full** (`INPUT`) | BASIC has built-in syntax |
| File I/O statements | Via runtime | **Full** (`OPEN/CLOSE/PRINT#/INPUT#`) | BASIC has built-in file channel syntax |
| File seek/position | Via runtime | **Full** (`SEEK`, `LOC`, `LOF`, `EOF`) | |
| Screen control | Via runtime | **Full** (`CLS`, `COLOR`, `LOCATE`, `CURSOR`) | BASIC has built-in syntax |
| Swap | None | **Full** (`SWAP a, b`) | BASIC only |
| Sleep | Via runtime | **Full** (`SLEEP ms`) | |
| Random seed | Via runtime | **Full** (`RANDOMIZE seed`) | |
| Sound/beep | Via runtime | **Full** (`BEEP`) | |
| Command-line args | Via runtime | **Full** (`ARGC()`, `ARG$()`, `COMMAND$()`) | |
| Math builtins | Via `bind Viper.Math` | **Full** (SQR, SIN, COS, etc.) | BASIC has 16+ built-in math functions |
| Constant folding | None (runtime eval) | **Full** | BASIC folds arithmetic, casts, and logical ops at compile time |

---

## 2. Dangerous Cross-Language Confusions

Ranked by potential for **silent incorrect behavior** (no compile error, just wrong results):

### HIGH: `^` operator means different things
- **BASIC**: `2 ^ 3` = `8.0` (exponentiation)
- **Zia**: `2 ^ 3` = `1` (bitwise XOR: `0b10 XOR 0b11 = 0b01`)
- **Impact**: A BASIC user porting `x ^ 2` to Zia gets different results.
- **Mitigation**: Warning W017 (default-enabled) alerts: "'^' is bitwise XOR in Zia; use Math.Pow() for exponentiation".

### HIGH: `&` operator means different things
- **BASIC**: `"hello" & " world"` = `"hello world"` (string concatenation)
- **Zia**: `&` is bitwise AND (binary) or address-of (unary)
- **Impact**: `intA & intB` computes different operations. String cases produce type errors (caught).
- **Mitigation**: Warning W018 (default-enabled) alerts: "'&' is bitwise AND in Zia; use '+' for string concatenation".

### HIGH: Boolean TRUE value differs
- **BASIC**: `TRUE = -1` (all bits set, `0xFFFFFFFFFFFFFFFF`)
- **Zia**: `TRUE = 1` (only bit 0 set)
- **Impact**: `TRUE + TRUE` = `-2` in BASIC, `2` in Zia. Bitwise ops wildly different: `TRUE AND mask` operates on -1 vs 1. Arithmetic with booleans gives different results.
- **Impact on interop**: If a BASIC runtime function returns -1 and Zia interprets it as a boolean, it works (non-zero = true). But if Zia code does arithmetic on it, results differ.

### HIGH: `=` in conditions
- **BASIC**: `IF x = 5 THEN` — compares (equality)
- **Zia**: `if (x = 5)` — assigns 5 to x, then type-errors (condition must be Boolean)
- **Impact**: Zia catches this with warning W007 + type error, so not silent. But confusing.

### MEDIUM: Integer division with `/`
- **BASIC**: Has separate operators: `/` (float) and `\` (integer). But the lowerer actually emits integer division for integer `/` too (semantic type says Float but IL is SDiv).
- **Zia**: `/` on two integers = integer result (SDiv). No float promotion.
- **Impact**: Both silently truncate `7 / 2` to `3`. BASIC users expecting `3.5` won't get it from either language.

### LOW: Scope rules
- **BASIC**: Flat procedure scope — all variables visible throughout the procedure.
- **Zia**: Block-scoped — variables in `{...}` blocks are not visible outside.
- **Impact**: A variable declared inside an `if` body disappears at `}` in Zia. BASIC users won't expect this. Zia catches it as a compile error (undeclared variable), so not silent.

---

## 3. IL Capabilities Not Wired Up

Features where the IL and/or runtime supports something but a frontend hasn't connected:

### Zia hasn't wired up:

| IL/Runtime Capability | IL Opcodes | Status in Zia |
|----------------------|------------|---------------|
| Result type | Type exists in type system | **No construction or destructuring** |
| Weak references | `weak` keyword parsed | **No lowering or runtime support** |
| Abstract/Final modifiers | Could validate in sema | **No syntax** |
| ByRef parameters | IL supports pointer-based pass-by-ref | **No syntax** |
| DELETE statement | `rt_heap_release` exists | **No explicit delete** — relies on deinit + GC |

### Recently closed Zia gaps (implemented):

| Feature | Status | Implementation |
|---------|--------|----------------|
| Exception handling | **Full** | `try/catch/finally/throw` → EhPush/EhPop/EhEntry/ResumeLabel |
| `is` type check | **Full** | `rt_obj_class_id` + ICmpEq |
| Set literals | **Full** | `{1,2,3}` → `rt_set_new` + `rt_set_add` loop |
| Default parameters | **Full** | Missing args padded with lowered default expressions |
| Interface dynamic dispatch | **Full** | `rt_get_interface_impl` + `call.indirect` via itables |
| Static members | **Full** | `static` modifier on fields (globals) and methods |
| Properties (get/set) | **Full** | `property` syntax, synthesized `get_X`/`set_X` methods |
| Destructors | **Full** | `deinit` block → `__dtor_TypeName` with field release |
| Compound assignment | **Full** | `+=`, `-=`, `*=`, `/=`, `%=` desugared in parser |
| Operator confusion warnings | **Full** | W017 (`^` XOR) and W018 (`&` AND) default-enabled |

### BASIC hasn't wired up:

| IL/Runtime Capability | Status in BASIC |
|----------------------|-----------------|
| Lambdas/closures | **Not wired** — IL supports indirect calls with env, but BASIC has no syntax |
| Optional types | **Not wired** — uses NOTHING for null, no type-level null safety |
| Generics | **Not wired** — uses untyped Object for polymorphism |
| String interpolation | **Not wired** — could desugar to concatenation like Zia |
| Boxing/Unboxing | **No explicit aliases** — limits polymorphic collection usage |
| Pattern matching | **Not wired** — SELECT CASE covers literals only, no destructuring |
| Range types | **Not wired** — FOR uses explicit TO/STEP |
| Bitwise operators | **Not wired** — no syntax for AND/OR/XOR/NOT at bit level (AND/OR are logical) |

---

## 4. Priority Gaps (BASIC to Zia Migration Pain Points)

For someone who starts in BASIC and moves to Zia, ranked by frustration:

### P0 — Blocking (features BASIC has that Zia lacks, no workaround)

All former P0 gaps have been resolved:
- ~~No try/catch/finally~~ → **Implemented** (`try/catch/finally/throw`)
- ~~No properties (get/set)~~ → **Implemented** (`property` syntax)
- ~~No static members~~ → **Implemented** (`static` modifier)
- ~~No destructors~~ → **Implemented** (`deinit` block)

### P1 — High friction (significant missing features with partial workarounds)

All former P1 gaps have been resolved:
- ~~No `is` type check~~ → **Implemented** (runtime `rt_obj_class_id` + comparison)
- ~~No interface dynamic dispatch~~ → **Implemented** (full itable dispatch via `rt_get_interface_impl`)
- ~~`^` operator confusion~~ → **Mitigated** (W017 warning, default-enabled)
- ~~`&` operator confusion~~ → **Mitigated** (W018 warning, default-enabled)
- ~~No default parameter values~~ → **Implemented** (missing args padded from AST defaults)
- ~~Set literals not lowered~~ → **Implemented** (`{1,2,3}` → `rt_set_new` + `rt_set_add`)

### P2 — Moderate friction (missing convenience features)

1. **No `continue` equivalent in BASIC** — `EXIT` only exits loops, can't skip to next iteration.
2. **No BASIC-style `DO/LOOP UNTIL`** in Zia — must use `while (!cond)`.
3. **No string interpolation in BASIC** — must use `&` concatenation.
4. **Boolean -1 vs 1** — arithmetic on booleans gives different results.
5. **No ByRef parameters in Zia** — BASIC can pass by reference; Zia always copies.
6. **No DELETE statement in Zia** — BASIC has `DELETE obj`; Zia relies on GC/`deinit`.
7. **No abstract/final method modifiers in Zia** — BASIC has `ABSTRACT`/`FINAL`.

### P3 — Low friction (different syntax, same capability)

8. **Different loop syntax** — `FOR i = 1 TO 10` vs `for (var i = 1; i <= 10; i = i + 1)`
9. **Different class syntax** — `CLASS/END CLASS` vs `entity { }`
10. **Different import syntax** — `USING` vs `bind`
11. **Different null literal** — `NOTHING` vs `null`

---

## 5. Source References

To spot-check specific claims, inspect:

### Operator differences
- Zia `^` as XOR: `src/frontends/zia/Parser_Expr.cpp` (`parseBitwiseXor`)
- BASIC `^` as power: `src/frontends/basic/Parser_Expr.cpp` (precedence 7, right-assoc)
- BASIC TRUE=-1: `src/frontends/basic/TypeCoercionEngine.cpp` line 12
- Zia W017/W018 warnings: `src/frontends/zia/Sema_Expr_Ops.cpp` (BitXor/BitAnd cases)

### Zia feature implementations
- `is` type check: `src/frontends/zia/Lowerer_Expr_Complex.cpp` (`lowerIsExpr`)
- Set literals: `src/frontends/zia/Lowerer_Expr_Collections.cpp` (`lowerSetLiteral`)
- Compound assignment: `src/frontends/zia/Parser_Expr.cpp` (desugared in `parseAssignment`)
- Default parameters: `src/frontends/zia/Lowerer_Expr_Call.cpp` (arg padding)
- Properties: `src/frontends/zia/Lowerer_Decl.cpp` (synthesized `get_X`/`set_X`)
- Static members: `src/frontends/zia/Lowerer_Decl.cpp` (globals + non-self methods)
- Destructors: `src/frontends/zia/Lowerer_Decl.cpp` (`__dtor_TypeName` emission)
- try/catch/finally: `src/frontends/zia/Lowerer_Stmt_EH.cpp` (`lowerTryStmt`/`lowerThrowStmt`)
- Interface itable dispatch: `src/frontends/zia/Lowerer_Dispatch.cpp` (`lowerInterfaceMethodCall`)
- Itable registration: `src/frontends/zia/Lowerer_Decl.cpp` (`emitItableInit`)

### BASIC reference implementations
- BASIC TRY/CATCH lowering: `src/frontends/basic/lower/Lower_TryCatch.cpp`
- BASIC vtable dispatch: `src/frontends/basic/lower/oop/Lower_OOP_MethodCall.cpp`
- BASIC itable dispatch: `src/frontends/basic/lower/oop/Lower_OOP_Emit.cpp` lines 900-954

### IL and runtime
- IL EH opcodes: `src/il/core/Opcode.def` (EhPush, EhPop, EhEntry, Resume*)
- Runtime class registry: `src/il/runtime/classes/RuntimeClasses.hpp` (RuntimeTypeId enum)
- Runtime definitions: `src/il/runtime/runtime.def` (225 RT_CLASS_BEGIN blocks, 3129 RT_FUNC entries)
