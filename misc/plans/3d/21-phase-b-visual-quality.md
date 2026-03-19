# Phase B: Visual Quality

## Goal

Add shadow mapping, distance fog, and debug gizmos — making 3D scenes look professional with proper light occlusion and atmospheric depth.

## Dependencies

- Phase 8 complete (render-to-texture: `vgfx3d_rendertarget_t` with color_buf + depth_buf)
- Backend shader system (Metal MSL at `vgfx3d_backend_metal.m:75-184`, GLSL, HLSL)
- Software rasterizer pipeline (`vgfx3d_backend_sw.c:300-434`, `raster_triangle` function)
- Canvas3D debug drawing (`rt_canvas3d.c:701-756`, `world_to_screen` + `draw_line_px`)

---

## B1. Shadow Mapping (~400 LOC)

### Architecture

Shadow mapping reuses the EXISTING render-to-texture infrastructure. No new backend vtable entries needed — shadow pass uses the same `set_render_target` → `begin_frame` → `submit_draw` → `end_frame` flow with a light-space camera.

```
Per frame (for first directional light with shadows enabled):
  1. Build light-space ortho VP matrix
  2. set_render_target(shadow_rt)  — depth-only render target
  3. begin_frame(light_camera)     — render from light's perspective
  4. submit_draw(each mesh)        — depth-only (no color output)
  5. end_frame()                   — shadow map now in shadow_rt->depth_buf
  6. set_render_target(NULL)       — back to screen
  7. Main render pass: sample shadow_rt->depth_buf in fragment shader
```

### Canvas3D Struct Additions (`rt_canvas3d_internal.h`)

Add after the `fog_color[3]` fields:
```c
/* Shadow mapping */
int8_t shadows_enabled;
int32_t shadow_resolution;      /* shadow map size (default 1024) */
float shadow_bias;              /* depth bias (default 0.005) */
vgfx3d_rendertarget_t *shadow_rt; /* depth-only render target (internal) */
float shadow_light_vp[16];     /* light-space VP matrix (row-major) */
```

### Light-Space Matrix Construction

For directional light, build orthographic projection from light direction:
```c
static void build_shadow_light_vp(const rt_canvas3d *c, float *out_vp) {
    /* Find the first directional light */
    rt_light3d *light = NULL;
    for (int i = 0; i < VGFX3D_MAX_LIGHTS; i++)
        if (c->lights[i] && c->lights[i]->type == 0) { light = c->lights[i]; break; }
    if (!light) return;

    /* Light direction (already normalized) */
    float lx = (float)light->direction[0];
    float ly = (float)light->direction[1];
    float lz = (float)light->direction[2];

    /* Light "position" = scene center - light_dir * distance */
    float scene_center[3] = {0, 0, 0};  /* TODO: compute from visible objects */
    float extent = 20.0f;  /* ortho frustum half-extent */
    float light_pos[3] = {
        scene_center[0] - lx * extent,
        scene_center[1] - ly * extent,
        scene_center[2] - lz * extent
    };

    /* Build lookAt from light position toward scene center */
    float view[16];
    /* ... build_look_at(view, light_pos, scene_center, up) ... */

    /* Orthographic projection: -extent to +extent on each axis */
    float proj[16];
    /* ortho(-extent, extent, -extent, extent, 0.1, extent * 4) */

    /* VP = proj * view */
    mat4f_mul(proj, view, out_vp);
}
```

### Software Rasterizer Shadow Sampling

