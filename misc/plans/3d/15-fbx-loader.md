# Phase 15: FBX Loader (Zero-Dependency)

## Goal

Full FBX binary format parser that produces Mesh3D, Skeleton3D, Animation3D, and Material3D from `.fbx` files. Handles both version <7500 (32-bit offsets) and >=7500 (64-bit offsets). Uses the existing `rt_compress_inflate()` from `src/runtime/io/rt_compress.c` for decompressing zlib-wrapped array properties (after stripping the zlib header/trailer). Zero external dependencies.

## Dependencies

- Phase 14 complete (Skeleton3D, Animation3D, AnimPlayer3D data structures)
- Phase 9 complete (multi-texture Material3D)
- Existing: `rt_compress_inflate()` in `src/runtime/io/rt_compress.c`
- Existing: `rt_bytes_read_i32le()` etc. in `src/runtime/io/rt_bytes.h`
- Existing: `rt_crc32_compute()` in `src/runtime/core/rt_crc32.h`

## Architecture

```
rt_fbx_load("model.fbx")
  ↓
  rt_fbx_parser.c: read binary header, parse node tree
    ↓ decompress arrays via rt_compress_inflate()
  rt_fbx_loader.c: resolve Connections (flat object list → hierarchy)
    ↓
  ├── rt_fbx_geometry.c:  extract vertices, normals, UVs, indices → Mesh3D
  ├── rt_fbx_skeleton.c:  extract bones, skin weights → Skeleton3D
  ├── rt_fbx_animation.c: extract anim curves → Animation3D
  └── rt_fbx_material.c:  extract colors, shininess → Material3D
    ↓
  Return FBX asset container (meshes[], skeleton, animations[], materials[])
```

## New Files

**`src/runtime/graphics/rt_fbx_loader.h`** (~40 LOC) — Public API declarations

**`src/runtime/graphics/rt_fbx_parser.c`** (~600 LOC) — Binary FBX node tree parser

**`src/runtime/graphics/rt_fbx_geometry.c`** (~500 LOC) — Geometry extraction

**`src/runtime/graphics/rt_fbx_skeleton.c`** (~400 LOC) — Skeleton + skin weight extraction

**`src/runtime/graphics/rt_fbx_animation.c`** (~350 LOC) — Animation curve extraction

**`src/runtime/graphics/rt_fbx_material.c`** (~200 LOC) — Material property extraction

**`src/runtime/graphics/rt_fbx_loader.c`** (~300 LOC) — Top-level orchestrator

## FBX Binary Format

### Header (27 bytes)

```
Bytes 0-22:   "Kaydara FBX Binary  \x00"  (magic, 23 bytes)
Bytes 23-24:  0x1A 0x00                    (unknown, always same)
Bytes 25-28:  uint32_t version             (e.g., 7400, 7500, 7700)
```

### Node Format

Each node has:
```
[end_offset]           uint32 or uint64 (version-dependent)
[num_properties]       uint32 or uint64
[property_list_length] uint32 or uint64
[name_length]          uint8
[name]                 char[name_length]
[properties]           ... (num_properties typed values)
[children]             ... (nested nodes until end_offset)
[null_record]          13 zero bytes (v<7500) or 25 zero bytes (v>=7500)
```

Version split: `is_64bit = (version >= 7500)`

### Property Types (13 total)

| Type Char | Type | Size |
|-----------|------|------|
| `C` | bool (uint8) | 1 |
| `Y` | int16 | 2 |
| `I` | int32 | 4 |
| `L` | int64 | 8 |
| `F` | float32 | 4 |
| `D` | float64 | 8 |
| `S` | string | length-prefixed (uint32 + chars) |
| `R` | raw bytes | length-prefixed (uint32 + bytes) |
| `b` | bool array | array header + data |
| `i` | int32 array | array header + data |
| `l` | int64 array | array header + data |
| `f` | float32 array | array header + data |
| `d` | float64 array | array header + data |

### Array Property Header

```
uint32_t count          — number of elements
uint32_t encoding       — 0 = raw, 1 = zlib-compressed
uint32_t compressed_len — byte size of (possibly compressed) data
[data]                  — compressed_len bytes
```

### Array Decompression

When `encoding == 1`:

FBX uses **zlib-wrapped DEFLATE** (RFC 1950), which consists of:
- 2-byte header (CMF + FLG)
- Raw DEFLATE payload (RFC 1951)
- 4-byte Adler-32 checksum trailer

The existing `rt_compress_inflate()` handles **raw DEFLATE only**. We must strip the 2-byte header AND 4-byte trailer before calling it.

