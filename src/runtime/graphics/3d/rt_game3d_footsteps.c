//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_footsteps.c
// Purpose: Surface-driven footsteps — SurfaceTable3D (per-surface clip sets +
//   loudness keyed by Game3D.Surfaces ids) and Footsteps3D (per-entity binding
//   that consumes animator "footstep" events, raycasts the ground, resolves
//   the surface row, and plays a deterministically-selected clip variant).
// Key invariants:
//   - Variant selection uses a per-component LCG seeded at bind: replays are
//     byte-identical. Row 0 is the untyped-surface fallback; a fully unset
//     table is a silent no-op.
// Ownership/Lifetime:
//   - Tables retain their clips; Footsteps3D retains its table and holds a
//     plain backref to the owning entity (cleared at entity teardown).
// Links: misc/plans/thirdpersonupgrade/23-footstep-surface-events.md, ADR 0092.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_game3d.h"
#include "rt_game3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_object.h"
#include "rt_physics3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FOOTSTEPS3D_MAX_SURFACE 255
#define FOOTSTEPS3D_MAX_CLIPS 8
#define FOOTSTEPS3D_MIN_INTERVAL 0.12

typedef struct rt_game3d_surface_row {
    void *clips[FOOTSTEPS3D_MAX_CLIPS]; /* retained audio clips */
    int32_t clip_count;
    double loudness; /* hearing-stimulus scale (default 1) */
} rt_game3d_surface_row;

typedef struct rt_game3d_surface_table {
    void *vptr;
    rt_game3d_surface_row rows[FOOTSTEPS3D_MAX_SURFACE + 1]; /* index = surface id; 0 = default */
} rt_game3d_surface_table;

typedef struct rt_game3d_footsteps {
    void *vptr;
    void *entity; /* plain backref; NULLed at entity teardown */
    void *table;  /* retained SurfaceTable3D */
    rt_string event_prefix;
    int64_t ground_mask;
    double volume_scale;
    double cooldown; /* seconds until the next step may fire */
    uint32_t rng;    /* deterministic variant selector */
    int64_t step_count;
    int64_t last_surface; /* surface id of the most recent step (tests/telemetry) */
} rt_game3d_footsteps;

/*==========================================================================
 * SurfaceTable3D
 *=========================================================================*/

static void game3d_surface_table_finalize(void *obj) {
    rt_game3d_surface_table *table = (rt_game3d_surface_table *)obj;
    if (!table)
        return;
    for (int32_t r = 0; r <= FOOTSTEPS3D_MAX_SURFACE; ++r) {
        for (int32_t c = 0; c < table->rows[r].clip_count; ++c)
            game3d_release_ref(&table->rows[r].clips[c]);
        table->rows[r].clip_count = 0;
    }
}

void *rt_game3d_surface_table_new(void) {
    rt_game3d_surface_table *table = (rt_game3d_surface_table *)rt_obj_new_i64(
        RT_G3D_GAME3D_SURFACETABLE_CLASS_ID, (int64_t)sizeof(rt_game3d_surface_table));
    if (!table) {
        rt_trap("Game3D.SurfaceTable3D.New: allocation failed");
        return NULL;
    }
    memset(table, 0, sizeof(*table));
    rt_obj_set_finalizer(table, game3d_surface_table_finalize);
    for (int32_t r = 0; r <= FOOTSTEPS3D_MAX_SURFACE; ++r)
        table->rows[r].loudness = 1.0;
    return table;
}

static rt_game3d_surface_table *game3d_surface_table_checked(void *obj, const char *method) {
    rt_game3d_surface_table *table =
        (rt_game3d_surface_table *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_SURFACETABLE_CLASS_ID);
    if (!table)
        rt_trap(method);
    return table;
}

/// @brief Append a clip variant for @p surface_id (row 0 = the untyped default).
void *rt_game3d_surface_table_add_clip(void *obj, int64_t surface_id, void *clip) {
    rt_game3d_surface_table *table =
        game3d_surface_table_checked(obj, "Game3D.SurfaceTable3D.addClip: invalid table");
    if (!table || !clip)
        return obj;
    if (surface_id < 0 || surface_id > FOOTSTEPS3D_MAX_SURFACE)
        return obj;
    rt_game3d_surface_row *row = &table->rows[surface_id];
    if (row->clip_count >= FOOTSTEPS3D_MAX_CLIPS)
        return obj;
    rt_obj_retain_maybe(clip);
    row->clips[row->clip_count++] = clip;
    return obj;
}

