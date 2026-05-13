# Language Audit — Validated Findings (2026-03-25)

This file was revalidated against the local compiler/runtime build in
`build/src/tools/viper/viper` and against the current parser, sema, and lowerer
sources on 2026-03-25, then spot-revalidated again on 2026-04-06 after the
latest frontend/runtime hardening pass, and again on 2026-05-13 for the Zia
alpha-hardening pass.

The earlier version of this audit mixed real defects, stale findings, and a few
feature requests. This revision keeps only validated claims, marks invalid ones,
and adds concrete recommendations.

---

## Confirmed Zia Bugs

### ZIA-BUG-001: Guard clause narrowing fails for optional primitives at lowering
**Severity:** P1
**Status:** FIXED (revalidated 2026-05-13)

```zia
func check(x: Integer?) {
    guard x != null else { return; }
    SayInt(x);          // ERROR: expects i64 but got ptr
    var y: Integer = x; // ERROR: expected Integer, got Integer?
}
```

**Resolved behavior**
- Optional primitive narrowing now survives both guard-clause forms:
  - `if (x == null) { return; }`
  - `guard x != null else { return; }`
- The fallthrough path correctly treats `Optional[T]` as `T` for reads and assignments.

**Regression coverage**
- `test_zia_guard_clause_narrowing`
- `test_zia_optional_narrowing`

### ZIA-BUG-002: All enum variant values evaluate to 0 at runtime
**Severity:** P0
**Status:** FIXED (verified 2026-03-25)

Auto-increment enums now return correct values (0, 1, 2). Explicit values untested
but likely also fixed.

### ZIA-BUG-003: Match arms do not accept bare `return` after `=>`
**Severity:** P2
**Status:** FIXED (revalidated 2026-05-13)

```zia
match c {
    Color.Red => return "red";  // ERROR: expected expression
}
```

**Resolved behavior**
- Match statement arms now accept a single statement body after `=>`, including
  `return`, `break`, `continue`, `throw`, declarations, and nested control-flow
  statements.
- Block arms and expression arms continue to work.

**Regression coverage**
- `zia_lang_44_language_promises`

### ZIA-BUG-004: Typed catch parses, but lowering produces invalid EH IL
**Severity:** P1
**Status:** FIXED (revalidated 2026-05-13)

```zia
try {
    throw 1;
} catch(e: RuntimeError) {
    var x = 1;
}
```

**Resolved behavior**
- `catch(e)` works, and the bound `Error` value can be read in the catch body.
- `Error` exposes `kind` / `type`, `message`, `code`, `line`, and `location`.
- `throw value` is caught by `catch(e: RuntimeError)`.
- Multiple catch clauses are checked in source order; catch-all clauses must be last.
- Typed catch composes with `finally`; mismatched typed catches run `finally` before
  rethrowing to an outer handler.
- Catch and finally bodies may contain internal branches while still producing
  verifier-valid EH IL.
- `e.message` reports the user `throw` payload for `RuntimeError` and a default
  runtime-fault message such as `Division by zero` for `DivideByZero`; stale
  throw messages do not leak into later catches.
- Bare `throw;` inside a catch rethrows the active error.
- Zia rethrow paths preserve `finally` side effects by raising the original
  trap kind/code instead of using retry-style `resume.same`.
- Native EH lowering rewrites typed-catch helper blocks instead of leaving residual
  `Error` / `ResumeTok` handler parameters.

**Regression coverage**
- `test_zia_try_catch`
- `zia_lang_31_typed_catch_list_shorthand`
- `zia_lang_42_try_catch_promises`
- `viper_run_o1_zia_42_try_catch_promises`
- `native_run_zia_42_try_catch_promises`
- `zia_lang_43_alpha_hardening`
- `viper_run_o1_zia_43_alpha_hardening`
- `native_run_zia_43_alpha_hardening`
- `zia_lang_44_language_promises`
- `test_native_eh_lowering`

### ZIA-BUG-005: Child class `init` requires explicit `override`
**Severity:** P3
**Status:** FIXED (revalidated 2026-05-13)

```zia
class A { expose func init(x: Integer) {} }
class B extends A {
    expose func init(x: Integer) {} // ERROR: must be marked override
}
```

**Resolved behavior**
- Child `init` methods may reuse a parent `init` signature without writing
  `override`.
- Ordinary method overrides still require `override`.

**Regression coverage**
- `zia_lang_44_language_promises`

### ZIA-BUG-006: Entity properties found but treated as private
**Severity:** P1
**Status:** FIXED (revalidated 2026-04-06)

```zia
class Counter {
    hide Integer _count;
    expose func init() { _count = 0; }
    property count: Integer {
        get { return _count; }
        set(v) { _count = v; }
    }
}

func start() {
    var c = new Counter();
    c.count = 42;     // ERROR: Cannot access private member 'count' of type 'Counter'
    SayInt(c.count);  // ERROR: Cannot access private member 'count' of type 'Counter'
}
```

