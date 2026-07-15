# Audit Tables

These tables summarize concrete findings from the 2026-07-02 live dump.

## Largest Function Owners

| Owner | Function count |
|---|---:|
| `Viper.Input.Keyboard` | 113 |
| `Viper.Graphics3D.Canvas3D` | 113 |
| `Viper.Input.Key` | 99 |
| `Viper.Input.Key` | 97 |
| `Viper.GUI.CodeEditor` | 96 |
| `Viper.Graphics.Canvas` | 86 |
| `Viper.Game3D.World3D` | 73 |
| `Viper.Game2D.SceneDocument` | 72 |
| `Viper.Graphics.Pixels` | 60 |
| `Viper.String` | 57 |

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

- `Viper.Collections.Ring.OwnsElements` and `SetOwnsElements`.
- `Viper.GUI.TextInput.Text` and `SetText`.
- `Viper.GUI.CodeEditor.Text` and `SetText`.
- `Viper.GUI.Dropdown.Selected` and `SetSelected`.
- `Viper.GUI.Slider.Value` and `SetValue`.
- `Viper.GUI.ProgressBar.Value` and `SetValue`.
- `Viper.Network.WsServer.Subprotocol` and `SetSubprotocol`.
- `Viper.Graphics3D.Material3D.Color` and `SetColor`.
- `Viper.Graphics3D.Light3D.Intensity` and `SetIntensity`.
- `Viper.Graphics3D.SceneNode.Position` and `SetPosition`.
- `Viper.Graphics3D.PhysicsBody3D.Velocity` and `SetVelocity`.
- `Viper.Graphics3D.Transform3D.Position` and `SetPosition`.

## Side-Channel APIs

Current count: 16.

- `Viper.Crypto.Tls.Error`.
- `Viper.System.Exec.LastExitCode`.
- `Viper.System.Pty.LastError`.
- `Viper.Zia.SemanticJob.Error`.
- `Viper.Data.JsonStream.Error`.
- `Viper.Data.Xml.Error`.
- `Viper.Data.Yaml.Error`.
- `Viper.Data.Serialize.Error`.
- `Viper.Game.UI.HudTable.LastHeaderClick`.
- `Viper.Game2D.SceneDocument.LastError`.
- `Viper.Network.SmtpClient.get_LastError`.
- `Viper.Network.RestClient.LastStatus`.
- `Viper.Network.RestClient.LastResponse`.
- `Viper.Network.RestClient.LastOk`.
- `Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadError`.
- `Viper.Graphics3D.AssetDiagnostics3D.get_LastLoadErrorCode`.

## Thin Classes

Zero-member classes:

- `Viper.Zia.SemanticJob.SemanticJobHandle`.
- `Viper.Zia.ProjectIndex.ProjectIndexHandle`.

Two-or-fewer-member classes include:

- `Viper.Core.ValueType`.
- `Viper.Diagnostics`.
- `Viper.GUI.Container`.
- `Viper.Assets.Resolver`.
- `Viper.Workspace.WorkspaceWatcher`.
- `Viper.Project.Manifest`.
- `Viper.Crypto.SecureRandom`.
- `Viper.GUI.Font`.
- `Viper.Graphics3D.CubeMap3D`.
- `Viper.Graphics3D.DistanceJoint3D`.
- `Viper.Graphics3D.RopeJoint3D`.
- `Viper.Game3D.Quality`.

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

- `Viper.Math.Mat4.New` with 16 values.
- `Viper.Math.Mat3.New` with 9 values.
- `Viper.Graphics3D.Mesh3D.SetBoneWeights` with 10 values.
- `Viper.Graphics2D.Tilemap.SetAutoTileLo/Hi` with 10 values.
- `Viper.Graphics3D.Mesh3D.AddVertex` with 9 values.
- `Viper.Graphics.Sprite.DrawTransformed`.
- `Viper.Graphics.Canvas.Triangle`, `Bezier`, `BlitRegion`.
- `Viper.Game.Collision.RectsOverlap`.
- `Viper.System.Pty.Open`.
- `Viper.Crypto.Tls.ConnectOptions`.

## Naming Outliers

Non-PascalCase public leaves:

- `Viper.Core.Convert.ToString_Int`.
- `Viper.Core.Convert.ToString_Double`.

Large acronym/mixed-acronym set: 153 leaves. Not all are wrong, but the policy
must choose canonical casing before release.

