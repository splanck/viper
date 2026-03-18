# Phase 1: Software 3D Renderer

## Goal

Implement a complete CPU-based 3D rendering pipeline that writes into the existing ViperGFX framebuffer. This serves as the always-available baseline and as the reference implementation for GPU backend correctness testing.

## Architecture

```
Canvas3D.Begin(camera)
  ↓
Canvas3D.DrawMesh(mesh, transform, material)
  ↓ per triangle:
  1. Model→World        (Mat4 multiply)
  2. World→View         (camera view matrix)
  3. View→Clip          (camera projection matrix)
  4. Frustum clip       (Sutherland-Hodgman, 6 planes)
  5. Perspective divide  (w-divide → NDC)
  6. Viewport transform  (NDC → screen pixels)
  7. Rasterize          (half-space edge functions)
  8. Per-fragment:
     a. Z-test          (compare with depth buffer)
     b. Shade           (lighting + texture lookup)
     c. Write pixel     (into existing framebuffer)
  ↓
Canvas3D.End()
  ↓
Canvas3D.Flip()  → vgfx_present() (existing path)
```

## New Files

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d.h`** — Public 3D pipeline API
```c
// Context holds framebuffer pointer, Z-buffer, viewport, current camera
typedef struct vgfx3d_context vgfx3d_context_t;

// Vertex: full layout defined upfront to avoid format changes in Phases 9 and 14.
// Unused fields (tangent, bone data) are zero-initialized until their phase populates them.
typedef struct {
    float pos[3];              // object-space position
    float normal[3];           // vertex normal
    float uv[2];               // texture coordinates
    float color[4];            // RGBA vertex color
    float tangent[3];          // tangent vector (Phase 9 normal mapping, zero until then)
    uint8_t bone_indices[4];   // bone palette indices (Phase 14 skinning, zero until then)
    float bone_weights[4];     // blend weights (Phase 14 skinning, zero until then)
} vgfx3d_vertex_t;             // 80 bytes

// Triangle indices (uint32_t for mesh indexing)
typedef struct {
    uint32_t v[3];
} vgfx3d_triangle_t;

// Draw command submitted between Begin/End
typedef struct {
    const vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    float model_matrix[16];     // row-major 4x4
    float diffuse_color[4];     // RGBA material color
    const uint8_t *texture;     // RGBA pixel data (or NULL)
    int32_t tex_width, tex_height;
    float shininess;            // specular exponent
} vgfx3d_draw_cmd_t;

// Light (up to VGFX3D_MAX_LIGHTS = 8)
typedef struct {
    int type;           // 0=directional, 1=point, 2=ambient
    float direction[3]; // for directional
    float position[3];  // for point
    float color[3];     // RGB intensity
    float intensity;    // multiplier
    float attenuation;  // for point lights (1 / (1 + att * d²))
} vgfx3d_light_t;

// Camera parameters
typedef struct {
    float view[16];       // view matrix (row-major)
    float projection[16]; // projection matrix (row-major)
    float position[3];    // eye position (for specular)
} vgfx3d_camera_t;

// API
vgfx3d_context_t *vgfx3d_create(struct vgfx_window *win);
void vgfx3d_destroy(vgfx3d_context_t *ctx);

void vgfx3d_clear(vgfx3d_context_t *ctx, float r, float g, float b);
void vgfx3d_clear_depth(vgfx3d_context_t *ctx);

void vgfx3d_set_camera(vgfx3d_context_t *ctx, const vgfx3d_camera_t *cam);
void vgfx3d_set_light(vgfx3d_context_t *ctx, int index, const vgfx3d_light_t *light);
void vgfx3d_set_ambient(vgfx3d_context_t *ctx, float r, float g, float b);

void vgfx3d_begin(vgfx3d_context_t *ctx);
void vgfx3d_draw(vgfx3d_context_t *ctx, const vgfx3d_draw_cmd_t *cmd);
void vgfx3d_end(vgfx3d_context_t *ctx);

