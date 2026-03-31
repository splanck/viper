# Plan 04: FBX Morph Target (Blend Shape) Extraction

## Context

The FBX loader extracts geometry, skeleton, and animation but ignores morph targets
(blend shapes). FBX stores morph data in `BlendShape` → `BlendShapeChannel` → `Shape`
node hierarchies. Users must currently recreate morph targets manually via the
`MorphTarget3D` API. This plan wires FBX morph data into the existing `rt_morphtarget3d`
system automatically.

## Scope

**In scope:**
- Parse `BlendShape` deformer nodes from FBX
- Extract `BlendShapeChannel` sub-nodes (one per morph shape)
- Extract `Shape` geometry nodes (delta positions per shape)
- Create `rt_morphtarget3d` with extracted shapes
- Attach morph target to the corresponding mesh

**Out of scope:**
- Morph normal deltas (FBX rarely exports these; compute from position deltas if needed)
- In-between shapes (multiple shapes per channel at different weight levels)
- Morph animation curves (animate weights over time — separate plan)

## FBX Morph Structure

```
Objects:
  Deformer: <id>, "BlendShape\x00\x01Deformer", "BlendShape" {
  }
  Deformer: <id>, "ShapeName\x00\x01SubDeformer", "BlendShapeChannel" {
    DeformPercent: 0.0        // default weight (0-100)
  }
  Geometry: <id>, "ShapeName\x00\x01Geometry", "Shape" {
    Indexes: [int32 array]    // affected vertex indices
    Vertices: [double array]  // position deltas (3 per index)
    Normals: [double array]   // normal deltas (optional)
  }

Connections:
  C: "OO", <shape_geometry_id>, <blendshape_channel_id>
  C: "OO", <blendshape_channel_id>, <blendshape_id>
  C: "OO", <blendshape_id>, <mesh_geometry_id>
```

## Implementation (~150 LOC)

### Step 1: Collect Shape Geometry

After existing geometry extraction, scan for `Geometry` nodes with type `"Shape"`:

```c
for each node in Objects where node.type == "Shape":
    extract Indexes (int32 array → affected vertex indices)
    extract Vertices (double array → 3 deltas per affected vertex)
    store as (shape_id, indices[], deltas[], count)
```

### Step 2: Build Morph Target from Connections

Trace the connection chain: Shape → BlendShapeChannel → BlendShape → Mesh

```c
for each BlendShape connected to a mesh geometry:
    create rt_morphtarget3d(vertex_count)
    for each BlendShapeChannel connected to this BlendShape:
        get shape name from channel node name
        shape_idx = rt_morphtarget3d_add_shape(morph, name)
        find connected Shape geometry
        for each (vertex_idx, delta) in shape data:
            rt_morphtarget3d_set_delta(morph, shape_idx, vertex_idx, dx, dy, dz)
    attach morph to mesh
```

### Step 3: Add to rt_fbx_asset

Extend the FBX asset structure to include morph targets per mesh:

```c
// In rt_fbx_asset:
void **morph_targets;    // rt_morphtarget3d*[] parallel to meshes[]
```

Add API:
```c
void *rt_fbx_get_morph_target(void *fbx, int64_t mesh_index);
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_fbx_loader.c` | Shape geometry parsing, connection tracing, morph creation (~150 LOC) |
| `src/runtime/graphics/rt_fbx_loader.h` | Add morph target getter declaration |
| `src/il/runtime/runtime.def` | Add `FBXGetMorphTarget` entry |
| `docs/viperlib/graphics/` | Document FBX morph target support |

## Tests

1. **FBX without morph targets** — existing behavior unchanged
2. **FBX with BlendShape** — verify morph target is non-NULL, shape count > 0, shape names match (requires test FBX with blend shapes, or construct FBX nodes synthetically)
3. **Sparse deltas** — verify only affected vertices have non-zero deltas

## Estimated LOC

~150 (Shape parsing ~50, connection tracing ~50, morph creation ~30, API ~20)