**Resolved behavior**
- Exposed properties are accessible through normal member syntax.
- Getter/setter methods are synthesized and used by lowering.
- Setter-only properties are supported; reads still fail with a write-only diagnostic.

**Regression coverage**
- `test_zia_properties_static`

### ZIA-BUG-007: Tuple destructuring in `var` declarations does not parse
**Severity:** P2
**Status:** FIXED (revalidated 2026-05-13)

```zia
func swap(a: Integer, b: Integer) -> (Integer, Integer) { return (b, a); }
var (x, y) = swap(1, 2);  // ERROR: expected variable name
```

**Resolved behavior**
- Two-element tuple bindings parse, analyze, and lower in `var`, `final`, and
  `let` declarations.
- Optional element type annotations are checked against the corresponding tuple
  element type.

**Regression coverage**
- `zia_lang_44_language_promises`

### ZIA-BUG-008: String instance methods like `ToUpper`, `Trim`, and `Substring` lower incorrectly
**Severity:** P1
**Status:** FIXED (verified 2026-03-25)

`s.ToUpper()` now correctly returns "HELLO". String method dispatch was fixed.

### ZIA-BUG-010: `async func` and `await`
**Severity:** P1
**Status:** FIXED (verified 2026-03-25)

`async func` + `await` now produces correct output ("data"). The async/await lowering
is functional.

### ZIA-BUG-011: `deinit` bodies cannot access symbols introduced via `bind`
**Severity:** P2 — Destructor lowering loses module binding context

```zia
bind Viper.Terminal;

class Res {
    deinit { Say("deinit"); }  // ERROR: unknown callee @Say
}
```

**Status:** Fixed on 2026-03-25

**Resolved behavior**
- `deinit` lowering now runs semantic analysis with the enclosing class/module context, so
  bound symbols such as `Say` resolve normally inside the synthesized destructor.
- Managed object release paths now dispatch through `__zia_dtor_dispatch` before `rt_obj_free`,
  so explicit `Viper.Memory.Release(obj)` also executes the user `deinit` body.

**Regression coverage**
- Unit: `test_zia_destructor_bindings`
- Unit: `test_zia_destructors`
- Runtime: `zia_lang_38_deinit_bindings`

### ZIA-BUG-016: Superseded by ZIA-BUG-008
**Status:** FIXED — ZIA-BUG-008 is fixed, so this duplicate is also resolved.

### ZIA-BUG-017: Superseded by ZIA-BUG-010
**Status:** FIXED — ZIA-BUG-010 is fixed, so this duplicate is also resolved.

---

## Confirmed BASIC Bugs

### BAS-BUG-002: `FOR EACH` iterates one element too many
**Severity:** P2 — Off-by-one in array iteration

```basic
DIM items(3) AS INTEGER
LET items(0) = 10
LET items(1) = 20
LET items(2) = 30

FOR EACH x IN items
    PRINT x
NEXT x
```

**Validated behavior**
- Output is `10`, `20`, `30`, `0`.
- This is a real bug because the docs define `DIM A(5)` as `0..4`, not `0..5`.

**Likely root cause**
- `FOR EACH` uses the runtime-reported length directly while the declared BASIC array
  extent is documented as an exclusive upper bound.

**Recommendation**
- Make `FOR EACH` honor the same array bound semantics as indexed `FOR I = 0 TO N - 1`.
- Add a regression covering partially initialized arrays and string/object arrays.

---

## Invalidated Or Stale Claims

These claims from the earlier audit were checked and are not accurate against the
current tree.

### ZIA-BUG-009: `Map.New()` not accessible via short bind
**Status:** Invalid

```zia
bind Viper.Collections;
var m = Map.New(); // WORKS
```

**Validated behavior**
- `bind Viper.Collections; var m = Map.New();` works.
- The shipped API audit examples already rely on this.

### ZIA-BUG-014: No `finally` block support
**Status:** FIXED / revalidated

**Validated behavior**
- `try { ... } finally { ... }` parses, lowers, and runs.
- `finally` runs before handled catches continue, before typed-catch mismatches
  rethrow, and in finally-only forms before the original error reaches an outer
  handler.
- `finally` also runs before `return`, `break`, and `continue` leave the protected
  region.
- Regression coverage: `zia_lang_42_try_catch_promises` and
  `zia_lang_43_alpha_hardening`.

### ZIA-BUG-015: Map/Set/List not accessible via short bind
**Status:** Invalid

Same invalidation as ZIA-BUG-009.

### ZIA-MISSING-001: Catch variable binding entirely missing
**Status:** Invalid as written

**Validated behavior**
- `catch(e) { ... }` works, including reading `e` inside the catch body.
- The missing/broken piece is typed catch lowering, covered by ZIA-BUG-004.
- `catch e { ... }` without parentheses is still not supported.

