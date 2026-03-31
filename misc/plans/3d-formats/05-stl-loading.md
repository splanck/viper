# Plan 05: STL Loading (3D Printing / CAD)

## Context

STL is the universal format for 3D printing, CNC machining, and CAD tools. It stores
triangle soup with face normals — no materials, UVs, or animation. Both binary and ASCII
variants exist. Binary STL is one of the simplest 3D formats: a fixed-size header followed
by a flat array of triangles.

## Scope

**In scope:**
- Binary STL loading (most common, compact)
- ASCII STL loading (text-based, larger files)
- Auto-detect binary vs ASCII from file content
- Per-face normals (stored in file) and per-vertex normals (computed)
- Returns a standard `rt_mesh3d` object

**Out of scope:**
- STL export/saving
- Color extensions (VisCAM, Magics — non-standard)
- Multi-solid ASCII STL (rare)

## Binary STL Format

```
Bytes 0-79:    Header (80 bytes, usually ASCII description, ignored)
Bytes 80-83:   Triangle count (uint32, little-endian)
Bytes 84+:     Triangle data (50 bytes each):
  Bytes 0-11:  Normal vector (3 × float32, little-endian)
  Bytes 12-23: Vertex 1 (3 × float32)
  Bytes 24-35: Vertex 2 (3 × float32)
  Bytes 36-47: Vertex 3 (3 × float32)
  Bytes 48-49: Attribute byte count (uint16, usually 0)
```

Total file size = 84 + (triangle_count × 50) bytes.

## ASCII STL Format

```
solid name
  facet normal ni nj nk
    outer loop
      vertex v1x v1y v1z
      vertex v2x v2y v2z
      vertex v3x v3y v3z
    endloop
  endfacet
  ...
endsolid name
```

## Implementation (~150 LOC)

### Auto-Detection

```c
void *rt_mesh3d_from_stl(rt_string path) {
    // Read first 80 bytes. If they start with "solid " AND the file
    // doesn't match the expected binary size, treat as ASCII.
    // Otherwise treat as binary.
    // (Some binary STL files start with "solid" in the header, so
    //  size-check is more reliable than content-check.)

    uint32_t tri_count = read_u32_le(data + 80);
    size_t expected_binary = 84 + (size_t)tri_count * 50;
    if (file_size == expected_binary)
        return load_stl_binary(data, file_size);
    else if (memcmp(data, "solid", 5) == 0)
        return load_stl_ascii(data, file_size);
    else
        return load_stl_binary(data, file_size); // fallback
}
```

### Binary Loader (~50 LOC)

```c
static void *load_stl_binary(const uint8_t *data, size_t len) {
    uint32_t tri_count = read_u32_le(data + 80);
    rt_mesh3d *mesh = rt_mesh3d_new();

    for (uint32_t i = 0; i < tri_count; i++) {
        const uint8_t *tri = data + 84 + i * 50;
        // Skip face normal (bytes 0-11) — we'll compute vertex normals
        float v1[3], v2[3], v3[3];
        memcpy(v1, tri + 12, 12);
        memcpy(v2, tri + 24, 12);
        memcpy(v3, tri + 36, 12);

        int base = mesh->vertex_count;
        rt_mesh3d_add_vertex(mesh, v1, v2, v3, ...); // 3 vertices
        rt_mesh3d_add_triangle(mesh, base, base+1, base+2);
    }

    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}
```

### ASCII Loader (~60 LOC)

Line-by-line parsing: skip `solid`/`endsolid`/`facet`/`outer loop`/`endloop`/`endfacet`,
extract `vertex x y z` lines, accumulate triangles.

### Vertex Welding (Optional)

STL is a triangle soup — adjacent triangles repeat shared vertices. For correct normal
computation, weld vertices that share the same position (within epsilon):

```c
// After loading, merge vertices within 1e-6 distance
// This produces smooth normals at shared edges
static void weld_vertices(rt_mesh3d *mesh, float epsilon);
```

This is optional but significantly improves visual quality. ~40 LOC with a simple O(n²)
search (acceptable for typical STL sizes).

## Files to Create

| File | Purpose |
|------|---------|
| `src/tests/unit/runtime/TestStlLoad.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_mesh3d.c` | Add `rt_mesh3d_from_stl()` (~150 LOC) |
| `src/runtime/graphics/rt_pixels.h` or `rt_mesh3d.h` | Declare new function |
| `src/il/runtime/runtime.def` | Add `Mesh3DFromSTL` entry |
| `src/tests/CMakeLists.txt` | Register test |
| `docs/viperlib/graphics/` | Document STL support |

## Tests

1. **Synthetic binary STL** — construct minimal 1-triangle binary STL in test code (84 + 50 = 134 bytes). Verify vertex_count==3, index_count==3.
2. **Synthetic ASCII STL** — construct text STL in test code. Verify same results.
3. **Reject garbage** — non-STL file → NULL return.
4. **Reject empty** — valid header but tri_count==0. Verify empty mesh or NULL.
5. **Normal computation** — 2-triangle STL forming a flat quad. Verify normals are (0, 1, 0) for XZ-plane triangles.

## Estimated LOC

~150 (binary loader ~50, ASCII loader ~60, auto-detect ~15, API ~10, vertex weld ~40 optional)
