# Phase F: Polish + Optimization

## Goal

Advanced rendering features and performance optimizations for production-quality games. Each feature is independent and can be implemented in any order.

## Dependencies

- Phases A-E complete (or at minimum Phase B for shader infrastructure)
- Backend shader system operational (Metal MSL, D3D11 HLSL, OpenGL GLSL)
- Scene graph with frustum culling (Phase 13)

---

## F1. LOD System (~150 LOC)

### Modified File: `src/runtime/graphics/rt_scene3d.c`

### Struct Addition to `rt_scene_node3d`:
```c
/* LOD levels: sorted by distance (ascending) */
typedef struct { double distance; void *mesh; } lod_entry_t;
lod_entry_t *lod_levels;   /* dynamic array (NULL = no LOD) */
int32_t lod_count;
int32_t lod_capacity;
```

### Integration in `draw_node` (before DrawMesh):
```c
/* LOD selection: find mesh with highest distance <= camera_dist */
if (node->lod_count > 0) {
    float dx = node->world_matrix[3] - cam_pos[0];
    float dy = node->world_matrix[7] - cam_pos[1];
    float dz = node->world_matrix[11] - cam_pos[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    void *selected_mesh = node->mesh; /* default: base mesh */
    for (int32_t l = node->lod_count - 1; l >= 0; l--) {
        if (dist >= node->lod_levels[l].distance) {
            selected_mesh = node->lod_levels[l].mesh;
            break;
        }
    }
    /* Use selected_mesh instead of node->mesh for this frame */
}
```

### API:
```c
void rt_scene_node3d_add_lod(void *node, double distance, void *mesh);
void rt_scene_node3d_clear_lod(void *node);
```

### runtime.def: 2 RT_FUNC
### Tests: LOD switches at correct distance, base mesh used when no LOD set

---

## F2. Decals (~200 LOC)

### New Files: `rt_decal3d.h` + `rt_decal3d.c`

### Data Structure:
```c
typedef struct {
    void *vptr;
    double position[3];     /* world position of decal center */
    double normal[3];       /* surface normal (decal orientation) */
    double size;            /* half-extent of the decal quad */
    void *texture;          /* Pixels object (borrowed) */
    double lifetime;        /* remaining lifetime in seconds (-1 = permanent) */
    double alpha;           /* current opacity [0,1] */
} rt_decal3d;
```

### Rendering:
Decals are rendered as camera-facing quads (like particles but oriented to surface normal):
```c
void rt_canvas3d_draw_decal(void *canvas, void *decal) {
    /* Build quad vertices at position, oriented along normal */
    /* Offset slightly from surface (position + normal * 0.01) to prevent z-fighting */
    /* Use Material3D with alpha blending + the decal texture */
    /* Register quad vertices with temp_buffers for deferred draw */
}
```

### Lifecycle:
```c
void rt_decal3d_update(void *decal, double dt);  /* reduce lifetime, fade alpha */
int8_t rt_decal3d_is_expired(void *decal);       /* lifetime <= 0 */
```

### API:
```c
void   *rt_decal3d_new(void *position, void *normal, double size, void *texture);
void    rt_decal3d_set_lifetime(void *decal, double seconds);
void    rt_decal3d_update(void *decal, double dt);
int8_t  rt_decal3d_is_expired(void *decal);
void    rt_canvas3d_draw_decal(void *canvas, void *decal);
```

### runtime.def: 5 RT_FUNC + 1 RT_CLASS
### Tests: Decal at surface, lifetime expiry, alpha fade

---

## F3. Occlusion Culling (~400 LOC)

### Architecture

**Software path (hierarchical z-buffer):**
After rendering opaque objects, the z-buffer contains depth values for all visible pixels. For remaining objects, test their screen-space AABB against the z-buffer:

```c
int is_occluded(const float *zbuf, int32_t w, int32_t h,
                 int x0, int y0, int x1, int y1, float min_depth) {
    /* Sample z-buffer at AABB corners and center */
    /* If ALL sample depths < min_depth → object is behind existing geometry → skip */
    /* Conservative: use 5 samples (4 corners + center) for speed */
    for each sample (sx, sy):
        if (zbuf[sy * w + sx] >= min_depth) return 0; /* visible */
    return 1; /* occluded */
}
```

**GPU path (Metal/D3D11/OpenGL):** Use hardware occlusion queries:
- Metal: `MTLVisibilityResultBuffer` + `setVisibilityResultMode`
- D3D11: `ID3D11Query` with `D3D11_QUERY_OCCLUSION`
- OpenGL: `glGenQueries` + `GL_SAMPLES_PASSED`

### Integration in Scene3D draw:
1. Sort opaque objects front-to-back (by distance from camera)
2. Render front-to-back (fills z-buffer early)
3. For each subsequent object: test screen-space AABB against z-buffer
4. Skip if fully occluded

### API:
```c
void rt_canvas3d_set_occlusion_culling(void *canvas, int8_t enabled);
```

### runtime.def: 1 RT_FUNC
### Tests: Object behind wall not drawn, draw call count decreases

---

## F4. Texture Atlasing (~300 LOC)