### BAS-BUG-001: BASIC class constructors do not accept arguments
**Status:** Invalid

**Validated behavior**
- `SUB NEW(...)` with arguments works.
- Existing e2e coverage for constructor args passes.

### BAS-BUG-003: `ARGC` built-in not accessible
**Status:** Invalid as written

**Validated behavior**
- `ARGC()` works.
- Bare `ARGC` does not, because it is a function, not a variable.

### BAS-MISSING-001: Constructor arguments for `CLASS`
**Status:** Invalid

Same invalidation as BAS-BUG-001.

### BAS-MISSING-002: `ARGC/ARG$/COMMAND$` not wired
**Status:** Invalid as a blanket claim

**Validated behavior**
- At minimum, `ARGC()` is wired and works.
- The docs should explain callable syntax instead of implying the builtins are absent.

---

## Reclassified Design Gaps / Wishlist Items

These behaviors are real, but they are not validated compiler bugs unless the
language spec commits to them.

### ZIA-BUG-012: No auto-generated `init` from exposed fields
**Status:** Reclassified — design gap, not a proven spec violation

```zia
class Person { expose Integer age; }
var p = new Person(30);  // ERROR: no init overload matching
```

**Validated behavior**
- No constructor is auto-generated from fields.

**Recommendation**
- If desired, treat this as a language-design proposal and specify:
  - when auto-init is generated
  - interaction with inheritance
  - interaction with defaults and visibility

### ZIA-BUG-013: No auto-init for child entities from field values
**Status:** Reclassified — follow-on design gap from ZIA-BUG-012

**Validated behavior**
- Child entities still need explicit `init`, and same-signature replacement still
  can now omit `override` for `init`.

**Recommendation**
- Decide this together with ZIA-BUG-005 and ZIA-BUG-012 as one constructor policy.

---

## Documentation Mismatches Found During Validation

### `docs/zia-reference.md`

Current doc drift on 2026-03-25:
- The try/catch section said named binding was unsupported, but `catch(e)` worked.
- The grammar still showed `tryStmt ::= "try" block ["catch" block] ["finally" block]`,
  which did not match parser support for `catch(e)` / `catch(e: Type)`.

**Update (2026-04-06)**
- The canonical reference now reflects the current parser and typed-catch behavior.
- Remaining documented limitations are the real ones: tuple destructuring in `var`
  declarations and match-arm `return` shorthand are still unsupported.

**Update (2026-05-13)**
- The canonical reference now documents `finally` execution for nonlocal exits,
  runtime-fault catch payloads, struct interface dispatch, constrained generic
  method calls, namespace globals, and the semicolon-terminated `foreign func`
  spelling.
- The same pass removed stale limitations for tuple destructuring and match-arm
  statement bodies, documented structured `Error` catch bindings, multiple catch
  clauses, bare rethrow, language-level `Result[T]` helpers, weak fields,
  function references, and child `init` override inference.

### `docs/basic-reference.md`

Current doc drift:
- `EXPORT` is already documented; the old audit note claiming it was missing was stale.
- Constructor examples using `NEW Class()` are fine, but the docs should explicitly
  note that argument-bearing `SUB NEW(...)` works.
- Argument builtins like `ARGC()` should be documented as function calls, not implied
  to be unavailable.
- Array bound semantics matter here because `FOR EACH` currently disagrees with the doc.

**Recommendation**
- Add short examples for `SUB NEW(arg)` and `ARGC()`.
- Clarify array extent semantics near `DIM` and `FOR EACH`.

---

## Status Summary (verified 2026-05-13)

### FIXED
- ✅ `ZIA-BUG-001` — guard-statement optional narrowing now works for primitives
- ✅ `ZIA-BUG-002` — enum values now correct (auto-increment works)
- ✅ `ZIA-BUG-003` — match-arm statement bodies now parse and lower
- ✅ `ZIA-BUG-004` — typed catch lowering and `finally` composition now verify
- ✅ `ZIA-BUG-005` — child `init` no longer requires explicit `override`
- ✅ `ZIA-BUG-006` — exposed properties are no longer treated as private
- ✅ `ZIA-BUG-007` — two-element tuple destructuring in declarations works
- ✅ `ZIA-BUG-008` — `String.ToUpper()` returns correct type
- ✅ `ZIA-BUG-010` — async/await produces correct output
- ✅ `ZIA-BUG-011` — deinit binding propagation (fix landed)
- ✅ `ZIA-BUG-016` — superseded by ZIA-BUG-008 fix
- ✅ `ZIA-BUG-017` — superseded by ZIA-BUG-010 fix

### OPEN — Priority Fix Order
1. `BAS-BUG-002` BASIC `FOR EACH` off-by-one (P2)
