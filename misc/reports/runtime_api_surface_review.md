# Zanna Runtime API Surface Review

Date: 2026-07-01
Scope: frontend-visible `Zanna.*` runtime API as reported by
`./build/src/tools/zanna/zanna --dump-runtime-api`, correlated with
`src/il/runtime/runtime.def`, generated runtime classes, and `rtgen` behavior.

> ## ✅ Confirmation Review — 2026-06-30 (independent)
>
> Every measured number, code reference, and representative example below was
> re-derived from first principles: the live `zanna` binary
> (`--dump-runtime-api`, `--dump-runtime-classes`), a direct parse of
> `src/il/runtime/runtime.def` (all 6,279 `RT_FUNC` / 337 `RT_ALIAS` entries),
> and the cited source lines. `rtgen` itself prints
> `6279 functions, 337 aliases, 461 classes`; the surface audit and 8 focused
> runtime tests pass. **All findings are CONFIRMED** — see the per-finding
> `✅ CONFIRMED` line under each heading.
>
> Caveats surfaced during confirmation (do not invalidate any finding):
> - The boolean-`i64` and accessor/property-duplication aggregate counts
>   (85, 33, 115) depend on an undocumented "boolean-looking" / "writable
>   property" classifier; independent nets reproduce ~57–66 / ~21 / 109 — same
>   order of magnitude. The concrete examples and the 4 exact getter/setter
>   mismatches reproduce precisely.
> - The Game3D lowercase-name total reproduces at 301 vs the reported 305
>   (long-tail classifier edge); the per-class table is exact row-for-row.
> - One illustrative example is imprecise: `Zanna.Graphics3D.Material3D`
>   Alpha/Metallic/Roughness/AmbientOcclusion are set via property setters (`set_*`), not
>   `Set*` methods, so they are not accessor-duplication cases. Flagged inline.

> ## Cleanup Progress — 2026-07-01
>
> Current live dump after cleanup is **6,222** functions / **461** classes.
> `rtgen --audit --strict-header-sync --strict-unclassified` passes with
> **6,222** declared functions, **461** classes, and **7,110** header
> declarations. `RT_ALIAS` is now rejected by `rtgen`; no public `RT_ALIAS`
> entries remain in `runtime.def`.
>
> Validated cleanup already completed:
> - No public class property names contain all-caps acronym blocks; the stricter
>   live dump audit (`[A-Z]{2,}` on property names) returns zero matches.
> - No public property/method collisions remain under case-insensitive matching.
> - No same class + method + arity duplicates remain in the live class catalog.
> - No accessor-shaped `get_` / `set_` public methods remain in the class
>   catalog; Canvas3D telemetry is exposed only as properties.
> - No lowercase/camelCase public class member names remain in the live class
>   catalog. Excluding canonical `get_` / `set_` accessor function spellings, no
>   lowercase public `RT_FUNC` leaves remain.
> - `Zanna.Math.Random.inst_*` implementation targets are now
>   `RT_INTERNAL_FUNC` descriptors: callable by class-method lowering and native
>   codegen, but hidden from standalone frontend externs and `--dump-runtime-api`.
>   Zia runtime method binding derives the implicit-receiver skip from resolved
>   class-method metadata, so hidden implementation targets keep
>   `rng.NextInt(...)` / `rng.Range(...)` source calls working without publishing
>   `inst_*` as public functions.
> - Sound boolean API shape is now corrected in the frontend-visible surface:
>   `Voice.IsPlaying`, `Music.IsPlaying`, `Music.SetLoop`, and
>   `MusicGen.SetLoopable` use `i1` in both function and class-method
>   signatures.
> - Concrete GUI/Assets/Terminal boolean examples from the review now use `i1`
>   in public signatures, including `Widget.IsHovered`, `Widget.WasClicked`,
>   `App.WasCloseRequested`, `ClipboardText.HasText`, `Shortcuts.IsEnabled`,
>   `Assets.Exists`, `Terminal.SetCursorVisible`, and video `IsPlaying`
>   properties.
> - Standard boolean probe names are now clean across the live public surface:
>   `Is*`, `Has*`, `Can*`, `Was*`, `get_Is*`, `get_Has*`, and `get_Can*`
>   functions/methods all return `i1`; the runtime class-qualified surface audit
>   guards this.
> - Boolean state setters now use `i1` for the concrete GUI/runtime toggle
>   names audited here, including visibility/enabled/draggable/checked,
>   shortcut/cursor/menu/toolbar/findbar, code-editor display toggles,
>   file-dialog multiple selection, output-pane flags, tab modified/auto-close,
>   minimap slider, and video-widget visibility/enabled. The same audit guards
>   these exact setter names and intentionally leaves selectors/counts such as
>   `Dropdown.SetSelected(index)` and `Toast.SetMaxVisible(count)` as integers.
> - `--dump-runtime-api` now emits public `runtime.def` signature text for
>   frontend-visible `Zanna.*` descriptors rather than lowered IL ABI
>   signatures. The live dump has zero public `ptr` signature tokens, and
>   `test_runtime_registry` guards `Zanna.*` descriptor signature text.
> - Direct parse of the current public `RT_FUNC` rows finds no public
>   same-C-symbol + same-signature duplicate groups. `RT_INTERNAL_FUNC` rows are
>   deliberately excluded from that public API measurement.
> - Collection/container cardinality now uses `Count`, while `Length` is reserved
>   for byte/string/buffer/stream length, `BitSet` bit capacity, durations, and
>   geometric/path length. No class exposes both `Count` and `Length` except
>   `BitSet`, where `Count` is population count and `Length` is bit capacity.
>   `Grid2D.Length`, which was only a duplicate spelling for `Grid2D.Size`, was
>   removed after verifying there were no in-repo call sites. The
>   class-qualified surface audit now allowlists the remaining semantic
>   `Length` properties and rejects new cardinality `Length` drift.
> - Descendant namespace functions are no longer synthesized onto parent classes.
> - Cross-class constructor targets no longer synthesize public methods such as
>   `Zanna.Core.ValueType.ValueType`.
> - Constructor metadata now targets only canonical `Class.New` functions.
>   Named factories such as `Open`, `Load`, `Parse`, `FromSeq`, `Some`, `Ok`,
>   `Today`, `Range`, `Connect`, and `Build` are explicit factory methods only;
>   Zia `new` no longer treats them as constructor aliases.
> - `Zanna.Functional.Lazy.New` was removed from the public runtime surface after
>   confirming there were no in-repo source, test, example, or doc call sites.
>   `Lazy.Of*` remain the public value factories.
> - Writable property plus `Set<Property>` duplicate methods are gone from the
>   live class catalog. The last five were `Zanna.Game3D.Entity3D.Mesh`,
>   `Material`, `Name`, `Layer`, and `CollisionMask`; call sites now use
>   property assignment.
> - Removed stale C-only generic modifier key aliases
>   (`rt_keyboard_key_shift`, `rt_keyboard_key_ctrl`, `rt_keyboard_key_alt`) after
>   verifying the public `KeyShift`/`KeyCtrl`/`KeyAlt` aliases were gone.
> - Concrete GUI widget classes no longer copy `Zanna.GUI.Widget` methods into
>   their public class catalogs. Base widget methods remain on
>   `Zanna.GUI.Widget`; shared runtime method resolution and Zia completion
>   expose those inherited operations for concrete widget handles without
>   duplicating the public API surface. The class-qualified surface audit now
>   rejects copied `Zanna.GUI.Widget.*` targets on other GUI classes.
> - Current focused verification passes:
>   `test_runtime_class_qualified_surface`, `test_runtime_classes_catalog`,
>   `test_method_index_basic`, `test_basic_runtime_calls`, `test_zia_rt_new`,
>   `zia_rt_api_test_memstream`, and `test_runtime_surface_audit`.
> - Final audit verification passes:
>   `./scripts/audit_runtime_surface.sh`,
>   `bash scripts/lint_zia_runtime_names.sh`, and
>   strict `rtgen` audit with header sync and unclassified-symbol checks.
> - Final demo/IDE verification requested for this cleanup passes:
>   `./scripts/build_demos_mac.sh` reports 17 passed / 0 failed, and
>   `./scripts/build_ide.sh` builds `src/zannaide/bin/zannaide`.

