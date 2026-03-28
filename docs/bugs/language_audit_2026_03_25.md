# Language Audit — Validated Findings (2026-03-25)

This file was revalidated against the local compiler/runtime build in
`build/src/tools/viper/viper` and against the current parser, sema, and lowerer
sources on 2026-03-25.

The earlier version of this audit mixed real defects, stale findings, and a few
feature requests. This revision keeps only validated claims, marks invalid ones,
and adds concrete recommendations.

---

## Confirmed Zia Bugs

### ZIA-BUG-001: Guard clause narrowing fails for optional primitives at lowering
**Severity:** P1 — Breaks documented narrowing behavior

```zia
func check(x: Integer?) {
    guard x != null else { return; }
    SayInt(x);          // ERROR: expects i64 but got ptr
    var y: Integer = x; // ERROR: expected Integer, got Integer?
}
```

**Validated behavior**
- Reproduced with the local compiler.
- Narrowing works for optional class references in common cases.
- Narrowing does not carry through correctly for optional primitives like `Integer?`.

**Likely root cause**
- Sema records the narrowed type, but IL lowering still treats the guarded value as
  pointer-shaped optional storage instead of the primitive inner type.

**Recommendation**
- Fix lowering to consult the narrowed type for guarded locals before mapping IL type.
- Add compile+run regression tests for `Integer?`, `Number?`, and `Boolean?` after
  `guard x != null else { return; }`.

### ZIA-BUG-002: All enum variant values evaluate to 0 at runtime
**Severity:** P0
**Status:** FIXED (verified 2026-03-25)

Auto-increment enums now return correct values (0, 1, 2). Explicit values untested
but likely also fixed.

### ZIA-BUG-003: Match arms do not accept bare `return` after `=>`
**Severity:** P2 — Parser limitation

```zia
match c {
    Color.Red => return "red";  // ERROR: expected expression
}
```

**Validated behavior**
- Bare `return` after `=>` is still a parse error.
- Block arms and expression arms work.

**Recommendation**
- Either:
  - extend the parser so `matchArm` accepts a statement body after `=>`, or
  - keep the grammar as-is and document that `return` must be wrapped in a block:
    `Color.Red => { return "red"; }`.

### ZIA-BUG-004: Typed catch parses, but lowering produces invalid EH IL
**Severity:** P1 — Feature appears supported but fails verification

```zia
try {
    throw 1;
} catch(e: RuntimeError) {
    var x = 1;
}
```

**Validated behavior**
- `catch(e)` works.
- `catch(e: RuntimeError)` parses.
- Typed catch currently fails during verification with EH dominance errors.

**Important correction**
- The old audit claim that "error variable binding does not work" was too broad.
- Named catch binding with parentheses is implemented.
- The real defect is typed-catch lowering, not anonymous-vs-named catch in general.

**Likely root cause**
- The typed-catch control-flow shape in `Lowerer_Stmt_EH.cpp` creates handler-style
  blocks that violate current EH verifier dominance constraints.

**Recommendation**
- Rework typed-catch lowering so all protected blocks are dominated by the `eh.push`
  site and handler transitions satisfy verifier constraints.
- Add regression coverage for:
  - `catch(e)`
  - `catch(e: Error)`
  - `catch(e: RuntimeError)`
  - typed catch with `finally`

### ZIA-BUG-005: Child class `init` requires explicit `override`
**Severity:** P3 — Confirmed behavior, mostly a DX issue

```zia
class A { expose func init(x: Integer) {} }
class B extends A {
    expose func init(x: Integer) {} // ERROR: must be marked override
}
```

**Validated behavior**
- Current sema requires `override` for same-signature child `init`.

**Assessment**
- This is current language behavior, not an accidental compiler failure.
- It is still a reasonable ergonomics complaint because constructors are a special
  case users often expect to redefine without explicit override markup.

