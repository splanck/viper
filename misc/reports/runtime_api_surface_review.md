# Viper Runtime API Surface Review

Date: 2026-07-01
Scope: frontend-visible `Viper.*` runtime API as reported by
`./build/src/tools/viper/viper --dump-runtime-api`, correlated with
`src/il/runtime/runtime.def`, generated runtime classes, and `rtgen` behavior.

> ## ✅ Confirmation Review — 2026-06-30 (independent)
>
> Every measured number, code reference, and representative example below was
> re-derived from first principles: the live `viper` binary
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
> - One illustrative example is imprecise: `Viper.Graphics3D.Material3D`
>   Alpha/Metallic/Roughness/AO are set via property setters (`set_*`), not
>   `Set*` methods, so they are not accessor-duplication cases. Flagged inline.

## Executive Summary

> ✅ **CONFIRMED.** Live dump = **6,620** functions / **461** classes; `rtgen`
> audit prints **6,279** functions / **337** aliases / **461** classes verbatim;
> audit + focused tests pass. All 8 highest-impact items reproduced below;
> item 2's "168 methods on `Viper.Math`" sums exactly to 168.

The current runtime API surface is not just large; it is polluted by aliases,
generator side effects, mixed naming conventions, and type-shape inconsistencies.
Because Viper is pre-alpha, the right policy is to delete compatibility aliases
and normalize the surface now rather than carrying migration debt forward.

Measured surface:

- Live dump: 6,620 public `Viper.*` functions and 461 runtime classes.
- `rtgen` audit: 6,279 declared functions, 337 `RT_ALIAS` entries, 461 classes.
- Surface audit and focused tests pass, which means the junk is consistently
  registered; it does not mean the surface is clean.

Highest-impact findings:

1. Delete all 337 public `RT_ALIAS` entries. They are pre-alpha debt.
2. Stop `rtgen` from auto-adding descendant namespace functions as parent-class
   methods. This creates 203 bogus class methods, including 168 methods on
   `Viper.Math` copied from `Viper.Math.Bits`, `BigInt`, `Vec2`, `Vec3`, etc.
3. Stop constructor auto-method injection or constrain it to explicit `New`
   factories. It creates odd methods such as `Viper.Core.ValueType.ValueType`.
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
- `src/tools/viper/main.cpp:292`
- `src/tools/viper/main.cpp:300`

## P0: Delete All Compatibility Aliases

> ✅ **CONFIRMED.** Exactly **337** `RT_ALIAS` entries (line-anchored count;
> `rtgen` audit agrees). The alias-group table reproduces exactly when rolled up
> to the first 3 namespace segments (`Viper.Game.UI` 72, `ParticleSystem2D` 28,
> `Emitter2D` 28, `Lighting2D` 13, `System.Process` 10, … `System.Machine` 6).
> Representative aliases all present: `Viper.Box.*` (16), `Viper.Parse.*` (13),
> `Viper.Convert.*` (6), plus `String.Length`, `ConcatSelf`, `FromFloat`,
> `List.Flip`, `List.Items`. `RuntimeSurfacePolicy.inc` exists as cited.

There are 337 `RT_ALIAS` entries. In a pre-alpha surface, these should not be
compatibility aliases; they should be compile errors in `rtgen` unless an
internal-only alias is explicitly needed for backend lowering.

Largest alias groups by public prefix:

| Count | Prefix |
|---:|---|
| 72 | `Viper.Game.UI` |
| 28 | `Viper.Graphics.ParticleSystem2D` |
| 28 | `Viper.Graphics.Emitter2D` |
| 13 | `Viper.Graphics.Lighting2D` |
| 10 | `Viper.System.Process` |
| 10 | `Viper.Graphics.ScreenScaler` |
| 10 | `Viper.Game3D.Diagnostics` |
| 8 | `Viper.Text.NumberFormat` |
| 8 | `Viper.Graphics.GpuTexture2D` |
| 8 | `Viper.Text.Json` |
| 7 | `Viper.Graphics.Surface2D` |
| 7 | `Viper.Math.Vec3` |
| 7 | `Viper.Graphics3D.Material3D` |
| 6 | `Viper.Graphics.SpriteFont` |
| 6 | `Viper.System.Environment` |
| 6 | `Viper.System.Machine` |

