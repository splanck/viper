# Audit Tables

These tables summarize concrete findings from the 2026-07-02 live dump.

## Largest Function Owners

| Owner | Function count |
|---|---:|
| `Zanna.Input.Keyboard` | 113 |
| `Zanna.Graphics3D.Canvas3D` | 113 |
| `Zanna.Input.Key` | 99 |
| `Zanna.Input.Key` | 97 |
| `Zanna.GUI.CodeEditor` | 96 |
| `Zanna.Graphics.Canvas` | 86 |
| `Zanna.Game3D.World3D` | 73 |
| `Zanna.Game2D.SceneDocument` | 72 |
| `Zanna.Graphics.Pixels` | 60 |
| `Zanna.String` | 57 |

## Duplicate C-Symbol/Signature Examples

The full current count is 175 groups involving 352 names.

| C symbol | Duplicate public names |
|---|---|
| `rt_bits_leadz` | `Bits.LeadZ`, `Bits.CountLeadingZeros` |
| `rt_bits_rotl` | `Bits.Rotl`, `Bits.RotateLeft` |
| `rt_bits_rotr` | `Bits.Rotr`, `Bits.RotateRight` |
| `rt_bits_trailz` | `Bits.TrailZ`, `Bits.CountTrailingZeros` |
| `rt_bits_ushr` | `Bits.Ushr`, `Bits.ShiftRightLogical` |
| `rt_bloomfilter_fpr` | `BloomFilter.Fpr`, `BloomFilter.FalsePositiveRate` |
| `rt_lrucache_put` | `LruCache.Put`, `LruCache.Set` |
| `rt_bimap_put` | `BiMap.Put`, `BiMap.Set` |
| `rt_multimap_put` | `MultiMap.Put`, `MultiMap.Add` |
| `rt_gc_collect` | `Memory.GC.Collect`, `Runtime.GC.Collect` |
| `rt_memory_retain` | `Memory.Retain`, `Runtime.Unsafe.Retain` |
| `rt_throw_msg_set` | `Runtime.Unsafe.SetThrowMsg`, `Error.SetThrowMsg` |

## Bare Object Exposure

| Pattern | Count |
|---|---:|
| Function returns bare `obj` | 1,049 |
| Function returns typed `obj<T>` | 445 |
| Function has bare `obj` parameter | 4,889 |
| Function has typed `obj<T>` parameter | 48 |
| Method returns bare `obj` | 1,014 |
| Method has bare `obj` parameter | 1,026 |
| Property type is bare `obj` | 50 |
| Property type is typed `obj<T>` | 81 |

## Boolean Getter Mismatches

| Setter | Getter |
|---|---|
| `CodeEditor.SetShowLineNumbers: void(obj,i1)` | `GetShowLineNumbers: i64(obj)` |
| `CodeEditor.SetInsertSpaces: void(obj,i1)` | `GetInsertSpaces: i64(obj)` |
| `CodeEditor.SetWordWrap: void(obj,i1)` | `GetWordWrap: i64(obj)` |
| `CodeEditor.SetShowIndentGuides: void(obj,i1)` | `GetShowIndentGuides: i64(obj)` |
| `CodeEditor.SetReadOnly: void(obj,i1)` | `GetReadOnly: i64(obj)` |

## Property Plus Setter-Method Pairs

Current count: 31. These are read-only catalog properties with separate setter
methods.

Representative pairs:

- `Zanna.Collections.Ring.OwnsElements` and `SetOwnsElements`.
- `Zanna.GUI.TextInput.Text` and `SetText`.
- `Zanna.GUI.CodeEditor.Text` and `SetText`.
- `Zanna.GUI.Dropdown.Selected` and `SetSelected`.
- `Zanna.GUI.Slider.Value` and `SetValue`.
- `Zanna.GUI.ProgressBar.Value` and `SetValue`.
- `Zanna.Network.WsServer.Subprotocol` and `SetSubprotocol`.
- `Zanna.Graphics3D.Material3D.Color` and `SetColor`.
- `Zanna.Graphics3D.Light3D.Intensity` and `SetIntensity`.
- `Zanna.Graphics3D.SceneNode.Position` and `SetPosition`.
- `Zanna.Graphics3D.PhysicsBody3D.Velocity` and `SetVelocity`.
- `Zanna.Graphics3D.Transform3D.Position` and `SetPosition`.