**Recommendation**
- Decide language policy explicitly:
  - if constructors are just methods, keep the behavior and document it clearly;
  - if constructors are a special form, relax the override rule for `init`.
- If behavior stays unchanged, add a targeted note to the inheritance docs.

### ZIA-BUG-006: Entity properties found but treated as private
**Severity:** P1 — Documented feature is non-functional on user entities

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

**Validated behavior (updated 2026-03-25)**
- Property declarations parse.
- Getter/setter methods are synthesized.
- Properties ARE found during member resolution (changed from earlier: no longer "no member").
- BUT: properties are treated as private regardless of visibility.

**Likely root cause**
- Property registration during sema sets visibility to private by default. The `property`
  keyword doesn't inherit or set an `expose` visibility flag.

**Recommendation**
- Allow `expose property count: Integer { ... }` or infer visibility from the class's
  default visibility context.
- Add compile+run tests for both get and set on user-defined entities.

### ZIA-BUG-007: Tuple destructuring in `var` declarations does not parse
**Severity:** P2 — Syntax advertised elsewhere but not implemented

```zia
func swap(a: Integer, b: Integer) -> (Integer, Integer) { return (b, a); }
var (x, y) = swap(1, 2);  // ERROR: expected variable name
```

**Validated behavior**
- Tuple return types work.
- `var (x, y) = expr` still fails to parse.

**Recommendation**
- Either implement tuple binding in `varDecl`, or remove any suggestion that it is
  supported and document `.0` / `.1` extraction as the current workaround.

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
**Status:** Invalid

**Validated behavior**
- `try { ... } finally { ... }` parses, lowers, and runs.
- There are also local tests for Zia and BASIC `finally`.

### ZIA-BUG-015: Map/Set/List not accessible via short bind
**Status:** Invalid

Same invalidation as ZIA-BUG-009.

### ZIA-MISSING-001: Catch variable binding entirely missing
**Status:** Invalid as written

**Validated behavior**
- `catch(e) { ... }` works.
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
  requires `override`.

**Recommendation**
- Decide this together with ZIA-BUG-005 and ZIA-BUG-012 as one constructor policy.

---

## Documentation Mismatches Found During Validation

### `docs/zia-reference.md`

Current doc drift:
- The try/catch section still says named binding is unsupported, but `catch(e)` works.
- The grammar still shows `tryStmt ::= "try" block ["catch" block] ["finally" block]`,
  which does not match the parser's current `catch(e)` / `catch(e: Type)` support.
- The tuple-destructuring limitation note is accurate.
- The enum runtime bug note is accurate.
- The OR-pattern and enum-variant-pattern documentation is present.

**Recommendation**
- Update the try/catch section to say:
  - `catch(e)` works
  - typed catch syntax parses but is currently broken at lowering
  - `catch e` without parentheses is not supported
- Update the grammar to match the actual parser.

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

## Status Summary (verified 2026-03-25)

### FIXED
- ✅ `ZIA-BUG-002` — enum values now correct (auto-increment works)
- ✅ `ZIA-BUG-008` — `String.ToUpper()` returns correct type
- ✅ `ZIA-BUG-010` — async/await produces correct output
- ✅ `ZIA-BUG-011` — deinit binding propagation (fix landed)
- ✅ `ZIA-BUG-016` — superseded by ZIA-BUG-008 fix
- ✅ `ZIA-BUG-017` — superseded by ZIA-BUG-010 fix

### OPEN — Priority Fix Order
1. `ZIA-BUG-001` optional primitive narrowing in lowering (P1)
2. `ZIA-BUG-006` class property access — private visibility (P1)
3. `ZIA-BUG-004` typed catch EH lowering (P1)
4. `ZIA-BUG-005` child init requires override (P3, design decision)
5. `ZIA-BUG-003` match arm bare return syntax (P2)
6. `ZIA-BUG-007` tuple destructuring in var (P2)
7. `BAS-BUG-002` BASIC `FOR EACH` off-by-one (P2)
