---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0158: Make Scene-Level Properties Fully Authorable

## Status

Accepted (2026-07-23)

## Context

`Zanna.Game2D.SceneDocument` stores scene-wide Boolean, integer,
floating-point, string, and null properties in canonical scene JSON. Games use
that data for level theme, environment, spawn, objective, adapter, and other
configuration that applies to the complete scene.

The public runtime surface can read and write the four non-null scalar kinds,
test one known key, and remove one known key. It cannot enumerate existing
scene properties, identify an existing property's exact scalar kind, or author
the supported null kind. Zanna Studio can therefore edit typed properties on
placed objects but cannot provide a truthful scene-level property inspector.
Parsing or patching JSON inside Studio would create a second mutation path
outside the canonical `SceneDocument` model.

The missing operations change the public runtime C ABI and registry surface, so
ADR 0006 requires an explicit decision.

## Decision

`Zanna.Game2D.SceneDocument` gains three additive instance methods:

```text
Keys() -> Seq[String]
PropertyKind(key: String) -> String
SetNull(key: String) -> Void
```

Their C ABI entry points are:

```c
void *rt_game_scene_keys(void *scene);
rt_string rt_game_scene_property_kind(void *scene, rt_string key);
void rt_game_scene_set_null(void *scene, rt_string key);
```

`Keys` returns every scene-level property key in the same deterministic
lexicographic order used by canonical JSON serialization. The returned
sequence is owned by the caller and is empty when the scene has no properties.

`PropertyKind` returns one of the stable lowercase tokens `null`, `bool`,
`int`, `float`, or `string`. It returns an empty string when the key does not
exist.

`SetNull` creates or replaces a scene-level property with the null scalar kind.
It follows the same property-count, 128-byte key, and diagnostic behavior as
the existing typed scene-property setters. Existing invalid-handle behavior is
unchanged.

Zanna Studio uses these operations to expose a bounded scene-property
inspector. One accepted create, rename, type/value update, or removal is one
canonical history transaction. Invalid values, duplicate rename targets,
rejected keys, and edits that already match canonical content leave the
document, selection, dirty state, and history unchanged.

## Consequences

- Games can author level-wide typed metadata through Studio without JSON
  surgery or string coercion.
- The scene model exposes the same kind/null/enumeration capabilities already
  available for object properties.
- Existing IL modules and scene files remain compatible; the API is additive
  and canonical scene JSON does not change.
- Runtime registry metadata, generated API documentation, authored
  `SceneDocument` documentation, native tests, and Studio probes must cover the
  new surface.

## Alternatives Considered

- **Parse canonical JSON in Studio.** Rejected because it duplicates runtime
  format rules and can disagree with validation or preserved rich sections.
- **Expose a string-only scene-property editor.** Rejected because it silently
  flattens Boolean, integer, floating-point, and null values.
- **Require users to know keys in advance.** Rejected because an inspector
  cannot discover, select, rename, or safely remove existing metadata.
- **Return a map of boxed values.** Rejected for this additive slice because
  the established property API already uses keys plus typed accessors, and the
  smaller introspection surface keeps ownership and compatibility simple.