// Debug
void vgfx3d_set_wireframe(vgfx3d_context_t *ctx, int enabled);
void vgfx3d_set_backface_cull(vgfx3d_context_t *ctx, int enabled);
```

**`vgfx3d.c`** — Pipeline orchestration (~400 LOC)
- Context creation: allocate Z-buffer (`float *zbuf`, width × height), store framebuffer pointer
- `vgfx3d_clear`: fill framebuffer + clear Z-buffer to `FLT_MAX`
- `vgfx3d_begin`: reset draw command list
- `vgfx3d_draw`: transform vertices, clip, rasterize into framebuffer
- `vgfx3d_end`: flush any deferred work (transparent surfaces sorted back-to-front)
- Context stores: camera, lights[8], ambient color, wireframe flag, cull flag

**`vgfx3d_raster.c`** — Triangle rasterizer (~600 LOC)
- Half-space edge function method (parallelizable, handles sub-pixel cases cleanly)
- For each triangle in screen space:
  1. Compute bounding box, clamp to viewport
  2. For each pixel in bbox:
     a. Evaluate 3 edge functions → barycentric coords (λ₀, λ₁, λ₂)
     b. If all ≥ 0 (inside triangle):
        - Interpolate Z: `z = λ₀*z₀ + λ₁*z₁ + λ₂*z₂`
        - Z-test: if `z < zbuf[y*w+x]`, proceed
        - Interpolate color (Gouraud): `c = λ₀*c₀ + λ₁*c₁ + λ₂*c₂`
        - Perspective-correct UV: `uv = (λ₀*uv₀/w₀ + λ₁*uv₁/w₁ + λ₂*uv₂/w₂) / (λ₀/w₀ + λ₁/w₁ + λ₂/w₂)`
        - Texture lookup (if texture bound): bilinear sample
        - Write pixel to framebuffer, update Z-buffer
- Wireframe mode: Bresenham lines between triangle vertices (reuse existing `vgfx_draw_line`)

**`vgfx3d_clip.c`** — Frustum clipping (~300 LOC)
- Sutherland-Hodgman polygon clipping against 6 clip planes:
  - Left: `x ≥ -w`, Right: `x ≤ w`
  - Bottom: `y ≥ -w`, Top: `y ≤ w`
  - Near: `z ≥ -w`, Far: `z ≤ w`
- Input: 3 vertices in clip space (4D homogeneous)
- Output: 0-9 vertices (each clip plane can add 1 vertex; max 3+6=9). Fan triangulation produces up to 7 triangles (9-2=7)
- Lerp attributes (normal, UV, color) at clip intersection points

**`vgfx3d_vertex.c`** — Vertex transform (~200 LOC)
- Batch transform: multiply vertex positions by MVP matrix
- Transform normals by inverse-transpose of model matrix (for lighting)
- Perspective divide: `x/w, y/w, z/w`
- Viewport transform: NDC `[-1,1]` → screen `[0,width]×[0,height]`

#### Runtime Level (`src/runtime/graphics/`)

**`rt_canvas3d.h` / `rt_canvas3d.c`** — Canvas3D runtime type (~300 LOC)
```c
typedef struct {
    void *vptr;
    rt_canvas *canvas2d;          // underlying 2D canvas (owns framebuffer)
    vgfx3d_context_t *ctx3d;      // 3D pipeline context
    int64_t width, height;
} rt_canvas3d;

// Viper.Graphics3D.Canvas3D
void *rt_canvas3d_new(const char *title, int64_t w, int64_t h);
void  rt_canvas3d_clear(void *obj, double r, double g, double b);
void  rt_canvas3d_begin(void *obj, void *camera);
void  rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
void  rt_canvas3d_end(void *obj);
void  rt_canvas3d_flip(void *obj);
int64_t rt_canvas3d_poll(void *obj);
int8_t  rt_canvas3d_should_close(void *obj);
void  rt_canvas3d_set_wireframe(void *obj, int8_t enabled);
int64_t rt_canvas3d_get_width(void *obj);
int64_t rt_canvas3d_get_height(void *obj);
int64_t rt_canvas3d_get_fps(void *obj);
// Debug line/point in 3D space
void  rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color);
void  rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size);
```

**`rt_mesh.h` / `rt_mesh.c`** — Mesh runtime type (~500 LOC)
```c
typedef struct {
    void *vptr;
    vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t *indices;
    uint32_t index_count;
    int64_t triangle_count;
} rt_mesh3d;

// Construction
void *rt_mesh3d_new(void);                    // empty mesh
void *rt_mesh3d_new_box(double sx, double sy, double sz);     // unit box
void *rt_mesh3d_new_sphere(double radius, int64_t segments);  // UV sphere
void *rt_mesh3d_new_plane(double sx, double sz);              // XZ plane
void *rt_mesh3d_new_cylinder(double r, double h, int64_t segments);
void *rt_mesh3d_from_obj(const char *path);   // Wavefront OBJ loader

// Properties
int64_t rt_mesh3d_get_vertex_count(void *obj);
int64_t rt_mesh3d_get_triangle_count(void *obj);

// Programmatic construction
void rt_mesh3d_add_vertex(void *obj, double x, double y, double z,
                          double nx, double ny, double nz,
                          double u, double v);
