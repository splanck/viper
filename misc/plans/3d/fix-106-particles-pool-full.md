# Fix 106: Particles Pool Full — Silent Failure

## Severity: P1 — High

## Problem

When the particle pool reaches `max_particles` (`rt_particles3d.c:397`), new particles
are silently dropped. There's no API to detect this condition, making it impossible for
users to diagnose why their particle effects stop working.

```c
static void spawn_particle(rt_particles3d *ps) {
    if (ps->count >= ps->max_particles)
        return;  // Silent drop — no feedback
```

## Fix

1. Add a `get_IsFull` property to query pool saturation
2. Add a `get_Count` / `get_MaxParticles` pair for monitoring
3. Optionally: implement oldest-particle eviction when pool is full

### New API

```c
int64_t rt_particles3d_get_max_particles(void *obj);
int8_t rt_particles3d_is_full(void *obj);
```

### Eviction (Optional Enhancement)

Instead of dropping new particles when full, replace the oldest particle:

```c
static void spawn_particle(rt_particles3d *ps) {
    vgfx3d_particle_t *p;
    if (ps->count >= ps->max_particles) {
        // Find oldest particle (lowest remaining life)
        int oldest = 0;
        float min_life = ps->particles[0].life;
        for (int i = 1; i < ps->count; i++) {
            if (ps->particles[i].life < min_life) {
                min_life = ps->particles[i].life;
                oldest = i;
            }
        }
        p = &ps->particles[oldest];
    } else {
        p = &ps->particles[ps->count++];
    }
    // Initialize p...
}
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_particles3d.c` | Add `get_MaxParticles`, `is_full` functions (~10 LOC) |
| `src/runtime/graphics/rt_particles3d.h` | Declare new functions |
| `src/il/runtime/runtime.def` | Register new API entries |

## Test

- Create emitter with max_particles=10, emit 20
- Verify `is_full` returns true after 10
- Verify `get_Count` returns 10 (not 20)