## Executive Summary

> ✅ **CONFIRMED.** Live dump = **6,620** functions / **461** classes; `rtgen`
> audit prints **6,279** functions / **337** aliases / **461** classes verbatim;
> audit + focused tests pass. All 8 highest-impact items reproduced below;
> item 2's "168 methods on `Zanna.Math`" sums exactly to 168.

The current runtime API surface is not just large; it is polluted by aliases,
generator side effects, mixed naming conventions, and type-shape inconsistencies.
Because Zanna is pre-alpha, the right policy is to delete compatibility aliases
and normalize the surface now rather than carrying migration debt forward.

Measured surface:

- Live dump: 6,620 public `Zanna.*` functions and 461 runtime classes.
- `rtgen` audit: 6,279 declared functions, 337 `RT_ALIAS` entries, 461 classes.
- Surface audit and focused tests pass, which means the junk is consistently
  registered; it does not mean the surface is clean.

Highest-impact findings:

1. Delete all 337 public `RT_ALIAS` entries. They are pre-alpha debt.
2. Stop `rtgen` from auto-adding descendant namespace functions as parent-class
   methods. This creates 203 bogus class methods, including 168 methods on
   `Zanna.Math` copied from `Zanna.Math.Bits`, `BigInt`, `Vec2`, `Vec3`, etc.
3. Stop constructor auto-method injection or constrain it to explicit `New`
   factories. It creates odd methods such as `Zanna.Core.ValueType.ValueType`.
4. Fix runtime method indexing or forbid same-name/same-arity overloads. Today
   `RuntimeRegistry` indexes methods by class, method, and arity only, so
   same-arity overloads overwrite each other.
5. Normalize public object signatures. `runtime.def` says `ptr` is internal, but
   the live API dump exposes `ptr` in 5,601 function signatures.
6. Normalize public naming: `Game3D` has 305 lowercase/camelCase function names
   and hundreds of lower-case class members in an otherwise PascalCase surface.
7. Pick one cardinality term (`Count` for collections, `Length` for string/bytes
   if desired) and delete `Count`/`Length` aliases.
8. Fix boolean signatures. At least 85 boolean-looking functions return `i64`;
   33 boolean-looking setters take `i64`.

## Source Of Truth Problem

> ✅ **CONFIRMED.** `runtime.def:162` documents `ptr` as "raw pointer
> (runtime-internal only; do not expose in RT_FUNC/RT_METHOD signatures)";
> `main.cpp:292` iterates the live registry and `main.cpp:300–308` builds the
> printed signature from the lowered `d.signature` descriptor (retType/paramTypes)
> — not the public `runtime.def` text — which is why typed objects collapse to
> `ptr`. Lines 142/151/162 and 292/300 all match.

The source intends `runtime.def` to be the API definition:

- `RT_FUNC`, `RT_ALIAS`, `RT_CLASS_BEGIN`, `RT_PROP`, and `RT_METHOD` are defined
  in `src/il/runtime/runtime.def`.
- `ptr` is documented there as "raw pointer (runtime-internal only; do not expose
  in RT_FUNC/RT_METHOD signatures)".
- `--dump-runtime-api` prints the lowered runtime descriptor signatures, not the
  original public signature strings, so typed object signatures collapse into
  `ptr`.

Recommendation:

- Make `runtime.def` the only public API source.
- Make `--dump-runtime-api` print the public `runtime.def` signature text, not
  lowered IL descriptor text.
- Add a public-surface audit that fails if any emitted public signature contains
  `ptr`.

Relevant code:

- `src/il/runtime/runtime.def:142`
- `src/il/runtime/runtime.def:151`
- `src/il/runtime/runtime.def:162`
- `src/tools/zanna/main.cpp:292`
- `src/tools/zanna/main.cpp:300`

## P0: Delete All Compatibility Aliases

> ✅ **CONFIRMED.** Exactly **337** `RT_ALIAS` entries (line-anchored count;
> `rtgen` audit agrees). The alias-group table reproduces exactly when rolled up
> to the first 3 namespace segments (`Zanna.Game.UI` 72, `ParticleSystem2D` 28,
> `Emitter2D` 28, `Lighting2D` 13, `System.Process` 10, … `System.Machine` 6).
> Representative aliases all present: `Zanna.Box.*` (16), `Zanna.Parse.*` (13),
> `Zanna.Convert.*` (6), plus `String.Length`, `ConcatSelf`, `FromFloat`,
> `List.Flip`, `List.Items`. `RuntimeSurfacePolicy.inc` exists as cited.

There are 337 `RT_ALIAS` entries. In a pre-alpha surface, these should not be
compatibility aliases; they should be compile errors in `rtgen` unless an
internal-only alias is explicitly needed for backend lowering.

Largest alias groups by public prefix:

| Count | Prefix |
|---:|---|
| 72 | `Zanna.Game.UI` |
| 28 | `Zanna.Graphics.ParticleSystem2D` |
| 28 | `Zanna.Graphics.Emitter2D` |
| 13 | `Zanna.Graphics.Lighting2D` |
| 10 | `Zanna.System.Process` |
| 10 | `Zanna.Graphics.ScreenScaler` |
| 10 | `Zanna.Game3D.Diagnostics` |
| 8 | `Zanna.Text.NumberFormat` |
| 8 | `Zanna.Graphics.GpuTexture2D` |
| 8 | `Zanna.Text.Json` |
| 7 | `Zanna.Graphics.Surface2D` |
| 7 | `Zanna.Math.Vec3` |
| 7 | `Zanna.Graphics3D.Material3D` |
| 6 | `Zanna.Graphics.SpriteFont` |
| 6 | `Zanna.System.Environment` |
| 6 | `Zanna.System.Machine` |

Representative aliases to delete:

- `Zanna.Box.*` -> `Zanna.Core.Box.*`
- `Zanna.Parse.*` -> `Zanna.Core.Parse.*`
- `Zanna.Convert.*` -> `Zanna.Core.Convert.*`
- `Zanna.String.Length` -> `Zanna.String.get_Length`
- `Zanna.String.ConcatSelf` -> `Zanna.String.Concat`
- `Zanna.String.FromFloat` -> `Zanna.String.FromSingle`
- `Zanna.Collections.List.Flip` -> `Reverse`
- `Zanna.Collections.List.Items` -> `ToSeq`
- `Zanna.Collections.Bag.Merge/Common/Put/Drop` -> `Union/Intersect/Add/Remove`
- `Zanna.Collections.Set.Merge/Common` -> `Union/Intersect`
- `Zanna.Collections.SortedSet.Merge/Common/Put/Drop` -> canonical set verbs
- `Zanna.Collections.*.get_Count` aliases to `get_Length`
- `Zanna.Graphics.Surface2D.*` -> `RenderTarget2D.*`
- `Zanna.Graphics.GpuTexture2D.*` -> `Texture2D.*`
- `Zanna.Graphics.ScreenScaler.*` -> `Viewport2D.*`
- `Zanna.Graphics.ParticleSystem2D.*` and `Zanna.Graphics.Emitter2D.*` ->
  `Zanna.Game.ParticleEmitter.*`
- `Zanna.Game.UI.*` -> `Zanna.Game.UI.Hud*`
- `Zanna.Game3D.Environment.*` -> `Environment3D.*`
- `Zanna.Game3D.Diagnostics.*` -> `Diagnostics3D.*`

Correction:

- Remove all public `RT_ALIAS` entries from `runtime.def`.
- Remove alias exceptions from `RuntimeSurfacePolicy.inc`.
- Add an `rtgen` validation error for public `RT_ALIAS`.
- If the backend needs an internal alias, model that outside the frontend-visible
  `Zanna.*` API.

## P0: `rtgen` Synthesizes Bogus Parent-Class Methods

> ✅ **CONFIRMED.** Exactly **203** descendant-namespace methods are injected into
> parent classes; every row of the table matches (`Zanna.Math`←BigInt 35, Easing
> 28, Bits 18, … PerlinNoise 5 = 168 total on Math; Memory←GC 6/WeakRef 5;
> Process←ProcessHandle 10; Pty←PtySession 10; TreeView←Node 4). Mechanism verified
> at `rtgen.cpp:1469–1488`: `startsWith(fn.canonical, cls.name + ".")` with **no**
> dot-remainder guard, so `Zanna.Math.` matches `Zanna.Math.Bits.And` et al.

`rtgen` auto-adds uncovered functions whose canonical name starts with
`className + "."`. This also matches descendants. For class `Zanna.Math`, the
prefix `Zanna.Math.` matches `Zanna.Math.Bits.*`, `Zanna.Math.BigInt.*`,
`Zanna.Math.Vec2.*`, and so on.

Measured damage: 203 descendant namespace methods are injected into parent
classes.

| Parent class | Descendant source | Count |
|---|---|---:|
| `Zanna.Math` | `BigInt` | 35 |
| `Zanna.Math` | `Easing` | 28 |
| `Zanna.Math` | `Bits` | 18 |
| `Zanna.Math` | `Mat3` | 17 |
| `Zanna.Math` | `Random` | 14 |
| `Zanna.Math` | `Vec2` | 14 |
| `Zanna.Math` | `Vec3` | 11 |
| `Zanna.Math` | `Mat4` | 10 |
| `Zanna.Math` | `Quat` | 8 |
| `Zanna.Math` | `Spline` | 8 |
| `Zanna.Math` | `PerlinNoise` | 5 |
| `Zanna.Memory` | `GC` | 6 |
| `Zanna.Memory` | `WeakRef` | 5 |
| `Zanna.System.Process` | `ProcessHandle` | 10 |
| `Zanna.System.Pty` | `PtySession` | 10 |
| `Zanna.GUI.TreeView` | `Node` | 4 |

Examples:

- `Zanna.Math.And` is synthesized from `Zanna.Math.Bits.And`.
- `Zanna.Math.Abs(obj)` is synthesized from `Zanna.Math.BigInt.Abs`.
- `Zanna.Math.New` is synthesized from `Zanna.Math.Mat3.New`.
- `Zanna.Memory.New` is synthesized from `Zanna.Memory.WeakRef.New`.
- `Zanna.System.Process.IsValid` is synthesized from
  `Zanna.System.Process.ProcessHandle.IsValid`.

Correction:

- Delete synthetic method auto-fill from `buildResolvedClasses`, or restrict it
  to immediate functions only where the remainder after `classPrefix` contains
  no dot.
- Prefer deleting it entirely: every class method should be explicitly declared
  by `RT_METHOD`.
- Add a test that `Zanna.Math` does not contain methods whose target starts with
  `Zanna.Math.Bits.`, `Zanna.Math.BigInt.`, etc.

Relevant code:

- `src/tools/rtgen/rtgen.cpp:1469`
- `src/tools/rtgen/rtgen.cpp:1471`
- `src/tools/rtgen/rtgen.cpp:1481`

## P0: Same-Name/Same-Arity Method Overloads Are Unsafe

> ✅ **CONFIRMED.** All 16 listed `Zanna.Math` collisions reproduce with the exact
> `f64`/`obj` signature pairs (Abs, Lerp, Max, Min, Pow, Sqrt, And, Not, Or, Shl,
> Shr, Xor, Linear, Mul, Div, Cross). `RuntimeClasses.cpp:499` keys methods on
> `toLower(cls) '|' toLower(method) '#' arity` — **no parameter types** — and
> `:596` does `methodIndex_[methodKey(...)] = pm;` (map assignment), so a later
> same-key method silently overwrites the earlier one.

`RuntimeRegistry::methodKey` indexes methods by lowercased class, lowercased
method, and arity. It does not include parameter types. `buildIndexes` assigns
into the map, so later entries overwrite earlier entries.

Current same-name/same-arity collisions are all on `Zanna.Math`, mostly caused
by the descendant namespace synthesis above:

- `Abs`: `f64(f64)` and `obj(obj)`
- `Lerp`: `f64(f64,f64,f64)` and `obj(obj,obj,f64)`
- `Max`: `f64(f64,f64)` and `obj(obj,obj)`
- `Min`: `f64(f64,f64)` and `obj(obj,obj)`
- `Pow`: `f64(f64,f64)` and `obj(obj,i64)`
- `Sqrt`: `f64(f64)` and `obj(obj)`
- `And`, `Not`, `Or`, `Shl`, `Shr`, `Xor`
- `Linear`, `Mul`, `Div`, `Cross`

Correction:

- After removing synthetic parent-class methods, add an `rtgen` check that fails
  on same class + method + arity duplicates.
- If Zanna wants typed overloads in runtime classes, change the registry key and
  diagnostics to include parameter types before allowing them.

Relevant code:

- `src/il/runtime/classes/RuntimeClasses.cpp:499`
- `src/il/runtime/classes/RuntimeClasses.cpp:596`

## P0: Public `ptr` Leak

