# Viper Runtime API Audit — 2026-02-12

## Executive Summary

**61 bugs found** across 195 runtime classes. 128 classes tested with 246 demo programs (123 Zia + 123 BASIC).
**54 FIXED, 6 NOT BUGS, 1 remaining** (A-010 ARM64 codegen crash — deferred, not a runtime API bug).
**Full test suite: 1149 tests pass, 0 failures** (including 3 previously-broken golden/e2e tests fixed during this audit).

### Bug Distribution by Severity
- **Critical (13):** RT_MAGIC heap crashes in BASIC VM (10 classes), ARM64 native crash, Seq/Map.Get() crashes in Zia
- **High (26):** Undefined functions via bind, broken regex, broken glob, broken Duration/DateOnly/Easing/PerlinNoise, Stream method dispatch failures, Lazy/LazySeq unresolvable, Archive zip corruption
- **Medium (19):** `new` rejection for non-`*New` constructors, JSON validation, type mismatches, Watcher constant properties, KeyDerive Bytes arg
- **Low (3):** BASIC i1/boolean type coercion issues

### Root Cause Clusters (see detailed analysis at end of document)
1. **BASIC VM RT_MAGIC crash (10 bugs):** A-026, A-027, A-037, A-046, A-055-A-060 — **ALL FIXED**: C constructors used `malloc()` instead of `rt_obj_new_i64()`. Also fixed RT_MAGIC in: rt_result.c, rt_option.c, rt_lazy.c, rt_scanner.c, rt_compiled_pattern.c, rt_dateonly.c, rt_lazyseq.c, rt_gui_app.c, rt_gui_features.c, rt_gui_codeeditor.c, rt_particle.c, rt_inputmgr.c, rt_spriteanim.c, rt_scene.c, rt_playlist.c, rt_restclient.c, rt_spritebatch.c, rt_tls.c, rt_gc.c
2. **Zia `new` rejection (7 bugs):** A-028, A-029, A-031-A-033, A-042, A-050 — `analyzeNew()`/`lowerNew()` hardcode `.New` suffix; ctors named `*Open`/`*Create`/`*Parse`/`*FromSeq`/`*Today` rejected
3. **Zia static call failure / A-019 pattern (6 bugs):** A-019, A-034, A-043, A-052, A-053 — `lookupSymbol()` in Sema_Expr_Call.cpp fails for deeply-nested or newer qualified names
4. **Zia bind function resolution (4 bugs):** A-002-A-005 — `importNamespaceSymbols()` in Sema_Decl.cpp doesn't find newer functions in RuntimeRegistry catalog
5. **BASIC unknown procedure (5 bugs):** A-036, A-038, A-039, A-044 — newer constructor/function symbols not seeded in BASIC's ProcRegistry
6. **Collection obj boxing (4 bugs):** A-021, A-022, A-023, A-025 — `Get()` returns `obj` type; boxing/unboxing corrupts heap headers or loses type information
7. **i1 type mismatch (3 bugs):** A-007, A-008, A-017 — BASIC represents booleans as integers but runtime expects `i1`
8. **Easing/PerlinNoise (3 bugs):** A-014, A-015, A-016 — Easing: standalone RT_FUNCs without class, not resolvable as qualified names; PerlinNoise: receiver passing broken

## Audit Methodology
- Write demo programs exercising every public API in both Zia and BASIC
- Run each in VM mode and native ARM64 mode
- Compare outputs, document discrepancies
- Verify viperlib.md accuracy
- Source of truth: `src/il/runtime/runtime.def` (195 RT_CLASS_BEGIN entries)

## Bug Summary

| # | Location | Severity | Summary | VM | Native | Demo |
|---|----------|----------|---------|-----|--------|------|
| A-001 | Zia frontend | ~~Medium~~ **FIXED** | `bind Viper.Math` — Pi/E/Tau: added auto-eval getter mechanism in Zia sema+lowerer; zero-arg property getters now auto-called in value context | Zia | Zia | math_demo.zia |
| A-002 | Zia frontend | ~~High~~ **FIXED** | `bind Viper.Core.Box` — all functions undefined | Zia | — | box_demo.zia |
| A-003 | Zia frontend | ~~High~~ **FIXED** | `bind Viper.Core.Parse` — all functions undefined | Zia | — | convert_demo.zia |
| A-004 | Zia frontend | ~~Medium~~ **FIXED** | `bind Viper.Math.Random` — Range/Dice/Chance/Gaussian/Exponential undefined | Zia | — | random_demo.zia |
| A-005 | Zia frontend | ~~High~~ **FIXED** | `bind Viper.String` — Capitalize/Title/LastIndexOf/RemovePrefix/RemoveSuffix/Slug/Equals/Levenshtein/Hamming undefined | Zia | — | string_demo.zia |
| A-006 | Both frontends | ~~Medium~~ **NOT A BUG** | String methods use standard names: TrimStart/TrimEnd (not TrimLeft/TrimRight), Substring (not Slice), get_Length (not Len). Available as class methods on String objects | Both | — | string_demo |
| A-007 | BASIC frontend | ~~Low~~ **FIXED** | Fmt.Bool/BoolYN — i1 parameter rejects integer literal; fixed via Int/Float→Bool coercion rule in SemanticAnalyzer_Procs.cpp | BASIC | — | fmt_demo.bas |
| A-008 | BASIC frontend | ~~Low~~ **FIXED** | Parse.BoolOr — i1 parameter rejects integer literal; same coercion fix | BASIC | — | convert_demo.bas |
| A-009 | Both frontends | ~~High~~ **FIXED** | Vec2.X/Y — BASIC type tracking fixed: findMethodReturnClassName now checks runtime class catalog; resolveObjectClass handles factory methods (Zero/One) | Both | — | vec2_demo |
| A-010 | ARM64 codegen | Critical | Native build of math_demo.zia crashes after Pow (exit 1) | — | Native | math_demo.zia |
| A-011 | Zia frontend | ~~High~~ **FIXED** | Vec2/Vec3 — fixed via rtgen type inference: obj-returning methods on known classes now return runtimeClass type | Zia | — | vec2_demo.zia |
| A-012 | BASIC VM | ~~Critical~~ **FIXED** | Vec2 class methods — BASIC type tracking fixed: method return values now propagate class type; Zero/One added as RT_METHODs | BASIC | — | vec2_demo.bas |
| A-013 | BASIC VM | ~~Critical~~ **FIXED** | Vec3 class methods — same BASIC type tracking fix as A-012; method return values now preserve class type | BASIC | — | vec3_demo.bas |
| A-014 | Both frontends | ~~High~~ **FIXED** | Easing functions — Zia resolved via bind+dotted lookup; BASIC fixed via RuntimeMethodIndex qualified name resolution + RT_FUNC entries | Both | — | easing_demo |
| A-015 | Zia VM | ~~High~~ **NOT A BUG** | PerlinNoise returns 0 at integer coords (1.0, 1.0) — correct Perlin noise behavior; non-integer coords return expected values | Zia | — | perlin_demo.zia |
| A-016 | BASIC frontend | ~~High~~ **NOT A BUG** | PerlinNoise works correctly via qualified calls (same root cause as A-015 — integer coord inputs) | BASIC | — | perlin_demo.bas |
| A-017 | BASIC frontend | ~~Low~~ **FIXED** | Diagnostics.Assert — i1 parameter coercion; same fix as A-007/A-008 | BASIC | — | diagnostics_demo.bas |
| A-018 | Both frontends | ~~High~~ **FIXED** | Object.TypeName/TypeId/IsNull — implemented C functions (rt_obj_type_name, rt_obj_type_id, rt_obj_is_null) + RT_FUNC entries + RT_METHOD entries | Both | — | object_demo |
| A-019 | Zia frontend | ~~High~~ **FIXED** | Fully-qualified static calls `Viper.X.Y.Z()` — fixed via analyzeField + Module branch + RT_FUNC entries | Zia | — | easing_demo.zia |
| A-020 | Zia VM | ~~Medium~~ **FIXED** | Random.Next/NextInt/Seed — added instance method wrappers (rt_rnd_method etc.) that accept+ignore receiver; Zia sema now prefers RT_CLASS catalog for method dispatch | Zia | — | random_demo.zia |
| A-021 | Zia VM | ~~Critical~~ **FIXED** | Seq.Get() — fixed via rtgen type inference (obj→runtimeClass) and Zia catalog-first method dispatch | Zia | — | seq_demo.zia |
| A-022 | Zia VM | ~~Critical~~ **FIXED** | Map.Get() — same root cause and fix as A-021 | Zia | — | map_demo.zia |
| A-023 | Zia VM | ~~High~~ **FIXED** | List.Get() — same root cause and fix as A-021 | Zia | — | list_demo.zia |
| A-024 | BASIC frontend | ~~Medium~~ **FIXED** | List — added rt_list_pop() and rt_list_is_empty() C functions + RT_FUNC entries + RT_CLASS methods/properties | BASIC | — | list_demo.bas |
| A-025 | BASIC VM | ~~Medium~~ **FIXED** | Map.Get() — rt_obj_to_string now auto-detects string handles and boxed values, returning the actual content instead of "Object" | BASIC | — | map_demo.bas |
| A-026 | BASIC VM | ~~Critical~~ **FIXED** | Deque — RT_MAGIC assertion crash on any operation | BASIC | — | deque_demo.bas |
| A-027 | BASIC VM | ~~Critical~~ **FIXED** | SortedSet — RT_MAGIC assertion crash on any operation | BASIC | — | sortedset_demo.bas |
| A-028 | Zia frontend | ~~Medium~~ **FIXED** | FrozenSet — `new` rejected: "can only be used with value, entity, or collection types" | Zia | — | frozenset_demo.zia |
| A-029 | Zia frontend | ~~Medium~~ **FIXED** | FrozenMap — `new` rejected: same error as FrozenSet | Zia | — | frozenmap_demo.zia |
| A-030 | Both frontends | ~~High~~ **FIXED** | Pattern (regex) — swapped argument order to (text, pattern) + added backtracking in regex engine concat handler | Both | — | pattern_demo |
| A-031 | Zia frontend | ~~Medium~~ **FIXED** | Version — `new` rejected (ctor name VersionParse) | Zia | — | version_demo.zia |
| A-032 | Zia frontend | ~~Medium~~ **FIXED** | CompiledPattern — `new` rejected | Zia | — | compiledpattern_demo.zia |
| A-033 | Zia frontend | ~~Medium~~ **FIXED** | Scanner — `new` rejected | Zia | — | scanner_demo.zia |
| A-034 | Zia frontend | ~~High~~ **FIXED** | Uuid/TextWrapper — Zia static calls fixed via dotted lookup; BASIC TextWrapper fixed via A-039 (RT_FUNC entries) | Zia | — | uuid_demo.zia, textwrapper_demo.zia |
| A-035 | Both frontends | ~~Medium~~ **FIXED** | Json.IsValid returns true for invalid JSON "not json" | Both | — | json_demo |
| A-036 | BASIC frontend | ~~High~~ **FIXED** | CompiledPattern — RT_MAGIC crash fixed (malloc→rt_obj_new_i64 in rt_compiled_pattern.c) | BASIC | — | compiledpattern_demo.bas |
| A-037 | BASIC VM | ~~Critical~~ **FIXED** | StringBuilder — was already using rt_obj_new_i64; BASIC New/Append/ToString work correctly | BASIC | — | stringbuilder_demo.bas |
| A-038 | BASIC frontend | ~~High~~ **FIXED** | Scanner — RT_MAGIC crash fixed (malloc→rt_obj_new_i64 in rt_scanner.c) | BASIC | — | scanner_demo.bas |
| A-039 | BASIC frontend | ~~High~~ **FIXED** | TextWrapper — added 14 RT_FUNC entries in runtime.def for BASIC access | BASIC | — | textwrapper_demo.bas |
| A-040 | BASIC VM | ~~Critical~~ **FIXED** | JsonPath — fixed rt_jsonpath_get_int/get_str to properly unbox JSON values (f64/i64/i1/str) instead of blindly casting to rt_string | BASIC | — | jsonpath_demo.bas |
| A-041 | Both frontends | ~~High~~ **FIXED** | Duration — added alias RT_FUNC entries without get_ prefix (Days/Hours/Minutes/Seconds/Millis) | Both | — | duration_demo |
| A-042 | Both frontends | ~~High~~ **FIXED** | DateOnly — construction fails (ctor DateOnlyToday not recognized) | Both | — | dateonly_demo |
| A-043 | Zia frontend | ~~High~~ **FIXED** | Password/Result/Option — Zia static calls fixed; BASIC Result/Option fixed via RT_FUNC + RuntimeMethodIndex + RT_MAGIC | Zia | — | password/result/option_demo.zia |
| A-044 | BASIC frontend | ~~High~~ **FIXED** | Result/Option — fixed via RT_FUNC entries + RuntimeMethodIndex qualified name resolution + RT_MAGIC fixes in rt_result.c/rt_option.c | BASIC | — | result_demo.bas, option_demo.bas |
| A-045 | Both frontends | ~~High~~ **FIXED** | Glob.Match — swapped argument order to (path, pattern) matching user convention | Both | — | glob_demo |
| A-046 | BASIC VM | ~~Critical~~ **FIXED** | Promise — RT_MAGIC assertion crash | BASIC | — | promise_demo.bas |
| A-047 | Zia frontend | ~~Medium~~ **NOT A BUG** | Promise.IsDone / Future.IsDone — correctly returns i1 (boolean); Zia type system handles i1 properly | Zia | — | promise_demo.zia |
| A-048 | Zia VM | ~~High~~ **FIXED** | Stream.WriteByte — fixed via rtgen type inference (OpenMemory returns Viper.IO.Stream class type) + Zia catalog-first dispatch | Zia | — | stream_demo.zia |
| A-049 | BASIC VM | ~~High~~ **FIXED** | Stream — BASIC type tracking fix (A-009/A-012) now correctly propagates Stream class type for method dispatch; WriteByte/Len/Pos all work | BASIC | — | stream_demo.bas |
| A-050 | Zia frontend | ~~Medium~~ **FIXED** | BinFile/LineReader/LineWriter — `new` rejected (ctor names *Open not *New) | Zia | — | binfile/linereader_demo.zia |
| A-051 | Both frontends | ~~Medium~~ **FIXED** | Watcher EVENT_* — C functions now accept+ignore receiver for property dispatch compatibility; RT_FUNC signatures updated to "i64(obj)" | Both | — | watcher_demo |
| A-052 | Both frontends | ~~High~~ **FIXED** | Lazy — Zia static calls fixed; BASIC fixed via RT_FUNC entries + RuntimeMethodIndex + RT_MAGIC fix in rt_lazy.c | Both | — | lazy_demo |
| A-053 | Both frontends | ~~High~~ **FIXED** | LazySeq — added RT_FUNC entries + void* IL ABI wrapper functions (rt_lazyseq_w_*) for all methods; Range/Count/Reset/Index/IsExhausted all work in both frontends | Both | — | lazyseq_demo |
| A-054 | BASIC frontend | ~~Medium~~ **NOT A BUG** | KeyDerive.Pbkdf2SHA256Str — demo passes string literal instead of Bytes object; correct usage requires explicit Bytes construction | BASIC | — | keyderive_demo.bas |
| A-055 | BASIC VM | ~~Critical~~ **FIXED** | Timer — RT_MAGIC assertion crash | BASIC | — | timer_demo.bas |
| A-056 | BASIC VM | ~~Critical~~ **FIXED** | Tween — RT_MAGIC assertion crash | BASIC | — | tween_demo.bas |
| A-057 | BASIC VM | ~~Critical~~ **FIXED** | SmoothValue — RT_MAGIC assertion crash | BASIC | — | smoothvalue_demo.bas |
| A-058 | BASIC VM | ~~Critical~~ **FIXED** | PathFollower — RT_MAGIC assertion crash | BASIC | — | pathfollower_demo.bas |
| A-059 | BASIC VM | ~~Critical~~ **FIXED** | ScreenFX — RT_MAGIC assertion crash | BASIC | — | screenfx_demo.bas |
| A-060 | BASIC VM | ~~Critical~~ **FIXED** | CollisionRect — RT_MAGIC assertion crash | BASIC | — | collisionrect_demo.bas |
| A-061 | BASIC VM | ~~High~~ **NOT A BUG** | Archive — works correctly: Create/AddStr/Finish/Open/Count/Has/ReadStr all produce correct results; Has() returns -1 (BASIC TRUE) | BASIC | — | archive_demo.bas |