In `raster_triangle()` after lighting computation (line ~405 in `vgfx3d_backend_sw.c`):
```c
/* Shadow test: transform fragment to light space, compare depth */
if (sw_ctx->shadow_rt) {
    float lp[4]; /* light-space clip position */
    /* lp = shadow_light_vp * world_pos */
    float lu = (lp[0]/lp[3] + 1.0f) * 0.5f;  /* UV [0,1] */
    float lv = (1.0f - lp[1]/lp[3]) * 0.5f;
    float ld = lp[2]/lp[3] * 0.5f + 0.5f;     /* depth [0,1] */

    int sx = (int)(lu * shadow_w), sy = (int)(lv * shadow_h);
    if (sx >= 0 && sx < shadow_w && sy >= 0 && sy < shadow_h) {
        float shadow_depth = sw_ctx->shadow_rt->depth_buf[sy * shadow_w + sx];
        if (ld > shadow_depth + sw_ctx->shadow_bias) {
            /* In shadow: darken diffuse/specular (keep ambient) */
            float shadow_factor = 0.3f; /* 30% of lit intensity */
            fr *= shadow_factor;
            fg *= shadow_factor;
            fb_c *= shadow_factor;
        }
    }
}
```

### Metal Shader Changes

**New uniform buffer (buffer index 3):**
```metal
struct ShadowParams {
    float4x4 lightVP;
    float bias;
    float shadowMapSize;
    float2 _pad;
};
```

**Vertex shader addition:**
```metal
out.lightSpacePos = shadowParams.lightVP * wp;
```

**Fragment shader addition:**
```metal
// Shadow sampling with 3x3 PCF
depth2d<float> shadowMap [[texture(1)]];
constexpr sampler shadowSampler(filter::linear, compare_func::less);

float2 shadowUV = in.lightSpacePos.xy / in.lightSpacePos.w * 0.5 + 0.5;
float currentDepth = in.lightSpacePos.z / in.lightSpacePos.w * 0.5 + 0.5;
float shadow = 0.0;
for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
        float2 offset = float2(dx, dy) / shadowParams.shadowMapSize;
        shadow += shadowMap.sample_compare(shadowSampler, shadowUV + offset, currentDepth - shadowParams.bias);
    }
}
shadow /= 9.0;
result *= max(shadow, 0.3);  // 30% ambient minimum
```

### Public API

```c
void rt_canvas3d_enable_shadows(void *canvas, int64_t resolution);
void rt_canvas3d_disable_shadows(void *canvas);
void rt_canvas3d_set_shadow_bias(void *canvas, double bias);
```

### runtime.def: 3 RT_FUNC, add to Canvas3D class
### Stubs: 3 functions (no-op)

---

## B2. Fog / Distance Fade (~100 LOC)

### Canvas3D Struct Additions (`rt_canvas3d_internal.h`)

Add after `skybox` field:
```c
/* Distance fog */
int8_t fog_enabled;
float fog_near;         /* distance where fog starts */
float fog_far;          /* distance where fog is fully opaque */
float fog_color[3];     /* RGB [0,1] */
```

### Init in `rt_canvas3d_new` (`rt_canvas3d.c`):
```c
c->fog_enabled = 0;
c->fog_near = 10.0f;
c->fog_far = 50.0f;
c->fog_color[0] = c->fog_color[1] = c->fog_color[2] = 0.5f;
```

### Software Rasterizer Integration

The `raster_triangle` function in `vgfx3d_backend_sw.c` needs the camera position for distance computation. Currently `sw_context_t` has `cam_pos[3]` (set in `sw_begin_frame`). Add fog params to context:

```c
/* In sw_context_t: */
int8_t fog_enabled;
float fog_near, fog_far;
float fog_color[3];
```

Set in `sw_begin_frame` from Canvas3D state. Apply in `raster_triangle` after lighting, before pixel write (line ~405):

```c
if (ctx->fog_enabled) {
    /* Camera-space distance (cheaper than world distance) */
    float dx = world_x - ctx->cam_pos[0];
    float dy = world_y - ctx->cam_pos[1];
    float dz = world_z - ctx->cam_pos[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    float fog_f = (dist - ctx->fog_near) / (ctx->fog_far - ctx->fog_near);
    fog_f = fog_f < 0.0f ? 0.0f : (fog_f > 1.0f ? 1.0f : fog_f);
    fr = fr * (1.0f - fog_f) + ctx->fog_color[0] * fog_f;
    fg = fg * (1.0f - fog_f) + ctx->fog_color[1] * fog_f;
    fb_c = fb_c * (1.0f - fog_f) + ctx->fog_color[2] * fog_f;
}
```