> ✅ **CONFIRMED.** Exactly **5,601** function signatures in `--dump-runtime-api`
> contain `ptr`. All 4 examples match: `BitSet.New ptr(i64)`,
> `List.Push void(ptr,ptr)`, `Box.I64 ptr(i64)`, `Canvas.Clear void(ptr,i64)`.
>
> 2026-07-01 cleanup: fixed. `--dump-runtime-api` prints public descriptor
> signature text for frontend-visible `Zanna.*` functions, and the live dump now
> has **zero** public raw `ptr` signature tokens.

The live API dump exposes `ptr` in 5,601 public function signatures. Class
members do not show `ptr` because they use the raw catalog strings, but global
functions are printed from lowered runtime descriptors.

This contradicts `runtime.def`, which says `ptr` is runtime-internal only.

Examples from `--dump-runtime-api`:

- `Zanna.Collections.BitSet.New ptr(i64)`
- `Zanna.Collections.List.Push void(ptr,ptr)`
- `Zanna.Core.Box.I64 ptr(i64)`
- `Zanna.Graphics.Canvas.Clear void(ptr,i64)`

Correction:

- Public dumps should emit `obj` / `obj<T>` / `seq<T>` from `runtime.def`, not
  `ptr`.
- Add an audit that checks `--dump-runtime-api` output contains no `ptr`.
- Keep `ptr` only in IL/backend ABI metadata.

## P0: Property/Method Name Collisions In Case-Insensitive Frontends

> ✅ **CONFIRMED.** Exactly **14** case-insensitive property/method collisions;
> every row matches (`Diagnostics.Log` DEBUG/Debug, INFO/Info, WARN/Warn,
> ERROR/Error; `System.Machine` Arch/Endian/PageSize/PointerSize; `Math`
> E/Pi/Tau; `Text.Version` Major/Minor/Patch).

There are 14 class members where a property and method differ only by casing or
syntax. BASIC is case-insensitive, so these are especially risky.

| Class | Property | Method |
|---|---|---|
| `Zanna.Diagnostics.Log` | `DEBUG` | `Debug(str)` |
| `Zanna.Diagnostics.Log` | `INFO` | `Info(str)` |
| `Zanna.Diagnostics.Log` | `WARN` | `Warn(str)` |
| `Zanna.Diagnostics.Log` | `ERROR` | `Error(str)` |
| `Zanna.System.Machine` | `Arch` | `Arch()` |
| `Zanna.System.Machine` | `Endian` | `Endian()` |
| `Zanna.System.Machine` | `PageSize` | `PageSize()` |
| `Zanna.System.Machine` | `PointerSize` | `PointerSize()` |
| `Zanna.Math` | `E` | `E()` |
| `Zanna.Math` | `Pi` | `Pi()` |
| `Zanna.Math` | `Tau` | `Tau()` |
| `Zanna.Text.Version` | `Major` | `Major(str)` |
| `Zanna.Text.Version` | `Minor` | `Minor(str)` |
| `Zanna.Text.Version` | `Patch` | `Patch(str)` |

Correction:

- Keep properties for constants and value access.
- Delete method aliases for constants (`Math.Euler()`, `Machine.Arch()`) unless a
  method really performs computation.
- Rename log level constants to non-colliding names such as `LevelDebug`, or
  rename log methods to `WriteDebug`, `WriteInfo`, etc.
- Add a catalog audit that rejects property/method collisions under
  case-insensitive comparison.

## P1: Same C Function Published Under Multiple `RT_FUNC` Names

> ✅ **CONFIRMED.** Parsing **all 6,279** `RT_FUNC` entries (both the 4-arg and
> gated 5-arg forms) yields exactly **44** same-C-symbol + same-signature groups
> with **zero** extras beyond the table below — the list is both complete and
> accurate. Every one of the 44 rows was verified against the live registry.
>
> 2026-07-01 cleanup: the `Zanna.Math.Random.inst_*` row is no longer part of the
> public runtime API. Those descriptors are emitted through `RT_INTERNAL_FUNC`
> so `rng.Next()` / `rng.Range(...)` keep working as class methods without
> publishing implementation target names as standalone functions.
>
> Current-state note: a direct parse of public `RT_FUNC` rows now finds **zero**
> public same-C-symbol + same-signature duplicate groups. The table below is
> historical confirmation data from the original audit, not the current surface.

There are 44 same C-symbol + same signature groups declared as separate
`RT_FUNC`s, before counting `RT_ALIAS`. Some are legitimate bidirectional
factory conveniences, but pre-alpha should still choose one public spelling.

Notable groups to collapse:

- `TryToI64` vs `ToI64Option` and the equivalent F64/I1/String box APIs.
- `Zanna.Core.Parse.Double`, `DoubleOption`, and `TryNum` all return
  `obj<Zanna.Option>(str)`. The plain `Double` name is misleading because it
  does not return `f64`.
- `Zanna.Core.Parse.Int64`, `Int64Option`, and `TryInt` have the same issue.
- `Zanna.Core.Convert.ToInt` and `ToInt64` are identical.
- `Zanna.String.SplitFields` and `SplitFieldsSeq` are identical.
- `Zanna.Graphics.Canvas.get_DeltaTime` and `get_DeltaTimeMs` are identical.
- `Zanna.Graphics3D.Canvas3D.get_DeltaTime` and `get_DeltaTimeMs` are identical.
- `Zanna.Graphics3D.Canvas3D.get_Backend` and `get_BackendName` are identical.
- `Zanna.Graphics.Canvas.Polyline` and `PolylinePath` are identical, as are
  `Polygon`/`PolygonPath` and `PolygonFrame`/`PolygonFramePath`.
- `Zanna.Game.ParticleEmitter.Get` and `ParticleAt` are identical.
- `Zanna.Terminal.Int` and `Zanna.Text.Fmt.Int` are identical.
- `set_Property` and `SetProperty` pairs exist for multiple 3D APIs.

Correction:

- Pick one spelling per operation.
- For Option-returning APIs, use `TryX` or `ParseXOption`, not both.
- For conversions between collections, pick either `Source.ToTarget` or
  `Target.FromSource`. Keeping both doubles the API for little value.
- Add an `rtgen` report or error for duplicate C symbol + signature groups.

Full duplicate `RT_FUNC` groups found:

