# Phase 18: Post-Processing Effects

## Goal

Full-screen post-processing pipeline with bloom, tone mapping, FXAA, color grading, and vignette. The scene renders to an offscreen target (Phase 8), effects are applied as a chain of full-screen passes, and the final result is blitted to the window.

## Dependencies

- Phase 8 complete (render-to-texture — offscreen targets for ping-pong rendering)
- Phase 10 complete (alpha blending — used for compositing)

## Architecture

```
Scene → [RenderTarget: scene_target]
  ↓
Effect Chain (each reads previous, writes to next):
  [scene_target] → Bloom → [ping]
  [ping]         → ToneMap → [pong]
  [pong]         → FXAA → [ping]
  [ping]         → ColorGrade → [pong]
  [pong]         → Vignette → [window framebuffer]

Each effect = full-screen quad textured with previous pass output
```

## New Files

#### Runtime Level (`src/runtime/graphics/`)

**`rt_postfx3d.h`** (~40 LOC)
**`rt_postfx3d.c`** (~300 LOC) — Effect chain management, Canvas3D integration

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d_postfx.c`** (~400 LOC) — Software post-processing (per-pixel operations)

**`vgfx3d_postfx_shaders.metal`** (~200 LOC) — Metal fragment shaders for each effect

**`vgfx3d_postfx_shaders.hlsl`** (~200 LOC) — HLSL pixel shaders for each effect

**`vgfx3d_postfx_shaders.glsl`** (~200 LOC) — GLSL fragment shaders for each effect

## Data Structures

```c
typedef enum {
    POSTFX_BLOOM = 0,
    POSTFX_TONEMAP,
    POSTFX_FXAA,
    POSTFX_COLOR_GRADE,
    POSTFX_VIGNETTE,
} vgfx3d_postfx_type_t;

typedef struct {
    vgfx3d_postfx_type_t type;
    int8_t enabled;
    union {
        struct {
            float threshold;     // brightness threshold (default 1.0)
            float intensity;     // bloom strength (default 0.5)
            int32_t blur_passes; // Gaussian blur iterations (default 5)
        } bloom;
        struct {
            int32_t mode;        // 0 = Reinhard, 1 = ACES filmic
            float exposure;      // exposure multiplier (default 1.0)
        } tonemap;
        struct {
            float edge_threshold;    // edge detection sensitivity (default 0.166)
            float min_threshold;     // minimum edge threshold (default 0.0833)
        } fxaa;
        struct {
            float brightness;    // additive (-1 to 1, default 0)
            float contrast;      // multiplicative (0 to 3, default 1)
            float saturation;    // 0 = grayscale, 1 = normal, 2 = super-saturated
            float tint[3];       // RGB color multiplier (default 1,1,1)
        } color_grade;
        struct {
            float radius;        // vignette inner radius (0 to 1, default 0.5)
            float softness;      // falloff softness (0 to 1, default 0.5)
        } vignette;
    } params;
} vgfx3d_postfx_t;

typedef struct {
    void *vptr;
    vgfx3d_postfx_t effects[8];  // max 8 effects in chain
    int32_t effect_count;
    int8_t enabled;               // master enable (default true)

    // Internal render targets (created on first use, sized to match canvas)
    vgfx3d_rendertarget_t *scene_target;
    vgfx3d_rendertarget_t *ping;
    vgfx3d_rendertarget_t *pong;
    vgfx3d_rendertarget_t *bloom_half;  // half-resolution for bloom blur
    int32_t rt_width, rt_height;
} rt_postfx3d;
```

## Effect Implementations

### Bloom

Three-pass algorithm:

1. **Bright pass (extract):** For each pixel, if `luminance > threshold`, output the pixel; otherwise output black. Render to `bloom_half` (half resolution for performance).

```
float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
if (lum > threshold)
    out = color * (lum - threshold) / lum;
else
    out = float4(0);
```

2. **Gaussian blur (separable):** Run N iterations of horizontal then vertical blur on `bloom_half`. Each pass samples 9 texels with weights `[0.0162, 0.0540, 0.1216, 0.1945, 0.2270, 0.1945, 0.1216, 0.0540, 0.0162]`.

3. **Composite:** Add blurred bloom back to original scene: `final = scene + bloom * intensity`.

### Tone Mapping

**Reinhard:**
```
mapped = color / (color + float3(1.0));
mapped = pow(mapped, float3(1.0 / 2.2));  // gamma correction
```

**ACES Filmic:**
```
// Approximation by Krzysztof Narkowicz
float3 x = color * exposure;
float3 a = x * (x * 2.51 + 0.03);
float3 b = x * (x * 2.43 + 0.59) + 0.14;
mapped = clamp(a / b, 0.0, 1.0);
```

### FXAA (Fast Approximate Anti-Aliasing)

Single-pass edge-aware anti-aliasing:

```
// 1. Sample 5 luminances (center + 4 neighbors)
float lumC = luminance(center);
float lumN = luminance(north);
float lumS = luminance(south);
float lumE = luminance(east);
float lumW = luminance(west);

