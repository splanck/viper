# Properties, Constructors, And Factories Plan

## Properties

### Problem

Many classes expose a property and a matching `Set<Property>` method. This makes
the property look read-only even when the object state is writable.

Examples:

- `TextInput.Text` plus `SetText`
- `CodeEditor.Text` plus `SetText`
- `Dropdown.Selected` plus `SetSelected`
- `Slider.Value` plus `SetValue`
- `Material3D.Color` plus `SetColor`
- `SceneNode.Position` plus `SetPosition`
- `Physics3DBody.Velocity` plus `SetVelocity`
- `World3D.WorkerCount` plus `SetWorkerCount`

### Decision

Use writable properties for simple state assignment. Use methods for commands.

Target examples:

```text
textInput.Text = "hello"
slider.Value = 0.5
dropdown.Selected = 2
material.Color = Color.RGB(...)
world.WorkerCount = 4
```

For vector-like state, prefer one of these shapes:

```text
node.Position = Vec3.New(x, y, z)
node.MoveTo(x, y, z)
body.Velocity = Vec3.New(x, y, z)
```

Avoid this shape long-term:

```text
node.Position   // read-only property
node.SetPosition(x, y, z)
```

### Method Naming For Behavioral Mutation

Keep methods when mutation does more than assignment:

- `MoveTo` for spatial movement with transform updates.
- `ResizeTo` for layout/geometry changes.
- `Bind`/`Unbind` for resource attachment.
- `Apply` for a batch of settings.
- `Configure` for multi-field settings.
- `Set*` only when assigning a named field and no property assignment is
  available.

## Constructors

### Problem

Factory shapes are inconsistent:

- `New`, `NewSized`, `NewCapacity`, `NewCap`
- `NewBox`, `NewSphere`, `NewPBR`
- `Create`, `Open`, `Load`, `Parse`, `From*`, `With*`
- classes with no constructor but many factory methods

### Decision

Use the following constructor/factory vocabulary:

| Shape | Use For |
|---|---|
| `New` | canonical constructor |
| `With*` | named option/configuration constructor |
| `From*` | conversion from existing representation |
| `Parse*` | text/data parse |
| `Load*` | filesystem/asset load |
| `Open*` | acquire a resource/session/stream |
| `Connect*` | network/IPC connection |
| `Create*` | external artifact or calendar value |
| noun factory | domain-specific value constructors, such as `Mesh3D.Box` |

### Named Constructor Cleanup

Targets:

- `Mesh3D.NewBox` -> `Mesh3D.Box`
- `Mesh3D.NewSphere` -> `Mesh3D.Sphere`
- `Collider3D.NewCapsule` -> `Collider3D.Capsule`
- `Light3D.NewDirectional` -> `Light3D.Directional`
- `BlendTree3D.New1D` -> `BlendTree3D.OneDimensional` or `BlendTree3D.Linear`
- `BlendTree3D.New2D` -> `BlendTree3D.TwoDimensional` or `BlendTree3D.Planar`
- `World3D.NewWithCamera` -> `World3D.WithCamera`
- `Seq.NewSized` -> `Seq.WithLength` or `Seq.Filled`
- `BinaryBuffer.NewCap` -> `BinaryBuffer.NewCapacity`

Open decision: whether to keep acronym-rich names such as `Material3D.PBR` or
expand to `PhysicallyBased`. The practical recommendation is to keep `PBR` for
graphics experts but document it as physically based rendering.

## Class Size Review

### Threshold

Any class with more than 50 members needs a deliberate review. It may be valid,
but it should not happen accidentally.

### Candidates And Splits

#### `Viper.Input.Keyboard`

Move constants to `Viper.Input.Key`. Leave state methods on `Keyboard`.

#### `Viper.Game3D.Keys`

Delete or hide after `Viper.Input.Key` exists.

#### `Viper.Graphics3D.Canvas3D`

Consider subsurfaces:

- `Canvas3D.Frame`
- `Canvas3D.Stats`
- `Canvas3D.Lighting`
- `Canvas3D.PostFx`
- `Canvas3D.Debug`
- `Canvas3D.Capture`

Keep a few common shortcuts on `Canvas3D` if they are essential for beginner
ergonomics.

#### `Viper.GUI.CodeEditor`

Consider subsurfaces:

- `CodeEditor.Document`
- `CodeEditor.Selection`
- `CodeEditor.Diagnostics`
- `CodeEditor.Completion`
- `CodeEditor.Theme`
- `CodeEditor.Commands`

The code editor is a feature-rich control, but the public class should not feel
like an unstructured IDE service object.

#### `Viper.Graphics.Canvas`

Split or group:

- drawing primitives
- text
- frame/timing
- input/window state
- capture/screenshot
- debug helpers

The immediate canvas can keep common draw calls directly, but timing and window
state should be clearly separated.

#### `Viper.Game2D.SceneDocument`

Split:

- object APIs
- layer APIs
- asset APIs
- serialization APIs
- validation APIs

#### `Viper.Game3D.World3D`

Split:

- entity spawning/query
- simulation
- streaming
- worker/thread settings
- diagnostics
- environment

## Implementation Slices

1. Add an audit that reports property plus `Set<Property>` pairs.
2. Convert GUI simple state to writable properties.
3. Convert collection/system capacity constructor names.
4. Convert 3D named constructors to noun factories.
5. Review classes above 50 members and either split or add an allowlist comment.
6. Update docs/examples after each slice.

## Acceptance Criteria

- No simple state property has a matching `Set<Property>` method unless
  allowlisted.
- Named factories follow the constructor vocabulary.
- Large class allowlist is explicit and justified.
- `new` lowering still maps only to canonical `New` constructors.