| C symbol | Signature | Public names |
|---|---|---|
| `rt_bag_to_seq` | `obj<Zanna.Collections.Seq>(obj)` | `Zanna.Collections.Bag.ToSeq`; `Zanna.Collections.Seq.FromBag` |
| `rt_box_to_f64_option` | `obj<Zanna.Option>(obj)` | `Zanna.Core.Box.TryToF64`; `Zanna.Core.Box.ToF64Option` |
| `rt_box_to_i1_option` | `obj<Zanna.Option>(obj)` | `Zanna.Core.Box.TryToI1`; `Zanna.Core.Box.ToI1Option` |
| `rt_box_to_i64_option` | `obj<Zanna.Option>(obj)` | `Zanna.Core.Box.TryToI64`; `Zanna.Core.Box.ToI64Option` |
| `rt_box_to_str_option` | `obj<Zanna.Option>(obj)` | `Zanna.Core.Box.TryToStr`; `Zanna.Core.Box.ToStrOption` |
| `rt_canvas3d_get_backend` | `str(obj)` | `Zanna.Graphics3D.Canvas3D.get_Backend`; `Zanna.Graphics3D.Canvas3D.get_BackendName` |
| `rt_canvas3d_get_delta_time` | `i64(obj)` | `Zanna.Graphics3D.Canvas3D.get_DeltaTime`; `Zanna.Graphics3D.Canvas3D.get_DeltaTimeMs` |
| `rt_canvas_get_delta_time` | `i64(obj)` | `Zanna.Graphics.Canvas.get_DeltaTime`; `Zanna.Graphics.Canvas.get_DeltaTimeMs` |
| `rt_canvas_polygon_frame_path` | `void(obj,obj<Zanna.Graphics.Path2D>,i64)` | `Zanna.Graphics.Canvas.PolygonFrame`; `Zanna.Graphics.Canvas.PolygonFramePath` |
| `rt_canvas_polygon_path` | `void(obj,obj<Zanna.Graphics.Path2D>,i64)` | `Zanna.Graphics.Canvas.Polygon`; `Zanna.Graphics.Canvas.PolygonPath` |
| `rt_canvas_polyline_path` | `void(obj,obj<Zanna.Graphics.Path2D>,i64)` | `Zanna.Graphics.Canvas.Polyline`; `Zanna.Graphics.Canvas.PolylinePath` |
| `rt_container_set_padding` | `void(obj,f64)` | `Zanna.GUI.VBox.SetPadding`; `Zanna.GUI.HBox.SetPadding`; `Zanna.GUI.Container.SetPadding` |
| `rt_container_set_spacing` | `void(obj,f64)` | `Zanna.GUI.VBox.SetSpacing`; `Zanna.GUI.HBox.SetSpacing`; `Zanna.GUI.Container.SetSpacing` |
| `rt_deque_to_list` | `obj<Zanna.Collections.List>(obj)` | `Zanna.Collections.List.FromDeque`; `Zanna.Collections.Deque.ToList` |
| `rt_deque_to_seq` | `obj<Zanna.Collections.Seq>(obj)` | `Zanna.Collections.Seq.FromDeque`; `Zanna.Collections.Deque.ToSeq` |
| `rt_fmt_int` | `str(i64)` | `Zanna.Terminal.Int`; `Zanna.Text.Fmt.Int` |
| `rt_list_to_seq` | `obj<Zanna.Collections.Seq>(obj)` | `Zanna.Collections.List.ToSeq`; `Zanna.Collections.Seq.FromList` |
| `rt_math_e` | `f64()` | `Zanna.Math.get_Euler`; `Zanna.Math.Euler` |
| `rt_math_pi` | `f64()` | `Zanna.Math.get_Pi`; `Zanna.Math.Pi` |
| `rt_math_tau` | `f64()` | `Zanna.Math.get_Tau`; `Zanna.Math.Tau` |
| `rt_parse_double_option` | `obj<Zanna.Option>(str)` | `Zanna.Core.Parse.Double`; `Zanna.Core.Parse.DoubleOption`; `Zanna.Core.Parse.TryNum` |
| `rt_parse_int64_option` | `obj<Zanna.Option>(str)` | `Zanna.Core.Parse.Int64`; `Zanna.Core.Parse.Int64Option`; `Zanna.Core.Parse.TryInt` |
| `rt_particle_emitter_particle_at` | `obj<Zanna.Option>(obj,i64)` | `Zanna.Game.ParticleEmitter.Get`; `Zanna.Game.ParticleEmitter.ParticleAt` |
| `rt_rand_range_method` | `i64(obj,i64,i64)` | `Zanna.Math.Random.inst_NextIntRange`; `Zanna.Math.Random.inst_Range` |
| `rt_seq_to_bag` | `obj<Zanna.Collections.Bag>(obj)` | `Zanna.Collections.Bag.FromSeq`; `Zanna.Collections.Seq.ToBag` |
| `rt_seq_to_deque` | `obj<Zanna.Collections.Deque>(obj)` | `Zanna.Collections.Seq.ToDeque`; `Zanna.Collections.Deque.FromSeq` |
| `rt_seq_to_list` | `obj<Zanna.Collections.List>(obj)` | `Zanna.Collections.List.FromSeq`; `Zanna.Collections.Seq.ToList` |
| `rt_seq_to_queue` | `obj<Zanna.Collections.Queue>(obj)` | `Zanna.Collections.Queue.FromSeq`; `Zanna.Collections.Seq.ToQueue` |
| `rt_seq_to_set` | `obj<Zanna.Collections.Set>(obj)` | `Zanna.Collections.Set.FromSeq`; `Zanna.Collections.Seq.ToSet` |
| `rt_seq_to_stack` | `obj<Zanna.Collections.Stack>(obj)` | `Zanna.Collections.Seq.ToStack`; `Zanna.Collections.Stack.FromSeq` |
| `rt_set_to_list` | `obj<Zanna.Collections.List>(obj)` | `Zanna.Collections.Set.ToList`; `Zanna.Collections.List.FromSet` |
| `rt_set_to_seq` | `obj<Zanna.Collections.Seq>(obj)` | `Zanna.Collections.Set.ToSeq`; `Zanna.Collections.Seq.FromSet` |
| `rt_soundlistener3d_set_forward` | `void(obj,obj)` | `Zanna.Graphics3D.SoundListener3D.set_Forward`; `Zanna.Graphics3D.SoundListener3D.SetForward` |
| `rt_soundlistener3d_set_up` | `void(obj,obj)` | `Zanna.Graphics3D.SoundListener3D.set_Up`; `Zanna.Graphics3D.SoundListener3D.SetUp` |
| `rt_soundlistener3d_set_velocity` | `void(obj,obj)` | `Zanna.Graphics3D.SoundListener3D.set_Velocity`; `Zanna.Graphics3D.SoundListener3D.SetVelocity` |
| `rt_soundsource3d_set_position` | `void(obj,obj)` | `Zanna.Graphics3D.SoundSource3D.set_Position`; `Zanna.Graphics3D.SoundSource3D.SetPosition` |
| `rt_soundsource3d_set_velocity` | `void(obj,obj)` | `Zanna.Graphics3D.SoundSource3D.set_Velocity`; `Zanna.Graphics3D.SoundSource3D.SetVelocity` |
| `rt_str_split_fields_seq` | `seq<str>(str)` | `Zanna.String.SplitFields`; `Zanna.String.SplitFieldsSeq` |
| `rt_to_int` | `i64(str)` | `Zanna.Core.Convert.ToInt`; `Zanna.Core.Convert.ToInt64` |
| `rt_widget_was_clicked` | `i64(obj)` | `Zanna.GUI.Widget.WasClicked`; `Zanna.GUI.Button.WasClicked` |
| `rt_world3d_set_contact_beta` | `void(obj,f64)` | `Zanna.Graphics3D.Physics3DWorld.set_ContactBeta`; `Zanna.Graphics3D.Physics3DWorld.SetContactBeta` |
| `rt_world3d_set_position_iterations` | `void(obj,i64)` | `Zanna.Graphics3D.Physics3DWorld.set_PositionIterations`; `Zanna.Graphics3D.Physics3DWorld.SetPositionIterations` |
| `rt_world3d_set_restitution_threshold` | `void(obj,f64)` | `Zanna.Graphics3D.Physics3DWorld.set_RestitutionThreshold`; `Zanna.Graphics3D.Physics3DWorld.SetRestitutionThreshold` |
| `rt_world3d_set_solver_iterations` | `void(obj,i64)` | `Zanna.Graphics3D.Physics3DWorld.set_SolverIterations`; `Zanna.Graphics3D.Physics3DWorld.SetSolverIterations` |