// 2. Compute local contrast
float range = max(lumN, lumS, lumE, lumW, lumC) - min(lumN, lumS, lumE, lumW, lumC);
if (range < max(min_threshold, max(lumN, lumS, lumE, lumW, lumC) * edge_threshold))
    return center;  // not an edge

// 3. Determine edge direction (horizontal vs vertical)
// 4. Search along edge for endpoints
// 5. Blend perpendicular to edge direction
```

### Color Grading

```
// Brightness + contrast
color = (color - 0.5) * contrast + 0.5 + brightness;

// Tint
color *= tint;

// Saturation
float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
color = mix(float3(lum), color, saturation);

color = clamp(color, 0.0, 1.0);
```

### Vignette

```
float2 uv = fragCoord / resolution;
float2 center = float2(0.5);
float dist = distance(uv, center);
float vignette = smoothstep(radius, radius - softness, dist);
color *= vignette;
```

## Pipeline Execution

```c
void postfx_apply(rt_postfx3d *fx, vgfx3d_context_t *ctx) {
    if (!fx->enabled || fx->effect_count == 0) return;

    // Input starts as scene_target
    vgfx3d_rendertarget_t *input = fx->scene_target;
    int use_ping = 1;

    for (int i = 0; i < fx->effect_count; i++) {
        if (!fx->effects[i].enabled) continue;

        // Output: alternate between ping and pong
        // Last effect outputs directly to window framebuffer
        int is_last = (i == fx->effect_count - 1);
        vgfx3d_rendertarget_t *output = is_last ? NULL : (use_ping ? fx->ping : fx->pong);

        if (output) ctx->backend->bind_render_target(ctx, output);
        else        ctx->backend->bind_render_target(ctx, NULL);  // window

        // Bind input as texture
        void *input_tex = ctx->backend->render_target_as_texture(ctx, input);
        ctx->backend->bind_texture_slot(ctx, 0, input_tex);

        // Draw full-screen quad with effect shader
        apply_effect(ctx, &fx->effects[i]);

        input = output;
        use_ping = !use_ping;
    }
}
```

## Canvas3D Integration

When `SetPostFX` is called on Canvas3D:
1. On `Canvas3D.Begin()`: redirect rendering to `fx->scene_target` instead of window
2. On `Canvas3D.End()`: run the post-processing chain
3. Final result lands in the window framebuffer

## Public API

```c
// Construction
void *rt_postfx3d_new(void);

// Add effects to chain (order matters — first added = first applied)
void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes);
void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure);
void rt_postfx3d_add_fxaa(void *obj);
void rt_postfx3d_add_color_grade(void *obj, double brightness, double contrast, double saturation);
void rt_postfx3d_add_vignette(void *obj, double radius, double softness);

// Control
void  rt_postfx3d_set_enabled(void *obj, int8_t enabled);
int8_t rt_postfx3d_get_enabled(void *obj);
void  rt_postfx3d_clear(void *obj);  // remove all effects
int64_t rt_postfx3d_get_effect_count(void *obj);