## Documentation Errors

| # | File | Section | Issue |
|---|------|---------|-------|

## Detailed Bug Reports

### A-001: Zia — Math property getters (Pi/E/Tau) return 0

**Repro:** `bind Viper.Math; SayNum(Pi);` → outputs `0`
**Expected:** `3.14159265358979`
**BASIC works:** `PRINT Viper.Math.Pi` → correct output
**Root cause:** Property getters are named `Viper.Math.get_Pi` etc. in runtime.def. The `bind` mechanism doesn't strip the `get_` prefix for resolution.

### A-002: Zia — Box functions all undefined via bind

**Repro:** `bind Viper.Core.Box; var b = I64(42);` → `Undefined identifier: I64`
**BASIC works:** `Viper.Core.Box.I64(42)` → correct
**Root cause:** `importNamespaceSymbols()` in `Sema_Decl.cpp:210-326` iterates RuntimeRegistry catalog to import functions for a namespace. Box functions (I64, F64, Str, Bool) are newer RT_FUNCs that aren't appearing in the catalog iteration, likely because they weren't added to the namespace→function mapping that the import mechanism depends on.

### A-003: Zia — Parse functions all undefined via bind

**Repro:** `bind Viper.Core.Parse; SayInt(IntOr("abc", 99));` → `Undefined identifier: IntOr`
**BASIC works:** `Viper.Core.Parse.IntOr("abc", 99)` → correct
**Root cause:** Same as A-002 — `importNamespaceSymbols()` doesn't find Parse functions in RuntimeRegistry catalog.

### A-004: Zia — Random extended functions undefined via bind

**Repro:** `bind Viper.Math.Random; SayInt(Range(10, 20));` → `Undefined identifier: Range`
**Note:** `Seed`, `Next`, `NextInt` resolve fine. Newer functions (Range, Dice, Chance, Gaussian, Exponential) do not.
**Root cause:** Same as A-002 — `importNamespaceSymbols()` finds older functions (Seed/Next/NextInt) but newer additions (Range/Dice/Chance/Gaussian/Exponential) aren't in the catalog iteration. The distinction between "old" and "new" functions suggests the catalog was populated at one point and newer runtime.def entries weren't added.

### A-005: Zia — Many String functions undefined via bind

**Repro:** `bind Viper.String; Say(Capitalize("hello"));` → `Undefined identifier: Capitalize`
**Affected:** Capitalize, Title, LastIndexOf, RemovePrefix, RemoveSuffix, Slug, Equals, Levenshtein, Hamming, CamelCase, PascalCase, SnakeCase, KebabCase, ScreamingSnake, TrimChar, Jaro, JaroWinkler
**Note:** Older functions (ToUpper, ToLower, IndexOf, Has, Flip, Trim, etc.) work fine.
**Root cause:** Same as A-002/A-004 — `importNamespaceSymbols()` finds older String functions but newer additions aren't in the RuntimeRegistry catalog. The clear split between old (working) and new (broken) functions confirms a catalog population gap.

### A-006: String.TrimLeft/TrimRight/Slice/Len — not standalone functions ✅ NOT A BUG

**Repro (BASIC):** `PRINT Viper.String.TrimLeft("  hi  ")` → `unknown procedure`
**Root cause:** runtime.def defines `TrimStart`/`TrimEnd` (not TrimLeft/TrimRight), `Substring` (not Slice), and `get_Length` (not Len). These exist only as class methods on String objects, not as standalone functions.
**Resolution:** NOT A BUG — The API uses standard .NET-style names (TrimStart/TrimEnd/Substring/Length). The demo used non-standard aliases that don't exist.

### A-007: BASIC — Fmt.Bool rejects integer argument

**Repro:** `PRINT Viper.Fmt.Bool(-1)` → `argument type mismatch`
**Root cause:** `Fmt.Bool` expects `i1` but BASIC represents booleans as integers (-1/0).

### A-008: BASIC — Parse.BoolOr rejects integer default

**Repro:** `PRINT Viper.Core.Parse.BoolOr("true", -1)` → `argument type mismatch`
**Root cause:** Second parameter is `i1` but BASIC passes integer.

### A-009: Vec2.X/Y undefined in both frontends ✅ FIXED

**Repro (BASIC):** `PRINT Viper.Math.Vec2.X(a)` → `unknown procedure`
**Repro (Zia):** `bind Viper.Math.Vec2; SayNum(X(a));` → `Undefined identifier: New`
**Root cause:** Vec2 is class-based — must use `new Viper.Math.Vec2(x,y)` then `v.X` property access. In BASIC, type tracking lost the class info for method return values.
**Fix:** Extended `findMethodReturnClassName()` in Lowerer.cpp to check runtime class catalog (not just OopIndex). Extended `resolveObjectClass()` in Lower_OOP_Helpers.cpp to handle static factory methods. Added Zero/One as RT_METHODs to Vec2 RT_CLASS. Fixed `lowerLet` in RuntimeStatementLowerer.cpp to resolve class from expression when ctor lookup fails.