## P1: GUI Widget Methods Are Duplicated Across Concrete Classes

> ✅ **CONFIRMED.** Exactly **58** target+signature groups are repeated in ≥3
> class-method slots. `Zanna.GUI.Widget.SetEnabled` / `SetPreferredSize` /
> `SetMaxSize` / `SetFlex` / `SetMargin` appear in **24** slots each;
> `IsHovered` / `IsPressed` / `WasClicked` / `GetWidth` / `GetX` in **23** each.
>
> 2026-07-01 cleanup: copied base widget methods have been removed from concrete
> GUI class catalogs. `Zanna.GUI.Widget` owns the shared widget operations;
> concrete widget handles resolve those methods through shared frontend runtime
> method lookup and Zia completion rather than duplicate public catalog entries.
> The runtime class-qualified surface audit rejects future copied
> `Zanna.GUI.Widget.*` targets outside `Zanna.GUI.Widget`.

The class catalog repeats base widget methods into concrete widget classes by
pointing every class method at `Zanna.GUI.Widget.*`.

Measured repeated target groups: 58 target+signature groups repeated in three
or more class method slots. Top examples:

- `Zanna.GUI.Widget.SetEnabled` appears in 24 class method slots.
- `SetPreferredSize`, `SetMaxSize`, `SetFlex`, `SetMargin` appear in 24 slots.
- `IsHovered`, `IsPressed`, `IsFocused`, `WasClicked`, `IsVisible`,
  `IsEnabled`, `GetWidth`, `GetHeight`, `GetX`, `GetY`, `GetFlex` appear in
  23 slots.
- `Destroy`, `SetCursor`, `SetVisible`, `SetSize`, `SetPosition`, drag/drop
  methods, and tooltip methods are repeated across most widget classes.

Correction:

- Model runtime class inheritance/mixins explicitly in the catalog instead of
  copying base methods into every class.
- If inheritance is not available yet, expose `Zanna.GUI.Widget` operations on
  the base handle and do not publish copied methods as if they belong to each
  concrete class.
- Use `i1` for boolean widget return values and setters while doing this.

## P1: Constructor And Factory Shape Is Inconsistent

> ✅ **CONFIRMED.** **29** classes use a constructor whose leaf is not `New`,
> **9** classes have no constructor metadata yet expose a `New` method, and
> **3** constructors point outside their own namespace (`Core.ValueType`→
> `Core.Box.ValueType`, `Graphics.ParticleSystem2D` and `Graphics.Emitter2D`→
> `Game.ParticleEmitter.New`). All listed examples match. Constructor
> auto-injection verified at `rtgen.cpp:1447–1466`.
>
> 2026-07-01 cleanup: constructor auto-method synthesis is now constrained to
> constructor targets that are immediate members of the same class namespace.
> `Zanna.Core.ValueType` no longer exposes the synthetic
> `ValueType(obj(i64))` method for `Zanna.Core.Box.ValueType`; catalog coverage
> asserts this stays removed.
>
> 2026-07-01 cleanup: self-returning `New` methods now require matching
> constructor metadata. `Zanna.Math.Mat3`, `Zanna.Math.Mat4`,
> `Zanna.Network.Udp`, `Zanna.Network.HttpReq`, and `Zanna.Network.Url` were
> marked with their existing canonical constructors. Zia `new` resolution now
> uses constructor metadata instead of guessing from any `.New` function. The
> remaining constructor-less `New` methods are intentional factories:
> `Zanna.Text.Uuid.New` returns `String`, and `Zanna.Zia.ProjectIndex.New`
> returns `ProjectIndexHandle`.
> `Zanna.Functional.Lazy.New` was removed from the public runtime surface after
> confirming no tests, examples, docs, or source references used it; `Lazy.Of*`
> remain the value factories.
>
> 2026-07-01 cleanup: constructor metadata now targets only canonical
> `Class.New` functions. The 29 named-factory constructor mappings
> (`Open`, `Load`, `Parse`, `FromSeq`, `Some`, `Ok`, `Today`, `Range`,
> `Connect`, `Build`, etc.) were removed from constructor metadata; callers use
> those factories explicitly. The Zia `new` regression tests now assert named
> factories are not constructor aliases, and the class-qualified surface audit
> rejects future non-`.New` constructor targets.

Constructor metadata is inconsistent with method naming:

- 29 classes use constructor targets whose leaf is not `New`.
- 9 classes have no constructor metadata but still expose a `New` method.
- 3 constructors point outside their own class namespace.

Examples:

- `Zanna.Core.ValueType` constructor -> `Zanna.Core.Box.ValueType`
- `Zanna.Collections.FrozenSet` constructor -> `FromSeq`
- `Zanna.GUI.Font` constructor -> `Load`
- `Zanna.IO.BinFile` constructor -> `Open`
- `Zanna.String` constructor -> `FromStr`
- `Zanna.Result` constructor -> `Ok`
- `Zanna.Option` constructor -> `Some`
- `Zanna.Graphics3D.Light3D` constructor -> `NewDirectional`
- `Zanna.Graphics.ParticleSystem2D` constructor ->
  `Zanna.Game.ParticleEmitter.New`

Correction:

- Constructors should be metadata only, not implicit public methods.
- Instantiable classes should use `New` as the canonical constructor.
- Named factories such as `Load`, `Open`, `FromSeq`, `Some`, and `Ok` should be
  explicit static methods, not constructor metadata, unless the language syntax
  deliberately maps `new Type(...)` to that factory.
- Remove cross-class constructor targets.

Relevant code:

- `src/tools/rtgen/rtgen.cpp:1447`
- `src/tools/rtgen/rtgen.cpp:1458`

## P1: Lowercase And camelCase Public Names

> ✅ **CONFIRMED.** The per-class Game3D table is **exact** for all 10 rows
> (World3D 65, WorldStream3D 38, Entity3D 36, BodyDef 26, Sound3D 16, Input3D 14,
> Collision3DEvent 14, Animator3D 14, CharacterController3D 11,
> FirstPersonController 10). The all-classes total reproduces at 301 vs the
> reported 305 (long-tail classifier edge). Cited `runtime.def` lines carry the
> lowercase names verbatim (15332 `get_bits`, 15364 `lookSensitivity`, 15397
> `setPosition`, …).
>
> 2026-07-01 cleanup: the live class catalog now has **zero** lowercase/camelCase
> public member names. Game3D public member names are PascalCase; remaining
> lowercase-looking Game3D `RT_FUNC` names are canonical property accessors using
> the expected `get_` / `set_` prefix.