**Note:** `raster_triangle` currently interpolates world position via barycentric coordinates. The world position is available from the per-vertex `world[3]` in `pipe_vert_t`. Need to interpolate it in the inner loop (add `b0*v0->world[i] + b1*v1->world[i] + b2*v2->world[i]`).

### Metal Shader Integration

Expand `mtl_per_scene_t` (currently 36 bytes, has 12 bytes padding):

```c
typedef struct {
    float cp[4];      /* camera position */
    float ac[4];      /* ambient color */
    int32_t lc;       /* light count */
    int32_t fog_on;   /* fog enabled (replaces _p[0]) */
    float fog_near;   /* replaces _p[1] */
    float fog_far;    /* replaces _p[2] */
    float fog_color[4]; /* RGB + pad (16 bytes, new allocation) */
} mtl_per_scene_t;  /* 52 bytes (was 36) */
```

Metal fragment shader:
```metal
if (scene.fog_on != 0) {
    float dist = length(in.worldPos - scene.cameraPosition.xyz);
    float fogFactor = clamp((dist - scene.fog_near) / (scene.fog_far - scene.fog_near), 0.0, 1.0);
    result = mix(result, scene.fog_color.rgb, fogFactor);
}
```

### D3D11/OpenGL: add same fog uniform to cbuffer/uniform block.

### Public API

```c
void rt_canvas3d_set_fog(void *canvas, double near, double far,
                          double r, double g, double b);
void rt_canvas3d_clear_fog(void *canvas);
```

### runtime.def: 2 RT_FUNC
### Stubs: 2 functions (no-op)

---

## B3. Debug Gizmos (~100 LOC)

### Modified File: `src/runtime/graphics/rt_canvas3d.c`

All gizmo functions use the existing `world_to_screen()` (line 701) and `draw_line_px()` (line 718) infrastructure. They write to the SOFTWARE FRAMEBUFFER.

**Note on Metal visibility:** On GPU backends (Metal), the software framebuffer is behind the Metal layer. Gizmos will be visible ONLY when:
1. Using the software backend, OR
2. The Metal layer is transparent at the gizmo pixel locations (alpha=0 clear), OR
3. Gizmos are drawn as 3D geometry via DrawLine3D (which uses world_to_screen + pixel write to software framebuffer)

For GPU-visible gizmos in the future, render them as actual 3D line meshes through the Metal pipeline. For now, software framebuffer gizmos are useful for development/debugging with the software backend.

### Implementations