/// @brief Hearing-stimulus scale for @p surface_id (plan 22 consumer; default 1).
void *rt_game3d_surface_table_set_loudness(void *obj, int64_t surface_id, double loudness) {
    rt_game3d_surface_table *table =
        game3d_surface_table_checked(obj, "Game3D.SurfaceTable3D.setLoudness: invalid table");
    if (!table || surface_id < 0 || surface_id > FOOTSTEPS3D_MAX_SURFACE)
        return obj;
    table->rows[surface_id].loudness =
        isfinite(loudness) && loudness >= 0.0 ? (loudness > 4.0 ? 4.0 : loudness) : 1.0;
    return obj;
}

/// @brief Clip count configured for @p surface_id (tests/tooling).
int64_t rt_game3d_surface_table_clip_count(void *obj, int64_t surface_id) {
    rt_game3d_surface_table *table =
        game3d_surface_table_checked(obj, "Game3D.SurfaceTable3D.clipCount: invalid table");
    if (!table || surface_id < 0 || surface_id > FOOTSTEPS3D_MAX_SURFACE)
        return 0;
    return table->rows[surface_id].clip_count;
}

/// @brief Resolve the effective row for a surface id (row 0 fallback, NULL when empty).
static rt_game3d_surface_row *game3d_surface_table_resolve(rt_game3d_surface_table *table,
                                                           int64_t surface_id) {
    if (!table)
        return NULL;
    if (surface_id >= 1 && surface_id <= FOOTSTEPS3D_MAX_SURFACE &&
        table->rows[surface_id].clip_count > 0)
        return &table->rows[surface_id];
    if (table->rows[0].clip_count > 0)
        return &table->rows[0];
    return NULL;
}

/*==========================================================================
 * Footsteps3D
 *=========================================================================*/

static void game3d_footsteps_finalize(void *obj) {
    rt_game3d_footsteps *steps = (rt_game3d_footsteps *)obj;
    if (!steps)
        return;
    steps->entity = NULL;
    game3d_release_ref(&steps->table);
    game3d_release_ref((void **)&steps->event_prefix);
}

void *rt_game3d_footsteps_new(void *entity_obj, void *table_obj) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_g3d_checked_or_null(entity_obj, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    rt_game3d_surface_table *table = (rt_game3d_surface_table *)rt_g3d_checked_or_null(
        table_obj, RT_G3D_GAME3D_SURFACETABLE_CLASS_ID);
    if (!entity || !table) {
        rt_trap("Game3D.Footsteps3D.New: entity and SurfaceTable3D required");
        return NULL;
    }
    rt_game3d_footsteps *steps = (rt_game3d_footsteps *)rt_obj_new_i64(
        RT_G3D_GAME3D_FOOTSTEPS_CLASS_ID, (int64_t)sizeof(rt_game3d_footsteps));
    if (!steps) {
        rt_trap("Game3D.Footsteps3D.New: allocation failed");
        return NULL;
    }
    memset(steps, 0, sizeof(*steps));
    rt_obj_set_finalizer(steps, game3d_footsteps_finalize);
    steps->entity = entity;
    rt_obj_retain_maybe(table_obj);
    steps->table = table_obj;
    steps->event_prefix = rt_string_ref(rt_const_cstr("footstep"));
    steps->ground_mask = -1;
    steps->volume_scale = 1.0;
    steps->rng = 0x9E3779B9u; /* fixed bind seed: deterministic replays */
    /* Install on the entity slot (previous component detaches, mirrors LipSync3D). */
    {
        rt_game3d_footsteps *previous = (rt_game3d_footsteps *)rt_g3d_checked_or_null(
            entity->footsteps, RT_G3D_GAME3D_FOOTSTEPS_CLASS_ID);
        if (previous && previous != steps)
            previous->entity = NULL;
        game3d_assign_typed_ref(&entity->footsteps, steps, RT_G3D_GAME3D_FOOTSTEPS_CLASS_ID);
    }
    return steps;
}

static rt_game3d_footsteps *game3d_footsteps_checked(void *obj, const char *method) {
    rt_game3d_footsteps *steps =
        (rt_game3d_footsteps *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_FOOTSTEPS_CLASS_ID);
    if (!steps)
        rt_trap(method);
    return steps;
}

void *rt_game3d_footsteps_set_event_prefix(void *obj, rt_string prefix) {
    rt_game3d_footsteps *steps =
        game3d_footsteps_checked(obj, "Game3D.Footsteps3D.setEventPrefix: invalid component");
    if (steps && prefix && rt_str_len(prefix) > 0)
        game3d_assign_ref((void **)&steps->event_prefix, prefix);
    return obj;
}

