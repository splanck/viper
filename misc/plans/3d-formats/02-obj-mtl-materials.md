# Plan 02: OBJ .mtl Material File Support

## Context

The OBJ loader (`rt_mesh3d.c:602-762`) ignores `mtllib` and `usemtl` directives (line 749).
Every OBJ exporter produces a companion .mtl file with material definitions and texture
paths. Without .mtl support, OBJ models load as untextured white meshes.

## Scope

**In scope:**
- Parse .mtl files referenced by `mtllib` in .obj
- Material properties: `Kd` (diffuse), `Ks` (specular), `Ns` (shininess), `d`/`Tr` (opacity)
- Texture maps: `map_Kd` (diffuse), `map_Bump`/`map_Kn` (normal), `map_Ks` (specular)
- `usemtl` directive to assign materials per face group
- Return materials alongside mesh from a new API function

**Out of scope:**
- PBR extensions (`Pr`, `Pm`, `map_Pr`) — non-standard
- Displacement maps (`disp`)
- Decal textures (`decal`)

## Technical Design

### New API

The current `rt_mesh3d_from_obj` returns a single mesh with no materials. Add a new
function that returns an asset bundle:

```c
// rt_mesh3d.h or rt_obj.h
typedef struct {
    void *mesh;       // rt_mesh3d*
    void *material;   // rt_material3d* (NULL if no usemtl)
} rt_obj_group_t;

// Load OBJ with materials. Returns array of mesh+material pairs.
void *rt_obj_load(rt_string path);          // Returns rt_obj_asset*
int64_t rt_obj_mesh_count(void *asset);
void *rt_obj_get_mesh(void *asset, int64_t index);
void *rt_obj_get_material(void *asset, int64_t index);
```

The existing `rt_mesh3d_from_obj` remains unchanged (backward compat, returns first mesh).

### .mtl Parser (~120 LOC)

```c
static void parse_mtl_file(const char *mtl_path, const char *obj_dir,
                            mtl_entry_t *materials, int *count) {
    // Line-by-line:
    // "newmtl Name"     → start new material
    // "Kd r g b"        → diffuse color
    // "Ks r g b"        → specular color
    // "Ns value"        → shininess (0-1000 → map to Blinn-Phong)
    // "d value"         → opacity (1.0 = opaque)
    // "Tr value"        → transparency (1.0 = transparent, = 1-d)
    // "map_Kd file.png" → diffuse texture (resolve relative to OBJ dir)
    // "map_Bump file"   → normal map
    // "map_Ks file"     → specular map
    // "illum N"         → illumination model (0=color, 1=diffuse, 2=specular)
}
```

### Texture Path Resolution

Texture paths in .mtl are relative to the .mtl file's directory (which is typically
the same as the .obj file's directory):

```c
// Given: obj_path = "/assets/models/robot.obj"
//        map_Kd = "textures/robot_diffuse.png"
// Resolve to: "/assets/models/textures/robot_diffuse.png"
```

Use the existing `rt_pixels_load()` auto-detect loader for texture files.

### OBJ Parser Integration

In `rt_mesh3d_from_obj`, the `usemtl` directive splits faces into groups. Each group
gets its own mesh+material pair. The parser needs to:

1. Parse `mtllib filename.mtl` → load materials into a lookup table
2. Track current material name via `usemtl MaterialName`
3. When material changes, finalize current mesh and start a new one
4. Return array of (mesh, material) pairs

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_mesh3d.c` | Add .mtl parser, `rt_obj_load` function, update OBJ parser for usemtl groups (~200 LOC) |
| `src/runtime/graphics/rt_pixels.h` or new `rt_obj.h` | Declare new API |
| `src/il/runtime/runtime.def` | Add `OBJLoad`, `OBJMeshCount`, `OBJGetMesh`, `OBJGetMaterial` |
| `src/tests/CMakeLists.txt` | Register test |
| `docs/viperlib/graphics/` | Update Mesh3D docs with .mtl support |

## Tests

1. **OBJ without .mtl** — existing `FromOBJ` behavior unchanged (regression)
2. **Synthetic .obj + .mtl** — write minimal OBJ (`v`/`f`) + MTL (`newmtl`/`Kd`) to temp files, load via `rt_obj_load`, verify material color matches
3. **Texture reference** — MTL with `map_Kd` pointing to a PNG. Verify material has texture set.
4. **Multiple materials** — OBJ with 2 `usemtl` groups. Verify mesh_count==2, each with correct material.
5. **Missing .mtl** — OBJ references non-existent .mtl. Verify graceful fallback (load mesh without materials).

## Estimated LOC

~200 (mtl parser ~120, OBJ grouping ~50, new API ~30)