Representative aliases to delete:

- `Viper.Box.*` -> `Viper.Core.Box.*`
- `Viper.Parse.*` -> `Viper.Core.Parse.*`
- `Viper.Convert.*` -> `Viper.Core.Convert.*`
- `Viper.String.Length` -> `Viper.String.get_Length`
- `Viper.String.ConcatSelf` -> `Viper.String.Concat`
- `Viper.String.FromFloat` -> `Viper.String.FromSingle`
- `Viper.Collections.List.Flip` -> `Reverse`
- `Viper.Collections.List.Items` -> `ToSeq`
- `Viper.Collections.Bag.Merge/Common/Put/Drop` -> `Union/Intersect/Add/Remove`
- `Viper.Collections.Set.Merge/Common` -> `Union/Intersect`
- `Viper.Collections.SortedSet.Merge/Common/Put/Drop` -> canonical set verbs
- `Viper.Collections.*.get_Count` aliases to `get_Length`
- `Viper.Graphics.Surface2D.*` -> `RenderTarget2D.*`
- `Viper.Graphics.GpuTexture2D.*` -> `Texture2D.*`
- `Viper.Graphics.ScreenScaler.*` -> `Viewport2D.*`
- `Viper.Graphics.ParticleSystem2D.*` and `Viper.Graphics.Emitter2D.*` ->
  `Viper.Game.ParticleEmitter.*`
- `Viper.Game.UI.*` -> `Viper.Game.UI.Hud*`
- `Viper.Game3D.Environment.*` -> `Environment3D.*`
- `Viper.Game3D.Diagnostics.*` -> `Diagnostics3D.*`

Correction:

- Remove all public `RT_ALIAS` entries from `runtime.def`.
- Remove alias exceptions from `RuntimeSurfacePolicy.inc`.
- Add an `rtgen` validation error for public `RT_ALIAS`.
- If the backend needs an internal alias, model that outside the frontend-visible
  `Viper.*` API.

## P0: `rtgen` Synthesizes Bogus Parent-Class Methods

> ✅ **CONFIRMED.** Exactly **203** descendant-namespace methods are injected into
> parent classes; every row of the table matches (`Viper.Math`←BigInt 35, Easing
> 28, Bits 18, … PerlinNoise 5 = 168 total on Math; Memory←GC 6/WeakRef 5;
> Process←ProcessHandle 10; Pty←PtySession 10; TreeView←Node 4). Mechanism verified
> at `rtgen.cpp:1469–1488`: `startsWith(fn.canonical, cls.name + ".")` with **no**
> dot-remainder guard, so `Viper.Math.` matches `Viper.Math.Bits.And` et al.

`rtgen` auto-adds uncovered functions whose canonical name starts with
`className + "."`. This also matches descendants. For class `Viper.Math`, the
prefix `Viper.Math.` matches `Viper.Math.Bits.*`, `Viper.Math.BigInt.*`,
`Viper.Math.Vec2.*`, and so on.

Measured damage: 203 descendant namespace methods are injected into parent
classes.

| Parent class | Descendant source | Count |
|---|---|---:|
| `Viper.Math` | `BigInt` | 35 |
| `Viper.Math` | `Easing` | 28 |
| `Viper.Math` | `Bits` | 18 |
| `Viper.Math` | `Mat3` | 17 |
| `Viper.Math` | `Random` | 14 |
| `Viper.Math` | `Vec2` | 14 |
| `Viper.Math` | `Vec3` | 11 |
| `Viper.Math` | `Mat4` | 10 |
| `Viper.Math` | `Quat` | 8 |
| `Viper.Math` | `Spline` | 8 |
| `Viper.Math` | `PerlinNoise` | 5 |
| `Viper.Memory` | `GC` | 6 |
| `Viper.Memory` | `WeakRef` | 5 |
| `Viper.System.Process` | `ProcessHandle` | 10 |
| `Viper.System.Pty` | `PtySession` | 10 |
| `Viper.GUI.TreeView` | `Node` | 4 |