void *rt_game3d_footsteps_set_ground_mask(void *obj, int64_t mask) {
    rt_game3d_footsteps *steps =
        game3d_footsteps_checked(obj, "Game3D.Footsteps3D.setGroundMask: invalid component");
    if (steps)
        steps->ground_mask = mask;
    return obj;
}

void *rt_game3d_footsteps_set_volume_scale(void *obj, double scale) {
    rt_game3d_footsteps *steps =
        game3d_footsteps_checked(obj, "Game3D.Footsteps3D.setVolumeScale: invalid component");
    if (steps && isfinite(scale) && scale >= 0.0)
        steps->volume_scale = scale > 4.0 ? 4.0 : scale;
    return obj;
}

int64_t rt_game3d_footsteps_get_step_count(void *obj) {
    rt_game3d_footsteps *steps =
        game3d_footsteps_checked(obj, "Game3D.Footsteps3D.get_StepCount: invalid component");
    return steps ? steps->step_count : 0;
}

int64_t rt_game3d_footsteps_get_last_surface(void *obj) {
    rt_game3d_footsteps *steps =
        game3d_footsteps_checked(obj, "Game3D.Footsteps3D.get_LastSurface: invalid component");
    return steps ? steps->last_surface : 0;
}

/// @brief Fire one footstep: ground raycast -> surface row -> deterministic clip.
static void game3d_footsteps_fire(rt_game3d_world *world,
                                  rt_game3d_entity *entity,
                                  rt_game3d_footsteps *steps) {
    if (steps->cooldown > 0.0)
        return;
    steps->cooldown = FOOTSTEPS3D_MIN_INTERVAL;
    double origin[3];
    if (!game3d_entity_world_position_components(entity, origin)) {
        origin[0] = origin[1] = origin[2] = 0.0;
    }
    int64_t surface = 0;
    if (world->physics) {
        void *o = rt_vec3_new(origin[0], origin[1] + 0.25, origin[2]);
        void *d = rt_vec3_new(0.0, -1.0, 0.0);
        if (o && d) {
            void *hit = rt_world3d_raycast(world->physics, o, d, 1.5, steps->ground_mask);
            if (hit) {
                surface = rt_physics_hit3d_get_surface_type(hit);
                game3d_release_ref(&hit);
            }
        }
        game3d_release_ref(&o);
        game3d_release_ref(&d);
    }
    steps->last_surface = surface;
    rt_game3d_surface_table *table = (rt_game3d_surface_table *)rt_g3d_checked_or_null(
        steps->table, RT_G3D_GAME3D_SURFACETABLE_CLASS_ID);
    rt_game3d_surface_row *row = game3d_surface_table_resolve(table, surface);
    steps->step_count++;
    if (!row || !world->audio)
        return;
    steps->rng = steps->rng * 1664525u + 1013904223u;
    void *clip = row->clips[(steps->rng >> 8) % (uint32_t)row->clip_count];
    void *pos = rt_vec3_new(origin[0], origin[1], origin[2]);
    if (clip && pos) {
        void *voice = rt_game3d_audio_play_at(world->audio, clip, pos);
        game3d_release_ref(&voice);
    }
    game3d_release_ref(&pos);
}

/// @brief Per-step tick: consume this frame's matching animator events.
void game3d_footsteps_tick(rt_game3d_world *world, rt_game3d_entity *entity, double dt) {
    rt_game3d_footsteps *steps = (rt_game3d_footsteps *)rt_g3d_checked_or_null(
        entity->footsteps, RT_G3D_GAME3D_FOOTSTEPS_CLASS_ID);
    if (!steps)
        return;
    if (steps->cooldown > 0.0) {
        steps->cooldown -= dt;
        if (steps->cooldown < 0.0)
            steps->cooldown = 0.0;
    }
    void *anim = entity->anim;
    if (!anim)
        return;
    const char *prefix = steps->event_prefix ? rt_string_cstr(steps->event_prefix) : "footstep";
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    int64_t event_count = rt_game3d_animator_event_count(anim);
    for (int64_t e = 0; e < event_count; ++e) {
        rt_string name = rt_game3d_animator_event_name(anim, e);
        const char *cname = name ? rt_string_cstr(name) : NULL;
        int matches = cname && prefix_len > 0 && strncmp(cname, prefix, prefix_len) == 0;
        if (name)
            rt_string_unref(name);
        if (matches)
            game3d_footsteps_fire(world, entity, steps);
    }
}

#else
typedef int rt_game3d_footsteps_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