void rt_mesh3d_add_triangle(void *obj, int64_t v0, int64_t v1, int64_t v2);
void rt_mesh3d_recalc_normals(void *obj);  // auto-compute face normals
```

**`rt_camera3d.h` / `rt_camera3d.c`** — Camera3D runtime type (~250 LOC)
```c
typedef struct {
    void *vptr;
    vgfx3d_camera_t cam;  // view + projection + position
    double fov, aspect, near_plane, far_plane;
    double eye[3], target[3], up[3];
} rt_camera3d;

void *rt_camera3d_new(double fov, double aspect, double near_val, double far_val);
void  rt_camera3d_look_at(void *obj, void *eye, void *target, void *up);
void  rt_camera3d_orbit(void *obj, void *target, double distance, double yaw, double pitch);
void  rt_camera3d_set_fov(void *obj, double fov);
double rt_camera3d_get_fov(void *obj);
void  rt_camera3d_set_position(void *obj, void *pos);
void *rt_camera3d_get_position(void *obj);
void *rt_camera3d_get_forward(void *obj);   // returns Vec3
void *rt_camera3d_get_right(void *obj);     // returns Vec3
void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh);
```

**`rt_material3d.h` / `rt_material3d.c`** — Material3D runtime type (~150 LOC)
```c
typedef struct {
    void *vptr;
    double diffuse[4];   // RGBA
    double specular[3];  // RGB
    double shininess;
    void *texture;       // Pixels object (or NULL)
    int8_t unlit;        // skip lighting if true
} rt_material3d;

void *rt_material3d_new(void);
void *rt_material3d_new_color(double r, double g, double b);
void *rt_material3d_new_textured(void *pixels);
void  rt_material3d_set_color(void *obj, double r, double g, double b);
void  rt_material3d_set_texture(void *obj, void *pixels);
void  rt_material3d_set_shininess(void *obj, double s);
void  rt_material3d_set_unlit(void *obj, int8_t unlit);
```

**`rt_light3d.h` / `rt_light3d.c`** — Light3D runtime type (~150 LOC)
```c
void *rt_light3d_new_directional(void *direction, double r, double g, double b);
void *rt_light3d_new_point(void *position, double r, double g, double b, double attenuation);
void *rt_light3d_new_ambient(double r, double g, double b);
void  rt_light3d_set_intensity(void *obj, double intensity);
void  rt_light3d_set_color(void *obj, double r, double g, double b);
// Canvas3D.SetLight(index, light) binds lights to render context
```

## runtime.def Entries (Complete)

```c
//=============================================================================
// GRAPHICS 3D - CANVAS3D
//=============================================================================