```c
static void *fbx_decompress_array(const uint8_t *data, uint32_t compressed_len,
                                   uint32_t count, uint32_t element_size) {
    // Zlib stream: 2-byte header + DEFLATE payload + 4-byte Adler-32 trailer
    if (compressed_len < 6) return NULL;  // minimum: 2 header + 0 payload + 4 trailer
    uint32_t deflate_len = compressed_len - 6;

    // Create a Bytes object with just the raw DEFLATE payload
    void *comp_bytes = rt_bytes_new(deflate_len);
    memcpy(bytes_data(comp_bytes), data + 2, deflate_len);

    void *inflated = rt_compress_inflate(comp_bytes);
    if (!inflated) return NULL;

    size_t expected = (size_t)count * element_size;
    if (bytes_len(inflated) < (int64_t)expected) {
        // Decompressed size mismatch
        return NULL;
    }

    // Copy to standalone allocation (rt_compress_inflate returns GC-managed rt_bytes)
    void *result = malloc(expected);
    memcpy(result, bytes_data(inflated), expected);
    return result;
}
```

## Internal Data Structures

```c
// Parser state
typedef struct {
    const uint8_t *data;
    size_t data_len;
    size_t pos;
    uint32_t version;
    int is_64bit;          // version >= 7500
} fbx_parser_t;

// Property value
typedef struct {
    char type;             // one of C/Y/I/L/F/D/S/R/b/i/l/f/d
    union {
        int8_t   bool_val;
        int16_t  i16;
        int32_t  i32;
        int64_t  i64;
        float    f32;
        double   f64;
        struct { char *str; uint32_t len; } string;
        struct { uint8_t *data; uint32_t len; } raw;
        struct { void *data; uint32_t count; } array;
    } v;
} fbx_property_t;

// Node in the parsed tree
typedef struct fbx_node {
    char *name;
    fbx_property_t *properties;
    uint32_t property_count;
    struct fbx_node *children;
    uint32_t child_count;
} fbx_node_t;

// Connection entry
typedef struct {
    int64_t child_id;
    int64_t parent_id;
    char type[4];          // "OO" or "OP"
    char property[64];     // for "OP" connections
} fbx_connection_t;

// Loaded FBX asset container (runtime type)
typedef struct {
    void *vptr;
    void **meshes;         // Mesh3D[]
    int32_t mesh_count;
    void *skeleton;        // Skeleton3D (or NULL)
    void **animations;     // Animation3D[]
    int32_t animation_count;
    void **materials;      // Material3D[]
    int32_t material_count;
} rt_fbx_asset;
```

## Connection Resolution

FBX stores all objects as flat top-level nodes with unique 64-bit IDs. The "Connections" section declares relationships:

```
"OO", child_id, parent_id             → object-to-object (hierarchy)
"OP", child_id, parent_id, "propname" → object-to-property
```

The loader builds a connection map and walks it to determine:
- Which Geometry nodes belong to which Model nodes
- Which Deformer/Skin nodes attach to which Geometry
- Which SubDeformer/Cluster nodes belong to which Skin (one per bone)
- Which AnimationCurve nodes drive which AnimationCurveNode
- Which AnimationCurveNode is connected to which Model's property

## Geometry Extraction (rt_fbx_geometry.c)

### Mapping Modes

FBX attributes (normals, UVs) use a mapping mode + reference mode system:

| Mapping Mode | Reference Mode | Meaning |
|-------------|---------------|---------|
| ByControlPoint | Direct | One value per vertex, indexed by vertex index |
| ByControlPoint | IndexToDirect | One value per unique entry, vertex index → lookup index → value |
| ByPolygonVertex | Direct | One value per polygon-vertex (face corner), indexed sequentially |
| ByPolygonVertex | IndexToDirect | One value per unique entry, polygon-vertex → lookup index → value |
| AllSame | Direct | Single value for all vertices |

Each combination requires different extraction logic (~30 LOC per combination per attribute).

### Negative Index Convention

FBX polygon vertex indices use negative values to mark the last vertex of a polygon:

```c
// PolygonVertexIndex: [0, 1, 2, -4, 4, 5, 6, 7, -9]
// Polygon 0: vertices {0, 1, 2, 3}  (last index: ~(-4) = 3)
// Polygon 1: vertices {4, 5, 6, 7, 8}  (last index: ~(-9) = 8)

for (int i = 0; i < index_count; i++) {
    int32_t idx = indices[i];
    if (idx < 0) {
        idx = ~idx;  // bitwise NOT to get actual index
        // This is the last vertex of the current polygon
        // Triangulate the polygon and start a new one
    }
    current_polygon[polygon_vertex_count++] = idx;
}
```

### Fan Triangulation

For polygons with >3 vertices (quads, n-gons):