Examples:

- `Viper.Math.And` is synthesized from `Viper.Math.Bits.And`.
- `Viper.Math.Abs(obj)` is synthesized from `Viper.Math.BigInt.Abs`.
- `Viper.Math.New` is synthesized from `Viper.Math.Mat3.New`.
- `Viper.Memory.New` is synthesized from `Viper.Memory.WeakRef.New`.
- `Viper.System.Process.IsValid` is synthesized from
  `Viper.System.Process.ProcessHandle.IsValid`.

Correction:

- Delete synthetic method auto-fill from `buildResolvedClasses`, or restrict it
  to immediate functions only where the remainder after `classPrefix` contains
  no dot.
- Prefer deleting it entirely: every class method should be explicitly declared
  by `RT_METHOD`.
- Add a test that `Viper.Math` does not contain methods whose target starts with
  `Viper.Math.Bits.`, `Viper.Math.BigInt.`, etc.

Relevant code:

- `src/tools/rtgen/rtgen.cpp:1469`
- `src/tools/rtgen/rtgen.cpp:1471`
- `src/tools/rtgen/rtgen.cpp:1481`

## P0: Same-Name/Same-Arity Method Overloads Are Unsafe

> ✅ **CONFIRMED.** All 16 listed `Viper.Math` collisions reproduce with the exact
> `f64`/`obj` signature pairs (Abs, Lerp, Max, Min, Pow, Sqrt, And, Not, Or, Shl,
> Shr, Xor, Linear, Mul, Div, Cross). `RuntimeClasses.cpp:499` keys methods on
> `toLower(cls) '|' toLower(method) '#' arity` — **no parameter types** — and
> `:596` does `methodIndex_[methodKey(...)] = pm;` (map assignment), so a later
> same-key method silently overwrites the earlier one.

`RuntimeRegistry::methodKey` indexes methods by lowercased class, lowercased
method, and arity. It does not include parameter types. `buildIndexes` assigns
into the map, so later entries overwrite earlier entries.

Current same-name/same-arity collisions are all on `Viper.Math`, mostly caused
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
- If Viper wants typed overloads in runtime classes, change the registry key and
  diagnostics to include parameter types before allowing them.

Relevant code:

- `src/il/runtime/classes/RuntimeClasses.cpp:499`
- `src/il/runtime/classes/RuntimeClasses.cpp:596`

## P0: Public `ptr` Leak

> ✅ **CONFIRMED.** Exactly **5,601** function signatures in `--dump-runtime-api`
> contain `ptr`. All 4 examples match: `BitSet.New ptr(i64)`,
> `List.Push void(ptr,ptr)`, `Box.I64 ptr(i64)`, `Canvas.Clear void(ptr,i64)`.

The live API dump exposes `ptr` in 5,601 public function signatures. Class
members do not show `ptr` because they use the raw catalog strings, but global
functions are printed from lowered runtime descriptors.

This contradicts `runtime.def`, which says `ptr` is runtime-internal only.

Examples from `--dump-runtime-api`:

- `Viper.Collections.BitSet.New ptr(i64)`
- `Viper.Collections.List.Push void(ptr,ptr)`
- `Viper.Core.Box.I64 ptr(i64)`
- `Viper.Graphics.Canvas.Clear void(ptr,i64)`

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
| `Viper.Diagnostics.Log` | `DEBUG` | `Debug(str)` |
| `Viper.Diagnostics.Log` | `INFO` | `Info(str)` |
| `Viper.Diagnostics.Log` | `WARN` | `Warn(str)` |
| `Viper.Diagnostics.Log` | `ERROR` | `Error(str)` |
| `Viper.System.Machine` | `Arch` | `Arch()` |
| `Viper.System.Machine` | `Endian` | `Endian()` |
| `Viper.System.Machine` | `PageSize` | `PageSize()` |
| `Viper.System.Machine` | `PointerSize` | `PointerSize()` |
| `Viper.Math` | `E` | `E()` |
| `Viper.Math` | `Pi` | `Pi()` |
| `Viper.Math` | `Tau` | `Tau()` |
| `Viper.Text.Version` | `Major` | `Major(str)` |
| `Viper.Text.Version` | `Minor` | `Minor(str)` |
| `Viper.Text.Version` | `Patch` | `Patch(str)` |