RT_FUNC(Canvas3DNew,             rt_canvas3d_new,             "Viper.Graphics3D.Canvas3D.New",             "obj(str,i64,i64)")
RT_FUNC(Canvas3DClear,           rt_canvas3d_clear,           "Viper.Graphics3D.Canvas3D.Clear",           "void(obj,f64,f64,f64)")
RT_FUNC(Canvas3DBegin,           rt_canvas3d_begin,           "Viper.Graphics3D.Canvas3D.Begin",           "void(obj,obj)")
RT_FUNC(Canvas3DDrawMesh,        rt_canvas3d_draw_mesh,       "Viper.Graphics3D.Canvas3D.DrawMesh",        "void(obj,obj,obj,obj)")
RT_FUNC(Canvas3DEnd,             rt_canvas3d_end,             "Viper.Graphics3D.Canvas3D.End",             "void(obj)")
RT_FUNC(Canvas3DFlip,            rt_canvas3d_flip,            "Viper.Graphics3D.Canvas3D.Flip",            "void(obj)")
RT_FUNC(Canvas3DPoll,            rt_canvas3d_poll,            "Viper.Graphics3D.Canvas3D.Poll",            "i64(obj)")
RT_FUNC(Canvas3DShouldClose,     rt_canvas3d_should_close,    "Viper.Graphics3D.Canvas3D.get_ShouldClose", "i1(obj)")
RT_FUNC(Canvas3DSetWireframe,    rt_canvas3d_set_wireframe,   "Viper.Graphics3D.Canvas3D.set_Wireframe",   "void(obj,i1)")
RT_FUNC(Canvas3DSetBackfaceCull, rt_canvas3d_set_backface_cull,"Viper.Graphics3D.Canvas3D.SetBackfaceCull","void(obj,i1)")
RT_FUNC(Canvas3DGetWidth,        rt_canvas3d_get_width,       "Viper.Graphics3D.Canvas3D.get_Width",       "i64(obj)")
RT_FUNC(Canvas3DGetHeight,       rt_canvas3d_get_height,      "Viper.Graphics3D.Canvas3D.get_Height",      "i64(obj)")
RT_FUNC(Canvas3DGetFps,          rt_canvas3d_get_fps,         "Viper.Graphics3D.Canvas3D.get_Fps",         "i64(obj)")
RT_FUNC(Canvas3DGetDeltaTime,    rt_canvas3d_get_delta_time,  "Viper.Graphics3D.Canvas3D.get_DeltaTime",   "i64(obj)")
RT_FUNC(Canvas3DSetDTMax,        rt_canvas3d_set_dt_max,      "Viper.Graphics3D.Canvas3D.SetDTMax",        "void(obj,i64)")
RT_FUNC(Canvas3DSetLight,        rt_canvas3d_set_light,       "Viper.Graphics3D.Canvas3D.SetLight",        "void(obj,i64,obj)")
RT_FUNC(Canvas3DSetAmbient,      rt_canvas3d_set_ambient,     "Viper.Graphics3D.Canvas3D.SetAmbient",      "void(obj,f64,f64,f64)")
RT_FUNC(Canvas3DDrawLine3D,      rt_canvas3d_draw_line3d,     "Viper.Graphics3D.Canvas3D.DrawLine3D",      "void(obj,obj,obj,i64)")
RT_FUNC(Canvas3DDrawPoint3D,     rt_canvas3d_draw_point3d,    "Viper.Graphics3D.Canvas3D.DrawPoint3D",     "void(obj,obj,i64,i64)")
RT_FUNC(Canvas3DGetBackend,      rt_canvas3d_get_backend,     "Viper.Graphics3D.Canvas3D.get_Backend",     "str(obj)")
RT_FUNC(Canvas3DScreenshot,     rt_canvas3d_screenshot,      "Viper.Graphics3D.Canvas3D.Screenshot",      "obj(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.Canvas3D", Canvas3D, "obj", Canvas3DNew)
    RT_PROP("ShouldClose", "i1", Canvas3DShouldClose, none)
    RT_PROP("Width", "i64", Canvas3DGetWidth, none)
    RT_PROP("Height", "i64", Canvas3DGetHeight, none)
    RT_PROP("Fps", "i64", Canvas3DGetFps, none)
    RT_PROP("DeltaTime", "i64", Canvas3DGetDeltaTime, none)
    RT_PROP("Backend", "str", Canvas3DGetBackend, none)
    RT_METHOD("Clear", "void(f64,f64,f64)", Canvas3DClear)
    RT_METHOD("Begin", "void(obj)", Canvas3DBegin)
    RT_METHOD("DrawMesh", "void(obj,obj,obj)", Canvas3DDrawMesh)
    RT_METHOD("End", "void()", Canvas3DEnd)
    RT_METHOD("Flip", "void()", Canvas3DFlip)
    RT_METHOD("Poll", "i64()", Canvas3DPoll)
    RT_METHOD("SetBackfaceCull", "void(i1)", Canvas3DSetBackfaceCull)
    RT_METHOD("SetDTMax", "void(i64)", Canvas3DSetDTMax)
    RT_METHOD("SetLight", "void(i64,obj)", Canvas3DSetLight)
    RT_METHOD("SetAmbient", "void(f64,f64,f64)", Canvas3DSetAmbient)
    RT_METHOD("DrawLine3D", "void(obj,obj,i64)", Canvas3DDrawLine3D)
    RT_METHOD("DrawPoint3D", "void(obj,i64,i64)", Canvas3DDrawPoint3D)
    RT_METHOD("Screenshot", "obj()", Canvas3DScreenshot)
RT_CLASS_END()

//=============================================================================
// GRAPHICS 3D - MESH3D
//=============================================================================

