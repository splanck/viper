# Phase 16: Morph Targets / Blend Shapes

## Goal

Support per-vertex position and normal deltas blended by weight, enabling facial animation, muscle flex, damage deformation, and other shape-based animation without skeleton overhead.

## Dependencies

- Phase 14 complete (skeletal animation infrastructure — morph targets can combine with skinning)
- Phase 1 complete (vertex transform pipeline)

## Architecture

```
MorphTarget3D.New(vertexCount)
  → AddShape("smile")
  → SetDelta(shapeIdx, vertexIdx, dx, dy, dz)
  → SetWeight("smile", 0.7)

Canvas3D.DrawMeshMorphed(mesh, transform, material, morphTargets)
  ↓
  CPU pre-pass: for each vertex:
    finalPos = basePos + Σ(weight[i] * posDeltas[i][vertex])
    finalNrm = normalize(baseNrm + Σ(weight[i] * nrmDeltas[i][vertex]))
  ↓
  Submit morphed vertices to normal draw pipeline
```

## New Files

**`src/runtime/graphics/rt_morphtarget3d.h`** (~30 LOC)
**`src/runtime/graphics/rt_morphtarget3d.c`** (~250 LOC)
**`src/lib/graphics/src/vgfx3d_morph.c`** (~150 LOC) — CPU morph computation

## Data Structures

```c
#define VGFX3D_MAX_MORPH_SHAPES 32

typedef struct {
    char name[64];
    float *pos_deltas;       // 3 * vertex_count floats (dx, dy, dz per vertex)
    float *nrm_deltas;       // 3 * vertex_count floats (or NULL — skip normal morphing)
} vgfx3d_morph_shape_t;

typedef struct {
    void *vptr;
    vgfx3d_morph_shape_t shapes[VGFX3D_MAX_MORPH_SHAPES];
    float weights[VGFX3D_MAX_MORPH_SHAPES];
    int32_t shape_count;
    int32_t vertex_count;    // must match mesh vertex count
} rt_morphtarget3d;
```

## CPU Morph Application

```c
void vgfx3d_apply_morph(const vgfx3d_vertex_t *src, vgfx3d_vertex_t *dst,
                         uint32_t vertex_count,
                         const rt_morphtarget3d *mt) {
    // Start with base mesh
    memcpy(dst, src, vertex_count * sizeof(vgfx3d_vertex_t));

    // Accumulate weighted deltas
    for (int s = 0; s < mt->shape_count; s++) {
        float w = mt->weights[s];
        if (fabsf(w) < 1e-6f) continue;  // skip zero-weight shapes

        const float *pd = mt->shapes[s].pos_deltas;
        const float *nd = mt->shapes[s].nrm_deltas;

        for (uint32_t v = 0; v < vertex_count; v++) {
            dst[v].pos[0] += w * pd[v * 3 + 0];
            dst[v].pos[1] += w * pd[v * 3 + 1];
            dst[v].pos[2] += w * pd[v * 3 + 2];

            if (nd) {
                dst[v].normal[0] += w * nd[v * 3 + 0];
                dst[v].normal[1] += w * nd[v * 3 + 1];
                dst[v].normal[2] += w * nd[v * 3 + 2];
            }
        }
    }

    // Renormalize normals
    for (uint32_t v = 0; v < vertex_count; v++) {
        float len = sqrtf(dst[v].normal[0]*dst[v].normal[0] +
                          dst[v].normal[1]*dst[v].normal[1] +
                          dst[v].normal[2]*dst[v].normal[2]);
        if (len > 1e-8f) {
            dst[v].normal[0] /= len;
            dst[v].normal[1] /= len;
            dst[v].normal[2] /= len;
        }
    }
}
```

## GPU Path (Future Optimization)

For GPU backends, morph deltas can be stored in a texture buffer or SSBO. The vertex shader samples per-vertex deltas and applies them:

```
// Vertex shader (pseudocode)
float3 morphedPos = aPosition;
for (int s = 0; s < activeShapeCount; s++) {
    float w = morphWeights[s];
    float3 delta = texelFetch(morphBuffer, vertexID * shapeCount + s).xyz;
    morphedPos += w * delta;
}
```

Initial implementation uses CPU path for all backends (simpler, correct). GPU optimization can be added later without API changes.

## Public API

```c
// Construction
void *rt_morphtarget3d_new(int64_t vertex_count);

// Shape management
int64_t rt_morphtarget3d_add_shape(void *mt, rt_string name);
void    rt_morphtarget3d_set_delta(void *mt, int64_t shape, int64_t vertex,
                                    double dx, double dy, double dz);
void    rt_morphtarget3d_set_normal_delta(void *mt, int64_t shape, int64_t vertex,
                                           double dx, double dy, double dz);

// Weight control
void   rt_morphtarget3d_set_weight(void *mt, int64_t shape, double weight);
double rt_morphtarget3d_get_weight(void *mt, int64_t shape);
void   rt_morphtarget3d_set_weight_by_name(void *mt, rt_string name, double weight);
int64_t rt_morphtarget3d_get_shape_count(void *mt);

// Mesh integration
void rt_mesh3d_set_morph_targets(void *mesh, void *morph_targets);

// Canvas3D drawing
void rt_canvas3d_draw_mesh_morphed(void *canvas, void *mesh, void *transform,
                                    void *material, void *morph_targets);
```

