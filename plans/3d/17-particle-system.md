# Phase 17: 3D Particle System

## Goal

Emitter-based 3D particle effects: spawn particles at a rate, update per-frame (velocity, gravity, lifetime, size/color interpolation), and render as camera-facing billboard quads with alpha or additive blending.

## Dependencies

- Phase 10 complete (alpha blending and blend state control)
- Phase 12 complete (scene graph — optional attachment to SceneNode3D)

## Architecture

```
Particles3D.New(maxParticles)
  → configure emitter: position, direction, spread, speed, lifetime, size, color, gravity
  → Start()

Per frame:
  particles.Update(deltaTime)
    ↓ for each particle:
    1. pos += vel * dt
    2. vel += gravity * dt
    3. life -= dt
    4. size = lerp(sizeStart, sizeEnd, age/maxLife)
    5. color = lerp(colorStart, colorEnd, age/maxLife)
    6. if life <= 0: kill
    ↓ spawn new particles: accumulate rate * dt
  particles.Draw(canvas, camera)
    ↓ for each alive particle:
    1. Build camera-facing quad (billboard)
    2. Submit as 2-triangle draw with particle color + alpha
    ↓ if additive: blend = src + dst (order-independent)
    ↓ if alpha: sort back-to-front, blend = src*a + dst*(1-a)
```

## New Files

**`src/runtime/graphics/rt_particles3d.h`** (~60 LOC)
**`src/runtime/graphics/rt_particles3d.c`** (~500 LOC)

## Data Structures

```c
// Single particle (internal)
typedef struct {
    float pos[3];
    float vel[3];
    float color[4];       // current RGBA
    float size;           // current size
    float life;           // remaining life (seconds)
    float max_life;       // initial life (for age ratio)
} vgfx3d_particle_t;

// Emitter / particle system (runtime type)
typedef struct {
    void *vptr;

    // Particle pool
    vgfx3d_particle_t *particles;
    int32_t count;          // alive particles
    int32_t max_particles;

    // Emitter properties
    double position[3];     // emission origin
    double emit_dir[3];     // emission direction (normalized internally)
    double emit_spread;     // cone half-angle (radians), 0 = line, PI = sphere
    double speed_min;
    double speed_max;
    double life_min;
    double life_max;
    double size_start;
    double size_end;
    double gravity[3];      // acceleration (e.g., 0, -9.8, 0)
    int64_t color_start;    // packed 0xRRGGBB
    int64_t color_end;      // packed 0xRRGGBB
    double alpha_start;     // starting alpha (default 1.0)
    double alpha_end;       // ending alpha (default 0.0 = fade out)
    double rate;            // particles per second
    double accumulator;     // fractional particle accumulation

    int8_t emitting;        // 1 = actively spawning
    int8_t additive_blend;  // 1 = additive, 0 = alpha blend
    void *texture;          // Pixels object for particle sprite (or NULL = solid quad)
    int32_t emitter_shape;  // 0=point (default), 1=sphere, 2=box
    double emitter_size[3]; // half-extents for box, radius for sphere (xyz)
} rt_particles3d;
```

## Particle Spawning

```c
static void spawn_particle(rt_particles3d *ps) {
    if (ps->count >= ps->max_particles) return;

    vgfx3d_particle_t *p = &ps->particles[ps->count++];

    // Position: emitter origin
    p->pos[0] = (float)ps->position[0];
    p->pos[1] = (float)ps->position[1];
    p->pos[2] = (float)ps->position[2];

    // Velocity: direction + random spread within cone + random speed
    float dir[3];
    random_cone_direction(ps->emit_dir, ps->emit_spread, dir);
    float speed = random_range(ps->speed_min, ps->speed_max);
    p->vel[0] = dir[0] * speed;
    p->vel[1] = dir[1] * speed;
    p->vel[2] = dir[2] * speed;

    // Life
    p->max_life = (float)random_range(ps->life_min, ps->life_max);
    p->life = p->max_life;

    // Initial size and color
    p->size = (float)ps->size_start;
    unpack_color(ps->color_start, p->color);
    p->color[3] = (float)ps->alpha_start;
}
```

## Random Cone Direction

Generate a random direction within a cone of half-angle `spread` around `dir`:

```c
static void random_cone_direction(const double *dir, double spread, float *out) {
    if (spread <= 0.0) {
        out[0] = (float)dir[0]; out[1] = (float)dir[1]; out[2] = (float)dir[2];
        return;
    }
    // Random angle within cone
    double theta = random_double() * spread;
    double phi = random_double() * 2.0 * M_PI;

    // Build rotation from dir to random direction within cone
    // ... (using axis-angle rotation around perpendicular axis)
}
```

Uses a simple xorshift PRNG seeded from a monotonic counter (no external RNG dependency).

## Billboard Rendering

Each particle is rendered as a camera-facing quad. Extract right and up vectors from the view matrix:

All particles are batched into a **single draw call** — submitting per-particle draw calls would be catastrophic for GPU backends (1000 particles = 1000 draw calls vs 1 batched call).

```c
void particles3d_draw(rt_particles3d *ps, void *canvas3d, void *camera) {
    if (ps->count == 0) return;

    // Get camera right/up from view matrix
    float right[3], up[3];
    extract_view_vectors(camera, right, up);

    // If alpha blend (not additive), sort particles back-to-front first
    if (!ps->additive_blend)
        sort_particles_by_distance(ps, camera);

    // Batch ALL particles into a single vertex/index buffer
    uint32_t vert_count = (uint32_t)ps->count * 4;
    uint32_t idx_count  = (uint32_t)ps->count * 6;
    vgfx3d_vertex_t *verts = (vgfx3d_vertex_t *)malloc(vert_count * sizeof(vgfx3d_vertex_t));
    uint32_t *indices = (uint32_t *)malloc(idx_count * sizeof(uint32_t));

    for (int i = 0; i < ps->count; i++) {
        vgfx3d_particle_t *p = &ps->particles[i];
        float hs = p->size * 0.5f;
        uint32_t base = (uint32_t)i * 4;

        // 4 quad vertices: center ± right*hs ± up*hs
        // v0 = center - right*hs - up*hs  (bottom-left)
        // v1 = center + right*hs - up*hs  (bottom-right)
        // v2 = center + right*hs + up*hs  (top-right)
        // v3 = center - right*hs + up*hs  (top-left)
        // UVs: (0,1), (1,1), (1,0), (0,0)
        // Color: p->color (RGBA with interpolated alpha)

        // 6 indices for 2 triangles (CCW winding)
        indices[i*6+0] = base+0; indices[i*6+1] = base+1; indices[i*6+2] = base+2;
        indices[i*6+3] = base+0; indices[i*6+4] = base+2; indices[i*6+5] = base+3;
    }

    // Submit ONE draw call for ALL particles
    vgfx3d_draw_cmd_t cmd = { .vertices = verts, .vertex_count = vert_count,
                               .indices = (const uint32_t *)indices, .index_count = idx_count,
                               /* identity model matrix, particle texture if set */ };
    vgfx3d_draw(ctx, &cmd);

    free(verts);
    free(indices);
}
```

## Blending Modes

**Alpha blend (default):** Sort particles back-to-front by distance from camera, then render with standard alpha blending (Phase 10 infrastructure). Good for smoke, dust.

**Additive blend:** No sorting needed (additive is order-independent). Render with blend func `src + dst`. Good for fire, sparks, glowing effects.

Backend-specific additive blend:
- Metal: `destinationRGBBlendFactor = .one`
- D3D11: `DestBlend = D3D11_BLEND_ONE`
- OpenGL: `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`

## Public API

```c
// Construction
void *rt_particles3d_new(int64_t max_particles);

// Emitter configuration
void rt_particles3d_set_position(void *obj, double x, double y, double z);
void rt_particles3d_set_direction(void *obj, double dx, double dy, double dz, double spread);
void rt_particles3d_set_speed(void *obj, double min_speed, double max_speed);
void rt_particles3d_set_lifetime(void *obj, double min_life, double max_life);
void rt_particles3d_set_size(void *obj, double start_size, double end_size);
void rt_particles3d_set_gravity(void *obj, double gx, double gy, double gz);
void rt_particles3d_set_color(void *obj, int64_t start_color, int64_t end_color);
void rt_particles3d_set_alpha(void *obj, double start_alpha, double end_alpha);
void rt_particles3d_set_rate(void *obj, double particles_per_second);
void rt_particles3d_set_additive(void *obj, int8_t additive);
void rt_particles3d_set_texture(void *obj, void *pixels);  // particle sprite (Pixels), NULL = solid

// Playback control
void rt_particles3d_start(void *obj);
void rt_particles3d_stop(void *obj);
void rt_particles3d_burst(void *obj, int64_t count);  // instant spawn N particles
void rt_particles3d_clear(void *obj);                  // kill all particles

// Per-frame
void rt_particles3d_update(void *obj, double delta_time);
void rt_particles3d_draw(void *obj, void *canvas3d, void *camera);

// Query
int64_t rt_particles3d_get_count(void *obj);
int8_t  rt_particles3d_get_emitting(void *obj);
```