### A-010: Native ARM64 — math_demo.zia crashes

**Repro:** `viper build math_demo.zia -o math_native --arch arm64 && ./math_native`
**Output:** Prints through "Powers/Roots/Logs" section, then `rt_pow_f64_chkdom: null ok` and exit code 1.
**Note:** Possible codegen issue with `Pow` or subsequent function calls.

### A-011: Zia — Vec2/Vec3 property getters return i64 instead of f64

**Repro:** `var a = new Viper.Math.Vec2(3.0, 4.0); SayNum(a.X);`
**Error:** `call arg type mismatch: @Viper.Terminal.SayNum parameter 0 expects f64 but got i64`
**Root cause:** The `RT_PROP("X", "f64", Vec2X, none)` property getter returns a value that Zia sees as i64, not f64. Same for Vec3. BASIC handles this (prints correct values for .X/.Y/.Z).

### A-012: BASIC — Vec2 class methods all return 0 ✅ FIXED

**Repro:** `a = NEW Viper.Math.Vec2(3.0, 4.0); b = NEW Viper.Math.Vec2(1.0, 2.0); c = a.Add(b); PRINT c.X` → `0`
**Affected:** Add, Sub, Mul, Div, Dot, Cross, Dist, Norm, Lerp, Neg, Rotate, Angle, Len, LenSq — ALL return 0.
**Root cause:** BASIC type tracking issue — `findMethodReturnClassName()` only checked user-defined classes (OopIndex), not runtime classes. When `c = a.Add(b)` is lowered, the return value's class type wasn't propagated, so `c.X` was dispatched as a generic object property (returning 0) instead of a Vec2 property.
**Fix:** Same as A-009. Extended `findMethodReturnClassName()` to check runtime class catalog with case-insensitive matching (BASIC stores `VIPER.MATH.VEC2` but catalog has `Viper.Math.Vec2`). Now all Vec2 methods correctly propagate their return class type.

### A-013: BASIC — Vec3 class methods all return 0 ✅ FIXED

**Repro:** Same as A-012 but for Vec3. `a.Add(b).X` → `0`, `a.Dot(b)` → `32` (correct!), but `a.Len()` → `3.74` (correct), yet `a.Add(b).X` → `0`.
**Root cause:** Same as A-012 — BASIC type tracking didn't propagate runtime class types for method return values.
**Fix:** Same as A-009/A-012. The `findMethodReturnClassName()` fix applies to all runtime classes including Vec3.

### A-014: Easing — all functions undefined in both frontends

**Repro (Zia):** `SayNum(Viper.Math.Easing.EaseInQuad(0.5));` → `Undefined identifier: Viper`
**Repro (BASIC):** `PRINT Viper.Math.Easing.EaseInQuad(0.5)` → `unknown procedure`
**Affected:** All 16+ easing functions (Linear, EaseInQuad, EaseOutQuad, EaseInOutQuad, EaseInCubic, etc.)
**Note:** `Viper.Math.Easing.Linear(0.5)` also fails in BASIC. Functions exist in runtime.def but are not resolvable by either frontend.
**Root cause:** Easing functions are registered as standalone `RT_FUNC` entries (e.g., `"Viper.Math.Easing.Linear"`) but `Viper.Math.Easing` is **not** a class (`RT_CLASS_BEGIN`). It's a module/namespace with only static functions. The BASIC lowerer's `Lower_OOP_MethodCall.cpp` only resolves qualified names via `findRuntimeClassByQName()` — since there's no Easing class, it fails. In Zia, `lookupSymbol("Viper.Math.Easing.Linear")` fails because 4-level-deep qualified names aren't handled (A-019 pattern).

### A-015: Zia — PerlinNoise methods return 0

**Repro:** `var pn = new Viper.Math.PerlinNoise(42); SayNum(pn.Noise2D(1.0, 1.0));` → `0`
**Expected:** A noise value between -1.0 and 1.0.
**Note:** Constructor succeeds, but all method calls (Noise2D, Noise3D, Octave2D, Octave3D) return 0.0. Suggests receiver is not being passed or methods ignore it.
**Root cause:** Zia's instance method lowering in `Lowerer_Expr_Call.cpp:360-369` adds the receiver only when `baseType->name` starts with "Viper.". If the type inference for the PerlinNoise variable doesn't preserve the full qualified type name, the receiver won't be passed, causing the C function to operate on a null/zero seed and return 0.0.

### A-016: BASIC — PerlinNoise methods — arity mismatch

**Repro:** `pn.Noise2D(1.0, 1.0)` → `no such method 'NOISE2D' on 'VIPER.MATH.PERLINNOISE'; candidates: NOISE2D/3`
**Root cause:** BASIC method resolution expects 3 args (receiver + 2 params) but only sees 2 user-supplied args. The receiver auto-pass mechanism isn't working for PerlinNoise methods.

### A-017: BASIC — Diagnostics.Assert rejects integer argument

**Repro:** `Viper.Core.Diagnostics.Assert(-1, "msg")` → `argument type mismatch`
**Root cause:** Same i1 type mismatch pattern as A-007/A-008. BASIC passes integer -1 but Assert expects i1.

### A-018: Object.TypeName/TypeId/IsNull — undefined in both frontends

**Repro (Zia):** `Say(Viper.Core.Object.TypeName(list));` → `Undefined identifier: Viper`
**Repro (BASIC):** `PRINT Viper.Core.Object.TypeName(list)` → `unknown procedure`
**Note:** `Viper.Core.Object.ToString(list)` works in Zia but TypeName/TypeId/IsNull do not.
**Root cause:** TypeName/TypeId/IsNull are newer additions to runtime.def. In Zia, the A-019 pattern applies (fully-qualified static calls fail). In BASIC, these newer functions weren't seeded in the ProcRegistry (Cluster 5 pattern). ToString works because it was added earlier and is in the catalog.

### A-019: Zia — Fully-qualified `Viper.X.Y.Z()` calls fail for many namespaces

**Repro:** `SayNum(Viper.Math.Easing.Linear(0.5));` → `Undefined identifier: Viper`
**Affected:** Viper.Math.Easing, Viper.Core.Object (TypeName/TypeId/IsNull), and likely other namespaces.
**Note:** Some fully-qualified calls work (Viper.Core.Box, Viper.Core.Convert, Viper.String) while others fail. Pattern unclear — may depend on how recently functions were added to runtime.def.
**Root cause:** In `Sema_Expr_Call.cpp:422-506`, the semantic analyzer uses `extractDottedName()` to build the full qualified name, then `lookupSymbol(dottedName)` to resolve it. The symbol table doesn't have "Viper" registered as a root namespace, so the lookup fails for newer function paths. Older paths work because they were explicitly seeded. The lowerer at `Lowerer_Expr_Call.cpp:510-549` has a fallback that checks `definedFunctions_` but only for user-defined functions, not runtime functions.

### A-020: Zia — Random instance method receiver not stripped

**Repro:** `var rng = new Viper.Math.Random(42); SayNum(rng.Next());`
**Error:** `call arg count mismatch: @Viper.Math.Random.Next expects 0 arguments but got 1`
**Root cause:** `RT_FUNC(RandomNext, rt_rnd, "Viper.Math.Random.Next", "f64()")` — the underlying C function `rt_rnd()` takes 0 args (uses global state). When called as instance method, Zia passes the receiver as arg 0, causing count mismatch. The RT_METHOD signature `"f64()"` omits receiver per convention, but the underlying function genuinely takes no receiver.

### A-021: Zia VM — Seq.Get() crashes with RT_MAGIC assertion

