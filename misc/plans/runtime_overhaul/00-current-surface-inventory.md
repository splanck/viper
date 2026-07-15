# Current Runtime Surface Inventory

Date: 2026-07-02

## Source Inputs

Primary inventory:

```sh
./build/src/tools/viper/viper --dump-runtime-api > /tmp/viper_runtime_api_current.json
```

Correlated sources:

- `src/il/runtime/runtime.def`
- `src/tools/rtgen/rtgen.cpp`
- `src/tools/viper/main.cpp`
- `docs/viperlib/**`
- runtime stub implementations under `src/runtime/**`
- prior review at `misc/reports/runtime_api_surface_review.md`

## Summary Counts

| Category | Count |
|---|---:|
| Public functions | 6310 |
| Runtime classes | 461 |
| Class properties | 1471 |
| Class methods | 4649 |

The surface is mechanically healthier than older reports: public aliases are
gone from the live dump, public `ptr` signatures are gone, class/method duplicate
collisions are not present, and focused registry audits pass. This plan targets
the remaining user-facing design problems.

## Largest Function Namespaces

| Prefix | Functions |
|---|---:|
| `Viper.Graphics3D` | 871 |
| `Viper.Game` | 718 |
| `Viper.GUI` | 672 |
| `Viper.Graphics` | 573 |
| `Viper.Game3D` | 517 |
| `Viper.Collections` | 484 |
| `Viper.Network` | 289 |
| `Viper.Math` | 266 |
| `Viper.Input` | 265 |
| `Viper.IO` | 236 |
| `Viper.Text` | 197 |
| `Viper.Threads` | 161 |
| `Viper.Sound` | 123 |
| `Viper.Localization` | 120 |
| `Viper.Time` | 116 |

## Largest Public Classes

| Class | Properties | Methods | Total |
|---|---:|---:|---:|
| `Viper.Input.Keyboard` | 97 | 16 | 113 |
| `Viper.Graphics3D.Canvas3D` | 41 | 71 | 112 |
| `Viper.Input.Key` | 99 | 0 | 99 |
| `Viper.GUI.CodeEditor` | 6 | 89 | 95 |
| `Viper.Graphics.Canvas` | 5 | 80 | 85 |
| `Viper.Game2D.SceneDocument` | 4 | 65 | 69 |
| `Viper.Game3D.World3D` | 25 | 43 | 68 |
| `Viper.Graphics.Pixels` | 2 | 58 | 60 |
| `Viper.String` | 2 | 54 | 56 |
| `Viper.Graphics2D.Tilemap` | 7 | 48 | 55 |

These classes are the main browseability risk. Some are naturally broad, but
several mix constants, telemetry, configuration, commands, and factories on one
surface.

## Signature Dialect Drift

Current public dump has no `ptr`, but it still exposes two dialects:

| Surface | Rows | Contains `str` | Contains `string` | Contains `seq` | Contains `obj<` |
|---|---:|---:|---:|---:|---:|
| Global functions | 6310 | 1474 | 1472 | 56 | 369 |
| Class methods | 4649 | 1388 | 0 | 56 | 246 |
| Class properties | 1471 | 78 | 0 | 0 | 81 |

Representative cause:

- Global descriptor output is transformed in `src/tools/rtgen/rtgen.cpp`.
- Class method/property signature strings are preserved from `runtime.def`.
- `--dump-runtime-api` currently combines those two views.

Decision work: see
[02-signature-schema-and-generator-plan.md](02-signature-schema-and-generator-plan.md).

## `Try*` Return Shapes

Current public `Try*` global functions return five different shapes:

| Return | Count | Meaning observed |
|---|---:|---|
| `i1` | 16 | Non-blocking/acquire/send attempt, or load success. |
| `obj` | 13 | Nullable value, locale/number parse result, or future/channel value. |
| `obj<Viper.Option>` | 3 | Parse result with explicit optionality. |
| `i64` | 1 | `DateTime.TryParse`, ambiguous failure sentinel. |
| `string` | 1 | `MessageBundle.TryGet`, empty-string failure sentinel. |

Problem examples:

- `Viper.Time.DateTime.TryParse` returns `0` on failure, but Unix epoch is also
  `0`.
- `Viper.Threads.Channel.TryRecv` returns `NULL`.
- `Viper.Localization.MessageBundle.TryGet` returns `""` when missing.
- `Viper.Core.Parse.TryInt/TryNum/TryBool` already use `Option`.

Decision work: see
[05-failure-nullability-and-lifecycle-plan.md](05-failure-nullability-and-lifecycle-plan.md).

## Property Plus Setter Pairs

The public catalog has no duplicate writable-property/setter wiring collision,
but it does have many read-only-looking properties with matching `Set<Property>`
methods. Examples:

