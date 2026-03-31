# Plan 06: Scene Export/Import Format (.vscn)

## Context

Viper has a complete scene graph system (`Scene3D` + `SceneNode3D`) but no way to
save or load assembled scenes. Users must rebuild scene hierarchies programmatically
every run. A scene export format enables:
- Level editors and save/load workflows
- Prebuilt scenes that load without reconstruction code
- Scene sharing between programs

## Scope

**In scope:**
- Custom JSON-based `.vscn` format
- Save: serialize Scene3D → JSON (nodes, transforms, mesh/material refs)
- Load: parse JSON → reconstruct Scene3D with Mesh3D and Material3D objects
- Node hierarchy (parent-child)
- Per-node transforms (position, rotation, scale)
- Mesh references (by filename — OBJ/FBX/glTF/STL)
- Material properties (color, shininess, texture paths)
- Light definitions
- Camera parameters

**Out of scope:**
- Binary scene format (JSON is human-readable, adequate for game scenes)
- Embedded mesh data (reference external model files)
- Scene streaming/LOD
- Physics constraints
- Animation state

## Format Design (.vscn)

```json
{
  "format": "vscn",
  "version": 1,
  "assets": {
    "meshes": {
      "robot_body": { "file": "models/robot.glb", "mesh_index": 0 },
      "terrain": { "file": "models/terrain.obj" }
    },
    "materials": {
      "metal": {
        "diffuse": [0.8, 0.8, 0.9],
        "shininess": 64.0,
        "texture": "textures/metal_diffuse.png",
        "normal_map": "textures/metal_normal.png"
      },
      "grass": {
        "diffuse": [0.3, 0.6, 0.2],
        "texture": "textures/grass.png"
      }
    }
  },
  "lights": [
    { "type": "directional", "direction": [0, -1, 0.5], "color": [1, 1, 0.9], "intensity": 1.0 },
    { "type": "ambient", "color": [0.2, 0.2, 0.3] }
  ],
  "camera": {
    "fov": 60.0,
    "near": 0.1,
    "far": 1000.0,
    "position": [0, 5, 10],
    "target": [0, 0, 0]
  },
  "nodes": [
    {
      "name": "RobotBody",
      "mesh": "robot_body",
      "material": "metal",
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1],
      "children": [
        {
          "name": "RobotArm",
          "mesh": "robot_body",
          "material": "metal",
          "position": [1.5, 0.5, 0],
          "rotation": [0, 0, 0.38, 0.92]
        }
      ]
    },
    {
      "name": "Ground",
      "mesh": "terrain",
      "material": "grass",
      "position": [0, -1, 0]
    }
  ]
}
```

## Technical Design

### Save API

```c
// Serialize a Scene3D to .vscn JSON file
int64_t rt_scene3d_save(void *scene, rt_string path);
```

The save function walks the scene graph recursively and builds a JSON string using
the existing `rt_json_format_pretty()`. Each node serializes its:
- Name
- Transform (position, rotation as quaternion, scale)
- Mesh reference (filename stored as node metadata)
- Material properties
- Children (recursive)

### Load API

```c
// Load a .vscn file, returning a populated Scene3D
void *rt_scene3d_load(rt_string path);
```

The load function:
1. Read file, call `rt_json_parse_object()`
2. Load referenced mesh files (OBJ/FBX/glTF/STL via auto-detect)
3. Create materials from property definitions, load textures
4. Build node hierarchy with transforms
5. Assign meshes and materials to nodes
6. Create lights and camera (accessible via getters)

### Asset Caching

Meshes and textures referenced by multiple nodes should be loaded once and shared.
Use a simple string → pointer hash map during load:

```c
typedef struct {
    char key[256];
    void *value;
} asset_cache_entry_t;
```

### Path Resolution

All file paths in .vscn are relative to the .vscn file's directory, matching the
convention used by .mtl and .gltf.

## Files to Create

| File | Purpose | Est. LOC |
|------|---------|----------|
| `src/runtime/graphics/rt_scene3d_io.c` | Save/load implementation | ~350 |
| `src/tests/unit/runtime/TestSceneIO.cpp` | Unit tests | ~100 |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/CMakeLists.txt` | Add `graphics/rt_scene3d_io.c` |
| `src/il/runtime/runtime.def` | Add `Scene3DSave`, `Scene3DLoad` entries |
| `src/runtime/graphics/rt_scene3d.c` | May need getter for node metadata (mesh filename, etc.) |
| `src/tests/CMakeLists.txt` | Register test |
| `docs/viperlib/graphics/` | Document .vscn format and save/load API |

## Tests

1. **Save empty scene** — create Scene3D with root only, save to .vscn, verify valid JSON
2. **Round-trip** — create scene with 2 nodes (box + sphere primitives), save, load, verify node_count matches and transforms are preserved within epsilon
3. **Material preservation** — save scene with colored material, load, verify diffuse color matches
4. **Nested hierarchy** — save scene with parent → child → grandchild, load, verify hierarchy structure
5. **Missing asset** — .vscn references non-existent mesh file. Verify graceful handling (node created without mesh).

## Estimated LOC

~400 (save ~150, load ~200, asset cache ~30, tests ~100)