## GC Finalizer

```c
static void rt_particles3d_finalize(void *obj) {
    rt_particles3d *ps = (rt_particles3d *)obj;
    free(ps->particles);
    ps->particles = NULL;
}
```

## runtime.def Entries

```c
RT_FUNC(Particles3DNew,          rt_particles3d_new,          "Viper.Graphics3D.Particles3D.New",          "obj(i64)")
RT_FUNC(Particles3DSetPosition,  rt_particles3d_set_position, "Viper.Graphics3D.Particles3D.SetPosition",  "void(obj,f64,f64,f64)")
RT_FUNC(Particles3DSetDirection, rt_particles3d_set_direction,"Viper.Graphics3D.Particles3D.SetDirection", "void(obj,f64,f64,f64,f64)")
RT_FUNC(Particles3DSetSpeed,     rt_particles3d_set_speed,    "Viper.Graphics3D.Particles3D.SetSpeed",     "void(obj,f64,f64)")
RT_FUNC(Particles3DSetLifetime,  rt_particles3d_set_lifetime, "Viper.Graphics3D.Particles3D.SetLifetime",  "void(obj,f64,f64)")
RT_FUNC(Particles3DSetSize,      rt_particles3d_set_size,     "Viper.Graphics3D.Particles3D.SetSize",      "void(obj,f64,f64)")
RT_FUNC(Particles3DSetGravity,   rt_particles3d_set_gravity,  "Viper.Graphics3D.Particles3D.SetGravity",   "void(obj,f64,f64,f64)")
RT_FUNC(Particles3DSetColor,     rt_particles3d_set_color,    "Viper.Graphics3D.Particles3D.SetColor",     "void(obj,i64,i64)")
RT_FUNC(Particles3DSetAlpha,     rt_particles3d_set_alpha,    "Viper.Graphics3D.Particles3D.SetAlpha",     "void(obj,f64,f64)")
RT_FUNC(Particles3DSetRate,      rt_particles3d_set_rate,     "Viper.Graphics3D.Particles3D.SetRate",      "void(obj,f64)")
RT_FUNC(Particles3DSetAdditive,  rt_particles3d_set_additive, "Viper.Graphics3D.Particles3D.set_Additive", "void(obj,i1)")
RT_FUNC(Particles3DSetEmitterShape, rt_particles3d_set_emitter_shape, "Viper.Graphics3D.Particles3D.SetEmitterShape", "void(obj,i64)")
RT_FUNC(Particles3DSetEmitterSize,  rt_particles3d_set_emitter_size,  "Viper.Graphics3D.Particles3D.SetEmitterSize",  "void(obj,f64,f64,f64)")
RT_FUNC(Particles3DSetTexture,  rt_particles3d_set_texture,  "Viper.Graphics3D.Particles3D.SetTexture",   "void(obj,obj)")
RT_FUNC(Particles3DStart,        rt_particles3d_start,        "Viper.Graphics3D.Particles3D.Start",        "void(obj)")
RT_FUNC(Particles3DStop,         rt_particles3d_stop,         "Viper.Graphics3D.Particles3D.Stop",         "void(obj)")
RT_FUNC(Particles3DBurst,        rt_particles3d_burst,        "Viper.Graphics3D.Particles3D.Burst",        "void(obj,i64)")
RT_FUNC(Particles3DClear,        rt_particles3d_clear,        "Viper.Graphics3D.Particles3D.Clear",        "void(obj)")
RT_FUNC(Particles3DUpdate,       rt_particles3d_update,       "Viper.Graphics3D.Particles3D.Update",       "void(obj,f64)")
RT_FUNC(Particles3DDraw,         rt_particles3d_draw,         "Viper.Graphics3D.Particles3D.Draw",         "void(obj,obj,obj)")
RT_FUNC(Particles3DGetCount,     rt_particles3d_get_count,    "Viper.Graphics3D.Particles3D.get_Count",    "i64(obj)")
RT_FUNC(Particles3DGetEmitting,  rt_particles3d_get_emitting, "Viper.Graphics3D.Particles3D.get_Emitting", "i1(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.Particles3D", Particles3D, "obj", Particles3DNew)
    RT_PROP("Count", "i64", Particles3DGetCount, none)
    RT_PROP("Emitting", "i1", Particles3DGetEmitting, none)
    RT_PROP("Additive", "i1", none, Particles3DSetAdditive)
    RT_METHOD("SetTexture", "void(obj)", Particles3DSetTexture)
    RT_METHOD("SetEmitterShape", "void(i64)", Particles3DSetEmitterShape)
    RT_METHOD("SetEmitterSize", "void(f64,f64,f64)", Particles3DSetEmitterSize)
    RT_METHOD("SetPosition", "void(f64,f64,f64)", Particles3DSetPosition)
    RT_METHOD("SetDirection", "void(f64,f64,f64,f64)", Particles3DSetDirection)
    RT_METHOD("SetSpeed", "void(f64,f64)", Particles3DSetSpeed)
    RT_METHOD("SetLifetime", "void(f64,f64)", Particles3DSetLifetime)
    RT_METHOD("SetSize", "void(f64,f64)", Particles3DSetSize)
    RT_METHOD("SetGravity", "void(f64,f64,f64)", Particles3DSetGravity)
    RT_METHOD("SetColor", "void(i64,i64)", Particles3DSetColor)
    RT_METHOD("SetAlpha", "void(f64,f64)", Particles3DSetAlpha)
    RT_METHOD("SetRate", "void(f64)", Particles3DSetRate)
    RT_METHOD("Start", "void()", Particles3DStart)
    RT_METHOD("Stop", "void()", Particles3DStop)
    RT_METHOD("Burst", "void(i64)", Particles3DBurst)
    RT_METHOD("Clear", "void()", Particles3DClear)
    RT_METHOD("Update", "void(f64)", Particles3DUpdate)
    RT_METHOD("Draw", "void(obj,obj)", Particles3DDraw)
RT_CLASS_END()
```

