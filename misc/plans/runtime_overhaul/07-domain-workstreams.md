# Domain Workstreams

This file turns the policy documents into implementation tracks. Workstreams can
be done independently, but each should finish with audits and docs updates.

## Workstream A: Core, Memory, Option, Result

Goals:

- Hide unsafe lifetime hooks from ordinary public API discovery.
- Remove user-facing boxed value-type construction hooks or move them under
  unsafe/internal.
- Rename `Convert.ToString_Int` and `Convert.ToString_Double`.
- Make `Option` and `Result` the standard failure vocabulary.

Tasks:

- Move `Viper.Memory.Retain/Release/RetainStr/ReleaseStr` to
  `Viper.Runtime.Unsafe` or `RT_INTERNAL_FUNC`.
- Move `Core.Box.ValueType` and `ValueTypeAddField` to internal/unsafe.
- Add or improve typed `Option`/`Result` helpers needed by other workstreams.
- Move or hide `Viper.Error.*` trap-state mutation hooks. If user-visible trap
  inspection remains, expose it as read-only diagnostics.
- Decide whether GC controls live under `Viper.Runtime.GC`,
  `Viper.Runtime.Diagnostics`, or an unsafe/tuning namespace.
- Update `docs/viperlib/core.md` and `docs/viperlib/system.md`.

Acceptance:

- Ordinary docs do not recommend raw retain/release.
- Public dump has no unsafe hooks outside unsafe/internal namespace.

## Workstream B: Collections

Goals:

- Normalize `Capacity`.
- Normalize map writes.
- Replace nullable `TryPop`/`TryPeek` with `Option`.
- Keep collection verbs predictable.

Tasks:

- `Cap` -> `Capacity` on `LruCache`, `Ring`, `Seq`, `Channel`, `Deque`.
- `NewCap` -> `NewCapacity`.
- `LruCache.Put` -> `Set`.
- `BiMap.Put` -> `Set`.
- `MultiMap.Put` -> `Add` if append semantics, otherwise `Set`.
- `TryPop`/`TryPeek` returns move to `Option`.
- Review `GetStr`/`GetOptStr`-style APIs and prefer `GetOr`/`TryGet`.

Acceptance:

- Public collection docs can explain `Count`, `Length`, and `Capacity` in one
  paragraph.
- No map-like collection uses `Put` except documented exceptions.

## Workstream C: Math

Goals:

- Make bit operations self-explanatory.
- Keep vector/matrix/quaternion names consistent.
- Define acronym allowance for math-heavy APIs.

Tasks:

- Rename `LeadZ`, `TrailZ`, `Rotl`, `Rotr`, `Ushr`.
- Review `FMod` and similar C-inherited names for user-facing clarity.
- Keep `Vec2`, `Vec3`, `Mat3`, `Mat4` because those are standard compact type
  names in graphics/math.

Acceptance:

- `docs/viperlib/math.md` reads without abbreviation explanations for every
  method name.

## Workstream D: Text, Data, Parse, Format

Goals:

- Rename parse/format abbreviations.
- Align parse APIs with `Option`/`Result`.
- Clearly mark limited implementations.

Tasks:

- `Parse.TryNum` -> `TryDouble`.
- `Parse.NumOr` -> `DoubleOr`.
- `Fmt.NumSci` -> `Scientific`.
- `Fmt.NumPct` -> `Percent`.
- `Fmt.BoolYN` -> `YesNo`.
- Regex docs and APIs should expose capabilities for unsupported features.
- Data parsers should consistently use `Parse`, `TryParse`, and `ParseResult`.

Acceptance:

- No public text/parse format API uses `Num` when it means `Double`.
- Regex feature limitations are discoverable programmatically or clearly
  documented as capability flags.

## Workstream E: Time And Localization

Goals:

- Remove ambiguous time parse failure.
- Normalize locale parse/load results.
- Make locale data limitations explicit.

Tasks:

- Replace `DateTime.TryParse -> i64` with optional/result shape.
- Align `DateOnly.Parse`/`DateTime.ParseISO`/format-specific parse naming.
- `Locale.TryParse` returns `Option<Locale>`.
- `MessageBundle.TryGet` returns `Option<str>`.
- Add `GetOr` for message bundles.
- Add or document locale-data capability APIs.

Acceptance:

- Unix epoch can be parsed without ambiguity.
- Missing localization keys cannot be confused with empty translations.

## Workstream F: Threads And Async

Goals:

- Keep boolean attempt APIs.
- Move value-producing `Try*` APIs to `Option`.
- Keep concurrency docs precise about blocking and ownership.

Tasks:

- Keep `Monitor.TryEnter`, `Gate.TryEnter`, `RateLimiter.TryAcquire` as `i1`.
- Convert `Channel.TryRecv`, `Future.TryGet`, and
  `ConcurrentQueue.TryDequeue` to `Option`.
- Review `RecvFor`/timeout APIs for consistent timeout units and names.
- Decide whether bare `For(i64)` timeout names become `ForDuration`,
  `ForMilliseconds`, or options-object forms.
- Update ownership docs for returned objects.

Acceptance:

- A user can infer from the return type whether `Try*` means "attempt operation"
  or "maybe value."

## Workstream G: Network And Crypto

Goals:

- Keep protocol names recognizable.
- Keep HTTP verb methods as verbs.
- Align fallible operations with `Result`.

Tasks:

- Keep `Get`, `Post`, `Put`, `Delete` in HTTP contexts.
- Review `Connect`/`ConnectFor` naming and timeout units.
- Ensure TLS/crypto parse/load failures report useful `Result` errors.
- Move legacy hashes, legacy ciphers, and verification bypasses under explicit
  legacy/unsafe names.
- Clarify `Math.Random` versus `Crypto.Rand`, including whether
  `Math.Random.Chance` should return `i1`.
- Audit acronyms: keep standard protocol acronyms, expand unclear local
  abbreviations.

Acceptance:

- No non-HTTP collection-like API uses `Put`.
- Network connection failure can expose an error without requiring global
  `Error()` polling where practical.

## Workstream H: IO And System

Goals:

- Normalize stream/buffer capacity naming.
- Keep resource acquisition/lifecycle clear.
- Move unsafe memory hooks.

Tasks:

- `BinaryBuffer.NewCap` -> `NewCapacity`.
- Ensure `Open` and `Create` names distinguish existing-resource acquisition
  from new artifact creation.
- Review `Close`, `Destroy`, and `IsValid` consistency.
- Ensure process/pty APIs use `Result` for acquisition failures where possible.
- Replace `Pty.LastError` as the primary acquisition diagnostic with an explicit
  open result.

Acceptance:

- `Open`, `Create`, and `Load` have distinct docs and behavior.

## Workstream I: Input

Goals:

- One canonical key constants surface.
- Clear split between constants, devices, actions, and mappings.

Tasks:

- Add `Viper.Input.Key`.
- Move constants out of `Keyboard` docs.
- Update `Input.Action`, `Input.Manager`, and Game3D controller docs to use
  `Key`.
- Remove or hide `Game3D.Keys`.

Acceptance:

- No duplicate key constants outside `Viper.Input.Key`.
- Game3D examples do not import `Game3D.Keys`.

## Workstream J: Graphics, Graphics2D, Graphics3D

Goals:

- Clarify namespace layering.
- Normalize graphics acronyms.
- Normalize disabled-stub behavior.
- Split large rendering surfaces.

Tasks:

- Document ownership of `Graphics`, `Graphics2D`, and `Graphics3D`.
- Rename `SetDTMax` to `SetMaxDeltaTime` in both Canvas APIs.
- Review Canvas/Canvas3D class size and split telemetry/config subsurfaces.
- Normalize `AOMap` and similar unclear abbreviations.
- Add capability probes and consistent disabled behavior for graphics stubs.
- Route 3D asset docs through `SceneAsset`.
- Replace global last-load diagnostics with `LoadResult` while keeping
  process-wide diagnostics only for debugging.

Acceptance:

- Disabled graphics build behavior is predictable.
- 3D loading docs have one primary path.
- Canvas class member count is justified or reduced.

## Workstream K: Game, Game2D, Game3D

Goals:

- Keep gameplay conveniences without duplicating foundational APIs.
- Clarify 3D entity/prefab/world model.

Tasks:

- Remove `Game3D.Keys`.
- Rename `Assets3D.LoadTemplate*` to `Prefab` terminology.
- Clarify `Entity3D`, `SceneNode`, `SceneAsset`, `Prefab`, and `World3D`
  relationship.
- Review `World3D` large surface for subgroups.
- Keep Game UI separate from GUI but deduplicate implementation internally.

Acceptance:

- Game3D names explain runtime object lifecycle: asset -> prefab -> entity ->
  world.

## Workstream L: GUI, IDE, Zia Services

Goals:

- Make large GUI controls navigable.
- Avoid empty-success stubs in language-service APIs.
- Convert simple widget state to writable properties.

Tasks:

- Split or subgroup `CodeEditor` public API.
- Convert `Text`, `Selected`, `Value`, `Visible`, `Enabled` state to writable
  properties where supported.
- Add capability/status outputs to Zia service stubs.
- Ensure GUI docs do not expose inherited widget methods as copied duplicates.

Acceptance:

- Code editor API can be documented by feature area.
- Zia service unavailability is distinguishable from an empty valid result.

## Workstream M: Tooling, Project, Workspace, And Examples

Goals:

- Decide whether `Viper.Basic`, `Viper.Zia`, `Viper.Project`,
  `Viper.Workspace`, and `Viper.Assets` are runtime application APIs, tooling
  APIs, or preview/internal services.
- Keep example and demo programs synchronized with public API migrations.
- Avoid presenting migration aliases as the modern style in public samples.

Tasks:

- Assign stability and capability tags to tooling namespaces.
- Move orphan one-off surfaces into owned namespaces or mark them preview.
- Audit `examples/smoke_manifest.tsv` coverage before each broad runtime rename.
- Update `examples/apiaudit/**` first so it demonstrates canonical API style.
- Update larger apps and games after foundational examples pass source review.
- Include `tests/runtime/demo_*.zia`, `misc/video/**`, `baseball/demos/**`, and
  embedding examples in migration searches.

Acceptance:

- All public samples use canonical names after each cleanup slice.
- Tooling namespaces are not mixed into beginner runtime docs unless they are
  supported application APIs.