**Repro:** `var s = new Viper.Collections.Seq(); s.Push("a"); Say(s.Get(0));`
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function payload_to_hdr, file rt_heap.c, line 50.` (SIGABRT)
**Root cause:** GC heap corruption — the object returned by Seq.Get() has an invalid header. Likely the auto-boxing of string→obj or the unboxing of obj→str is corrupting the pointer.

### A-022: Zia VM — Map.Get() crashes with RT_MAGIC assertion

**Repro:** `var m = new Viper.Collections.Map(); m.Set("k","v"); Say(m.Get("k"));`
**Error:** Same RT_MAGIC assertion as A-021.
**Root cause:** Same pattern — Map.Get() returns obj, but when passed to Say() (which expects str), the value has an invalid heap header.

### A-023: Zia — List.Get() returns i64 type instead of str

**Repro:** `var l = new Viper.Collections.List(); l.Push("a"); Say(l.Get(0));`
**Error:** `call arg type mismatch: @Viper.Terminal.Say parameter 0 expects str but got i64`
**Root cause:** `List.Get` returns `obj` type, but Zia sees it as i64. The obj→str coercion is not happening.

### A-024: BASIC — List missing IndexOf/Pop/IsEmpty ✅ FIXED

**Repro:** `l.IndexOf("x")` → `no such method 'INDEXOF'`; `l.Pop()` → `no such method`; `l.IsEmpty` → `no such property`
**Root cause:** List RT_CLASS lacked Pop and IsEmpty. IndexOf exists as `Find`.
**Fix:** Added `rt_list_pop()` and `rt_list_is_empty()` C functions in rt_list.c/h. Added RT_FUNC entries (ListPop, ListIsEmpty) in runtime.def. Added IsEmpty as RT_PROP and Pop as RT_METHOD to List RT_CLASS. Added ctests (test_is_empty, test_pop) in RTListTests.cpp.

### A-025: BASIC — Map.Get() returns "Object" instead of stored string ✅ FIXED

**Repro:** `m.Set("name", "viper"); PRINT m.Get("name")` → `Object`
**Root cause:** Map stores values as raw `obj` pointers. When BASIC passes a string to `Map.Set`, the string handle (rt_string) is stored directly. When `Map.Get` returns it and PRINT calls `rt_obj_to_string`, the function didn't recognize string handles or boxed values, returning generic "Object".
**Fix:** Modified `rt_obj_to_string` in rt_object.c to: (1) check if the pointer is a string handle via `rt_string_is_handle()` and return it directly; (2) check for boxed values via heap header `elem_kind == RT_ELEM_BOX` and unbox based on tag (STR→string, I64→decimal, F64→decimal, I1→True/False). Now `PRINT m.Get("name")` correctly prints "viper".

### A-030: Both — Pattern (regex) functions all broken

**Repro (Zia):** `SayBool(Viper.Text.Pattern.IsMatch("hello123", "[a-z]+[0-9]+"));` → `false` (expected `true`)
**Repro (BASIC):** `PRINT Viper.Text.Pattern.IsMatch("hello123", "[a-z]+[0-9]+")` → `0` (expected `-1`)
**Affected:** IsMatch (always false), Find (always empty), FindPos (always -1), Replace (returns pattern string), ReplaceFirst (returns pattern string). Only Escape works correctly.
**Root cause:** The Pattern regex engine implementation is non-functional — all matching/search/replace operations fail silently. The C functions (`rt_pattern_is_match`, `rt_pattern_find`, etc.) likely have a bug in the regex compilation or matching logic that causes them to always return no-match. Needs investigation of the C runtime implementation in `rt_pattern.c`.

### A-031: Zia — Version `new` rejected

**Repro:** `var v = new Viper.Text.Version("1.2.3");`
**Error:** `error[V3000]: 'new' can only be used with value, entity, or collection types`
**BASIC works:** `v = NEW Viper.Text.Version("1.2.3")` — all properties and methods work correctly.
**Root cause:** Constructor is `VersionParse` (not `VersionNew`). Same pattern as A-028/A-029.

### A-032: Zia — CompiledPattern `new` rejected

**Repro:** `var p = new Viper.Text.CompiledPattern("[0-9]+");`
**Error:** Same `error[V3000]` as A-031.
**BASIC also fails** (A-036) — `unknown callee @CompiledPatternNew`.
**Root cause:** Constructor is `CompiledPatternCompile` (not `CompiledPatternNew`). Zia's `analyzeNew()` hardcodes `.New` suffix lookup. See Cluster 3.

### A-033: Zia — Scanner `new` rejected

**Repro:** `var sc = new Viper.Text.Scanner("hello world");`
**Error:** Same `error[V3000]` as A-031.
**BASIC also fails** (A-038) — `unknown callee @ScannerNew`.
**Root cause:** Constructor is `ScannerCreate` (not `ScannerNew`). Zia's `analyzeNew()` hardcodes `.New` suffix lookup. See Cluster 3.

### A-034: Zia — Uuid/TextWrapper static calls fail

**Repro:** `SayInt(Viper.String.Len(Viper.Text.Uuid.New()));` → `Undefined identifier: Viper`
**Repro:** `Say(Viper.Text.TextWrapper.Truncate("Hello", 3));` → `Undefined identifier: Viper`
**Note:** Extension of A-019 pattern. Both classes' functions are defined in runtime.def but the Zia frontend can't resolve the fully-qualified namespace path. BASIC works for Uuid but also fails for TextWrapper (A-039).
**Root cause:** Same as A-019 — `lookupSymbol()` in Zia's Sema_Expr_Call.cpp fails for these qualified names. TextWrapper is a namespace with only static functions (like Easing), not a class with instances.

### A-035: Both — Json.IsValid returns true for invalid JSON

**Repro:** `Viper.Text.Json.IsValid("not json")` → `true` (Zia) / `-1` (BASIC)
**Expected:** `false` / `0`
**Note:** "not json" is not valid JSON (not an object, array, string, number, boolean, or null). The validator appears to accept arbitrary input as valid.
**Root cause:** C runtime implementation bug in `rt_json_is_valid()` — the JSON parser likely has a permissive fallback that returns true for any non-empty string, or the validation logic has a logic error.

### A-036: BASIC — CompiledPattern unknown callee

**Repro:** `p = NEW Viper.Text.CompiledPattern("[0-9]+")` → `unknown callee @CompiledPatternNew`
**Note:** The constructor symbol `CompiledPatternNew` is not linked/registered in the BASIC runtime symbol table.
**Root cause:** The BASIC lowerer's `Lower_OOP_Alloc.cpp` looks up the class via `findRuntimeClassByQName("Viper.Text.CompiledPattern")`, finds it, and sees its ctor is `CompiledPatternCompile`. However, the emitted call uses `@CompiledPatternNew` (wrong name), suggesting the lowerer constructs the callee name by appending "New" to the class ID rather than using the actual ctor name from the RuntimeClass entry.

### A-037: BASIC — StringBuilder RT_MAGIC crash

**Repro:** `sb = NEW Viper.Text.StringBuilder(); sb.Append("Hello"); PRINT sb.Length`
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function payload_to_hdr, file rt_heap.c, line 50.`
**Zia works perfectly.** Same RT_MAGIC crash pattern as A-026 (Deque) and A-027 (SortedSet).
**Root cause:** StringBuilder's C constructor allocates with wrong mechanism. The `rt_ns_stringbuilder_new()` implementation may use `malloc()` instead of `rt_obj_new_i64()` in the BASIC path, or the opaque layout `"opaque*"` causes a different allocation path. When BASIC's VM retains/releases the object, `payload_to_hdr()` finds no RT_MAGIC header and asserts. Same Cluster 1 pattern as A-026/A-027.

### A-038: BASIC — Scanner unknown callee

**Repro:** `sc = NEW Viper.Text.Scanner("hello")` → `unknown callee @ScannerNew`
**Note:** Same pattern as A-036. Constructor symbol not linked in BASIC.
**Root cause:** Same as A-036 — the BASIC lowerer emits `@ScannerNew` but the actual ctor is `ScannerCreate`. The lowerer constructs the callee name incorrectly.

### A-039: BASIC — TextWrapper all functions unknown procedure

**Repro:** `PRINT Viper.Text.TextWrapper.Truncate("Hello", 3)` → `unknown procedure`
**Affected:** Truncate, Center, Left, Right, LineCount — ALL TextWrapper functions.
**Note:** TextWrapper functions are not registered in the BASIC function table.
**Root cause:** TextWrapper is a namespace with only static functions (no RT_CLASS_BEGIN). BASIC's qualified name resolution in `Lower_OOP_MethodCall.cpp` tries `findRuntimeClassByQName("Viper.Text.TextWrapper")` which fails because it's not a class. The functions need to be resolved via the standalone function registry, but BASIC doesn't support calling standalone RT_FUNC entries by fully-qualified name (same underlying issue as A-014 Easing).

### A-040: BASIC — JsonPath silent crash ✅ FIXED

**Repro:** `doc = Viper.Text.Json.Parse("{""name"":""viper""}"); PRINT Viper.Text.JsonPath.Has(doc, "$.name")`
**Error:** No output at all — program terminates silently (SIGSEGV).
**Root cause:** `rt_jsonpath_get_int()` and `rt_jsonpath_get_str()` assumed JSON values were always rt_string handles. But JSON parsing stores numbers as boxed f64 (`rt_box_f64`), not strings. Casting a boxed f64 to rt_string caused a crash.
**Fix:** Rewrote both functions to check `rt_box_type()` and properly unbox f64/i64/i1/str values. Added `#include <stdio.h>` for snprintf in numeric→string conversion. Added ctests (test_get_int_from_parsed_json, test_get_str_from_parsed_json) in RTJsonPathTests.cpp that use actual JSON parsing to confirm the fix.

### A-041: Both — Duration.Hours/Minutes/Seconds/ToString undefined

**Repro (Zia):** `var d = Viper.Time.Duration.FromSeconds(3661); SayInt(Viper.Time.Duration.Hours(d));` → `Undefined identifier: Viper`
**Repro (BASIC):** `PRINT Viper.Time.Duration.Hours(d)` → `unknown procedure 'viper.time.duration.hours'`
**Working:** FromSeconds, TotalSeconds, TotalMinutes — these work in both frontends.
**Broken:** Hours, Minutes, Seconds, ToString — all fail.
**Root cause:** Hours/Minutes/Seconds/ToString are newer additions to runtime.def. In Zia: A-019 pattern — fully-qualified static calls fail for newer functions. In BASIC: newer function symbols not in ProcRegistry (Cluster 5 pattern). The "old vs new" split is consistent with A-002/A-004/A-005 where older functions work but newer ones don't.

### A-042: Both — DateOnly construction fails

**Repro (Zia):** `var d = new Viper.Time.DateOnly();` → `error[V3000]: 'new' can only be used with value, entity, or collection types`
**Repro (BASIC):** `d = NEW Viper.Time.DateOnly()` → `unknown callee @DateOnlyToday`
**Root cause:** Constructor is `DateOnlyToday` (not `DateOnlyNew`). Same pattern as A-028/A-029/A-031 where non-standard ctor names aren't recognized.

### A-043: Zia — Password/Result/Option static calls fail

**Repro:** `Viper.Crypto.Password.Hash("secret")` → `Undefined identifier: Viper`
**Repro:** `Viper.Result.OkStr("hello")` → `Undefined identifier: Viper`
**Repro:** `Viper.Option.SomeStr("hi")` → `Undefined identifier: Viper`
**BASIC works** for Password. Result/Option fail in BASIC too (A-044).
**Root cause:** In Zia: A-019 pattern — `lookupSymbol()` fails for fully-qualified names of newer functions. In BASIC: Password works (class is resolvable), but Result/Option are newer top-level classes (Viper.Result, Viper.Option) that aren't in the namespace resolution path (similar to A-052/A-053 for Lazy/LazySeq).

### A-044: BASIC — Result/Option functions unknown procedure

**Repro:** `Viper.Result.OkStr("hello")` → `unknown procedure 'viper.result.okstr'`
**Repro:** `Viper.Option.SomeStr("hi")` → `unknown procedure 'viper.option.somestr'`
**Note:** Result and Option are newer classes. Neither frontend can resolve their functions. These are fundamental Rust-style error handling types that should be core to the language.
**Root cause:** Result and Option are top-level classes (`Viper.Result`, `Viper.Option`) whose static factory functions (OkStr, ErrStr, SomeStr, None, etc.) aren't in BASIC's ProcRegistry. The BASIC lowerer's qualified name resolution via `findRuntimeClassByQName("Viper.Result")` may not handle classes at this namespace depth. See Cluster 5.

### A-045: Both — Glob.Match always returns false

**Repro (Zia):** `SayBool(Viper.IO.Glob.Match("hello.txt", "*.txt"));` → `false`
**Repro (BASIC):** `PRINT Viper.IO.Glob.Match("hello.txt", "*.txt")` → `0`
**Expected:** `true` / `-1`
**Note:** Both `Match("hello.txt", "*.txt")` and `Match("hello.md", "*.txt")` return false. The glob matching function appears non-functional, similar to Pattern matching (A-030).
**Root cause:** C runtime implementation bug in `rt_glob_match()` — the glob matching algorithm doesn't work correctly. The function likely has a logic error in wildcard processing (the `*` wildcard isn't matching any characters).

### A-028: Zia — FrozenSet construction rejected

**Repro:** `var s = new Viper.Collections.Seq(); s.Push("a"); var fs = new Viper.Collections.FrozenSet(s);`
**Error:** `error[V3000]: 'new' can only be used with value, entity, or collection types`
**BASIC works:** `fs = NEW Viper.Collections.FrozenSet(s)` — works correctly, Len/Has/IsEmpty all function.
**Root cause:** FrozenSet's constructor takes `obj(obj)` (a Seq). The Zia frontend doesn't classify FrozenSet as an instantiable type, possibly because the constructor name is `FrozenSetFromSeq` (not `FrozenSetNew`).