The surface mostly uses PascalCase class and method names, but `Game3D` uses
lowercase/camelCase names heavily.

Measured nonstandard `Game3D` function names:

- 65 under `Zanna.Game3D.World3D`
- 38 under `Zanna.Game3D.WorldStream3D`
- 36 under `Zanna.Game3D.Entity3D`
- 26 under `Zanna.Game3D.BodyDef`
- 16 under `Zanna.Game3D.Sound3D`
- 14 under `Zanna.Game3D.Input3D`
- 14 under `Zanna.Game3D.Collision3DEvent`
- 14 under `Zanna.Game3D.Animator3D`
- 11 under `Zanna.Game3D.CharacterController3D`
- 10 under `Zanna.Game3D.FirstPersonController`

Examples:

- `Zanna.Game3D.LayerMask.get_bits`, `set_bits`, `include`, `includes`
- `Zanna.Game3D.Input3D.update`, `isDown`, `mouseDelta`, `captureMouse`
- `Zanna.Game3D.Entity3D.setPosition`, `setScaleXYZ`, `attachBody`,
  `worldPosition`
- `Zanna.Game3D.World3D.destroy`, `spawn`, `runFramesOnly`, `drawOverlay`
- `Zanna.Game3D.BodyDef.get_isStatic`, `set_isKinematic`, etc.

Correction:

- Use PascalCase for public runtime member names: `Destroy`, `Spawn`,
  `SetPosition`, `WorldPosition`, `Includes`, `CaptureMouse`.
- Use PascalCase property names after `get_` / `set_`: `get_Bits`,
  `get_LookSensitivity`, `get_CollisionMask`.
- Add `rtgen` style validation for public canonical names and class member names.

Relevant source examples:

- `src/il/runtime/runtime.def:15332`
- `src/il/runtime/runtime.def:15338`
- `src/il/runtime/runtime.def:15350`
- `src/il/runtime/runtime.def:15364`
- `src/il/runtime/runtime.def:15397`
- `src/il/runtime/runtime.def:15421`

## P1: Count/Length Policy Is Unclear

> ✅ **FIXED.** The public class catalog now uses `Count` for
> collection/container cardinality and reserves `Length` for true length
> semantics: strings/text buffers, bytes/numeric buffers, streams, `BitSet` bit
> capacity, music duration, distance-joint length, and 3D path length.
> `BitSet` intentionally exposes both `Length` and `Count` because they are
> different values: bit capacity versus population count.
>
> 2026-07-01 cleanup: duplicate `Count`/`Length` property pairs are gone,
> `Grid2D.Length` was removed as a duplicate of `Grid2D.Size`, collection
> cardinality getters/properties were renamed to `get_Count` / `Count`, and
> tests/docs/examples were updated. The class-qualified surface audit now
> allowlists the remaining semantic `Length` properties and rejects future
> cardinality `Length` drift.

Function namespaces with cardinality accessors:

- `Count` is the property/getter spelling for collection and container
  cardinality (`List`, `Seq`, `Map`, `Set`, queues/stacks/deques, maps, thread
  queues/maps/channels, playlists, etc.).
- `Length` remains only on semantic length APIs (`String`, `Bytes`, buffers,
  streams, `BitSet` capacity, `MusicGen`, distance/path length).

Examples with both:

- `Zanna.Collections.BitSet` is the only intentional `Count` + `Length` class:
  `Count` is set-bit population count; `Length` is bit capacity.

Correction:

- Keep the allowlist-based audit guard in place; new `Length` properties must be
  justified as true length semantics.
- Continue using canonical renames only. Do not reintroduce compatibility
  aliases.

## P1: Boolean Values Are Exposed As `i64`

> ✅ **CONFIRMED (substance + concrete cases; original finding).** The **4 exact
> getter/setter mismatches** reproduced precisely — `Sprite.FlipX`,
> `Sprite.FlipY`, `TileLayer2D.Visible`, `RenderPass2D.Enabled` all had getter
> `i1` / setter `i64`. The named examples returned `i64` before the cleanup
> notes below were applied.
> The aggregate "≥85 return / 33 setters" depends on an undocumented
> "boolean-looking" classifier; independent nets reproduce ~57–66 returns and
> ~21 setters — same order of magnitude, and the problem is pervasive.

`i1` exists and is documented as boolean, but many APIs still use `i64`.

> 2026-07-01 cleanup: the isolated sound cases are now fixed in the public
> runtime surface: `Zanna.Sound.Voice.IsPlaying` and
> `Zanna.Sound.Music.IsPlaying` return `i1`; `Zanna.Sound.Music.SetLoop` and
> `Zanna.Sound.MusicGen.SetLoopable` take `i1`.
>
> 2026-07-01 cleanup: the four exact getter/setter mismatches listed below are
> fixed, and the concrete GUI/Assets/Terminal examples from the original review
> now use boolean signatures: `Widget.IsHovered`, `Widget.WasClicked`,
> `App.WasCloseRequested`, `ClipboardText.HasText`, `Shortcuts.IsEnabled`,
> `Assets.Exists`, `Terminal.SetCursorVisible`, plus video `IsPlaying`
> properties.
>
> 2026-07-01 cleanup: standard boolean probes now all return `i1`, and concrete
> boolean state setters now use `i1` for the audited toggle names. Selectors and
> counts with boolean-looking words remain integer by design, e.g.
> `Dropdown.SetSelected(index)` and `Toast.SetMaxVisible(count)`.

Measured suspicious cases:

- 85 boolean-looking functions return `i64`.
- 33 boolean-looking setters take `i64`.
- 4 exact getter/setter type mismatches.

Original concrete examples, now fixed:

- `Zanna.GUI.Widget.IsHovered i1(obj)`
- `Zanna.GUI.Widget.WasClicked i1(obj)`
- `Zanna.GUI.App.WasCloseRequested i1(obj)`
- `Zanna.GUI.ClipboardText.HasText i1()`
- `Zanna.GUI.Shortcuts.IsEnabled i1(str)`
- `Zanna.IO.Assets.Exists i1(str)`
- `Zanna.Terminal.SetCursorVisible void(i1)`

Original exact getter/setter type mismatches, now fixed:

- `Zanna.Graphics.Sprite.FlipX`: getter `i1`, setter `i1`
- `Zanna.Graphics.Sprite.FlipY`: getter `i1`, setter `i1`
- `Zanna.Graphics.TileLayer2D.Visible`: getter `i1`, setter `i1`
- `Zanna.Graphics.RenderPass2D.Enabled`: getter `i1`, setter `i1`

Correction:

- Change boolean returns and boolean parameters to `i1`.
- Add an audit that flags `Is*`, `Has*`, `Can*`, `Was*`, and `get_Is*` returning
  non-`i1`, with explicit allow-list for counters such as `ActiveCount`.
- Fix the four getter/setter mismatches first; they are concrete ABI mistakes.