Correction:

- Keep properties for constants and value access.
- Delete method aliases for constants (`Math.E()`, `Machine.Arch()`) unless a
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

There are 44 same C-symbol + same signature groups declared as separate
`RT_FUNC`s, before counting `RT_ALIAS`. Some are legitimate bidirectional
factory conveniences, but pre-alpha should still choose one public spelling.

Notable groups to collapse:

- `TryToI64` vs `ToI64Option` and the equivalent F64/I1/String box APIs.
- `Viper.Core.Parse.Double`, `DoubleOption`, and `TryNum` all return
  `obj<Viper.Option>(str)`. The plain `Double` name is misleading because it
  does not return `f64`.
- `Viper.Core.Parse.Int64`, `Int64Option`, and `TryInt` have the same issue.
- `Viper.Core.Convert.ToInt` and `ToInt64` are identical.
- `Viper.String.SplitFields` and `SplitFieldsSeq` are identical.
- `Viper.Graphics.Canvas.get_DeltaTime` and `get_DeltaTimeMs` are identical.
- `Viper.Graphics3D.Canvas3D.get_DeltaTime` and `get_DeltaTimeMs` are identical.
- `Viper.Graphics3D.Canvas3D.get_Backend` and `get_BackendName` are identical.
- `Viper.Graphics.Canvas.Polyline` and `PolylinePath` are identical, as are
  `Polygon`/`PolygonPath` and `PolygonFrame`/`PolygonFramePath`.
