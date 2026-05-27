# Zia sample and convenience-syntax feasibility

Game3D is runtime-first. The authoritative implementation should be normal C
runtime code registered through `runtime.def`, not a Zia source package. This
file only records whether Zia can comfortably consume that runtime surface in
samples, docs snippets, and optional thin callback/convenience wrappers.

## Status legend

- **Confirmed** - a checked-in test or shipping example exercises it.
- **Confirm-0A** - works in principle but one detail must be pinned down in
  Phase 0A.
- **Resolved by design** - the plan deliberately avoids relying on the feature.

| Feature Zia consumers may use | Used for | Status | Proof |
|---|---|---|---|
| Lambdas (expr + block body) | `world.run((dt) => {...})`, optional callback sugar | Confirmed | `tests/zia_runtime/19_lambdas.zia` (`(x: Integer) => x + 1`, `() => { ... }`); `tests/runtime/game3d_surface_probe.zia:52-55` block lambda passed to helper |
| **Function-typed parameters** | `run(update: (Float) -> Unit)`, optional callback wrappers | Confirmed | `tests/zia_runtime/19_lambdas.zia:10,14`; `tests/runtime/game3d_surface_probe.zia:37` confirms no-return callback parameter |
| Closures (capture outer vars) | stateful sample callbacks/controllers | Confirmed | `tests/zia_runtime/19_lambdas.zia` Tests 16-20 |
| Interfaces (decl, `implements`, as param, as var) | `CameraController`, behavior objects, callback fallback | Confirmed | `tests/zia_runtime/25_interfaces.zia` (`interface Shape`, `class Rectangle implements Shape`, `func getArea(s: Shape)`) |
| Interface method with **no return** (void) | controller `update`, behavior hooks | Confirmed | `tests/zia_runtime/25_interfaces.zia` style; void interface methods declared as `func m(...);` (no `->`) |
| Optional types `T?` + `null` | `Mesh3D?`, `CameraController?`, `findEntity -> Entity3D?` | Confirmed | `tests/zia_runtime/48_optional_method_chain.zia` (`var actor: Actor? = real; var missing: Actor? = null;`) |
| **Optional method chaining `x?.m()` on classes AND interfaces** | `controller?.update(...)`, `findEntity(n)?.applyImpulse(...)` | Confirmed | `tests/zia_runtime/48_optional_method_chain.zia:39,42,48` (`actor?.speak(...)`, `actor?.tick()`, `speaker?.speak(...)` where `speaker: Speaker?`) |
| `?? fallback` (null-coalescing) | default values from optionals | Confirmed | `tests/zia_runtime/48_optional_method_chain.zia` (`actor?.speak(...) ?? "none"`) |
| Enums (auto, explicit `=`, negative) | `Layers`, `BodyShape`, `SyncMode`, `AlphaMode`, `ShadingModel`, `QualityLevel` | Confirmed | `tests/zia_runtime/33_enum_runtime.zia`; real-game use `examples/games/xenoscape/config.zia` (`enum PlayerState {...}`, `enum GameState {...}`) |
| **Enum -> Integer coercion** | pass an enum where runtime expects `Integer` (light slot, sync mode, alpha mode) | Confirmed | `tests/zia_runtime/33_enum_runtime.zia:51,58` (`var code: Integer = HttpStatus.Ok` => 200) |
| `match` on enum (exhaustive) | state dispatch | Confirmed | `tests/zia_runtime/33_enum_runtime.zia:33` |
| Fluent chaining `a.b().c()` | `Entity3D` builder (`.setPosition(...).attachBody(...)`) | Confirmed | `examples/games/baseball/src/season/date_utils.zia` (`DO.FromDays(d).AddDays(n).ToString()`) |
| String file bind / alias bind | examples and small helper imports | Confirmed | `bind "./_support";` in every `tests/zia_runtime/*.zia`; alias-first binds used across `examples/` |
| Runtime namespace bind | `bind Viper.Game3D as G;` examples | Confirmed | `tests/runtime/game3d_surface_probe.zia:4,42` proves aliasing and calling an existing `Viper.*` runtime namespace (`Viper.Graphics3D`). `Viper.Game3D` must be implemented as the same kind of runtime namespace, not as a Zia source package. |
| Void-returning **function type** spelling | optional Zia callback annotations | Confirmed | Use `Unit`: `tests/runtime/game3d_surface_probe.zia:37,52-55` passes with `func runUpdate(fn: (Float) -> Unit, ...)`. `(Float) -> Void` mismatched block lambdas during the spike. |
| Default/no-op interface methods | `CameraController.lateUpdate` ergonomics | Resolved by design | Do not depend on interface default methods for Game3D. Require both `update` and `lateUpdate` on `CameraController` or provide an explicit no-op base/runtime helper later. |
| Bitmask ergonomics without bitwise syntax | collision masks | Confirmed by design | `LayerMask` stores an `Integer` and exposes `include(...)`; user code does not need bitwise operators |
| Object identity map | body-owner lookup | Avoided by design | runtime Game3D owns body/entity identity; Zia does not need a generic object-keyed map assumption |

## Phase 0A confirmation scope

Resolved before broad implementation:

1. **Runtime namespace bind.** Runtime namespace aliasing is proven by the
   probe. `Viper.Game3D` remains the recommended namespace, implemented through
   `runtime.def` like `Viper.Graphics3D`, so examples and docs do not imply a
   source package.
2. **Event/callback shape.** Runtime-owned collision/animation event buffers are
   the required API. Optional callback sugar may use `Unit`-returning function
   types, but it remains convenience over pollable runtime buffers.
3. **Void function-type spelling.** Confirm how a no-return function type is
   annotated: use `(Float) -> Unit` for no-return callbacks.
4. **No-op controller methods.** Confirm whether default interface methods are
   suitable for `CameraController.lateUpdate`: avoid them in the plan; require
   both methods or add an explicit no-op helper.

## Fallbacks if a feature regresses

- **Lambda callbacks** -> pass an object implementing a small interface
  (proven).
- **Optional `?.`** -> explicit `if (x != null) { ... }` guards (proven
  elsewhere).
- **Enum args** -> plain `Integer` constants in a constants module.
- **Fluent chaining** -> non-chaining setters (each returns nothing); slightly
  more verbose.
- **Layer masks** -> expose named helpers (`LayerMask.WorldOnly`,
  `LayerMask.All`) if chainable `include(...)` is awkward.
- **Controller no-op methods** -> use `BaseCameraController` instead of relying
  on interface defaults.

None of the fallbacks block the runtime-first design; they only trade sample
ergonomics.