### New Files: `rt_texatlas3d.h` + `rt_texatlas3d.c`

### Bin Packing Algorithm: Shelf Next-Fit
Simple row-based packing:
```c
typedef struct {
    int32_t x, y;        /* top-left position in atlas */
    int32_t w, h;        /* dimensions */
} atlas_region_t;

typedef struct {
    void *vptr;
    uint32_t *data;      /* atlas pixel data (0xRRGGBBAA) */
    int32_t width, height;
    atlas_region_t *regions;
    int32_t region_count, region_capacity;
    int32_t shelf_x, shelf_y, shelf_h; /* current shelf position */
} rt_texatlas3d;
```

Packing:
```c
int64_t rt_texatlas3d_add(void *atlas, void *pixels) {
    /* If texture fits on current shelf: place it */
    /* If not: start new shelf row (shelf_y += shelf_h, shelf_x = 0) */
    /* Copy pixel data into atlas at (region.x, region.y) */
    /* Add 1-pixel border padding to prevent texture bleeding */
    /* Return region index */
}
```

UV Remapping:
```c
/* Original UV [0,1] → atlas UV: */
float atlas_u = (region.x + original_u * region.w) / atlas_width;
float atlas_v = (region.y + original_v * region.h) / atlas_height;
```

### API:
```c
void   *rt_texatlas3d_new(int64_t width, int64_t height);
int64_t rt_texatlas3d_add(void *atlas, void *pixels);
void   *rt_texatlas3d_get_texture(void *atlas);      /* combined Pixels */
void    rt_texatlas3d_get_uv_rect(void *atlas, int64_t id,
                                    double *u0, double *v0, double *u1, double *v1);
```

### runtime.def: 4 RT_FUNC + 1 RT_CLASS
### Tests: Pack 3 textures, verify UV rects, verify combined pixels

---

## F5. SSAO (Screen-Space Ambient Occlusion) (~300 LOC)

### PostFX3D Addition

Added as a new effect type in the post-processing chain.

### Algorithm: Hemisphere Sampling
```c
/* For each pixel: */
1. Read depth and reconstruct world position from depth buffer
2. Read normal (from G-buffer or estimated from depth neighbors)
3. Sample N=16 random points in hemisphere around normal:
   - Use Hammersley sequence for well-distributed samples
   - For each sample: offset position, project to screen, compare depth
   - If sample is closer to camera than depth at that pixel → occluded
4. Occlusion factor = occluded_count / N
5. Blur result (4x4 bilateral blur to remove noise)
6. Multiply scene color by (1.0 - occlusion * intensity)
```

### Sampling Pattern (Hammersley, deterministic):
```c
static void hammersley_hemisphere(int i, int N, float *out_x, float *out_y, float *out_z) {
    float u = (float)i / (float)N;
    float v = radical_inverse_vdc(i); /* van der Corput sequence */
    float phi = 2.0f * M_PI * u;
    float cos_theta = 1.0f - v;
    float sin_theta = sqrtf(1.0f - cos_theta * cos_theta);
    *out_x = sin_theta * cosf(phi);
    *out_y = sin_theta * sinf(phi);
    *out_z = cos_theta; /* hemisphere: z always positive */
}
```

### API:
```c
void rt_postfx3d_add_ssao(void *postfx, double radius, double intensity, int64_t samples);
```

### runtime.def: 1 RT_FUNC (added to PostFX3D class methods)
### Tests: Flat surface → no occlusion, corner → darkened, intensity scales effect

---

## F6. Depth of Field (~200 LOC)

### PostFX3D Addition

### Algorithm: Variable-Radius Gaussian Blur
```c
/* For each pixel: */
1. Read depth at pixel
2. Compute Circle of Confusion (CoC):
   CoC = abs(depth - focus_distance) * aperture / depth
   CoC = clamp(CoC, 0, max_blur)
3. Blur radius = CoC (in pixels)
4. Two-pass separable Gaussian blur with variable radius:
   - Horizontal pass: sample ±CoC pixels, weighted by Gaussian kernel
   - Vertical pass: same
5. Blend: sharp image where CoC ≈ 0, blurred where CoC > 0
```

### API:
```c
void rt_postfx3d_add_dof(void *postfx, double focus_distance,
                           double aperture, double max_blur);
```

### runtime.def: 1 RT_FUNC
### Tests: In-focus object sharp, background blurred, focus distance shifts correctly

---

## F7. Motion Blur (~200 LOC)

### PostFX3D Addition

### Algorithm: Per-Pixel Velocity Blur
```c
/* Requires: current VP matrix + previous frame's VP matrix */
/* For each pixel: */
1. Read depth, reconstruct world position
2. Compute current clip position: cur_clip = current_VP * world_pos
3. Compute previous clip position: prev_clip = prev_VP * world_pos
4. Velocity = (cur_clip.xy - prev_clip.xy) * 0.5 (in screen space)
5. Sample N=8 pixels along velocity direction
6. Average samples → motion-blurred color
```