RT_FUNC(Mesh3DNew,              rt_mesh3d_new,              "Viper.Graphics3D.Mesh3D.New",              "obj()")
RT_FUNC(Mesh3DNewBox,           rt_mesh3d_new_box,          "Viper.Graphics3D.Mesh3D.NewBox",           "obj(f64,f64,f64)")
RT_FUNC(Mesh3DNewSphere,        rt_mesh3d_new_sphere,       "Viper.Graphics3D.Mesh3D.NewSphere",        "obj(f64,i64)")
RT_FUNC(Mesh3DNewPlane,         rt_mesh3d_new_plane,        "Viper.Graphics3D.Mesh3D.NewPlane",         "obj(f64,f64)")
RT_FUNC(Mesh3DNewCylinder,      rt_mesh3d_new_cylinder,     "Viper.Graphics3D.Mesh3D.NewCylinder",      "obj(f64,f64,i64)")
RT_FUNC(Mesh3DFromOBJ,          rt_mesh3d_from_obj,         "Viper.Graphics3D.Mesh3D.FromOBJ",          "obj(str)")
RT_FUNC(Mesh3DGetVertexCount,   rt_mesh3d_get_vertex_count, "Viper.Graphics3D.Mesh3D.get_VertexCount",  "i64(obj)")
RT_FUNC(Mesh3DGetTriangleCount, rt_mesh3d_get_triangle_count,"Viper.Graphics3D.Mesh3D.get_TriangleCount","i64(obj)")
RT_FUNC(Mesh3DAddVertex,        rt_mesh3d_add_vertex,       "Viper.Graphics3D.Mesh3D.AddVertex",        "void(obj,f64,f64,f64,f64,f64,f64,f64,f64)")
RT_FUNC(Mesh3DAddTriangle,      rt_mesh3d_add_triangle,     "Viper.Graphics3D.Mesh3D.AddTriangle",      "void(obj,i64,i64,i64)")
RT_FUNC(Mesh3DRecalcNormals,    rt_mesh3d_recalc_normals,   "Viper.Graphics3D.Mesh3D.RecalcNormals",    "void(obj)")
RT_FUNC(Mesh3DClone,            rt_mesh3d_clone,            "Viper.Graphics3D.Mesh3D.Clone",            "obj(obj)")
RT_FUNC(Mesh3DTransform,        rt_mesh3d_transform,        "Viper.Graphics3D.Mesh3D.Transform",        "void(obj,obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.Mesh3D", Mesh3D, "obj", Mesh3DNew)
    RT_PROP("VertexCount", "i64", Mesh3DGetVertexCount, none)
    RT_PROP("TriangleCount", "i64", Mesh3DGetTriangleCount, none)
    RT_METHOD("AddVertex", "void(f64,f64,f64,f64,f64,f64,f64,f64)", Mesh3DAddVertex)
    RT_METHOD("AddTriangle", "void(i64,i64,i64)", Mesh3DAddTriangle)
    RT_METHOD("RecalcNormals", "void()", Mesh3DRecalcNormals)
    RT_METHOD("Clone", "obj()", Mesh3DClone)
    RT_METHOD("Transform", "void(obj)", Mesh3DTransform)
RT_CLASS_END()

//=============================================================================
// GRAPHICS 3D - CAMERA3D
//=============================================================================

RT_FUNC(Camera3DNew,            rt_camera3d_new,            "Viper.Graphics3D.Camera3D.New",            "obj(f64,f64,f64,f64)")
RT_FUNC(Camera3DLookAt,         rt_camera3d_look_at,        "Viper.Graphics3D.Camera3D.LookAt",         "void(obj,obj,obj,obj)")
RT_FUNC(Camera3DOrbit,          rt_camera3d_orbit,          "Viper.Graphics3D.Camera3D.Orbit",          "void(obj,obj,f64,f64,f64)")
RT_FUNC(Camera3DGetFov,         rt_camera3d_get_fov,        "Viper.Graphics3D.Camera3D.get_Fov",        "f64(obj)")
RT_FUNC(Camera3DSetFov,         rt_camera3d_set_fov,        "Viper.Graphics3D.Camera3D.set_Fov",        "void(obj,f64)")
RT_FUNC(Camera3DGetPosition,    rt_camera3d_get_position,   "Viper.Graphics3D.Camera3D.get_Position",   "obj(obj)")
RT_FUNC(Camera3DSetPosition,    rt_camera3d_set_position,   "Viper.Graphics3D.Camera3D.set_Position",   "void(obj,obj)")
RT_FUNC(Camera3DGetForward,     rt_camera3d_get_forward,    "Viper.Graphics3D.Camera3D.get_Forward",    "obj(obj)")
RT_FUNC(Camera3DGetRight,       rt_camera3d_get_right,      "Viper.Graphics3D.Camera3D.get_Right",      "obj(obj)")
RT_FUNC(Camera3DScreenToRay,    rt_camera3d_screen_to_ray,  "Viper.Graphics3D.Camera3D.ScreenToRay",    "obj(obj,i64,i64,i64,i64)")

RT_CLASS_BEGIN("Viper.Graphics3D.Camera3D", Camera3D, "obj", Camera3DNew)
    RT_PROP("Fov", "f64", Camera3DGetFov, Camera3DSetFov)
    RT_PROP("Position", "obj", Camera3DGetPosition, Camera3DSetPosition)
    RT_PROP("Forward", "obj", Camera3DGetForward, none)
    RT_PROP("Right", "obj", Camera3DGetRight, none)
    RT_METHOD("LookAt", "void(obj,obj,obj)", Camera3DLookAt)
    RT_METHOD("Orbit", "void(obj,f64,f64,f64)", Camera3DOrbit)
    RT_METHOD("ScreenToRay", "obj(i64,i64,i64,i64)", Camera3DScreenToRay)