## Stubs

```c
void *rt_particles3d_new(int64_t max) {
    (void)max;
    rt_trap("Particles3D.New: graphics support not compiled in");
    return NULL;
}
// All methods: no-ops. Getters return 0/false.
```

## Usage Example (Zia)

```rust
// Dust cloud when sliding
var dust = Particles3D.New(200)
dust.SetPosition(playerX, 0.0, playerZ)
dust.SetDirection(0.0, 1.0, 0.0, 0.8)   // upward, 0.8 rad spread
dust.SetSpeed(1.0, 3.0)
dust.SetLifetime(0.3, 0.8)
dust.SetSize(0.1, 0.4)
dust.SetGravity(0.0, -2.0, 0.0)
dust.SetColor(0xBBAA88, 0x887766)        // tan to dark tan
dust.SetAlpha(0.6, 0.0)                  // fade out
dust.SetRate(50.0)

// On slide start:
dust.Start()

// Per frame:
dust.Update(dt)
dust.Draw(canvas, cam)

// On slide end:
dust.Stop()
```

## Tests (15)

| Test | Description |
|------|-------------|
| Spawn rate | Rate 10.0, update 1.0s → ~10 particles alive |
| Lifetime expiry | Particle with life 0.5s → dead after 0.5s update |
| Gravity | Gravity (0,-10,0) → particle Y velocity decreases |
| Size interpolation | Size start=1.0, end=0.0 → at half-life size=0.5 |
| Color interpolation | Red→Blue → at half-life = purple |
| Alpha interpolation | Alpha 1.0→0.0 → fades out |
| Burst mode | Burst(50) → exactly 50 particles spawned instantly |
| Max particles cap | Spawning beyond max → count stays at max |
| Additive blend flag | Set additive → different blend func used |
| Billboard orientation | Quads face camera regardless of particle position |
| Start/Stop toggle | Stop → no new particles; existing continue updating |
| Clear | Clear → count drops to 0 |
| Zero rate | Rate 0 → no spawning (only burst works) |
| Spread angle | Spread 0 → all particles go in same direction |
| Large spread | Spread PI → particles go in all directions (sphere) |
| Batched draw | 1000 particles → 1 draw call (4000 verts, 6000 indices) |
| Textured particle | SetTexture with RGBA Pixels → billboard samples texture |