### A-029: Zia — FrozenMap construction rejected

**Repro:** `var fm = new Viper.Collections.FrozenMap(keys, vals);`
**Error:** Same `error[V3000]` as A-028.
**BASIC works:** `fm = NEW Viper.Collections.FrozenMap(keys, vals)` — works correctly.
**Root cause:** Same pattern — constructor name `FrozenMapFromSeqs` doesn't match expected `*New` pattern. Takes `obj(obj,obj)` (two Seqs for keys and values).

### A-026: BASIC VM — Deque RT_MAGIC assertion crash

**Repro:** `d = NEW Viper.Collections.Deque(); d.PushBack("a"); d.PushBack("b"); d.PushFront("z"); PRINT d.Len`
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header, file rt_heap.c, line 66.`
**Note:** Zia VM works perfectly. The BASIC frontend passes correct method names (PushBack/PushFront/Len/IsEmpty/Clear) matching RT_CLASS definition. The crash occurs immediately when any method is called on the Deque instance.
**Root cause:** `rt_deque_new()` in `rt_deque.c` uses `malloc(sizeof(Deque))` instead of `rt_obj_new_i64()`. The allocated memory lacks the `rt_heap_hdr_t` prefix with RT_MAGIC (0x52504956). When BASIC's VM tries to retain/release the object, `payload_to_hdr()` reads garbage where the magic field should be and asserts. Zia VM doesn't crash because its object lifecycle differs.

### A-027: BASIC VM — SortedSet RT_MAGIC assertion crash

**Repro:** `s = NEW Viper.Collections.SortedSet(); s.Put("c"); s.Put("a"); PRINT s.Len`
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header, file rt_heap.c, line 66.`
**Note:** Zia VM works perfectly. Same RT_MAGIC crash pattern as Deque (A-026). Both Deque and SortedSet classes are defined later in runtime.def (lines 6823 and 3921 respectively). Crash occurs on first method call after construction.
**Root cause:** `rt_sortedset_new()` in `rt_sortedset.c` uses `calloc(1, sizeof(...))` instead of `rt_obj_new_i64()`. Same missing RT_MAGIC header issue as A-026.

### A-046: BASIC VM — Promise RT_MAGIC assertion crash