```c
void rt_canvas3d_draw_aabb_wire(void *obj, void *min_v, void *max_v, int64_t color) {
    /* 8 corners, 12 edges */
    float corners[8][3];
    float mn[3] = {(float)rt_vec3_x(min_v), ...};
    float mx[3] = {(float)rt_vec3_x(max_v), ...};

    /* Generate 8 corners from min/max combinations */
    for (int i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? mx[0] : mn[0];
        corners[i][1] = (i & 2) ? mx[1] : mn[1];
        corners[i][2] = (i & 4) ? mx[2] : mn[2];
    }

    /* 12 edges: bottom(0-1,1-3,3-2,2-0), top(4-5,5-7,7-6,6-4), vertical(0-4,1-5,2-6,3-7) */
    static const int edges[12][2] = {
        {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4}, {0,4},{1,5},{2,6},{3,7}
    };
    for (int e = 0; e < 12; e++)
        rt_canvas3d_draw_line3d(obj, vec3_from_float(corners[edges[e][0]]),
                                      vec3_from_float(corners[edges[e][1]]), color);
}

void rt_canvas3d_draw_sphere_wire(void *obj, void *center, double radius, int64_t color) {
    /* 3 circles (XY, XZ, YZ) with 24 segments each */
    float cx = (float)rt_vec3_x(center), cy = (float)rt_vec3_y(center), cz = (float)rt_vec3_z(center);
    float r = (float)radius;
    int segs = 24;
    for (int axis = 0; axis < 3; axis++) {
        for (int i = 0; i < segs; i++) {
            float a0 = (float)i * 2.0f * 3.14159f / segs;
            float a1 = (float)(i+1) * 2.0f * 3.14159f / segs;
            float p0[3], p1[3];
            /* Compute circle points on the selected plane */
            /* axis 0 (XY): z=cz, x=cx+cos*r, y=cy+sin*r */
            /* axis 1 (XZ): y=cy, x=cx+cos*r, z=cz+sin*r */
            /* axis 2 (YZ): x=cx, y=cy+cos*r, z=cz+sin*r */
            rt_canvas3d_draw_line3d(obj, vec3_new(p0), vec3_new(p1), color);
        }
    }
}

void rt_canvas3d_draw_debug_ray(void *obj, void *origin, void *dir,
                                  double length, int64_t color) {
    /* origin → origin + dir * length */
    double ex = rt_vec3_x(origin) + rt_vec3_x(dir) * length;
    double ey = rt_vec3_y(origin) + rt_vec3_y(dir) * length;
    double ez = rt_vec3_z(origin) + rt_vec3_z(dir) * length;
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ex, ey, ez), color);
}

void rt_canvas3d_draw_axis(void *obj, void *origin, double scale) {
    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox+scale, oy, oz), 0xFF0000); /* X=red */
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy+scale, oz), 0x00FF00); /* Y=green */
    rt_canvas3d_draw_line3d(obj, origin, rt_vec3_new(ox, oy, oz+scale), 0x0000FF); /* Z=blue */
}
```

### Public API

```c
void rt_canvas3d_draw_aabb_wire(void *canvas, void *min, void *max, int64_t color);
void rt_canvas3d_draw_sphere_wire(void *canvas, void *center, double radius, int64_t color);
void rt_canvas3d_draw_debug_ray(void *canvas, void *origin, void *dir, double length, int64_t color);
void rt_canvas3d_draw_axis(void *canvas, void *origin, double scale);
```

### runtime.def: 4 RT_FUNC added to Canvas3D class
### Stubs: 4 functions (no-op)

---

## Files Modified/Created Summary

| Action | File | Est. LOC |
|--------|------|----------|
| MOD | `src/runtime/graphics/rt_canvas3d_internal.h` | +15 (fog + shadow fields) |
| MOD | `src/runtime/graphics/rt_canvas3d.c` | +150 (gizmos + fog/shadow API + init) |
| MOD | `src/runtime/graphics/rt_canvas3d.h` | +12 (declarations) |
| MOD | `src/runtime/graphics/vgfx3d_backend_sw.c` | +60 (fog + shadow in raster) |
| MOD | `src/runtime/graphics/vgfx3d_backend_metal.m` | +80 (shader changes + uniform) |
| MOD | `src/runtime/graphics/vgfx3d_backend_d3d11.c` | +40 (shader fog) |
| MOD | `src/runtime/graphics/vgfx3d_backend_opengl.c` | +40 (shader fog) |
| MOD | `src/runtime/graphics/rt_graphics_stubs.c` | +10 |
| MOD | `src/il/runtime/runtime.def` | +9 entries |

---

## Tests

### Shadow Tests (5)
- Shadow map created at requested resolution
- enable/disable toggle
- Shadow bias configurable
- No crash with no directional light
- Shadow RT freed on canvas destroy

### Fog Tests (4)
- Near distance: no fog applied
- Far distance: full fog color
- Mid distance: correctly blended
- Clear fog: disabled

### Gizmo Tests (3)
- AABB wireframe draws 12 line segments
- Sphere wireframe draws 3 circles × 24 segments
- Axis draws 3 colored lines

## Verification

1. Build clean (zero warnings)
2. 1334+ ctest pass (new add ~12)
3. Demo: scene with shadows from directional light
4. Demo: foggy corridor (increasing density)
5. Demo: AABB/sphere gizmos around collision volumes
6. All 3 GPU backends + software backend support fog
7. Native compilation verified