- `Viper.Game.ParticleEmitter.Get` and `ParticleAt` are identical.
- `Viper.Terminal.Int` and `Viper.Text.Fmt.Int` are identical.
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
| `rt_bag_to_seq` | `obj<Viper.Collections.Seq>(obj)` | `Viper.Collections.Bag.ToSeq`; `Viper.Collections.Seq.FromBag` |
| `rt_box_to_f64_option` | `obj<Viper.Option>(obj)` | `Viper.Core.Box.TryToF64`; `Viper.Core.Box.ToF64Option` |
| `rt_box_to_i1_option` | `obj<Viper.Option>(obj)` | `Viper.Core.Box.TryToI1`; `Viper.Core.Box.ToI1Option` |
| `rt_box_to_i64_option` | `obj<Viper.Option>(obj)` | `Viper.Core.Box.TryToI64`; `Viper.Core.Box.ToI64Option` |
| `rt_box_to_str_option` | `obj<Viper.Option>(obj)` | `Viper.Core.Box.TryToStr`; `Viper.Core.Box.ToStrOption` |
| `rt_canvas3d_get_backend` | `str(obj)` | `Viper.Graphics3D.Canvas3D.get_Backend`; `Viper.Graphics3D.Canvas3D.get_BackendName` |
| `rt_canvas3d_get_delta_time` | `i64(obj)` | `Viper.Graphics3D.Canvas3D.get_DeltaTime`; `Viper.Graphics3D.Canvas3D.get_DeltaTimeMs` |
| `rt_canvas_get_delta_time` | `i64(obj)` | `Viper.Graphics.Canvas.get_DeltaTime`; `Viper.Graphics.Canvas.get_DeltaTimeMs` |
| `rt_canvas_polygon_frame_path` | `void(obj,obj<Viper.Graphics.Path2D>,i64)` | `Viper.Graphics.Canvas.PolygonFrame`; `Viper.Graphics.Canvas.PolygonFramePath` |
| `rt_canvas_polygon_path` | `void(obj,obj<Viper.Graphics.Path2D>,i64)` | `Viper.Graphics.Canvas.Polygon`; `Viper.Graphics.Canvas.PolygonPath` |
| `rt_canvas_polyline_path` | `void(obj,obj<Viper.Graphics.Path2D>,i64)` | `Viper.Graphics.Canvas.Polyline`; `Viper.Graphics.Canvas.PolylinePath` |
| `rt_container_set_padding` | `void(obj,f64)` | `Viper.GUI.VBox.SetPadding`; `Viper.GUI.HBox.SetPadding`; `Viper.GUI.Container.SetPadding` |
| `rt_container_set_spacing` | `void(obj,f64)` | `Viper.GUI.VBox.SetSpacing`; `Viper.GUI.HBox.SetSpacing`; `Viper.GUI.Container.SetSpacing` |
| `rt_deque_to_list` | `obj<Viper.Collections.List>(obj)` | `Viper.Collections.List.FromDeque`; `Viper.Collections.Deque.ToList` |
| `rt_deque_to_seq` | `obj<Viper.Collections.Seq>(obj)` | `Viper.Collections.Seq.FromDeque`; `Viper.Collections.Deque.ToSeq` |
| `rt_fmt_int` | `str(i64)` | `Viper.Terminal.Int`; `Viper.Text.Fmt.Int` |
| `rt_list_to_seq` | `obj<Viper.Collections.Seq>(obj)` | `Viper.Collections.List.ToSeq`; `Viper.Collections.Seq.FromList` |
| `rt_math_e` | `f64()` | `Viper.Math.get_E`; `Viper.Math.E` |
| `rt_math_pi` | `f64()` | `Viper.Math.get_Pi`; `Viper.Math.Pi` |
| `rt_math_tau` | `f64()` | `Viper.Math.get_Tau`; `Viper.Math.Tau` |
| `rt_parse_double_option` | `obj<Viper.Option>(str)` | `Viper.Core.Parse.Double`; `Viper.Core.Parse.DoubleOption`; `Viper.Core.Parse.TryNum` |
| `rt_parse_int64_option` | `obj<Viper.Option>(str)` | `Viper.Core.Parse.Int64`; `Viper.Core.Parse.Int64Option`; `Viper.Core.Parse.TryInt` |
| `rt_particle_emitter_particle_at` | `obj<Viper.Option>(obj,i64)` | `Viper.Game.ParticleEmitter.Get`; `Viper.Game.ParticleEmitter.ParticleAt` |
| `rt_rand_range_method` | `i64(obj,i64,i64)` | `Viper.Math.Random.inst_NextIntRange`; `Viper.Math.Random.inst_Range` |
| `rt_seq_to_bag` | `obj<Viper.Collections.Bag>(obj)` | `Viper.Collections.Bag.FromSeq`; `Viper.Collections.Seq.ToBag` |
| `rt_seq_to_deque` | `obj<Viper.Collections.Deque>(obj)` | `Viper.Collections.Seq.ToDeque`; `Viper.Collections.Deque.FromSeq` |
| `rt_seq_to_list` | `obj<Viper.Collections.List>(obj)` | `Viper.Collections.List.FromSeq`; `Viper.Collections.Seq.ToList` |
| `rt_seq_to_queue` | `obj<Viper.Collections.Queue>(obj)` | `Viper.Collections.Queue.FromSeq`; `Viper.Collections.Seq.ToQueue` |
| `rt_seq_to_set` | `obj<Viper.Collections.Set>(obj)` | `Viper.Collections.Set.FromSeq`; `Viper.Collections.Seq.ToSet` |
| `rt_seq_to_stack` | `obj<Viper.Collections.Stack>(obj)` | `Viper.Collections.Seq.ToStack`; `Viper.Collections.Stack.FromSeq` |
| `rt_set_to_list` | `obj<Viper.Collections.List>(obj)` | `Viper.Collections.Set.ToList`; `Viper.Collections.List.FromSet` |
| `rt_set_to_seq` | `obj<Viper.Collections.Seq>(obj)` | `Viper.Collections.Set.ToSeq`; `Viper.Collections.Seq.FromSet` |
| `rt_soundlistener3d_set_forward` | `void(obj,obj)` | `Viper.Graphics3D.SoundListener3D.set_Forward`; `Viper.Graphics3D.SoundListener3D.SetForward` |
| `rt_soundlistener3d_set_up` | `void(obj,obj)` | `Viper.Graphics3D.SoundListener3D.set_Up`; `Viper.Graphics3D.SoundListener3D.SetUp` |
| `rt_soundlistener3d_set_velocity` | `void(obj,obj)` | `Viper.Graphics3D.SoundListener3D.set_Velocity`; `Viper.Graphics3D.SoundListener3D.SetVelocity` |
| `rt_soundsource3d_set_position` | `void(obj,obj)` | `Viper.Graphics3D.SoundSource3D.set_Position`; `Viper.Graphics3D.SoundSource3D.SetPosition` |
| `rt_soundsource3d_set_velocity` | `void(obj,obj)` | `Viper.Graphics3D.SoundSource3D.set_Velocity`; `Viper.Graphics3D.SoundSource3D.SetVelocity` |
| `rt_str_split_fields_seq` | `seq<str>(str)` | `Viper.String.SplitFields`; `Viper.String.SplitFieldsSeq` |
| `rt_to_int` | `i64(str)` | `Viper.Core.Convert.ToInt`; `Viper.Core.Convert.ToInt64` |
| `rt_widget_was_clicked` | `i64(obj)` | `Viper.GUI.Widget.WasClicked`; `Viper.GUI.Button.WasClicked` |
| `rt_world3d_set_contact_beta` | `void(obj,f64)` | `Viper.Graphics3D.Physics3DWorld.set_ContactBeta`; `Viper.Graphics3D.Physics3DWorld.SetContactBeta` |
| `rt_world3d_set_position_iterations` | `void(obj,i64)` | `Viper.Graphics3D.Physics3DWorld.set_PositionIterations`; `Viper.Graphics3D.Physics3DWorld.SetPositionIterations` |
| `rt_world3d_set_restitution_threshold` | `void(obj,f64)` | `Viper.Graphics3D.Physics3DWorld.set_RestitutionThreshold`; `Viper.Graphics3D.Physics3DWorld.SetRestitutionThreshold` |
| `rt_world3d_set_solver_iterations` | `void(obj,i64)` | `Viper.Graphics3D.Physics3DWorld.set_SolverIterations`; `Viper.Graphics3D.Physics3DWorld.SetSolverIterations` |