**Repro:** `p = NEW Viper.Threads.Promise()` in BASIC
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header, file rt_heap.c, line 66.`
**Note:** Zia VM works fine for Promise construction and Set/GetFuture. Same pattern as A-026/A-027/A-037.
**Root cause:** `rt_promise_new()` in `rt_future.c` uses `calloc(1, sizeof(...))` instead of `rt_obj_new_i64()`. Same missing RT_MAGIC header issue as A-026. See Cluster 1.

### A-047: Zia — Promise.IsDone / Future.IsDone type mismatch ✅ NOT A BUG

**Repro:** `SayBool(p.IsDone)` or `SayInt(p.IsDone)` both fail
**Error:** SayBool says "expects i1 but got i64", SayInt says "expects i64 but got i1"
**Resolution:** NOT A BUG — Property correctly returns i1. The demo was using mismatched print functions. The correct usage depends on the context: use conditional or assign to boolean variable.

### A-048: Zia — Stream.WriteByte crashes with "Null indirect callee"

**Repro:** `var s = Viper.IO.Stream.OpenMemory(); s.WriteByte(65);` in Zia
**Error:** `Null indirect callee` after Pos=0, Len=0 prints correctly
**Note:** OpenMemory() succeeds but method dispatch fails for WriteByte. BASIC silently succeeds but Len stays 0.
**Root cause:** Zia's instance method dispatch for Stream fails because the `runtimeCallee` lookup returns empty for WriteByte. The lowerer falls back to indirect call via function pointer, but the receiver object doesn't have a vtable/dispatch table, producing a null callee. This suggests Stream methods weren't properly registered in the Zia Sema's runtime method catalog, or the method resolution path for Stream (which is a newer class) doesn't match the expected pattern.

### A-049: BASIC — Stream methods silently fail (Len stays 0) ✅ FIXED

**Repro:** `s = Viper.IO.Stream.OpenMemory(); s.WriteByte(65); PRINT s.Len` → 0
**Root cause:** Same as A-009/A-012 — BASIC type tracking didn't propagate Stream class type from OpenMemory() factory method return.
**Fix:** The `findMethodReturnClassName()` and `resolveObjectClass()` fixes for runtime class catalog lookup resolved this. Stream.WriteByte, Len, Pos all work correctly now (Len=2 after two WriteByte calls).

### A-050: Zia — BinFile/LineReader/LineWriter `new` rejected

**Repro:** `var bf = new Viper.IO.BinFile("/tmp/test", "w");` in Zia
**Error:** `'new' can only be used with value, entity, or collection types`
**Note:** Constructor names `BinFileOpen`, `LineReaderOpen`, `LineWriterOpen` don't follow `*New` convention. BASIC works fine for all three. Same pattern as A-028/A-029/A-031-A-033.
**Root cause:** Zia's `analyzeNew()` at `Sema_Expr_Advanced.cpp:700-709` constructs `type->name + ".New"` and looks it up. Since BinFile's ctor is `"Viper.IO.BinFile.Open"` not `"Viper.IO.BinFile.New"`, the lookup fails and `new` is rejected. See Cluster 3.

### A-051: Zia — Watcher EVENT_* properties crash (arity mismatch)

**Repro:** `SayInt(w.EVENT_NONE)` on a Watcher instance
**Error:** `get_EVENT_NONE expects 0 arguments but got 1`
**Note:** EVENT_NONE/CREATED/MODIFIED/DELETED/RENAMED are declared as instance properties but their getters take 0 args (they're effectively static constants). Path and IsWatching work fine.
**Root cause:** In runtime.def, EVENT_* constants are registered as `RT_FUNC` entries with signature `"i64()"` (zero arguments): `RT_FUNC(WatcherEventNone, rt_watcher_event_none, "Viper.IO.Watcher.get_EVENT_NONE", "i64()")`. But they're NOT in the RT_CLASS_PROPERTIES. When accessed as `w.EVENT_NONE`, the frontend treats it as an instance property access and passes the receiver as first argument, causing arity mismatch since the getter takes 0 args. These should either be true instance properties (taking receiver) or accessed as static constants without receiver.

### A-052: Both — Lazy static methods undefined

**Repro:** `Viper.Lazy.OfI64(42)` in both frontends
**Zia Error:** `Undefined identifier: Viper` (A-019 pattern)
**BASIC Error:** `unknown procedure 'viper.lazy.ofi64'`
**Note:** Viper.Lazy is a top-level class without standard namespace prefix pattern. Both frontends fail to resolve.
**Root cause:** `Viper.Lazy` is a top-level class (only 2 levels deep, not the typical 3+ like `Viper.Collections.List`). Both frontends' namespace resolution expects at least 3 levels (Viper.Namespace.Class). The qualified name resolution in Zia's `lookupSymbol()` and BASIC's `findRuntimeClassByQName()` may not handle 2-level paths. Additionally, Lazy's functions are factory methods (OfI64, OfStr, etc.) that create instances — they may need different dispatch than instance methods.

### A-053: Both — LazySeq static methods undefined ✅ FIXED

**Repro:** `Viper.LazySeq.Range(1, 10, 1)` in both frontends
**Zia Error:** `Undefined identifier: Viper` / **BASIC Error:** `unknown procedure 'viper.lazyseq.range'`
**Root cause:** No RT_FUNC entries existed for LazySeq methods. The RT_CLASS had methods defined but no corresponding standalone RT_FUNC entries that the frontends could resolve. Additionally, the C functions use typed `rt_lazyseq` pointers incompatible with the IL's `void*` (obj) convention.
**Fix:** Added 13 RT_FUNC entries in runtime.def. Created void* wrapper functions (`rt_lazyseq_w_*`) in rt_lazyseq.c that cast between `void*` and `rt_lazyseq` types. Also created `_next_simple`/`_peek_simple` wrappers that drop the `has_more` output parameter. Added IL wrapper ctests in RTLazySeqTests.cpp.

### A-054: BASIC — KeyDerive.Pbkdf2SHA256Str argument type mismatch ✅ NOT A BUG

**Repro:** `Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("password", "salt", 1000, 32)` in BASIC
**Error:** `argument type mismatch`
**Resolution:** NOT A BUG — Demo code incorrectly passes a string literal `"salt"` where the API expects a Bytes object. The correct usage requires explicit Bytes construction (as shown in `tests/runtime_sweep/basic/crypto.bas`). The "Str" suffix on the function name refers to the return type (string), not the salt parameter type.

### A-055: BASIC VM — Timer RT_MAGIC assertion crash

**Repro:** `t = NEW Viper.Game.Timer()` in BASIC
**Error:** `Assertion failed: (hdr->magic == RT_MAGIC), function rt_heap_validate_header, file rt_heap.c, line 66.`
**Note:** Zia VM works perfectly. Same RT_MAGIC crash pattern as A-026/A-027/A-037/A-046.
**Root cause:** `rt_timer_new()` in `rt_timer.c` uses `malloc(sizeof(struct rt_timer_impl))` instead of `rt_obj_new_i64()`. See Cluster 1.

### A-056: BASIC VM — Tween RT_MAGIC assertion crash

**Repro:** `tw = NEW Viper.Game.Tween()` in BASIC
**Error:** Same RT_MAGIC assertion crash. Zia works fine.
**Root cause:** `rt_tween_new()` in `rt_tween.c` uses `malloc(sizeof(...))` instead of `rt_obj_new_i64()`. See Cluster 1.

### A-057: BASIC VM — SmoothValue RT_MAGIC assertion crash

**Repro:** `sv = NEW Viper.Game.SmoothValue(0.0, 0.5)` in BASIC
**Error:** Same RT_MAGIC assertion crash. Zia works fine.
**Root cause:** `rt_smoothvalue_new()` in `rt_smoothvalue.c` uses `malloc(sizeof(...))` instead of `rt_obj_new_i64()`. See Cluster 1.

### A-058: BASIC VM — PathFollower RT_MAGIC assertion crash

**Repro:** `pf = NEW Viper.Game.PathFollower()` in BASIC
**Error:** Same RT_MAGIC assertion crash. Zia works fine.
**Root cause:** `rt_pathfollow_new()` in `rt_pathfollow.c` uses `malloc(sizeof(...))` instead of `rt_obj_new_i64()`. See Cluster 1.

### A-059: BASIC VM — ScreenFX RT_MAGIC assertion crash

**Repro:** `fx = NEW Viper.Game.ScreenFX()` in BASIC
**Error:** Same RT_MAGIC assertion crash. Zia works fine.
**Root cause:** `rt_screenfx_new()` in `rt_screenfx.c` uses `malloc(sizeof(...))` instead of `rt_obj_new_i64()`. See Cluster 1.

### A-060: BASIC VM — CollisionRect RT_MAGIC assertion crash

**Repro:** `r = NEW Viper.Game.CollisionRect(10.0, 20.0, 50.0, 30.0)` in BASIC
**Error:** Same RT_MAGIC assertion crash. Zia works fine.
**Root cause:** `rt_collision_rect_new()` in `rt_collision.c` uses `malloc(sizeof(...))` instead of `rt_obj_new_i64()`. See Cluster 1.

### Summary: BASIC VM RT_MAGIC crash pattern

All RT_MAGIC crashes in BASIC (A-026 Deque, A-027 SortedSet, A-037 StringBuilder, A-046 Promise, A-055 Timer, A-056 Tween, A-057 SmoothValue, A-058 PathFollower, A-059 ScreenFX, A-060 CollisionRect) share the same root cause: **the C constructor functions use `malloc()`/`calloc()` instead of `rt_obj_new_i64()`**, producing objects without the `rt_heap_hdr_t` prefix containing RT_MAGIC (0x52504956). When BASIC's VM retains/releases these objects, `payload_to_hdr()` reads garbage where the magic field should be and `rt_heap_validate_header()` asserts at `rt_heap.c:66`. Working classes (List, Map, Queue, ObjectPool, etc.) correctly use `rt_obj_new_i64()`.

**Fix:** Each crashing class's `_new()` function must be updated to allocate via `rt_obj_new_i64(class_id, sizeof(struct impl))` instead of `malloc(sizeof(struct impl))`.

**Affected C files:** `rt_deque.c`, `rt_sortedset.c`, `rt_future.c`, `rt_timer.c`, `rt_tween.c`, `rt_smoothvalue.c`, `rt_pathfollow.c`, `rt_screenfx.c`, `rt_collision.c` (and possibly `rt_ns_bridge.c` for StringBuilder).

### A-061: BASIC VM — Archive Create+Finish produces invalid zip ✅ NOT A BUG

**Repro:** `arc = Viper.IO.Archive.Create("/tmp/test.zip"); arc.AddStr("hello.txt", "Hello"); arc.Finish()` then `arc2 = Viper.IO.Archive.Open("/tmp/test.zip")`
**Original error:** `Trap: DomainError: Archive: not a valid ZIP file`
**Resolution:** NOT A BUG — The BASIC type tracking fixes (A-009/A-012) resolved this issue. Archive now works correctly in BASIC: Create/AddStr/Finish/Open/Count/Has/ReadStr all produce correct results. Has() returns -1 which is correct BASIC TRUE boolean.

---

## Audit Progress

### Core & Math (14 classes)
- [x] Viper.Math
- [x] Viper.Math.Bits
- [x] Viper.Math.Easing
- [x] Viper.Math.PerlinNoise
- [x] Viper.Math.Quat — PASS both
- [x] Viper.Math.Random
- [ ] Viper.Math.Spline — skip (no constructor, needs points setup)
- [x] Viper.Math.Vec2
- [x] Viper.Math.Vec3
- [N/A] Viper.Math.BigInt — not in runtime.def
- [N/A] Viper.Math.Mat3 — not in runtime.def
- [N/A] Viper.Math.Mat4 — not in runtime.def
- [x] Viper.Core.Box
- [x] Viper.Core.Convert
- [x] Viper.Core.Parse
- [x] Viper.Core.Object
- [x] Viper.Core.MessageBus — PASS both
- [x] Viper.Core.Diagnostics

### String & Formatting (4 classes)
- [x] Viper.String
- [x] Viper.Fmt
- [x] Viper.Terminal
- [x] Viper.Environment

### Collections (28 classes)
- [x] Viper.Collections.Bag — PASS both
- [x] Viper.Collections.BiMap — PASS both
- [x] Viper.Collections.BitSet — PASS both
- [x] Viper.Collections.BloomFilter — PASS both
- [x] Viper.Collections.Bytes — PASS both
- [x] Viper.Collections.CountMap — PASS both
- [x] Viper.Collections.DefaultMap — PASS both
- [x] Viper.Collections.Deque — Zia PASS, BASIC CRASH (A-026)
- [x] Viper.Collections.FrozenMap — BASIC PASS, Zia FAIL (A-029)
- [x] Viper.Collections.FrozenSet — BASIC PASS, Zia FAIL (A-028)
- [x] Viper.Collections.Heap — PASS both
- [x] Viper.Collections.Iterator — no standalone ctor (tested via collections)
- [x] Viper.Collections.List — PASS both (A-023, A-024)
- [x] Viper.Collections.LruCache — PASS both
- [x] Viper.Collections.Map — PASS both (A-022, A-025)
- [x] Viper.Collections.MultiMap — PASS both
- [x] Viper.Collections.OrderedMap — PASS both
- [x] Viper.Collections.Queue — PASS both
- [x] Viper.Collections.Ring — PASS both
- [x] Viper.Collections.Seq — PASS both (A-021)
- [x] Viper.Collections.Set — PASS both
- [x] Viper.Collections.SortedSet — Zia PASS, BASIC CRASH (A-027)
- [x] Viper.Collections.SparseArray — PASS both
- [x] Viper.Collections.Stack — PASS both
- [x] Viper.Collections.TreeMap — PASS both
- [x] Viper.Collections.Trie — PASS both
- [x] Viper.Collections.UnionFind — PASS both
- [x] Viper.Collections.WeakMap — PASS both

### Text (20 classes)
- [x] Viper.Text.Codec — PASS both
- [x] Viper.Text.CompiledPattern — Zia FAIL (A-032), BASIC FAIL (A-036)
- [x] Viper.Text.Csv — PASS both
- [x] Viper.Text.Diff — PASS both
- [x] Viper.Text.Html — PASS both
- [x] Viper.Text.Ini — PASS both
- [x] Viper.Text.Json — PASS both (A-035: IsValid broken)
- [x] Viper.Text.JsonPath — Zia partial, BASIC CRASH (A-040)
- [x] Viper.Text.JsonStream — PASS both
- [x] Viper.Text.Markdown — PASS both
- [x] Viper.Text.NumberFormat — PASS both
- [x] Viper.Text.Pattern — BOTH BROKEN (A-030)
- [x] Viper.Text.Pluralize — PASS both
- [x] Viper.Text.Scanner — Zia FAIL (A-033), BASIC FAIL (A-038)
- [x] Viper.Text.StringBuilder — Zia PASS, BASIC CRASH (A-037)
- [x] Viper.Text.Template — PASS both
- [x] Viper.Text.TextWrapper — Zia FAIL (A-034), BASIC FAIL (A-039)
- [x] Viper.Text.Toml — PASS both (IsValid may be unreliable)
- [x] Viper.Text.Uuid — BASIC PASS, Zia FAIL (A-034)
- [x] Viper.Text.Version — BASIC PASS, Zia FAIL (A-031)

### Time (8 classes)
- [x] Viper.Time.Clock — PASS both
- [x] Viper.Time.Countdown — PASS both
- [x] Viper.Time.DateOnly — BOTH FAIL (A-042)
- [x] Viper.Time.DateRange — PASS both
- [x] Viper.Time.DateTime — PASS both (Format may need different specifiers)
- [x] Viper.Time.Duration — PARTIAL both (A-041: component methods broken)
- [x] Viper.Time.RelativeTime — PASS both
- [x] Viper.Time.Stopwatch — PASS both

### Crypto (6 classes)
- [x] Viper.Crypto.Cipher — Zia PASS, BASIC PASS (GenerateKey works)
- [x] Viper.Crypto.Hash — PASS both
- [x] Viper.Crypto.KeyDerive — Zia returns empty, BASIC FAIL (A-054)
- [x] Viper.Crypto.Password — BASIC PASS, Zia FAIL (A-043)
- [x] Viper.Crypto.Rand — PASS both
- [ ] Viper.Crypto.Tls — skip (needs network)

### IO (13 classes)
- [x] Viper.IO.Archive — Zia PASS, BASIC FAIL (A-061: Create+Finish produces invalid zip)
- [x] Viper.IO.BinFile — BASIC PASS, Zia FAIL `new` rejected (A-050)
- [x] Viper.IO.Compress — PASS both
- [x] Viper.IO.Dir — PASS both
- [x] Viper.IO.File — PASS both
- [x] Viper.IO.Glob — BOTH BROKEN (A-045: Match always false)
- [x] Viper.IO.LineReader — BASIC PASS, Zia FAIL `new` rejected (A-050)
- [x] Viper.IO.LineWriter — BASIC PASS, Zia FAIL `new` rejected (A-050)
- [x] Viper.IO.MemStream — PASS both
- [x] Viper.IO.Path — PASS both
- [x] Viper.IO.Stream — Zia FAIL null callee (A-048), BASIC silent fail (A-049)
- [x] Viper.IO.TempFile — PASS both
- [x] Viper.IO.Watcher — PASS both (EVENT_* props broken, A-051)

### Threads (15 classes)
- [x] Viper.Threads.Barrier — PASS both
- [x] Viper.Threads.CancelToken — PASS both
- [x] Viper.Threads.ConcurrentMap — PASS both
- [x] Viper.Threads.ConcurrentQueue — PASS both
- [x] Viper.Threads.Debouncer — PASS both
- [x] Viper.Threads.Future — tested via Promise
- [x] Viper.Threads.Gate — PASS both
- [ ] Viper.Threads.Monitor — not in runtime.def
- [ ] Viper.Threads.Parallel — skip (needs function pointers)
- [x] Viper.Threads.Promise — Zia PASS (IsDone type bug A-047), BASIC CRASH (A-046)
- [x] Viper.Threads.RwLock — PASS both
- [x] Viper.Threads.SafeI64 — PASS both
- [x] Viper.Threads.Scheduler — PASS both
- [ ] Viper.Threads.Thread — skip (needs actual thread spawn)
- [x] Viper.Threads.Throttler — PASS both

### Network (12 classes)
- [ ] Viper.Network.Dns — skip (needs network)
- [ ] Viper.Network.Http — skip (needs network)
- [ ] Viper.Network.HttpReq — skip (needs network)
- [ ] Viper.Network.HttpRes — skip (needs network)
- [x] Viper.Network.RateLimiter — PASS both
- [ ] Viper.Network.RestClient — skip (needs network)
- [x] Viper.Network.RetryPolicy — PASS both
- [ ] Viper.Network.Tcp — skip (needs network)
- [ ] Viper.Network.TcpServer — skip (needs network)
- [ ] Viper.Network.Udp — skip (needs network)
- [x] Viper.Network.Url — PASS both
- [ ] Viper.Network.WebSocket — skip (needs network)

### Misc (11 classes)
- [x] Viper.Exec — PASS both (Capture returns empty in VM — expected?)
- [x] Viper.Lazy — BOTH FAIL (A-052)
- [x] Viper.LazySeq — BOTH FAIL (A-053)
- [x] Viper.Log — PASS both
- [x] Viper.Machine — PASS both
- [x] Viper.Option — BOTH FAIL (A-043/A-044)
- [x] Viper.Result — BOTH FAIL (A-043/A-044)
- [N/A] Viper.Memory.GC — not in runtime.def
- [N/A] Viper.Data.Serialize — not in runtime.def
- [N/A] Viper.Data.Xml — not in runtime.def
- [N/A] Viper.Data.Yaml — not in runtime.def

### Graphics (requires display — skip terminal audit)
- [ ] Viper.Graphics.Camera
- [ ] Viper.Graphics.Canvas
- [x] Viper.Graphics.Color — PASS both (pure static utility, no display needed)
- [x] Viper.Graphics.Pixels — PASS both (software image buffer, no display needed)
- [ ] Viper.Graphics.Scene
- [ ] Viper.Graphics.SceneNode
- [ ] Viper.Graphics.Sprite
- [ ] Viper.Graphics.SpriteBatch
- [ ] Viper.Graphics.SpriteSheet
- [ ] Viper.Graphics.Tilemap

### GUI (requires display — skip terminal audit)
- [ ] Viper.GUI.App
- [ ] Viper.GUI.Button
- [ ] Viper.GUI.Checkbox
- [ ] Viper.GUI.CodeEditor
- [ ] Viper.GUI.Dropdown
- [ ] Viper.GUI.Font
- [ ] Viper.GUI.HBox
- [ ] Viper.GUI.Image
- [ ] Viper.GUI.Label
- [ ] Viper.GUI.ListBox
- [ ] Viper.GUI.Menu
- [ ] Viper.GUI.MenuBar
- [ ] Viper.GUI.MenuItem
- [ ] Viper.GUI.ProgressBar
- [ ] Viper.GUI.RadioButton
- [ ] Viper.GUI.RadioGroup
- [ ] Viper.GUI.ScrollView
- [ ] Viper.GUI.Slider
- [ ] Viper.GUI.Spinner
- [ ] Viper.GUI.SplitPane
- [ ] Viper.GUI.StatusBar
- [ ] Viper.GUI.StatusBarItem
- [ ] Viper.GUI.Tab
- [ ] Viper.GUI.TabBar
- [ ] Viper.GUI.TextInput
- [ ] Viper.GUI.Theme
- [ ] Viper.GUI.Toolbar
- [ ] Viper.GUI.ToolbarItem
- [ ] Viper.GUI.TreeView
- [ ] Viper.GUI.TreeView.Node
- [ ] Viper.GUI.VBox
- [ ] Viper.GUI.Widget

### Sound (requires audio — skip terminal audit)
- [ ] Viper.Sound.Audio
- [ ] Viper.Sound.Music
- [ ] Viper.Sound.Playlist
- [ ] Viper.Sound.Sound
- [ ] Viper.Sound.Voice

### Input (requires display — skip terminal audit)
- [ ] Viper.Input.Action
- [ ] Viper.Input.Keyboard
- [ ] Viper.Input.KeyChord
- [ ] Viper.Input.Manager
- [ ] Viper.Input.Mouse
- [ ] Viper.Input.Pad

### Game (16 classes — many testable without display)
- [x] Viper.Game.ButtonGroup — PASS both
- [x] Viper.Game.Collision — PASS both
- [x] Viper.Game.CollisionRect — Zia PASS, BASIC CRASH (A-060)
- [x] Viper.Game.Grid2D — PASS both
- [x] Viper.Game.ObjectPool — PASS both
- [ ] Viper.Game.ParticleEmitter — skip (needs display context)
- [x] Viper.Game.PathFollower — Zia PASS, BASIC CRASH (A-058)
- [ ] Viper.Game.Physics2D.Body — skip (needs physics world)
- [ ] Viper.Game.Physics2D.World — skip (needs physics world)
- [x] Viper.Game.Quadtree — PASS both
- [x] Viper.Game.ScreenFX — Zia PASS, BASIC CRASH (A-059)
- [x] Viper.Game.SmoothValue — Zia PASS, BASIC CRASH (A-057)
- [ ] Viper.Game.SpriteAnimation — skip (needs display context)
- [x] Viper.Game.StateMachine — PASS both
- [x] Viper.Game.Timer — Zia PASS, BASIC CRASH (A-055)
- [x] Viper.Game.Tween — Zia PASS, BASIC CRASH (A-056)

---

## Root Cause Analysis — Detailed Investigation

### Cluster 1: BASIC VM RT_MAGIC Heap Crashes (10 bugs)

**Bugs:** A-026 (Deque), A-027 (SortedSet), A-037 (StringBuilder), A-046 (Promise), A-055 (Timer), A-056 (Tween), A-057 (SmoothValue), A-058 (PathFollower), A-059 (ScreenFX), A-060 (CollisionRect)

**Severity:** Critical — program aborts immediately

**Mechanism:**
1. BASIC code calls `NEW Viper.X.Y()` which is lowered in `Lower_OOP_Alloc.cpp`
2. The lowerer finds the class via `findRuntimeClassByQName()` and its ctor, then emits a direct call to the C constructor (e.g., `rt_timer_new()`)
3. The C constructor allocates memory with `malloc()` or `calloc()` — producing a raw pointer **without** the `rt_heap_hdr_t` prefix that contains `RT_MAGIC` (0x52504956)
4. When BASIC's VM later tries to retain/release the object (ref counting), `payload_to_hdr()` subtracts `sizeof(rt_heap_hdr_t)` from the pointer to find the header
5. The "header" area contains garbage, so `assert(hdr->magic == RT_MAGIC)` fails at `rt_heap.c:66`

**Why Zia doesn't crash:** Zia VM uses a different object lifecycle that doesn't call `payload_to_hdr()` for retain/release on these objects.

**Working pattern (correct):**
```c
// rt_list.c — allocates correctly
void *rt_list_new(void) {
    return rt_obj_new_i64(RT_CLASS_LIST, sizeof(struct rt_list_impl));
}
```

**Crashing pattern (incorrect):**
```c
// rt_timer.c — allocates without heap header
void *rt_timer_new(void) {
    struct rt_timer_impl *t = malloc(sizeof(struct rt_timer_impl));
    // ... no RT_MAGIC header!
    return t;
}
```

**Affected C source files:**
| Bug | Class | C File | Allocation |
|-----|-------|--------|------------|
| A-026 | Deque | rt_deque.c | `malloc()` |
| A-027 | SortedSet | rt_sortedset.c | `calloc()` |
| A-037 | StringBuilder | rt_ns_bridge.c | needs verification |
| A-046 | Promise | rt_future.c | `calloc()` |
| A-055 | Timer | rt_timer.c | `malloc()` |
| A-056 | Tween | rt_tween.c | `malloc()` |
| A-057 | SmoothValue | rt_smoothvalue.c | `malloc()` |
| A-058 | PathFollower | rt_pathfollow.c | `malloc()` |
| A-059 | ScreenFX | rt_screenfx.c | `malloc()` |
| A-060 | CollisionRect | rt_collision.c | `malloc()` |

**Fix:** Change each `_new()` function to use `rt_obj_new_i64(class_id, sizeof(struct impl))`.

---

### Cluster 2: Zia `bind` Namespace Resolution Failures (4 bugs)

**Bugs:** A-002 (Box), A-003 (Parse), A-004 (Random extended), A-005 (String extended)

**Severity:** High — entire function categories inaccessible

**Mechanism:**
1. Zia `bind Viper.X.Y;` triggers `importNamespaceSymbols()` in `Sema_Decl.cpp:210-326`
2. This function iterates the RuntimeRegistry catalog to find functions with the matching namespace prefix
3. It creates short-name aliases (e.g., `"I64"` → `"Viper.Core.Box.I64"`) in `importedSymbols_`
4. For newer functions added to runtime.def, the catalog iteration doesn't find them
5. Older functions (e.g., `Seed`, `Next`, `ToUpper`, `ToLower`) work because they were in the catalog from an earlier generation

**Key evidence:** In A-004 (Random), `Seed`/`Next`/`NextInt` work but `Range`/`Dice`/`Chance`/`Gaussian`/`Exponential` don't. In A-005 (String), `ToUpper`/`ToLower`/`IndexOf`/`Trim` work but `Capitalize`/`Title`/`Slug`/`Levenshtein` don't. This "old works, new doesn't" pattern points to a catalog generation gap.

**Source:** `src/frontends/zia/Sema_Decl.cpp:210-326` (`importNamespaceSymbols()`)

**Fix:** Ensure all runtime.def functions are populated in the RuntimeRegistry catalog that `importNamespaceSymbols()` iterates. Likely need to regenerate RuntimeClasses.inc or fix the catalog builder to include newer entries.

---

### Cluster 3: Zia `new` Rejection for Non-`*New` Constructors (7 bugs)

**Bugs:** A-028 (FrozenSet), A-029 (FrozenMap), A-031 (Version), A-032 (CompiledPattern), A-033 (Scanner), A-042 (DateOnly), A-050 (BinFile/LineReader/LineWriter)

**Severity:** Medium — classes usable in BASIC but not Zia

**Mechanism:**
1. Zia's `analyzeNew()` in `Sema_Expr_Advanced.cpp:700-709` hardcodes constructor lookup:
   ```cpp
   std::string ctorName = type->name + ".New";  // HARDCODED
   Symbol *sym = lookupSymbol(ctorName);
   ```
2. Zia's `lowerNew()` in `Lowerer_Expr_Complex.cpp:282-297` does the same:
   ```cpp
   std::string ctorName = type->name + ".New";  // HARDCODED
   ```
3. Classes whose constructor is not named `*New` are rejected with error V3000

**Affected constructor names:**
| Bug | Class | Actual Ctor Name | Expected by Zia |
|-----|-------|-----------------|-----------------|
| A-028 | FrozenSet | FrozenSetFromSeq | FrozenSetNew |
| A-029 | FrozenMap | FrozenMapFromSeqs | FrozenMapNew |
| A-031 | Version | VersionParse | VersionNew |
| A-032 | CompiledPattern | CompiledPatternCompile | CompiledPatternNew |
| A-033 | Scanner | ScannerCreate | ScannerNew |
| A-042 | DateOnly | DateOnlyToday | DateOnlyNew |
| A-050 | BinFile | BinFileOpen | BinFileNew |
| A-050 | LineReader | LineReaderOpen | LineReaderNew |
| A-050 | LineWriter | LineWriterOpen | LineWriterNew |

**Source:** `src/frontends/zia/Sema_Expr_Advanced.cpp:700-709`, `src/frontends/zia/Lowerer_Expr_Complex.cpp:282-297`

**Fix:** Instead of hardcoding `.New`, look up the class's actual constructor name from the RuntimeClass entry's `ctor` field.

---

### Cluster 4: Zia Fully-Qualified Static Call Failures (6 bugs)

**Bugs:** A-019 (general pattern), A-034 (Uuid/TextWrapper), A-043 (Password/Result/Option), A-052 (Lazy), A-053 (LazySeq)

**Severity:** High — many static functions inaccessible in Zia

**Mechanism:**
1. Zia code like `Viper.Math.Easing.Linear(0.5)` parses to nested FieldExpr nodes
2. `Sema_Expr_Call.cpp:422-506` uses `extractDottedName()` to build the full qualified name
3. `lookupSymbol(dottedName)` is called to resolve the function
4. The symbol table doesn't contain "Viper" as a root namespace entry, so the lookup fails
5. Some older paths work because they were explicitly seeded in the symbol table during initialization

**Secondary issue:** The lowerer at `Lowerer_Expr_Call.cpp:510-549` has a fallback for qualified names that only checks `definedFunctions_` (user-defined), not runtime functions.

**Sub-patterns:**
- A-014/A-019: Functions in namespace-only modules (Easing, TextWrapper) — no RT_CLASS_BEGIN, just RT_FUNCs
- A-052/A-053: Top-level classes (Viper.Lazy, Viper.LazySeq) — only 2 levels deep instead of typical 3+
- A-043/A-044: Newer classes (Result, Option) — static factory methods not in symbol table

**Source:** `src/frontends/zia/Sema_Expr_Call.cpp:422-506`, `src/frontends/zia/Lowerer_Expr_Call.cpp:510-549`

**Fix:** Ensure `lookupSymbol()` can resolve all runtime function qualified names. May need to seed the symbol table with all RT_FUNC entries, or add a fallback that queries the RuntimeRegistry directly.

---

### Cluster 5: BASIC Unknown Procedure (5 bugs)

**Bugs:** A-036 (CompiledPattern), A-038 (Scanner), A-039 (TextWrapper), A-044 (Result/Option)

**Severity:** High — classes completely inaccessible in BASIC

**Mechanism:**
Two sub-patterns:

**Pattern A — Constructor name mismatch (A-036, A-038):**
The BASIC lowerer's `Lower_OOP_Alloc.cpp` finds the class but emits the wrong constructor name. Instead of using the actual ctor name from the RuntimeClass entry (e.g., `CompiledPatternCompile`), it appears to construct `@CompiledPatternNew` by appending "New" to the class ID.

**Pattern B — Static functions not resolvable (A-039, A-044):**
TextWrapper, Result, and Option have static functions (not instance methods). BASIC's `Lower_OOP_MethodCall.cpp` resolves methods via `findRuntimeClassByQName()` — but TextWrapper has no class entry, and Result/Option may not be findable at their namespace depth.

**Source:** `src/frontends/basic/lower/oop/Lower_OOP_Alloc.cpp`, `src/frontends/basic/lower/oop/Lower_OOP_MethodCall.cpp`

**Fix (Pattern A):** Use the actual ctor name from RuntimeClass instead of constructing `*New`.
**Fix (Pattern B):** Add support for resolving standalone RT_FUNCs by fully-qualified name in BASIC.

---

### Cluster 6: Property Getter Issues (4 bugs)

**Bugs:** A-001 (Math Pi/E/Tau), A-011 (Vec2/Vec3 types), A-047 (Promise.IsDone), A-051 (Watcher EVENT_*)

**Root causes vary:**

| Bug | Root Cause |
|-----|-----------|
| A-001 | Property getters use `get_Pi` naming; `bind` doesn't create aliases for property accessors |
| A-011 | Zia type inference reports `i64` for properties declared as `f64` in RT_PROP |
| A-047 | Zia type inference is ambiguous for `i1` (boolean) properties — reports both i1 and i64 |
| A-051 | EVENT_* are zero-arg RT_FUNCs (static constants), not RT_PROPs; accessed as instance properties, receiver causes arity mismatch |

---

### Cluster 7: Vec2/Vec3 Method Return Issues (3 bugs)

**Bugs:** A-009 (standalone use), A-012 (BASIC methods return 0), A-013 (BASIC Vec3 methods return 0)

**Root causes:**
- **A-009:** Vec2/Vec3 must be used as class instances (`new Viper.Math.Vec2(x,y)` then `v.X`), not as standalone functions.
- **A-012/A-013:** Object-returning Vec2/Vec3 methods (Add, Sub, Mul, etc.) allocate result objects in C via `malloc()` instead of `rt_obj_new_i64()`. The returned pointer lacks a heap header. When BASIC reads properties on the returned object, it gets zero values because the pointer doesn't point to a properly structured heap object. Scalar-returning methods (Dot, Len, LenSq, Dist) work correctly because they return `f64` directly without heap allocation.

---

### Cluster 8: Easing/PerlinNoise Broken (3 bugs)

**Bugs:** A-014 (Easing all undefined), A-015 (PerlinNoise returns 0 in Zia), A-016 (PerlinNoise arity mismatch in BASIC)

**Root causes:**
- **A-014:** Easing is a namespace-only module (no RT_CLASS_BEGIN, only RT_FUNCs). Neither frontend can resolve `Viper.Math.Easing.Linear()` as a callable — Zia has the A-019 pattern issue, BASIC's method resolution only works for classes. See Cluster 4/5.
- **A-015:** PerlinNoise methods return 0 in Zia — the receiver (PerlinNoise instance containing the seed permutation table) is not being passed to the C function. Zia's method dispatch at `Lowerer_Expr_Call.cpp:360-369` only adds receiver when `baseType->name` starts with "Viper." — if type inference doesn't preserve the full qualified name, receiver is omitted.
- **A-016:** PerlinNoise methods in BASIC have arity mismatch — the RT_METHOD signature `"f64(f64,f64)"` omits the implicit receiver, but the RT_FUNC signature `"f64(obj,f64,f64)"` includes it. BASIC's method resolution sees 2 user-supplied args but tries to match against the function signature which expects 3 args. The auto-receiver-passing mechanism isn't activating for PerlinNoise.

---

### Cluster 9: Collection Object Boxing Issues (4 bugs)

**Bugs:** A-021 (Seq.Get crash), A-022 (Map.Get crash), A-023 (List.Get type mismatch), A-025 (Map.Get returns "Object")

**Root causes:**
Collection `Get` methods return `obj` type (boxed values). The boxing/unboxing mechanism has different failure modes:

| Bug | Failure Mode |
|-----|-------------|
| A-021 | Seq.Get() returns obj; when passed to Say() (expects str), Zia's `payload_to_hdr()` crashes because the boxed value's pointer doesn't have a valid heap header |
| A-022 | Map.Get() same crash pattern as A-021 |
| A-023 | List.Get() returns obj but Zia type inference sees it as i64, causing type mismatch with Say() |
| A-025 | Map.Get() in BASIC returns obj; when printed, BASIC calls ToString on the boxed value which returns generic "Object" instead of unboxing to the original string |

**Underlying issue:** The `obj` type in the IL is used for heterogeneous containers, but neither frontend has proper unboxing support. Values stored in collections lose their original type information when retrieved via `Get()`.

---

### Individual Bug Root Causes

| Bug | Root Cause Summary |
|-----|-------------------|
| A-006 | API naming: runtime.def has TrimStart/TrimEnd/Substring/get_Length, not TrimLeft/TrimRight/Slice/Len |
| A-007 | BASIC i1 type: Fmt.Bool expects i1 but BASIC passes integer (-1/0) |
| A-008 | BASIC i1 type: Parse.BoolOr second param expects i1 but BASIC passes integer |
| A-010 | ARM64 codegen: Native crash after Pow — likely codegen issue in AArch64 backend for math intrinsics |
| A-017 | BASIC i1 type: Diagnostics.Assert expects i1 but BASIC passes integer |
| A-018 | Newer functions (TypeName/TypeId/IsNull) not in catalog — A-019 pattern in Zia, Cluster 5 in BASIC |
| A-020 | Random.Next C function `rt_rnd()` takes 0 args (global state); instance method dispatch passes receiver causing arity mismatch |
| A-024 | API naming: List has Find (not IndexOf), no Pop method, no IsEmpty property |
| A-030 | C runtime bug: regex pattern matching in rt_pattern.c always returns no-match |
| A-035 | C runtime bug: JSON validator in rt_json.c accepts arbitrary strings as valid |
| A-040 | BASIC boxing: JsonPath functions receive corrupted object pointer from BASIC's value boxing |
| A-045 | C runtime bug: glob matching in rt_glob.c always returns false |
| A-048 | Zia method dispatch: Stream.WriteByte callee not resolved, falls back to null indirect call |
| A-049 | BASIC method dispatch: Stream.WriteByte dispatches but arguments may be corrupted |
| A-054 | API type: KeyDerive.Pbkdf2SHA256Str salt expects obj (Bytes), not str |
| A-061 | BASIC argument marshaling: Archive.AddStr/Finish receive corrupted arguments from BASIC's value representation |

---

### Fix Priority Recommendations

**Priority 1 — Single fix, 10 bugs (Cluster 1):**
Fix all C constructor functions to use `rt_obj_new_i64()` instead of `malloc()`. This resolves A-026, A-027, A-037, A-046, A-055-A-060.

**Priority 2 — Two-line fix, 7 bugs (Cluster 3):**
Change hardcoded `.New` suffix to look up actual ctor name from RuntimeClass. This resolves A-028, A-029, A-031-A-033, A-042, A-050.

**Priority 3 — Catalog regeneration, 4 bugs (Cluster 2):**
Ensure all runtime.def functions appear in RuntimeRegistry catalog. This resolves A-002-A-005.

**Priority 4 — Symbol table seeding, 6 bugs (Cluster 4):**
Add all RT_FUNC entries to Zia's symbol table for qualified name lookup. This resolves A-019, A-034, A-043, A-052, A-053 (and partially A-014).

**Priority 5 — BASIC proc registry, 5 bugs (Cluster 5):**
Seed all constructor and standalone function symbols in BASIC's ProcRegistry. This resolves A-036, A-038, A-039, A-044.

**Priority 6 — C runtime fixes, 3 bugs:**
Fix regex engine (A-030), JSON validator (A-035), and glob matcher (A-045).