## Side-Channel APIs

Current count: 16.

- `Zanna.Crypto.Tls.Error`.
- `Zanna.System.Exec.LastExitCode`.
- `Zanna.System.Pty.LastError`.
- `Zanna.Zia.SemanticJob.Error`.
- `Zanna.Data.JsonStream.Error`.
- `Zanna.Data.Xml.Error`.
- `Zanna.Data.Yaml.Error`.
- `Zanna.Data.Serialize.Error`.
- `Zanna.Game.UI.HudTable.LastHeaderClick`.
- `Zanna.Game2D.SceneDocument.LastError`.
- `Zanna.Network.SmtpClient.get_LastError`.
- `Zanna.Network.RestClient.LastStatus`.
- `Zanna.Network.RestClient.LastResponse`.
- `Zanna.Network.RestClient.LastOk`.
- `Zanna.Graphics3D.AssetDiagnostics3D.get_LastLoadError`.
- `Zanna.Graphics3D.AssetDiagnostics3D.get_LastLoadErrorCode`.

## Thin Classes

Zero-member classes:

- `Zanna.Zia.SemanticJob.SemanticJobHandle`.
- `Zanna.Zia.ProjectIndex.ProjectIndexHandle`.

Two-or-fewer-member classes include:

- `Zanna.Core.ValueType`.
- `Zanna.Diagnostics`.
- `Zanna.GUI.Container`.
- `Zanna.Assets.Resolver`.
- `Zanna.Workspace.WorkspaceWatcher`.
- `Zanna.Project.Manifest`.
- `Zanna.Crypto.SecureRandom`.
- `Zanna.GUI.Font`.
- `Zanna.Graphics3D.CubeMap3D`.
- `Zanna.Graphics3D.DistanceJoint3D`.
- `Zanna.Graphics3D.RopeJoint3D`.
- `Zanna.Game3D.Quality`.

## Cross-Owner Method Targets

Current count: 65.

Representative examples:

- `System.Environment` methods delegate to `System.Machine` and `IO.Dir`.
- `Graphics.Surface2D` methods delegate to `RenderTarget2D`.
- `Graphics.ScreenScaler` delegates to `Viewport2D`.
- `GUI.VBox` and `GUI.HBox` delegate to `GUI.Container`.
- `Graphics.ParticleSystem2D` and `Graphics.Emitter2D` delegate to
  `Game.ParticleEmitter`.
- `Game.Physics2D.CircleBody` delegates to `Physics2D.Body`.
- `Text.Json` delegates to `Collections.Map`.

## High-Arity Hotspots

Current count: 210 public functions/methods with arity 6 or higher.

Representative examples:

- `Zanna.Math.Mat4.New` with 16 values.
- `Zanna.Math.Mat3.New` with 9 values.
- `Zanna.Graphics3D.Mesh3D.SetBoneWeights` with 10 values.
- `Zanna.Graphics2D.Tilemap.SetAutoTileLo/Hi` with 10 values.
- `Zanna.Graphics3D.Mesh3D.AddVertex` with 9 values.
- `Zanna.Graphics.Sprite.DrawTransformed`.
- `Zanna.Graphics.Canvas.Triangle`, `Bezier`, `BlitRegion`.
- `Zanna.Game.Collision.RectsOverlap`.
- `Zanna.System.Pty.Open`.
- `Zanna.Crypto.Tls.ConnectOptions`.

## Naming Outliers

Non-PascalCase public leaves:

- `Zanna.Core.Convert.ToString_Int`.
- `Zanna.Core.Convert.ToString_Double`.

Large acronym/mixed-acronym set: 153 leaves. Not all are wrong, but the policy
must choose canonical casing before release.