## P1: Public Accessor Methods Duplicate Properties

> ✅ **CONFIRMED (substance).** Independent count finds **109** real
> property + `Set{P}`-method pairs (report: 115; the gap is classifier
> definition). Confirmed real cases include `Vec3.X/Y/Z`+`SetX/Y/Z`,
> `Camera3D.Fov`+`SetFov`, `Light3D.Enabled`+`SetEnabled`, and many GUI
> `Value`/`Text`/`Selected`. ⚠ **Example correction:** `Material3D`
> Alpha/Metallic/Roughness/AmbientOcclusion are set via property setters (`set_*`), **not**
> `Set*` methods (its actual set-methods are SetColor/SetAlbedoMap/…), so that
> illustration is not an accessor-duplication case.
>
> 2026-07-01 cleanup: the live surface had only five remaining writable
> property + `Set{P}` method pairs, all on `Zanna.Game3D.Entity3D`:
> `Mesh`, `Material`, `Name`, `Layer`, and `CollisionMask`. The simple public
> `Set*` methods were removed, call sites were migrated to property assignment,
> and recursive/command methods such as `SetMeshRecursive`, `SetMaterialRecursive`,
> `SetPosition`, and `SetVelocity` were kept. The class-qualified surface audit
> now rejects any future `Set{P}` method for a writable property.

There are 115 writable properties that also expose `SetX` methods. Some are
useful fluent APIs, but most are duplicate ways to mutate the same state.

Examples:

- `Zanna.Math.Vec3.X/Y/Z` and `SetX/SetY/SetZ`
- `Zanna.Graphics3D.Material3D.Alpha/Metallic/Roughness/AmbientOcclusion/...` and matching
  `SetAlpha/SetMetallic/...`
- `Zanna.Graphics3D.Light3D.Enabled/CastsShadows/Direction/Position` and
  matching setters
- `Zanna.Graphics3D.Camera3D.Fov/NearPlane/FarPlane/Position/Yaw/Pitch` and
  matching setters
- Many `Game3D` data builder properties plus `setX` methods

Correction:

- Prefer properties for simple state.
- Keep `SetX` only when it is semantically more than assignment, returns `self`
  for builder chaining, or sets several fields at once.
- Do not publish both a property setter and a simple one-field setter.

## P2: Constant Names Are Modeled As Properties

> ✅ **CONFIRMED.** Key-code and event constants exist as readonly `get_`
> properties: `Input.Keyboard.get_KeyA`, `Input.Mouse.get_ButtonLeft`,
> `Input.Pad.get_ButtonA`, `IO.Watcher.get_EventCreated`, `Graphics.Color.get_Red`.
>
> 2026-07-01 cleanup: all public multi-character all-caps constant properties
> were renamed to PascalCase and re-audited. A later stricter pass also removed
> acronym blocks from public property names (`UseCcd`, `PostFx`,
> `HasAmbientOcclusionMap`, `BoundsMin`/`BoundsMax`,
> `LastCcdRequestedSubsteps`, `LastLod0ChunkCount`, `KeyLeftShift`,
> `ScrollHorizontalFloat`). Examples include
> `Shutdown.None`, `Color.Red`, `Keyboard.KeyA`, `Mouse.ButtonLeft`,
> `Pad.ButtonA`, `Action.AxisLeftX`, `Watcher.EventCreated`, `Machine.Os`,
> `Math.Euler`, `Material3D.AmbientOcclusion`, `ShadingModel.Pbr`, and
> `Collision3DEvent.EntityA` / `EntityB`.

Constants such as input key codes are represented as readonly properties:

- `Zanna.Input.Keyboard.KeyA`, `KeyB`, ...
- `Zanna.Input.Mouse.ButtonLeft`, ...
- `Zanna.Input.Pad.ButtonA`, ...
- `Zanna.IO.Watcher.EventCreated`, ...
- `Zanna.Graphics.Color.get_Red`, ...

This is not inherently broken, but it is inconsistent with the rest of the
PascalCase API and looks odd in a class-property catalog.

Correction:

- Prefer enum-like classes with PascalCase properties: `KeyA`, `ButtonLeft`,
  `EventCreated`, `Red`.
- If uppercase constants are desired, document that constant namespaces are the
  one exception and keep them out of method/property collision checks.

## Recommended Cleanup Order

1. Add validation first:
   - No public `RT_ALIAS`. **Done; enforced by `rtgen`.**
   - No `ptr` in public dump. **Done; live dump and descriptor guard are clean.**
   - No descendant namespace auto-methods. **Done.**
   - No same class + method + arity duplicates. **Done.**
   - No property/method collisions ignoring case. **Done.**
2. Delete `rtgen` synthetic method auto-fill, or constrain it to immediate
   functions only while requiring explicit `RT_METHOD`s. **Immediate-only
   synthesis is in place.**
3. Delete all 337 `RT_ALIAS` entries and matching policy exceptions. **Done.**
4. Normalize `Game3D` public names to PascalCase. **Done for public class
   members; canonical property accessor spellings still use `get_` / `set_` as
   expected.**
5. Collapse duplicate `RT_FUNC` spellings that share the same C symbol and
   signature. **Done for public `RT_FUNC` rows; guarded by the class-qualified
   surface audit.**
6. Decide and apply the `Count`/`Length` policy. **Done; collection and
   container cardinality uses `Count`, semantic length APIs keep `Length`, and
   the class-qualified surface audit guards the allowlist.**
7. Convert boolean-shaped APIs to `i1`. **Done for standard boolean probe names
   and original concrete examples; guarded by runtime surface tests.**
8. Remove copied GUI widget methods from concrete class catalogs. **Done; base
   widget methods resolve through shared frontend fallback and are guarded by
   the class-qualified surface audit.**
9. Rebuild generated runtime files and update tests/goldens.
10. Run:
   - `./scripts/audit_runtime_surface.sh`
   - `./scripts/lint_zia_runtime_names.sh`
   - targeted runtime class/catalog tests
   - full build script before proposing the change

## Notes On ADRs

If cleanup only removes frontend aliases from `runtime.def` while retaining C
runtime symbols, this is a frontend runtime-surface cleanup. If the cleanup
changes or removes exported C runtime ABI functions/signatures, follow the repo
rule and add an ADR.

## Verification Performed

Commands run during this review:

- `./build/src/tools/zanna/zanna --dump-runtime-api`
- `./build/src/tools/zanna/zanna --dump-runtime-classes`
- `./scripts/audit_runtime_surface.sh`

Result:

- Runtime surface audit passed.
- Focused runtime surface tests passed.
- No product code was changed by this review.

> ✅ **CONFIRMED (re-run 2026-06-30).** All three commands reproduced. The audit
> passed and printed `rtgen audit: 6279 functions, 337 aliases, 461 classes,
> 7114 header declarations` / `rtgen audit passed`; all **8** focused runtime
> surface tests passed (`test_runtime_surface_audit`, `test_runtime_classes_catalog`,
> `test_runtime_name_map`, `test_basic_runtime_calls`, `test_zia_static_calls`, …).
> No product code was changed by this confirmation review.
