# Plan 01: glTF 2.0 Loading (.gltf / .glb)

## Context

glTF 2.0 is the Khronos Group open standard for 3D assets — the "JPEG of 3D." It's the
default export format for Blender, and supported by Unity, Unreal, Godot, Three.js, and
every major 3D tool. Viper currently only supports FBX (binary) and OBJ (basic). Adding
glTF makes Viper interoperable with the modern 3D ecosystem.

## Scope

**In scope:**
- .gltf (JSON + external binary buffers + external textures)
- .glb (single binary container: JSON chunk + BIN chunk)
- Meshes with positions, normals, UVs, vertex colors, tangents
- PBR materials (metallic-roughness → Blinn-Phong approximation)
- Embedded textures (base64 data URIs in .gltf, binary chunk in .glb)
- External texture file references (loaded via `rt_pixels_load`)
- Skeletal animation (skin + joints + inverse bind matrices)
- Morph targets (position + normal deltas)
- Scene hierarchy (nodes with TRS transforms)
- Multi-mesh scenes

**Out of scope:**
- Draco mesh compression (KHR_draco_mesh_compression) — optional, rare
- KTX2/Basis Universal textures — GPU-compressed, rare in game assets
- Sparse accessors — optimization for morph targets, can add later
- glTF extensions beyond KHR_materials_unlit (ignore unknown extensions gracefully)
- glTF export/save

## Key Insight: JSON Parser Already Exists

Viper has a full JSON parser at `src/runtime/text/rt_json.h`:
```c
void *rt_json_parse(rt_string text);       // Returns Map/Seq/String/boxed number
void *rt_json_parse_object(rt_string text); // Ensures root is object
```

This eliminates the ~300 LOC JSON parser from the estimate. The glTF loader can call
`rt_json_parse_object()` on the JSON content and navigate the result using the existing
Map/Seq runtime API.

## Technical Design

### File Structure

| File | Purpose | Est. LOC |
|------|---------|----------|
| `src/runtime/graphics/rt_gltf.h` | Public API | ~40 |
| `src/runtime/graphics/rt_gltf.c` | glTF/glb loader | ~1200 |
| `src/tests/unit/runtime/TestGltfLoad.cpp` | Unit tests | ~150 |

### Return Structure

Follow the `rt_fbx_asset` pattern:
```c
typedef struct {
    void *vptr;
    void **meshes;           // rt_mesh3d*[]
    int32_t mesh_count;
    void **materials;        // rt_material3d*[]
    int32_t material_count;
    void *skeleton;          // rt_skeleton3d* (NULL if no skin)
    void **animations;       // rt_animation3d*[]
    int32_t animation_count;
    void *scene_root;        // rt_scene_node3d* (NULL if no scene)
} rt_gltf_asset;
```

### Public API

```c
void *rt_gltf_load(rt_string path);                    // Load .gltf or .glb
int64_t rt_gltf_mesh_count(void *asset);
void *rt_gltf_get_mesh(void *asset, int64_t index);
int64_t rt_gltf_material_count(void *asset);
void *rt_gltf_get_material(void *asset, int64_t index);
void *rt_gltf_get_skeleton(void *asset);
int64_t rt_gltf_animation_count(void *asset);
void *rt_gltf_get_animation(void *asset, int64_t index);
rt_string rt_gltf_get_animation_name(void *asset, int64_t index);
```

### Loader Pipeline

```
1. Detect format (.gltf vs .glb) from magic bytes
   - .glb: 0x46546C67 ("glTF" LE) + version + length
   - .gltf: starts with '{' (JSON)

2. Parse JSON
   - .gltf: read file, call rt_json_parse_object()
   - .glb: extract JSON chunk (type 0x4E4F534A), parse it;
           extract BIN chunk (type 0x004E4942) as byte buffer

3. Load buffers
   - Resolve buffer.uri (external file or data:base64)
   - .glb: BIN chunk IS buffer 0

4. Extract accessors
   - For each accessor: type (SCALAR/VEC2/VEC3/VEC4/MAT4),
     componentType (5120=byte..5126=float), count, bufferView → byte range

5. Extract meshes
   - For each mesh.primitive:
     - Read POSITION, NORMAL, TEXCOORD_0, COLOR_0, TANGENT accessors
     - Read indices accessor
     - Create rt_mesh3d, populate vertices and triangles
     - Read morph targets (if present): POSITION/NORMAL deltas

6. Extract materials
   - For each material:
     - pbrMetallicRoughness.baseColorFactor → diffuse color
     - pbrMetallicRoughness.baseColorTexture → diffuse texture
     - pbrMetallicRoughness.metallicFactor → specular intensity
     - pbrMetallicRoughness.roughnessFactor → 1/shininess mapping
     - normalTexture → normal map
     - emissiveFactor + emissiveTexture → emissive
     - alphaMode ("OPAQUE"/"MASK"/"BLEND") → alpha handling

7. Load textures
   - Resolve image.uri (file path or data:base64)
   - Load via rt_pixels_load() (auto-detect PNG/JPEG)
   - .glb embedded: write temp or decode from memory

8. Extract skeleton (if skins[] present)
   - For each skin: joints array → bone indices
   - inverseBindMatrices accessor → MAT4 array
   - Build rt_skeleton3d with bone hierarchy from node tree

9. Extract animations (if animations[] present)
   - For each animation.channel:
     - sampler → input (timestamps) + output (values) accessors
     - target.path = "translation"/"rotation"/"scale"
     - Create rt_animation3d with keyframes

10. Build scene hierarchy (if scenes/nodes present)
    - Create rt_scene_node3d for each glTF node
    - Set TRS from node.translation/rotation/scale
    - Attach mesh/material references
    - Wire parent-child from node.children arrays
```

### PBR → Blinn-Phong Material Mapping

```c
// glTF PBR metallic-roughness → Viper Blinn-Phong
mat->diffuse = baseColorFactor * (1 - metallic);
mat->specular = lerp({0.04}, baseColorFactor, metallic);
mat->shininess = pow(1.0 - roughness, 2) * 256.0; // roughness → shininess
mat->emissive = emissiveFactor;
mat->alpha = baseColorFactor.a;
```

### Base64 Decoding

For embedded textures/buffers in .gltf files (`data:image/png;base64,...`):
```c
static uint8_t *decode_base64(const char *input, size_t *out_len);
```
~40 LOC with a 64-entry lookup table.

## Files to Create

| File | Purpose |
|------|---------|
| `src/runtime/graphics/rt_gltf.h` | Public API header |
| `src/runtime/graphics/rt_gltf.c` | glTF/glb loader implementation |
| `src/tests/unit/runtime/TestGltfLoad.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/CMakeLists.txt` | Add `graphics/rt_gltf.c` to RT_GRAPHICS_SOURCES |
| `src/il/runtime/runtime.def` | Add RT_FUNC entries for glTF API |
| `src/tests/CMakeLists.txt` | Register `test_gltf_load` |
| `docs/viperlib/graphics/` | Add glTF documentation |

## Tests

1. **Reject NULL/invalid** — NULL path → NULL, garbage file → NULL
2. **Minimal .glb** — construct a minimal valid .glb in test code (~200 bytes: header + JSON chunk with 1 triangle + BIN chunk with vertex data). Verify mesh_count==1, vertex_count==3.
3. **Material extraction** — .glb with PBR material. Verify diffuse color and shininess mapping.
4. **Reject non-glTF** — feed a PNG file, expect NULL return.
5. **Empty scene** — valid glTF with no meshes. Verify mesh_count==0, no crash.

## Verification

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R test_gltf_load
```