```c
// Fan from vertex 0: (0,1,2), (0,2,3), (0,3,4), ...
for (int i = 1; i < polygon_vertex_count - 1; i++) {
    add_triangle(polygon[0], polygon[i], polygon[i + 1]);
}
```

## Skeleton Extraction (rt_fbx_skeleton.c)

1. Find "Deformer" nodes of subtype "Skin" connected to geometry
2. For each child "SubDeformer" (subtype "Cluster"):
   - `Indexes` (int32 array): which vertices this bone influences
   - `Weights` (double array): weight per vertex
   - `Transform` (double[16]): geometry-to-bone-space matrix
   - `TransformLink` (double[16]): bone-to-world matrix at bind pose
3. Each Cluster connects to a Model node (the bone) via the connection table
4. Build bone hierarchy from Model parent-child connections
5. Limit to 4 bones per vertex (keep top 4 by weight, renormalize)

## Animation Extraction (rt_fbx_animation.c)

1. Find "AnimationStack" nodes (top-level clips)
2. Walk: AnimationStack → AnimationLayer → AnimationCurveNode → AnimationCurve
3. AnimationCurveNode connects to a Model (bone) property: `"Lcl Translation"`, `"Lcl Rotation"`, or `"Lcl Scaling"`
4. Each AnimationCurve has:
   - `KeyTime` (int64 array): timestamps in FBX ticks
   - `KeyValueFloat` (float array): values at each key
5. FBX time conversion: `seconds = fbx_time / 46186158000LL`
6. Build `rt_animation3d` keyframes per bone

## Coordinate System Handling

FBX files store coordinate system info in "GlobalSettings":
- `UpAxis` (0=X, 1=Y, 2=Z), `UpAxisSign` (+1/-1)
- `FrontAxis`, `FrontAxisSign`
- `CoordAxis`, `CoordAxisSign`