RT_CLASS_END()

//=============================================================================
// GRAPHICS 3D - MATERIAL3D
//=============================================================================

RT_FUNC(Material3DNew,          rt_material3d_new,          "Viper.Graphics3D.Material3D.New",          "obj()")
RT_FUNC(Material3DNewColor,     rt_material3d_new_color,    "Viper.Graphics3D.Material3D.NewColor",     "obj(f64,f64,f64)")
RT_FUNC(Material3DNewTextured,  rt_material3d_new_textured, "Viper.Graphics3D.Material3D.NewTextured",  "obj(obj)")
RT_FUNC(Material3DSetColor,     rt_material3d_set_color,    "Viper.Graphics3D.Material3D.SetColor",     "void(obj,f64,f64,f64)")
RT_FUNC(Material3DSetTexture,   rt_material3d_set_texture,  "Viper.Graphics3D.Material3D.SetTexture",   "void(obj,obj)")
RT_FUNC(Material3DSetShininess, rt_material3d_set_shininess,"Viper.Graphics3D.Material3D.SetShininess", "void(obj,f64)")
RT_FUNC(Material3DSetUnlit,     rt_material3d_set_unlit,    "Viper.Graphics3D.Material3D.SetUnlit",     "void(obj,i1)")

RT_CLASS_BEGIN("Viper.Graphics3D.Material3D", Material3D, "obj", Material3DNew)
    RT_METHOD("SetColor", "void(f64,f64,f64)", Material3DSetColor)
    RT_METHOD("SetTexture", "void(obj)", Material3DSetTexture)
    RT_METHOD("SetShininess", "void(f64)", Material3DSetShininess)
    RT_METHOD("SetUnlit", "void(i1)", Material3DSetUnlit)
RT_CLASS_END()

//=============================================================================
// GRAPHICS 3D - LIGHT3D
//=============================================================================

RT_FUNC(Light3DNewDirectional,  rt_light3d_new_directional, "Viper.Graphics3D.Light3D.NewDirectional",  "obj(obj,f64,f64,f64)")
RT_FUNC(Light3DNewPoint,        rt_light3d_new_point,       "Viper.Graphics3D.Light3D.NewPoint",        "obj(obj,f64,f64,f64,f64)")
RT_FUNC(Light3DNewAmbient,      rt_light3d_new_ambient,     "Viper.Graphics3D.Light3D.NewAmbient",      "obj(f64,f64,f64)")
RT_FUNC(Light3DSetIntensity,    rt_light3d_set_intensity,   "Viper.Graphics3D.Light3D.SetIntensity",    "void(obj,f64)")
RT_FUNC(Light3DSetColor,        rt_light3d_set_color,       "Viper.Graphics3D.Light3D.SetColor",        "void(obj,f64,f64,f64)")

RT_CLASS_BEGIN("Viper.Graphics3D.Light3D", Light3D, "obj", Light3DNewDirectional)
    RT_METHOD("SetIntensity", "void(f64)", Light3DSetIntensity)
    RT_METHOD("SetColor", "void(f64,f64,f64)", Light3DSetColor)
RT_CLASS_END()
```

**Total new RT_FUNC entries:** 56
**Total new RT_CLASS blocks:** 5

## GC Finalizer Pattern (verified from rt_canvas.c:34-50)

Canvas3D and Mesh3D own heap-allocated resources that must be cleaned up:

```c
// Canvas3D finalizer — destroy 3D context and underlying 2D canvas
static void rt_canvas3d_finalize(void *obj) {
    rt_canvas3d *c3d = (rt_canvas3d *)obj;
    if (c3d->ctx3d) { vgfx3d_destroy(c3d->ctx3d); c3d->ctx3d = NULL; }
    // Inner canvas2d is separately GC-managed; do NOT free it here
}

// Mesh3D finalizer — free vertex/index arrays
static void rt_mesh3d_finalize(void *obj) {
    rt_mesh3d *m = (rt_mesh3d *)obj;
    free(m->vertices); m->vertices = NULL;
    free(m->indices);  m->indices = NULL;
}
```

Camera3D, Material3D, Light3D contain only scalar fields — no finalizer needed.

## OBJ Loader Specification (~300 LOC)

Custom Wavefront OBJ parser (no external deps). Supports:
- `v x y z` — vertex positions
- `vn x y z` — vertex normals
- `vt u v` — texture coordinates
- `f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3` — triangulated faces (also `v//vn` and `v` forms)
- `#` comments ignored
- Negative indices (relative to end of vertex list)
- Auto-triangulation of quads (split into 2 triangles)

