---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0160: Add Project Scene Component Schemas

## Status

Accepted (2026-07-23)

## Context

The 2D and 3D scene editors can author exact typed scalar data, but each key
must be entered manually. Substantial projects repeat groups such as enemy
spawn archetype, encounter membership, trigger target, checkpoint identity,
and pickup configuration across many objects or nodes. Free-form editing makes
those groups slow to create and easy to spell or type inconsistently.

The schema must remain project-defined. Zanna cannot prescribe one gameplay
component model for Xenoscape, Ashfall, and unrelated games, and Studio must
not add a package manager or external schema dependency. The existing 2D
`SceneDocument` property table and 3D `SceneNode` metadata table are the
canonical persistence layers.

## Decision

Studio recognizes one optional `scene-components.json` file at the root of each
open workspace folder. Version 1 has this shape:

```json
{
  "version": 1,
  "components": [
    {
      "name": "enemy-spawn",
      "label": "Enemy Spawn",
      "target": "both",
      "description": "Reusable enemy placement data.",
      "fields": [
        {
          "key": "enemy.archetype",
          "label": "Archetype",
          "type": "string",
          "default": "grunt"
        },
        {"key": "enemy.level", "type": "int", "default": 1},
        {"key": "enemy.radius", "type": "float", "default": 4.0},
        {"key": "enemy.active", "type": "bool", "default": true},
        {"key": "enemy.target", "type": "null"}
      ]
    }
  ]
}
```

`target` is `2d-object`, `3d-node`, or `both`. Field types are `string`, `int`,
`float`, `bool`, and `null`. An omitted default means the canonical zero value:
empty string, `0`, `0.0`, `false`, or null. Labels and descriptions are
presentation only. Component names and field keys use the portable ASCII set
`A-Z`, `a-z`, `0-9`, `.`, `_`, `:`, and `-`.

Parsing is bounded and fail-closed:

- The file is at most 1 MB.
- A schema contains at most 128 components, 64 fields per component, and 2,048
  fields total.
- Component names are unique case-insensitively; field keys are unique within
  one component.
- Names are at most 64 characters, keys 128 characters, labels 128
  characters, descriptions 1,024 characters, and string defaults 16,384
  characters.
- Every explicit default must have the declared JSON scalar type. Integers
  must parse as exact integers and floats must be finite.
- Any invalid component or field rejects the complete schema. Studio never
  publishes a valid prefix or retains definitions from an older load.

A saved scene uses the schema belonging to the longest matching workspace
root. An unsaved scene uses the only open root, but no root is guessed in a
multi-root workspace. Changing the document path or workspace ownership
re-resolves the schema. The inspector also exposes **Reload** for explicit
external-file rereads.

Both scene editors expose the same **Edit Schema** form. The component
application picker remains filtered to the active 2D or 3D target, while the
authoring list deliberately shows the complete project schema so a target-only
definition is never hidden from maintenance in the other editor. The form can
create, update, remove, and stably reorder components and fields, including
their labels, descriptions, targets, scalar kinds, and typed defaults. Removing
or renaming a definition changes only the reusable template; existing scene
data is retained and is not migrated.

Structured changes mutate the parsed version-1 JSON tree and preserve unknown
members on retained root, component, and field objects. Every candidate is
pretty-formatted and reparsed through the same fail-closed schema validator
before publication. Existing files are replaced atomically through a
root-confined workspace edit with expected size and modification time plus an
exact-content preflight. A missing file is created through a no-overwrite
same-directory atomic create. A detected external change rejects save, undo, or
redo until reload; bounded polling also accepts external changes and clears
stale local history.

Schema history is independent of scene history. Each editor session retains at
most 20 exact file-presence/text snapshots for **Undo Schema** and **Redo
Schema**. Creating, editing, undoing, or redoing the project file never changes
`Document.content`, scene revision, or scene dirty state.

Both scene editors present the same component palette. **Add Missing** first
examines every selected object or node and every component field:

- An absent key is staged with its typed default.
- An existing key of the same kind is preserved, including its authored value.
- An existing key of a different kind aborts the complete selection before any
  write.

After successful preflight, all missing values are applied and verified through
the runtime scene APIs. One canonical scene serialization creates one history
entry. A rejected write or failed serialization restores the complete prior
scene; an already-complete component is a history no-op. This operation is
multi-selection aware in both editors.

**Edit Field** copies the selected schema field into the raw typed editor.
The 2D batch property editor can use that draft for one or more objects. The 3D
raw metadata editor remains single-node, so its Edit Field action is disabled
for a node group even though Add Missing remains available.

The schema defines reusable authoring vocabulary, not runtime behavior. Game
code still chooses how keys map to entities, systems, scripts, triggers, or
other component implementations.

## Consequences

- Projects gain repeatable, typed scene authoring without a built-in gameplay
  model or an external dependency.
- The same schema can target 2D objects, 3D nodes, or both, while persistence
  continues through the existing canonical scene formats.
- Batch application is safe for heterogeneous selections: it preserves valid
  authored overrides and makes conflicts visible instead of coercing them.
- Schemas are intentionally root-local. A scene moved between roots may expose
  a different palette, but its already-authored values remain ordinary scene
  data.
- Structured schema editing preserves forward-compatible unknown members and
  protects external changes, but pretty-formats the complete JSON file after an
  accepted edit.
- Studio does not yet provide inheritance, required-field validation, enums,
  asset-reference pickers, nested values, automatic schema/scene-data
  migrations, or game-code generation.
- Parser, authoring, 2D editor, and 3D editor probes must cover limits, target
  filtering, cross-target maintenance, exact scalar defaults, unknown-member
  preservation, atomic conflicts, independent file undo/redo, no-op scene
  history, and canonical scene round trips.

## Alternatives Considered

- **Hard-code components in Studio.** Rejected because projects have unrelated
  gameplay models and release independently of the editor.
- **Infer components from existing keys.** Rejected because inference cannot
  establish defaults, intended scalar kinds, target compatibility, or stable
  labels.
- **Overwrite every field when applying a component.** Rejected because
  applying a template must not erase deliberate per-instance tuning.
- **Coerce conflicting scalar kinds.** Rejected because coercion can silently
  change gameplay meaning and destroy exact integer/float/null distinctions.
- **Store component membership separately in scene formats.** Deferred because
  the current need is reusable typed data; a generalized runtime component
  graph would be a larger cross-layer contract.
- **Search parent directories for arbitrary schema files.** Rejected because
  it makes ownership ambiguous in nested and multi-root workspaces.
- **Reconstruct only known JSON members after each form edit.** Rejected because
  a version-1 tool must not silently erase project or future-extension data it
  does not understand.