## FBX Integration (Phase 15 Extension)

FBX stores morph targets as "BlendShape" deformers with "BlendShapeChannel" sub-deformers, each containing "Shape" geometry nodes with vertex deltas. The FBX loader (Phase 15) can extract these and build `rt_morphtarget3d` automatically:

```c
void *rt_fbx_get_morph_targets(void *fbx, int64_t mesh_index);
```

## GC Finalizer

```c
static void rt_morphtarget3d_finalize(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    for (int i = 0; i < mt->shape_count; i++) {
        free(mt->shapes[i].pos_deltas);
        free(mt->shapes[i].nrm_deltas);
    }
}
```

## runtime.def Entries

```c
RT_FUNC(MorphTarget3DNew,            rt_morphtarget3d_new,              "Viper.Graphics3D.MorphTarget3D.New",              "obj(i64)")
RT_FUNC(MorphTarget3DAddShape,       rt_morphtarget3d_add_shape,        "Viper.Graphics3D.MorphTarget3D.AddShape",         "i64(obj,str)")
RT_FUNC(MorphTarget3DSetDelta,       rt_morphtarget3d_set_delta,        "Viper.Graphics3D.MorphTarget3D.SetDelta",         "void(obj,i64,i64,f64,f64,f64)")
RT_FUNC(MorphTarget3DSetNormalDelta, rt_morphtarget3d_set_normal_delta, "Viper.Graphics3D.MorphTarget3D.SetNormalDelta",   "void(obj,i64,i64,f64,f64,f64)")
RT_FUNC(MorphTarget3DSetWeight,      rt_morphtarget3d_set_weight,       "Viper.Graphics3D.MorphTarget3D.SetWeight",        "void(obj,i64,f64)")
RT_FUNC(MorphTarget3DGetWeight,      rt_morphtarget3d_get_weight,       "Viper.Graphics3D.MorphTarget3D.GetWeight",        "f64(obj,i64)")
RT_FUNC(MorphTarget3DSetWeightByName,rt_morphtarget3d_set_weight_by_name,"Viper.Graphics3D.MorphTarget3D.SetWeightByName","void(obj,str,f64)")
RT_FUNC(MorphTarget3DShapeCount,     rt_morphtarget3d_get_shape_count,  "Viper.Graphics3D.MorphTarget3D.get_ShapeCount",  "i64(obj)")
RT_FUNC(Mesh3DSetMorphTargets,       rt_mesh3d_set_morph_targets,       "Viper.Graphics3D.Mesh3D.SetMorphTargets",        "void(obj,obj)")
RT_FUNC(Canvas3DDrawMeshMorphed,     rt_canvas3d_draw_mesh_morphed,     "Viper.Graphics3D.Canvas3D.DrawMeshMorphed",      "void(obj,obj,obj,obj,obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.MorphTarget3D", MorphTarget3D, "obj", MorphTarget3DNew)
    RT_PROP("ShapeCount", "i64", MorphTarget3DShapeCount, none)
    RT_METHOD("AddShape", "i64(str)", MorphTarget3DAddShape)
    RT_METHOD("SetDelta", "void(i64,i64,f64,f64,f64)", MorphTarget3DSetDelta)
    RT_METHOD("SetNormalDelta", "void(i64,i64,f64,f64,f64)", MorphTarget3DSetNormalDelta)
    RT_METHOD("SetWeight", "void(i64,f64)", MorphTarget3DSetWeight)
    RT_METHOD("GetWeight", "f64(i64)", MorphTarget3DGetWeight)
    RT_METHOD("SetWeightByName", "void(str,f64)", MorphTarget3DSetWeightByName)
RT_CLASS_END()
```

## Stubs

```c
void *rt_morphtarget3d_new(int64_t vc) {
    (void)vc;
    rt_trap("MorphTarget3D.New: graphics support not compiled in");
    return NULL;
}
// All other functions: no-ops returning 0/NULL
```

## Usage Example (Zia)

```rust
// Procedural morph targets
var mt = MorphTarget3D.New(mesh.VertexCount)
var smile = mt.AddShape("smile")
mt.SetDelta(smile, 0, 0.0, 0.1, 0.0)   // vertex 0 moves up slightly
mt.SetDelta(smile, 5, 0.0, 0.15, 0.02)  // vertex 5 moves up and forward

// Animate weight over time
mt.SetWeight(smile, Math.Sin(time) * 0.5 + 0.5)

canvas.DrawMeshMorphed(faceMesh, transform, skinMat, mt)
```

## Tests (12)

| Test | Description |
|------|-------------|
| Single shape w=0.0 | No deformation (matches base mesh) |
| Single shape w=1.0 | Full deformation (vertex at base + delta) |
| Single shape w=0.5 | Half deformation |
| Multi-shape additive | Two shapes both w=1.0 → deltas sum |
| Negative weight | w=-0.5 → delta reversed |
| Normal delta | Perturbed normals re-normalized |
| No normal delta | Normal deltas NULL → normals unchanged |
| SetWeightByName | String lookup works correctly |
| Shape count | ShapeCount property reflects added shapes |
| Max shapes | 32 shapes added → works; 33rd → trap |
| Vertex count mismatch | Mesh vertex count != morph vertex count → trap at draw time |
| Combined with skinning | DrawMeshMorphed → morph first, then skin → correct result |