Does NOT support: materials (.mtl), groups, smooth shading directives, curves/surfaces.

File I/O pattern matches existing `rt_pixels_load_bmp()`: `fopen/fread/fseek` with cross-platform 64-bit seek.

## Stubs for Non-Graphics Builds (verified from rt_graphics_stubs.c)

Pattern: Canvas3D.New traps immediately; all other functions are silent no-ops.

```c
// In rt_graphics_stubs.c:
void *rt_canvas3d_new(const char *title, int64_t w, int64_t h) {
    (void)title; (void)w; (void)h;
    rt_trap("Canvas3D.New: graphics support not compiled in");
    return NULL;
}
void rt_canvas3d_clear(void *obj, double r, double g, double b) {
    (void)obj; (void)r; (void)g; (void)b;
}
int64_t rt_canvas3d_get_width(void *obj) { (void)obj; return 0; }
// ... (same pattern for all 56 RT_FUNC functions across all 5 types)
```

Mesh3D factory functions (NewBox, NewSphere, FromOBJ) also trap. Property getters return 0/NULL.

## Build Changes

**`src/lib/graphics/CMakeLists.txt`**:
- Add `vgfx3d.c`, `vgfx3d_raster.c`, `vgfx3d_clip.c`, `vgfx3d_vertex.c` to source list
- Phase 2: add `vgfx3d_metal.m` (macOS only, guarded by `APPLE`)
- Phase 3: add `vgfx3d_d3d11.c` (Windows only, guarded by `WIN32`)
- Phase 4: add `vgfx3d_opengl.c`, `vgfx3d_opengl_loader.c`, `vgfx3d_opengl_shaders.c` (Linux only)
- Framework linking: Metal+MetalKit+QuartzCore (macOS), d3d11+dxgi+d3dcompiler (Windows), dl (Linux)

**`src/runtime/CMakeLists.txt`**:
- Add `rt_canvas3d.c`, `rt_mesh.c`, `rt_camera3d.c`, `rt_material3d.c`, `rt_light3d.c` to RT_GRAPHICS_SOURCES
- Add all headers to RT_PUBLIC_HEADERS
- Stubs: add stub functions to `rt_graphics_stubs.c` (compiled when `VIPER_ENABLE_GRAPHICS=OFF`)

**`src/il/runtime/RuntimeSignatures.cpp`**:
- Add `#include "rt_canvas3d.h"`, `"rt_mesh.h"`, `"rt_camera3d.h"`, `"rt_material3d.h"`, `"rt_light3d.h"`

**`src/il/runtime/classes/RuntimeClasses.hpp`**:
- Add `RTCLS_Canvas3D`, `RTCLS_Mesh3D`, `RTCLS_Camera3D`, `RTCLS_Material3D`, `RTCLS_Light3D` to enum

**`src/runtime/graphics/rt_graphics_stubs.c`**:
- Add stubs for all 56 RT_FUNC functions (Canvas3D: 21, Mesh3D: 13, Camera3D: 10, Material3D: 7, Light3D: 5)

## Rasterizer Technical Notes (verified against codebase)

**Viewport transform (NDC → screen):**
```c
// Mat4 perspective outputs OpenGL NDC: x,y in [-1,1], z in [-1,1]
// Screen space is Y-down (top-left origin, matching vgfx_draw.c convention)
screen_x = (ndc_x + 1.0f) * 0.5f * width;
screen_y = (1.0f - ndc_y) * 0.5f * height;  // Y-flip here!
screen_z = ndc_z;  // preserve for Z-buffer (range [-1,1])
```

**Pixel write format (verified from rt_drawing.c):**
```c
// Framebuffer is uint8_t[4] per pixel: [R][G][B][A]
uint8_t *dst = &fb.pixels[y * fb.stride + x * 4];
dst[0] = r;  // Red
dst[1] = g;  // Green
dst[2] = b;  // Blue
dst[3] = 0xFF; // Alpha (always opaque for solid geometry)
```

**Framebuffer access pattern (via public API):**
```c
vgfx_framebuffer_t fb;
if (!vgfx_get_framebuffer(canvas->gfx_win, &fb)) return;
// fb.pixels, fb.stride, fb.width, fb.height now available
```

## Winding Order Convention

Counter-clockwise (CCW) vertex ordering is front-facing. This follows the OpenGL convention.