## P1: GUI Widget Methods Are Duplicated Across Concrete Classes

> ✅ **CONFIRMED.** Exactly **58** target+signature groups are repeated in ≥3
> class-method slots. `Viper.GUI.Widget.SetEnabled` / `SetPreferredSize` /
> `SetMaxSize` / `SetFlex` / `SetMargin` appear in **24** slots each;
> `IsHovered` / `IsPressed` / `WasClicked` / `GetWidth` / `GetX` in **23** each.

The class catalog repeats base widget methods into concrete widget classes by
pointing every class method at `Viper.GUI.Widget.*`.

Measured repeated target groups: 58 target+signature groups repeated in three
or more class method slots. Top examples:

- `Viper.GUI.Widget.SetEnabled` appears in 24 class method slots.
- `SetPreferredSize`, `SetMaxSize`, `SetFlex`, `SetMargin` appear in 24 slots.
- `IsHovered`, `IsPressed`, `IsFocused`, `WasClicked`, `IsVisible`,
  `IsEnabled`, `GetWidth`, `GetHeight`, `GetX`, `GetY`, `GetFlex` appear in
  23 slots.
- `Destroy`, `SetCursor`, `SetVisible`, `SetSize`, `SetPosition`, drag/drop
  methods, and tooltip methods are repeated across most widget classes.

Correction:

- Model runtime class inheritance/mixins explicitly in the catalog instead of
  copying base methods into every class.
- If inheritance is not available yet, expose `Viper.GUI.Widget` operations on
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

Constructor metadata is inconsistent with method naming:

- 29 classes use constructor targets whose leaf is not `New`.
- 9 classes have no constructor metadata but still expose a `New` method.
- 3 constructors point outside their own class namespace.

Examples:

- `Viper.Core.ValueType` constructor -> `Viper.Core.Box.ValueType`
- `Viper.Collections.FrozenSet` constructor -> `FromSeq`
- `Viper.GUI.Font` constructor -> `Load`
- `Viper.IO.BinFile` constructor -> `Open`
- `Viper.String` constructor -> `FromStr`
- `Viper.Result` constructor -> `Ok`
- `Viper.Option` constructor -> `Some`
- `Viper.Graphics3D.Light3D` constructor -> `NewDirectional`
- `Viper.Graphics.ParticleSystem2D` constructor ->
  `Viper.Game.ParticleEmitter.New`

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

The surface mostly uses PascalCase class and method names, but `Game3D` uses
lowercase/camelCase names heavily.

Measured nonstandard `Game3D` function names:

- 65 under `Viper.Game3D.World3D`
- 38 under `Viper.Game3D.WorldStream3D`
- 36 under `Viper.Game3D.Entity3D`
- 26 under `Viper.Game3D.BodyDef`
- 16 under `Viper.Game3D.Sound3D`
- 14 under `Viper.Game3D.Input3D`
- 14 under `Viper.Game3D.Collision3DEvent`
- 14 under `Viper.Game3D.Animator3D`
- 11 under `Viper.Game3D.CharacterController3D`
- 10 under `Viper.Game3D.FirstPersonController`

Examples:

- `Viper.Game3D.LayerMask.get_bits`, `set_bits`, `include`, `includes`
- `Viper.Game3D.Input3D.update`, `isDown`, `mouseDelta`, `captureMouse`
- `Viper.Game3D.Entity3D.setPosition`, `setScaleXYZ`, `attachBody`,
  `worldPosition`
- `Viper.Game3D.World3D.destroy`, `spawn`, `runFramesOnly`, `drawOverlay`
- `Viper.Game3D.BodyDef.get_isStatic`, `set_isKinematic`, etc.

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

> ✅ **CONFIRMED.** Exactly **10** paths expose both `get_Length` and `get_Count`,
> **38** only `get_Length`, **22** only `get_Count`. The 10 "both" paths match the
> report's list exactly (BitSet, BloomFilter, UnionFind, F64Buffer, I64Buffer,
> List, Seq, Iterator, ButtonGroup, ParticleEmitter).

Function namespaces with cardinality accessors:

- 10 paths expose both `get_Length` and `get_Count`.
- 38 paths expose only `get_Length`.
- 22 paths expose only `get_Count`.

Examples with both:

- `Viper.Collections.BitSet`
- `Viper.Collections.BloomFilter`
- `Viper.Collections.UnionFind`
- `Viper.Collections.F64Buffer`
- `Viper.Collections.I64Buffer`
- `Viper.Collections.List`
- `Viper.Collections.Seq`
- `Viper.Collections.Iterator`
- `Viper.Game.ButtonGroup`
- `Viper.Game.ParticleEmitter`

Correction:

- Adopt one rule:
  - `Count` for collections and containers.
  - `Length` for strings, byte buffers, durations, and geometric/path length.
- Delete aliases that violate the rule.
- Rename canonical functions rather than retaining both.

## P1: Boolean Values Are Exposed As `i64`

> ✅ **CONFIRMED (substance + concrete cases).** The **4 exact getter/setter
> mismatches** reproduce precisely — `Sprite.FlipX`, `Sprite.FlipY`,
> `TileLayer2D.Visible`, `RenderPass2D.Enabled` all have getter `i1` / setter
> `i64`. All 8 named examples return `i64` (IsHovered, WasClicked,
> WasCloseRequested, HasText, IsEnabled, IsPlaying, Exists, SetCursorVisible).
> The aggregate "≥85 return / 33 setters" depends on an undocumented
> "boolean-looking" classifier; independent nets reproduce ~57–66 returns and
> ~21 setters — same order of magnitude, and the problem is pervasive.

`i1` exists and is documented as boolean, but many APIs still use `i64`.

Measured suspicious cases:

- 85 boolean-looking functions return `i64`.
- 33 boolean-looking setters take `i64`.
- 4 exact getter/setter type mismatches.

Examples:

- `Viper.GUI.Widget.IsHovered i64(obj)`
- `Viper.GUI.Widget.WasClicked i64(obj)`
- `Viper.GUI.App.WasCloseRequested i64(obj)`
- `Viper.GUI.ClipboardText.HasText i64()`
- `Viper.GUI.Shortcuts.IsEnabled i64(str)`
- `Viper.Sound.Music.IsPlaying i64(obj)`
- `Viper.IO.Assets.Exists i64(str)`
- `Viper.Terminal.SetCursorVisible void(i64)`

Exact getter/setter type mismatches:

- `Viper.Graphics.Sprite.FlipX`: getter `i1`, setter `i64`
- `Viper.Graphics.Sprite.FlipY`: getter `i1`, setter `i64`
- `Viper.Graphics.TileLayer2D.Visible`: getter `i1`, setter `i64`
- `Viper.Graphics.RenderPass2D.Enabled`: getter `i1`, setter `i64`

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
> Alpha/Metallic/Roughness/AO are set via property setters (`set_*`), **not**
> `Set*` methods (its actual set-methods are SetColor/SetAlbedoMap/…), so that
> illustration is not an accessor-duplication case.

There are 115 writable properties that also expose `SetX` methods. Some are
useful fluent APIs, but most are duplicate ways to mutate the same state.

Examples:

- `Viper.Math.Vec3.X/Y/Z` and `SetX/SetY/SetZ`
- `Viper.Graphics3D.Material3D.Alpha/Metallic/Roughness/AO/...` and matching
  `SetAlpha/SetMetallic/...`
- `Viper.Graphics3D.Light3D.Enabled/CastsShadows/Direction/Position` and
  matching setters
- `Viper.Graphics3D.Camera3D.Fov/NearPlane/FarPlane/Position/Yaw/Pitch` and
  matching setters
- Many `Game3D` data builder properties plus `setX` methods

Correction:

- Prefer properties for simple state.
- Keep `SetX` only when it is semantically more than assignment, returns `self`
  for builder chaining, or sets several fields at once.
- Do not publish both a property setter and a simple one-field setter.

## P2: Constant Names Are Modeled As Properties

> ✅ **CONFIRMED.** Key-code and event constants exist as readonly `get_`
> properties: `Input.Keyboard.get_KEY_A`, `Input.Mouse.get_BUTTON_LEFT`,
> `Input.Pad.get_PAD_A`, `IO.Watcher.get_EVENT_CREATED`, `Graphics.Color.get_RED`.

Constants such as input key codes are represented as readonly properties:

- `Viper.Input.Keyboard.KEY_A`, `KEY_B`, ...
- `Viper.Input.Mouse.BUTTON_LEFT`, ...
- `Viper.Input.Pad.PAD_A`, ...
- `Viper.IO.Watcher.EVENT_CREATED`, ...
- `Viper.Graphics.Color.get_RED`, ...

This is not inherently broken, but it is inconsistent with the rest of the
PascalCase API and looks odd in a class-property catalog.

Correction:

- Prefer enum-like classes with PascalCase properties: `KeyA`, `ButtonLeft`,
  `EventCreated`, `Red`.
- If uppercase constants are desired, document that constant namespaces are the
  one exception and keep them out of method/property collision checks.

## Recommended Cleanup Order

1. Add validation first:
   - No public `RT_ALIAS`.
   - No `ptr` in public dump.
   - No descendant namespace auto-methods.
   - No same class + method + arity duplicates.
   - No property/method collisions ignoring case.
2. Delete `rtgen` synthetic method auto-fill, or constrain it to immediate
   functions only while requiring explicit `RT_METHOD`s.
3. Delete all 337 `RT_ALIAS` entries and matching policy exceptions.
4. Normalize `Game3D` public names to PascalCase.
5. Collapse duplicate `RT_FUNC` spellings that share the same C symbol and
   signature.
6. Decide and apply the `Count`/`Length` policy.
7. Convert boolean-shaped APIs to `i1`.
8. Rebuild generated runtime files and update tests/goldens.
9. Run:
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

- `./build/src/tools/viper/viper --dump-runtime-api`
- `./build/src/tools/viper/viper --dump-runtime-classes`
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