- `Viper.GUI.TextInput.Text` plus `SetText`
- `Viper.GUI.CodeEditor.Text` plus `SetText`
- `Viper.GUI.Dropdown.Selected` plus `SetSelected`
- `Viper.GUI.Slider.Value` plus `SetValue`
- `Viper.Network.WsServer.Subprotocol` plus `SetSubprotocol`
- `Viper.Graphics3D.Material3D.Color` plus `SetColor`
- `Viper.Graphics3D.SceneNode.Position` plus `SetPosition`
- `Viper.Graphics3D.PhysicsBody3D.Position` plus `SetPosition`
- `Viper.Game3D.World3D.WorkerCount` plus `SetWorkerCount`

Decision work: see
[06-properties-constructors-and-factories-plan.md](06-properties-constructors-and-factories-plan.md).

## Constructor And Factory Shape

Observed:

- 273 classes have constructor metadata.
- 62 classes have no constructor but do have factory-shaped methods.
- 50 classes have constructor metadata plus additional factory-shaped methods.

Representative no-constructor factory classes:

- `Viper.Time.DateTime.Create/Now/ParseISO`
- `Viper.Result.Ok/Err`
- `Viper.Option.Some/None`
- `Viper.Graphics3D.Light3D.NewDirectional/NewPoint/NewAmbient/NewSpot`
- `Viper.Graphics3D.Collider3D.NewBox/NewSphere/NewCapsule`
- `Viper.Game3D.Assets3D.LoadEntity/LoadTemplate/...`

Representative constructor-plus-factory classes:

- `Viper.Collections.Bytes.New/FromBase64/FromHex/FromStr`
- `Viper.IO.MemStream.New/NewCapacity/FromBytes`
- `Viper.Graphics3D.Mesh3D.New/NewBox/NewSphere/FromOBJ/FromSTL`
- `Viper.Graphics3D.Material3D.New/NewColor/NewTextured/NewPBR`
- `Viper.Game3D.World3D.New/NewWithCamera/NewWithHorizontalCamera`

Decision work: see
[06-properties-constructors-and-factories-plan.md](06-properties-constructors-and-factories-plan.md).

## Duplication Hotspots

### Input Keys

`Viper.Input.Keyboard` exposes 97 properties, most of them key constants.
`Viper.Input.Key` exposes 99 key constants. These should not both be public
canonical surfaces.

Relevant source regions:

- `src/il/runtime/runtime.def:4944` for `Viper.Input.Keyboard`
- `src/il/runtime/runtime.def:9302` for `Viper.Input.Keyboard` class metadata
- `src/il/runtime/runtime.def:14016` for `Viper.Input.Key`

### 3D Assets

The 3D stack has several overlapping load vocabularies:

- `Viper.Graphics3D.Gltf.Load`
- `Viper.Graphics3D.Fbx.Load`
- `Viper.Graphics3D.SceneAsset.Load/LoadAsset/LoadAnimation/...`
- `Viper.Game3D.Assets3D.LoadEntity/LoadTemplate/...`

Relevant source regions:

- `src/il/runtime/runtime.def:13209` for FBX
- `src/il/runtime/runtime.def:13226` for GLTF
- `src/il/runtime/runtime.def:13233` for `SceneAsset`
- `src/il/runtime/runtime.def:14905` for `Game3D.Assets3D`

### 2D/3D/Graphics/Game Boundaries

The API currently has four active 2D-oriented namespaces
(`Viper.Graphics`, `Viper.Graphics2D`, `Viper.Game`, `Viper.Game2D`) and two
active 3D-oriented namespaces (`Viper.Graphics3D`, `Viper.Game3D`). The split can
work, but the ownership rules are not obvious from the names alone.

Decision work: see
[03-namespace-and-domain-model-plan.md](03-namespace-and-domain-model-plan.md).

## Minimal Or Stubbed Behavior

Representative incomplete/minimal areas:

- Graphics-disabled 3D stubs sometimes return `NULL` silently, for example in
  `src/runtime/graphics/common/rt_3d_world_stubs.c`.
- `Canvas3D.New` traps in `src/runtime/graphics/common/rt_canvas3d_stubs.c`,
  while other 3D constructors return null/no-op.
- Zia service stubs return empty/unavailable results in
  `src/runtime/core/rt_zia_completion_stub.c`.
- Regex docs state advanced features such as lookahead/lookbehind/named groups
  are not implemented.
- Localization docs state only `en-US` built-in data is present.

Decision work: see
[05-failure-nullability-and-lifecycle-plan.md](05-failure-nullability-and-lifecycle-plan.md)
and [07-domain-workstreams.md](07-domain-workstreams.md).