Viper uses right-handed Y-up (+X right, +Y up, +Z toward viewer). If the FBX uses a different system (e.g., Blender's Z-up), apply a correction matrix to all vertex positions, normals, and bone transforms:

```c
// Z-up to Y-up correction:
// [1  0  0  0]
// [0  0  1  0]    (swap Y and Z)
// [0 -1  0  0]    (negate new Z for handedness)
// [0  0  0  1]
```

## Public API

```c
void *rt_fbx_load(rt_string path);
int64_t rt_fbx_mesh_count(void *fbx);
void *rt_fbx_get_mesh(void *fbx, int64_t index);
void *rt_fbx_get_skeleton(void *fbx);
int64_t rt_fbx_animation_count(void *fbx);
void *rt_fbx_get_animation(void *fbx, int64_t index);
rt_string rt_fbx_get_animation_name(void *fbx, int64_t index);
int64_t rt_fbx_material_count(void *fbx);
void *rt_fbx_get_material(void *fbx, int64_t index);
```

## GC Finalizer

```c
static void rt_fbx_asset_finalize(void *obj) {
    rt_fbx_asset *fbx = (rt_fbx_asset *)obj;
    // meshes, skeleton, animations, materials are all GC-managed — do NOT free them
    // Only free the pointer arrays themselves
    free(fbx->meshes);     fbx->meshes = NULL;
    free(fbx->animations); fbx->animations = NULL;
    free(fbx->materials);  fbx->materials = NULL;
}
```

## runtime.def Entries

```c
RT_FUNC(FBXLoad,          rt_fbx_load,              "Viper.Graphics3D.FBX.Load",              "obj(str)")
RT_FUNC(FBXMeshCount,     rt_fbx_mesh_count,        "Viper.Graphics3D.FBX.get_MeshCount",     "i64(obj)")
RT_FUNC(FBXGetMesh,       rt_fbx_get_mesh,          "Viper.Graphics3D.FBX.GetMesh",           "obj(obj,i64)")
RT_FUNC(FBXGetSkeleton,   rt_fbx_get_skeleton,      "Viper.Graphics3D.FBX.GetSkeleton",       "obj(obj)")
RT_FUNC(FBXAnimCount,     rt_fbx_animation_count,   "Viper.Graphics3D.FBX.get_AnimationCount","i64(obj)")
RT_FUNC(FBXGetAnim,       rt_fbx_get_animation,     "Viper.Graphics3D.FBX.GetAnimation",      "obj(obj,i64)")
RT_FUNC(FBXGetAnimName,   rt_fbx_get_animation_name,"Viper.Graphics3D.FBX.GetAnimationName",  "str(obj,i64)")
RT_FUNC(FBXMaterialCount, rt_fbx_material_count,    "Viper.Graphics3D.FBX.get_MaterialCount", "i64(obj)")
RT_FUNC(FBXGetMaterial,   rt_fbx_get_material,      "Viper.Graphics3D.FBX.GetMaterial",       "obj(obj,i64)")

RT_CLASS_BEGIN("Viper.Graphics3D.FBX", FBX, "obj", FBXLoad)
    RT_PROP("MeshCount", "i64", FBXMeshCount, none)
    RT_PROP("AnimationCount", "i64", FBXAnimCount, none)
    RT_PROP("MaterialCount", "i64", FBXMaterialCount, none)
    RT_METHOD("GetMesh", "obj(i64)", FBXGetMesh)
    RT_METHOD("GetSkeleton", "obj()", FBXGetSkeleton)
    RT_METHOD("GetAnimation", "obj(i64)", FBXGetAnim)
    RT_METHOD("GetAnimationName", "str(i64)", FBXGetAnimName)
    RT_METHOD("GetMaterial", "obj(i64)", FBXGetMaterial)
RT_CLASS_END()
```

## Stubs

```c
void *rt_fbx_load(rt_string path) {
    (void)path;
    rt_trap("FBX.Load: graphics support not compiled in");
    return NULL;
}
int64_t rt_fbx_mesh_count(void *fbx) { (void)fbx; return 0; }
void *rt_fbx_get_mesh(void *fbx, int64_t i) { (void)fbx; (void)i; return NULL; }
// ... etc for all functions
```

## Usage Example (Zia)

```rust
var asset = FBX.Load("player.fbx")

var mesh = asset.GetMesh(0)
var skeleton = asset.GetSkeleton()
var walkAnim = asset.GetAnimation(0)
var material = asset.GetMaterial(0)

var player = AnimPlayer3D.New(skeleton)
player.Play(walkAnim)

while !canvas.ShouldClose {
    canvas.Poll()
    var dt = canvas.DeltaTime
    player.Update(dt / 1000.0)  // DeltaTime is ms, animation expects seconds

    canvas.Clear(0.1, 0.1, 0.2)
    canvas.Begin(cam)
    canvas.DrawMeshSkinned(mesh, transform, material, player)
    canvas.End()
    canvas.Flip()
}
```

## Tests (25)

| Test | Description |
|------|-------------|
| Header parse (v7400) | 32-bit offset version, correct node count |
| Header parse (v7700) | 64-bit offset version, correct node count |
| Bad magic | Invalid header → trap |
| Property type C (bool) | Parse boolean property |
| Property type I (int32) | Parse int32 property |
| Property type L (int64) | Parse int64 property |
| Property type F (float) | Parse float property |
| Property type D (double) | Parse double property |
| Property type S (string) | Parse string property |
| Property type R (raw) | Parse raw bytes property |
| Array uncompressed | Parse raw float array |
| Array zlib compressed | Compressed array → strip zlib header/trailer → rt_compress_inflate → correct values |
| Node tree traversal | Find specific named node in parsed tree |
| Connection resolution | Wire geometry → model → deformer correctly |
| Geometry: cube | Extract cube geometry, verify 8 vertices / 12 triangles |
| Geometry: negative indices | Handle bitwise-NOT polygon boundaries |
| Geometry: quad triangulation | Quad split into 2 triangles |
| Geometry: ByPolygonVertex normals | Extract per-face-corner normals |
| Geometry: IndexToDirect UVs | Indirect UV lookup |
| Skeleton extraction | 3-bone chain, verify hierarchy + bind poses |
| Skin weights | 4 bones per vertex, weights sum to 1.0 |
| Animation extraction | Keyframe times + values match expected |
| Coord system Z-up → Y-up | Blender Z-up file → Y-up vertices |
| Material extraction | Diffuse color + shininess extracted |
| Truncated file | Incomplete FBX → trap with meaningful message |

## Test Data Strategy

Three complementary approaches:

1. **Programmatic generator**: A helper function `build_test_fbx_binary()` that constructs valid FBX binary data from parameters (version, vertex positions, bone hierarchy). This is more maintainable than hand-editing hex arrays and self-documents the expected structure. Use for geometry, skeleton, and animation extraction tests.

2. **Static byte arrays**: `static const uint8_t[]` for minimal edge cases (bad magic, truncated file, unsupported property types). These test specific byte sequences where hand-crafting is straightforward.

3. **Real exported fixtures** (optional): Small `.fbx` files exported from Blender in `tests/fixtures/` (single triangle, 2-bone skeleton). These catch real-world format variations that synthetic data may miss. Mark tests with a `fixtures_required` label for CI environments without asset files.

Minimal test FBX: single triangle with 3 vertices, no skeleton (~500 bytes).
Skeleton test FBX: cube with 2-bone skeleton (~2KB).

## Build Changes

Add all 7 new `.c` files to `RT_GRAPHICS_SOURCES` in `src/runtime/CMakeLists.txt`.