- **Edge functions:** for CCW winding, all three half-space edge function values are ≥ 0 for interior points
- **Backface culling:** when enabled, discard triangles where the signed area (cross product of two edges) is negative (CW winding = back face)
- **Consistency:** all mesh generators (`NewBox`, `NewSphere`, `NewPlane`, `NewCylinder`) and the OBJ loader must emit CCW winding. GPU backends use the same convention (OpenGL default; Metal/D3D11 configured to match)

## Usage Example (Zia)

```rust
// Input via global Viper.Input module (Canvas3D is rendering-only)
var canvas = Canvas3D.New("3D Demo", 640, 480)
var cam = Camera3D.New(60.0, 640.0/480.0, 0.1, 100.0)
cam.LookAt(Vec3.New(0, 2, -5), Vec3.Zero(), Vec3.New(0, 1, 0))

var cube = Mesh3D.NewBox(1.0, 1.0, 1.0)
var mat = Material3D.NewColor(0.8, 0.2, 0.2)
var light = Light3D.NewDirectional(Vec3.New(-1, -1, 1), 1.0, 1.0, 1.0)
var transform = Mat4.Identity()

canvas.SetLight(0, light)
canvas.SetAmbient(0.1, 0.1, 0.1)

while !canvas.ShouldClose {
    canvas.Poll()
    canvas.Clear(0.1, 0.1, 0.2)
    canvas.Begin(cam)
    canvas.DrawMesh(cube, transform, mat)
    canvas.End()
    canvas.Flip()
    transform = Mat4.RotateY(0.02).Mul(transform)
}
```

## Usage Example (BASIC)

```basic
USING Viper.Graphics3D
USING Viper.Math

DIM canvas AS Canvas3D
DIM cam AS Camera3D
DIM cube AS Mesh3D
DIM mat AS Material3D
DIM light AS Light3D
DIM transform AS Mat4

canvas = Canvas3D.New("3D Demo", 640, 480)
cam = Camera3D.New(60.0, 640.0/480.0, 0.1, 100.0)
cam.LookAt(Vec3.New(0, 2, -5), Vec3.Zero(), Vec3.New(0, 1, 0))

cube = Mesh3D.NewBox(1.0, 1.0, 1.0)
mat = Material3D.NewColor(0.8, 0.2, 0.2)
light = Light3D.NewDirectional(Vec3.New(-1, -1, 1), 1.0, 1.0, 1.0)
transform = Mat4.Identity()

canvas.SetLight(0, light)
canvas.SetAmbient(0.1, 0.1, 0.1)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    canvas.Clear(0.1, 0.1, 0.2)
    canvas.Begin(cam)
    canvas.DrawMesh(cube, transform, mat)
    canvas.End()
    canvas.Flip()
    transform = Mat4.RotateY(0.02).Mul(transform)
LOOP
```

## Double→Float Conversion at API Boundary

The runtime API uses `double` (matching IL's f64 and the existing Mat4/Vec3 types). The internal rasterizer uses `float` for Z-buffer and interpolation efficiency. Conversion happens inside `rt_canvas3d_draw_mesh`:

```c
void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *mat4_transform, void *material) {
    rt_canvas3d *c3d = (rt_canvas3d *)obj;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    // mesh vertices are already float (vgfx3d_vertex_t uses float)
    // mat4_transform is double (ViperMat4) — convert to float[16] for pipeline
    double *src = ((ViperMat4 *)mat4_transform)->m;
    float model[16];
    for (int i = 0; i < 16; i++) model[i] = (float)src[i];
    // Build draw command with float model matrix...
}
```

This keeps the public API fully double-precision while the hot inner loop (rasterizer) operates on float.

## ViperDOS / Headless Compatibility

- When `VIPER_ENABLE_GRAPHICS=OFF`: all 56 functions compiled as stubs in `rt_graphics_stubs.c`
- Canvas3D.New, Mesh3D.NewBox, etc. trap with "graphics support not compiled in"
- Property getters return 0/NULL (silent no-op)
- ViperDOS builds: Canvas3D is unavailable (same as Canvas)
- Software renderer only needs the framebuffer — no GPU, no OS windowing beyond what Canvas provides
- CI/headless: tests that create Canvas3D are skipped via `display_required` ctest label

## Verification

1. `./scripts/build_viper.sh` — zero warnings
2. `./scripts/check_runtime_completeness.sh` — all RT_METHOD/RT_PROP handlers have RT_FUNC
3. All Phase 1 tests pass (see 06-testing.md)
4. `ctest --test-dir build -L graphics3d` — run only 3D tests
5. Manual visual test: rotating colored cube renders correctly at 60fps
6. `VIPER_ENABLE_GRAPHICS=OFF` build compiles cleanly (stubs only)
7. Backend property: `canvas.Backend` returns `"software"` in Phase 1