### State:
Canvas3D needs to store previous frame's VP matrix:
```c
float prev_vp[16]; /* cached from last frame */
```
Updated in `rt_canvas3d_flip()`: copy `cached_vp` to `prev_vp`.

### API:
```c
void rt_postfx3d_add_motion_blur(void *postfx, double intensity, int64_t samples);
```

### runtime.def: 1 RT_FUNC
### Tests: Static scene → no blur, moving camera → streak artifacts

---

## F8. Water Rendering (~400 LOC)

### New Files: `rt_water3d.h` + `rt_water3d.c`

### Architecture:
1. **Reflection pass**: Mirror camera across water plane (Y-flip eye position, negate pitch), render to RTT
2. **Wave animation**: Sine-based vertex displacement on water quad
3. **Compositing**: Draw water quad with reflection texture + Fresnel blending

### Wave Formula (Gerstner approximation):
```c
float wave_y = amplitude * sin(frequency * (x + z) + time * speed);
float wave_dx = amplitude * frequency * cos(frequency * (x + z) + time * speed);
```

### Reflection Camera:
```c
/* Mirror eye across water plane at Y = water_height */
double reflected_eye[3] = {eye[0], 2.0 * water_height - eye[1], eye[2]};
/* Flip pitch */
double reflected_pitch = -pitch;
```

### API:
```c
void   *rt_water3d_new(double width, double depth);
void    rt_water3d_set_height(void *water, double y);
void    rt_water3d_set_wave_params(void *water, double speed, double amplitude, double frequency);
void    rt_water3d_set_color(void *water, double r, double g, double b, double alpha);
void    rt_water3d_update(void *water, double dt);
void    rt_canvas3d_draw_water(void *canvas, void *water, void *camera);
```

### runtime.def: 6 RT_FUNC + 1 RT_CLASS
### Tests: Water height query, wave displacement, reflection texture generated

---

## F9. Sprite3D (~200 LOC)

### New Files: `rt_sprite3d.h` + `rt_sprite3d.c`

### Architecture:
Camera-facing quad (same billboard math as Particles3D) with sprite sheet animation support.

### Data Structure:
```c
typedef struct {
    void *vptr;
    void *texture;            /* Pixels object (borrowed) */
    double position[3];       /* world position */
    double scale[2];          /* width, height in world units */
    double anchor[2];         /* pivot point [0,1], default (0.5, 0.5) = center */
    int32_t frame_x, frame_y; /* sprite sheet frame position (pixels) */
    int32_t frame_w, frame_h; /* sprite sheet frame size (pixels) */
    int32_t tex_w, tex_h;     /* total texture size (for UV computation) */
} rt_sprite3d;
```

### Billboard Rendering:
Reuse camera right/up extraction from `rt_particles3d.c:418-420`:
```c
/* From camera view matrix (row-major): */
float right[3] = {(float)cam->view[0], (float)cam->view[1], (float)cam->view[2]};
float up[3] = {(float)cam->view[4], (float)cam->view[5], (float)cam->view[6]};

/* Build 4 quad vertices: center ± right * half_w ± up * half_h */
/* UV from frame rect: u = (frame_x + local_u * frame_w) / tex_w */
```

### API:
```c
void   *rt_sprite3d_new(void *texture);
void    rt_sprite3d_set_position(void *spr, double x, double y, double z);
void    rt_sprite3d_set_scale(void *spr, double w, double h);
void    rt_sprite3d_set_anchor(void *spr, double ax, double ay);
void    rt_sprite3d_set_frame(void *spr, int64_t fx, int64_t fy, int64_t fw, int64_t fh);
void    rt_canvas3d_draw_sprite3d(void *canvas, void *sprite, void *camera);
```

### runtime.def: 6 RT_FUNC + 1 RT_CLASS
### Tests: Billboard faces camera, frame UV correct, anchor offset correct

---

## Priority Recommendations

| Feature | Impact | Effort | Best For |
|---------|--------|--------|----------|
| **F1 LOD** | High | Low (150 LOC) | Open-world, large scenes |
| **F2 Decals** | Medium | Low (200 LOC) | Shooters, action games |
| **F9 Sprite3D** | Medium | Low (200 LOC) | RPGs, retro-style |
| **F5 SSAO** | Medium | Medium (300 LOC) | Visual quality |
| **F4 Atlasing** | Medium | Medium (300 LOC) | Many-texture scenes |
| **F8 Water** | Medium | Medium (400 LOC) | Outdoor scenes |
| **F3 Occlusion** | High | High (400 LOC) | Complex indoor scenes |
| **F6 DoF** | Low | Medium (200 LOC) | Cinematics |
| **F7 Motion Blur** | Low | Medium (200 LOC) | Racing, action |

---

## Implementation Notes

- Each feature follows the Viper pattern: C impl → runtime.def → stubs → tests → docs
- All shader changes must be applied to Metal MSL, D3D11 HLSL, and OpenGL GLSL
- Software rasterizer implementations are always required
- PostFX effects add to existing `rt_postfx3d.c` chain infrastructure
- All features must be cross-platform: macOS + Windows + Linux
- No new compile-time feature flags — everything under `VIPER_ENABLE_GRAPHICS`
