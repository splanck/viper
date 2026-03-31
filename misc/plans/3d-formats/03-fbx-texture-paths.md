# Plan 03: FBX Texture Path Extraction

## Context

The FBX loader extracts materials with diffuse color and shininess (`rt_fbx_loader.c:781-809`)
but ignores texture references. FBX files contain `Texture` nodes that link to image files
via `RelativeFilename` or `Filename` properties. Without this, FBX models load as flat-
colored meshes even when the source file has full textures.

## Scope

**In scope:**
- Parse `Texture` nodes from the FBX node tree
- Extract `RelativeFilename` (preferred) or `Filename` properties
- Load textures via `rt_pixels_load()` (auto-detect format)
- Assign textures to materials via `Connections` table mapping
- Support diffuse, normal, specular, and emissive texture slots

**Out of scope:**
- Embedded texture data (FBX can embed images as binary blobs — rare in practice)
- Texture transform (UV offset/scale/rotation from FBX)
- Multi-layer textures

## Technical Design

### FBX Texture Node Structure

In FBX binary format, textures appear as:
```
Objects:
  Texture: <id>, "TextureName\x00\x01Texture", "" {
    Properties70: {
      P: "RelativeFilename", "KString", "", "", "textures/diffuse.png"
      P: "Filename", "KString", "", "", "C:/project/textures/diffuse.png"
    }
  }

Connections:
  C: "OO", <texture_id>, <material_id>  // texture → material
```

The connection type determines which material slot:
- `DiffuseColor` → diffuse texture (slot 0)
- `NormalMap` / `Bump` → normal map (slot 1)
- `SpecularColor` → specular map (slot 2)
- `EmissiveColor` → emissive map (slot 3)

### Implementation (~100 LOC)

Add a texture extraction pass after material extraction in `rt_fbx_loader.c`:

```c
// 1. Collect all Texture nodes and their filenames
typedef struct {
    int64_t id;
    char filename[512];
} fbx_texture_ref_t;

// 2. For each Texture node in Objects:
//    - Read RelativeFilename (prefer) or Filename from Properties70
//    - Store in texture_refs[] array

// 3. For each Connection linking texture → material:
//    - Find the texture_ref by source ID
//    - Find the material by destination ID
//    - Determine slot from connection property name
//    - Resolve path relative to FBX file directory
//    - Load via rt_pixels_load()
//    - Assign to material via rt_material3d_set_texture/normal_map/etc.
```

### Path Resolution

FBX `RelativeFilename` is relative to the FBX file itself:
```c
// Given: fbx_path = "/assets/models/character.fbx"
//        RelativeFilename = "textures/body.png"
// Resolve to: "/assets/models/textures/body.png"

static void resolve_texture_path(const char *fbx_dir, const char *rel_filename,
                                  char *out, size_t out_size);
```

Handle both forward and back slashes (FBX from Windows may use `\`).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_fbx_loader.c` | Add texture extraction pass (~100 LOC) |

## Tests

1. **FBX without textures** — existing behavior unchanged (regression)
2. **FBX with texture node** — verify material's texture field is non-NULL after load (requires a real FBX with texture reference; construct synthetically or use a minimal test FBX)
3. **Missing texture file** — FBX references texture that doesn't exist. Verify material loads without texture (graceful NULL).

## Estimated LOC

~100 (texture node traversal ~40, connection mapping ~30, path resolution ~30)