// Canvas3D integration
void rt_canvas3d_set_post_fx(void *canvas, void *postfx);  // NULL = disable
```

## GC Finalizer

```c
static void rt_postfx3d_finalize(void *obj) {
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    // Internal render targets need cleanup
    if (fx->scene_target) vgfx3d_rt_destroy(fx->scene_target);
    if (fx->ping)         vgfx3d_rt_destroy(fx->ping);
    if (fx->pong)         vgfx3d_rt_destroy(fx->pong);
    if (fx->bloom_half)   vgfx3d_rt_destroy(fx->bloom_half);
    fx->scene_target = fx->ping = fx->pong = fx->bloom_half = NULL;
}
```

## runtime.def Entries

```c
RT_FUNC(PostFX3DNew,            rt_postfx3d_new,            "Viper.Graphics3D.PostFX3D.New",            "obj()")
RT_FUNC(PostFX3DAddBloom,       rt_postfx3d_add_bloom,      "Viper.Graphics3D.PostFX3D.AddBloom",       "void(obj,f64,f64,i64)")
RT_FUNC(PostFX3DAddTonemap,     rt_postfx3d_add_tonemap,    "Viper.Graphics3D.PostFX3D.AddTonemap",     "void(obj,i64,f64)")
RT_FUNC(PostFX3DAddFXAA,        rt_postfx3d_add_fxaa,       "Viper.Graphics3D.PostFX3D.AddFXAA",        "void(obj)")
RT_FUNC(PostFX3DAddColorGrade,  rt_postfx3d_add_color_grade,"Viper.Graphics3D.PostFX3D.AddColorGrade",  "void(obj,f64,f64,f64)")
RT_FUNC(PostFX3DAddVignette,    rt_postfx3d_add_vignette,   "Viper.Graphics3D.PostFX3D.AddVignette",    "void(obj,f64,f64)")
RT_FUNC(PostFX3DSetEnabled,     rt_postfx3d_set_enabled,    "Viper.Graphics3D.PostFX3D.set_Enabled",    "void(obj,i1)")
RT_FUNC(PostFX3DGetEnabled,     rt_postfx3d_get_enabled,    "Viper.Graphics3D.PostFX3D.get_Enabled",    "i1(obj)")
RT_FUNC(PostFX3DClear,          rt_postfx3d_clear,          "Viper.Graphics3D.PostFX3D.Clear",          "void(obj)")
RT_FUNC(PostFX3DEffectCount,    rt_postfx3d_get_effect_count,"Viper.Graphics3D.PostFX3D.get_EffectCount","i64(obj)")
RT_FUNC(Canvas3DSetPostFX,      rt_canvas3d_set_post_fx,    "Viper.Graphics3D.Canvas3D.SetPostFX",      "void(obj,obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.PostFX3D", PostFX3D, "obj", PostFX3DNew)
    RT_PROP("Enabled", "i1", PostFX3DGetEnabled, PostFX3DSetEnabled)
    RT_PROP("EffectCount", "i64", PostFX3DEffectCount, none)
    RT_METHOD("AddBloom", "void(f64,f64,i64)", PostFX3DAddBloom)
    RT_METHOD("AddTonemap", "void(i64,f64)", PostFX3DAddTonemap)
    RT_METHOD("AddFXAA", "void()", PostFX3DAddFXAA)
    RT_METHOD("AddColorGrade", "void(f64,f64,f64)", PostFX3DAddColorGrade)
    RT_METHOD("AddVignette", "void(f64,f64)", PostFX3DAddVignette)
    RT_METHOD("Clear", "void()", PostFX3DClear)
RT_CLASS_END()

// Add to Canvas3D class:
//   RT_METHOD("SetPostFX", "void(obj)", Canvas3DSetPostFX)
```

## Stubs

```c
void *rt_postfx3d_new(void) {
    rt_trap("PostFX3D.New: graphics support not compiled in");
    return NULL;
}
// All methods: no-ops. Getters return 0/false.
```

## Usage Example (Zia)

```rust
var fx = PostFX3D.New()
fx.AddBloom(0.8, 0.5, 5)          // bright areas glow
fx.AddTonemap(1, 1.2)             // ACES filmic, slight overexposure
fx.AddFXAA()                       // smooth edges
fx.AddColorGrade(0.05, 1.1, 1.2)  // slight brightness, contrast, saturation boost
fx.AddVignette(0.6, 0.4)          // subtle corner darkening

canvas.SetPostFX(fx)

// All subsequent Draw calls automatically go through the FX pipeline
while !canvas.ShouldClose {
    canvas.Poll()
    canvas.Clear(0.0, 0.0, 0.0)
    canvas.Begin(cam)
    scene.Draw(canvas, cam)
    canvas.End()
    canvas.Flip()
}
```

## Tests (15)

| Test | Description |
|------|-------------|
| Bloom threshold | Only pixels above threshold contribute to bloom |
| Bloom intensity | Higher intensity → brighter glow |
| Bloom blur passes | More passes → wider, softer glow |
| Tonemap Reinhard | HDR input → output clamped to [0,1] |
| Tonemap ACES | Different curve shape than Reinhard |
| Tonemap exposure | Higher exposure → brighter output |
| FXAA edge smoothing | Aliased edge → smoother after FXAA |
| FXAA no-edge preservation | Flat color area → unchanged |
| Color grade brightness | Positive brightness → lighter image |
| Color grade contrast | Higher contrast → more separation |
| Color grade saturation 0 | Grayscale output |
| Color grade tint | Red tint → image shifted toward red |
| Vignette corners | Corner pixels darker than center |
| Effect chain order | Bloom then tonemap → different result than tonemap then bloom |
| Enable/disable toggle | Disabled PostFX → scene renders directly to window (no FX) |

## Build Changes

**`src/lib/graphics/CMakeLists.txt`:**
- Add `vgfx3d_postfx.c` to all-platform sources
- Add Metal shader compilation for `vgfx3d_postfx_shaders.metal` (macOS)
- Add HLSL shader embedding for `vgfx3d_postfx_shaders.hlsl` (Windows)
- Add `vgfx3d_postfx_shaders.glsl` GLSL strings (Linux, embedded as C string literals)

**`src/runtime/CMakeLists.txt`:**
- Add `rt_postfx3d.c` to `RT_GRAPHICS_SOURCES`
