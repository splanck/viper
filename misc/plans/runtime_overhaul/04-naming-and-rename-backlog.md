# Naming And Rename Backlog

## Naming Policy

Public API names should read as modern, plain English:

- Prefer full words over compressed abbreviations.
- Keep standard technical acronyms when they are the user-facing term.
- Use PascalCase for public leaves.
- Avoid underscores except generated `get_`/`set_` accessors.
- Keep verbs consistent across domains.

## High-Priority Renames

| Current | Target | Reason |
|---|---|---|
| `Viper.Math.Bits.LeadZ` | `CountLeadingZeros` | Avoid terse abbreviation. |
| `Viper.Math.Bits.TrailZ` | `CountTrailingZeros` | Avoid terse abbreviation. |
| `Viper.Math.Bits.Rotl` | `RotateLeft` | Full word is clearer. |
| `Viper.Math.Bits.Rotr` | `RotateRight` | Full word is clearer. |
| `Viper.Math.Bits.Ushr` | `ShiftRightLogical` | `Ushr` is JVM-like and obscure. |
| `Viper.Collections.BloomFilter.Fpr` | `FalsePositiveRate` | Public metric should be explicit. |
| `get_Cap` / `Cap` | `get_Capacity` / `Capacity` | Align with existing `Capacity` APIs. |
| `NewCap` | `NewCapacity` | Avoid unexplained abbreviation. |
| `SetDTMax` | `SetMaxDeltaTime` | `DT` is not obvious to beginners. |
| `ToString_Int` | `ToStringInt` | Remove underscore. |
| `ToString_Double` | `ToStringDouble` | Remove underscore. |
| `Parse.TryNum` | `TryDouble` | Signature returns `f64`; use the type. |
| `Parse.NumOr` | `DoubleOr` | Signature returns `f64`; use the type. |
| `Fmt.NumSci` | `Scientific` | Formatting term is enough. |
| `Fmt.NumPct` | `Percent` | Formatting term is enough. |
| `Fmt.BoolYN` | `YesNo` | Output shape is the useful concept. |

## Collection Verb Renames

Decision: map-like write is `Set`. HTTP verb remains `Put`.

| Current | Target |
|---|---|
| `LruCache.Put` | `Set` |
| `BiMap.Put` | `Set` |
| `MultiMap.Put` | `Set` or `Add` depending on multiplicity contract |

Decision detail:

- `MultiMap.Add(key, value)` is preferable if it appends another value under the
  key.
- `MultiMap.Set(key, value)` is preferable only if it replaces the key's value
  set.
- Keep `Network.Http.Put`, `HttpClient.Put`, and router/server `Put` methods as
  HTTP verbs.

## Constructor And Factory Renames

Decision: remove `New` from named factories when the class name already provides
the noun.

| Current | Target |
|---|---|
| `Mesh3D.NewBox` | `Mesh3D.Box` |
| `Mesh3D.NewSphere` | `Mesh3D.Sphere` |
| `Mesh3D.NewPlane` | `Mesh3D.Plane` |
| `Mesh3D.NewCylinder` | `Mesh3D.Cylinder` |
| `Collider3D.NewBox` | `Collider3D.Box` |
| `Collider3D.NewSphere` | `Collider3D.Sphere` |
| `Collider3D.NewCapsule` | `Collider3D.Capsule` |
| `Light3D.NewDirectional` | `Light3D.Directional` |
| `Light3D.NewPoint` | `Light3D.Point` |
| `Light3D.NewAmbient` | `Light3D.Ambient` |
| `Light3D.NewSpot` | `Light3D.Spot` |
| `Material3D.NewColor` | `Material3D.Color` or `Material3D.FromColor` |
| `Material3D.NewTextured` | `Material3D.Textured` |
| `Material3D.NewPBR` | `Material3D.PBR` |
| `World3D.NewWithCamera` | `World3D.WithCamera` |
| `World3D.NewWithHorizontalCamera` | `World3D.WithHorizontalCamera` |

Keep `FromOBJ`, `FromSTL`, `LoadKTX2`, `LoadBDF`, and similar format names.

## Acronym Policy

Keep all-caps acronyms when the acronym is the domain term:

- File/data/protocol: `JSON`, `XML`, `YAML`, `TOML`, `HTTP`, `TLS`, `SMTP`,
  `SSE`, `URL`, `URI`, `DNS`, `IP`, `IPv4`, `IPv6`.
- Crypto/hash: `MD5`, `SHA1`, `SHA256`, `CRC32`, `HMAC`, `PBKDF2`.
- Graphics/math: `RGB`, `RGBA`, `HSL`, `AABB`, `PBR`, `LOD`, `IK`, `FXAA`,
  `SSAO`, `GLTF`, `FBX`, `KTX2`, `OBJ`, `STL`.
- Byte order and integer width: `I16LE`, `U32BE`, etc.

Expand acronyms when they are not the searched-for concept:

- `AOMap` -> `AmbientOcclusionMap`
- `DT` -> `DeltaTime`
- `Fpr` -> `FalsePositiveRate`
- `BoolYN` -> `YesNo`

Open decision: graphics acronyms such as `DOF`, `IK`, `LOD`, and `AABB` may be
kept for expert APIs, but beginner-facing docs should spell them out once.

## Getter/Setter Text

Generated accessor names (`get_Count`, `set_Pos`) are acceptable in low-level
function rows because they mirror property metadata. User-facing class docs
should render properties as properties.

Work item:

- Ensure docs do not tell Zia users to call `get_`/`set_` directly unless that is
  still a language limitation.
- If direct accessor calls are still required in one language, document that as a
  language bridge, not as the API's preferred style.

## Audit Backlog

Add a public-name lint that fails on:

- `_` in public leaves except `get_`/`set_`.
- banned abbreviations: `LeadZ`, `TrailZ`, `Ushr`, `Rotl`, `Rotr`, `Fpr`, `Cap`,
  `DT`, `BoolYN`, `NumSci`, `NumPct`, `TryNum`, `NumOr`.
- public `Put` outside HTTP/network allowlist.
- new `New[A-Z]` named factories outside an explicit allowlist.

## Suggested Implementation Order

1. Fix underscore names in Core Convert.
2. Rename `Cap`/`NewCap` to `Capacity`/`NewCapacity`.
3. Rename math bits because the surface is small and heavily reusable.
4. Rename parse/format abbreviations.
5. Normalize collection `Put`.
6. Rename named 3D factories.
7. Add the audit rule after each category is clean.

