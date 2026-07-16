# Class, Property, Method, And Constructor Shape

This document defines the target shape of runtime classes.

## Class Kinds

The catalog should distinguish:

- instance handle;
- static module;
- enum/domain class;
- namespace facade;
- value object;
- result object;
- option object;
- event object;
- unsafe module;
- internal or hidden class.

Current inference already labels some rows, but this should be declared rather
than guessed.

## Thin Classes

The live dump has 17 classes with two or fewer public members and two public
zero-member classes:

- `Viper.Zia.SemanticJob.SemanticJobHandle`;
- `Viper.Zia.ProjectIndex.ProjectIndexHandle`.

Decision: thin classes are acceptable only if they are intentional opaque
handles or enum/domain containers. Metadata should say so. Otherwise they
should be merged, completed, or hidden.

## Properties

Decision: properties represent state. Methods represent work.

Preferred:

- read-only property for computed or immutable state;
- writable property for simple mutable state;
- command method for operations with side effects beyond setting state;
- event polling methods or event objects for transient state.

Current property/setter-method pairs are read-only catalog properties with
separate `SetX` methods. There are 31 such pairs. Examples:

- `TextInput.Text` and `SetText`;
- `CodeEditor.Text` and `SetText`;
- `Slider.Value` and `SetValue`;
- `Material3D.Color` and `SetColor`;
- `Light3D.Intensity` and `SetIntensity`;
- `SceneNode.Position` and `SetPosition`;
- `Transform3D.Position` and `SetPosition`.

Decision: for stable APIs, either:

- expose writable property metadata and keep `SetX` as legacy compatibility;
  or
- do not expose a property and keep command-style methods only.

Do not expose read-only properties that are obviously mutable through a
separate setter unless the language cannot support property assignment yet. If
that limitation exists, document it in catalog metadata.

## Constructors And Factories

All constructors currently use `New`, and `rtgen` may auto-inject a constructor
method when the class constructor target is under the class prefix.

Decision:

- constructor methods should be explicit in source or explicitly marked as
  auto-generated in emitted metadata;
- `New` creates an in-memory object and does not perform external IO;
- `Open`, `Load`, `Connect`, and `Parse` are factories that can fail and
  should return `Result<T>`;
- `FromX` converts existing values and should be infallible or return
  `Result<T>` if validation is required.

## Methods And Overloads

The runtime method registry currently keys lookups by class, method, and arity.
This supports overload by arity but not overload by type.

Decision:

- add an audit that rejects same class/name/arity duplicate methods;
- if type overloads are desired, change the lookup key and document overload
  resolution before exposing them publicly;
- keep overloads sparse and obvious for user-facing APIs.

## Large Classes

Large classes make the API harder to learn and maintain. Current hotspots
include:

- `Viper.Input.Keyboard`;
- `Viper.Graphics3D.Canvas3D`;
- `Viper.Input.Key`;
- `Viper.Input.Key`;
- `Viper.GUI.CodeEditor`;
- `Viper.Graphics.Canvas`;
- `Viper.Game3D.World3D`;
- `Viper.Game2D.SceneDocument`.

Decision: large classes need sub-objects or grouped config/event/result types
when member count reflects multiple responsibilities.

Examples:

- `CodeEditor.Syntax`, `CodeEditor.Cursors`, `CodeEditor.Folds`,
  `CodeEditor.Performance`.
- `World3D.Assets`, `World3D.Entities`, `World3D.Physics`,
  `World3D.Streaming`.
- `Canvas3D.FrameStats`, `Canvas3D.Overlay`, `Canvas3D.Rendering`.

## Events

Transient state should not use ambiguous "last" properties. Use event queues or
poll result objects:

- `PollEvents() -> Seq<Event>`;
- `TakeEvent() -> Option<Event>`;
- `PollInput() -> InputFrame`;
- `CommandRegistry.Poll() -> Option<CommandInvocation>`.

