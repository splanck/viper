//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_particle.c
/// @brief Implementation of particle system.
///
//===----------------------------------------------------------------------===//

#include "rt_particle.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Internal particle structure.
struct particle
{
    double x, y;       ///< Position.
    double vx, vy;     ///< Velocity.
    double size;       ///< Current size.
    double start_size; ///< Initial size.
    int64_t life;      ///< Remaining frames.
    int64_t max_life;  ///< Total lifetime.
    int64_t color;     ///< Base color.
    int8_t active;     ///< 1 if alive.
};

/// Internal emitter structure.
struct rt_particle_emitter_impl
{
    struct particle *particles; ///< Particle array.
    int64_t max_particles;      ///< Maximum particles.
    int64_t active_count;       ///< Number of active particles.

    // Emitter position
    double x, y;

    // Emission settings
    double rate;             ///< Particles per frame.
    double rate_accumulator; ///< Fractional particle accumulator.
    int8_t emitting;         ///< 1 if currently emitting.

    // Particle settings
    int64_t min_life, max_life;
    double min_speed, max_speed;
    double min_angle, max_angle;
    double gx, gy;
    int64_t color;
    double min_size, max_size;
    int8_t fade_out;
    int8_t shrink;

    // Random state (simple LCG)
    uint64_t rand_state;
};

/// Simple random number generator (internal).
static double rand_double(struct rt_particle_emitter_impl *e)
{
    e->rand_state = e->rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(e->rand_state >> 33) / (double)(1ULL << 31);
}

/// Random double in range [min, max].
static double rand_range(struct rt_particle_emitter_impl *e, double min, double max)
{
    return min + rand_double(e) * (max - min);
}

/// Random int64 in range [min, max].
static int64_t rand_range_i64(struct rt_particle_emitter_impl *e, int64_t min, int64_t max)
{
    if (min >= max)
        return min;
    return min + (int64_t)(rand_double(e) * (double)(max - min + 1));
}

rt_particle_emitter rt_particle_emitter_new(int64_t max_particles)
{
    if (max_particles < 1)
        max_particles = 1;
    if (max_particles > RT_PARTICLE_MAX)
        max_particles = RT_PARTICLE_MAX;

    struct rt_particle_emitter_impl *e =
        (struct rt_particle_emitter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_particle_emitter_impl));

    e->particles = calloc((size_t)max_particles, sizeof(struct particle));
    if (!e->particles)
    {
        return NULL;
    }

    e->max_particles = max_particles;
    e->active_count = 0;

    e->x = 0.0;
    e->y = 0.0;
    e->rate = 1.0;
    e->rate_accumulator = 0.0;
    e->emitting = 0;

    e->min_life = 30;
    e->max_life = 60;
    e->min_speed = 1.0;
    e->max_speed = 3.0;
    e->min_angle = 0.0;
    e->max_angle = 360.0;
    e->gx = 0.0;
    e->gy = 0.0;
    e->color = 0xFFFFFFFF; // White, full alpha
    e->min_size = 2.0;
    e->max_size = 4.0;
    e->fade_out = 1;
    e->shrink = 0;

    // Seed random with address (simple seeding)
    e->rand_state = (uint64_t)(uintptr_t)e ^ 0x5DEECE66DULL;

    return e;
}

void rt_particle_emitter_destroy(rt_particle_emitter emitter)
{
    if (!emitter)
        return;
    if (emitter->particles)
        free(emitter->particles);
}

void rt_particle_emitter_set_position(rt_particle_emitter emitter, double x, double y)
{
    if (!emitter)
        return;
    emitter->x = x;
    emitter->y = y;
}

double rt_particle_emitter_x(rt_particle_emitter emitter)
{
    return emitter ? emitter->x : 0.0;
}

double rt_particle_emitter_y(rt_particle_emitter emitter)
{
    return emitter ? emitter->y : 0.0;
}

void rt_particle_emitter_set_rate(rt_particle_emitter emitter, double rate)
{
    if (!emitter)
        return;
    if (rate < 0.0)
        rate = 0.0;
    emitter->rate = rate;
}

double rt_particle_emitter_rate(rt_particle_emitter emitter)
{
    return emitter ? emitter->rate : 0.0;
}

void rt_particle_emitter_set_lifetime(rt_particle_emitter emitter,
                                      int64_t min_frames,
                                      int64_t max_frames)
{
    if (!emitter)
        return;
    if (min_frames < 1)
        min_frames = 1;
    if (max_frames < min_frames)
        max_frames = min_frames;
    emitter->min_life = min_frames;
    emitter->max_life = max_frames;
}

void rt_particle_emitter_set_velocity(rt_particle_emitter emitter,
                                      double min_speed,
                                      double max_speed,
                                      double min_angle,
                                      double max_angle)
{
    if (!emitter)
        return;
    if (min_speed < 0.0)
        min_speed = 0.0;
    if (max_speed < min_speed)
        max_speed = min_speed;
    emitter->min_speed = min_speed;
    emitter->max_speed = max_speed;
    emitter->min_angle = min_angle;
    emitter->max_angle = max_angle;
}

void rt_particle_emitter_set_gravity(rt_particle_emitter emitter, double gx, double gy)
{
    if (!emitter)
        return;
    emitter->gx = gx;
    emitter->gy = gy;
}

void rt_particle_emitter_set_color(rt_particle_emitter emitter, int64_t color)
{
    if (!emitter)
        return;
    emitter->color = color;
}

void rt_particle_emitter_set_size(rt_particle_emitter emitter, double min_size, double max_size)
{
    if (!emitter)
        return;
    if (min_size < 0.1)
        min_size = 0.1;
    if (max_size < min_size)
        max_size = min_size;
    emitter->min_size = min_size;
    emitter->max_size = max_size;
}

void rt_particle_emitter_set_fade_out(rt_particle_emitter emitter, int8_t fade_out)
{
    if (!emitter)
        return;
    emitter->fade_out = fade_out ? 1 : 0;
}

void rt_particle_emitter_set_shrink(rt_particle_emitter emitter, int8_t shrink)
{
    if (!emitter)
        return;
    emitter->shrink = shrink ? 1 : 0;
}

void rt_particle_emitter_start(rt_particle_emitter emitter)
{
    if (!emitter)
        return;
    emitter->emitting = 1;
}

void rt_particle_emitter_stop(rt_particle_emitter emitter)
{
    if (!emitter)
        return;
    emitter->emitting = 0;
}

int8_t rt_particle_emitter_is_emitting(rt_particle_emitter emitter)
{
    return emitter ? emitter->emitting : 0;
}

int8_t rt_particle_emitter_fade_out(rt_particle_emitter emitter)
{
    return emitter ? emitter->fade_out : 0;
}

int8_t rt_particle_emitter_shrink(rt_particle_emitter emitter)
{
    return emitter ? emitter->shrink : 0;
}

int64_t rt_particle_emitter_color(rt_particle_emitter emitter)
{
    return emitter ? emitter->color : 0;
}

/// Emit a single particle.
static void emit_one(struct rt_particle_emitter_impl *e)
{
    // Find an inactive slot
    for (int64_t i = 0; i < e->max_particles; i++)
    {
        if (!e->particles[i].active)
        {
            struct particle *p = &e->particles[i];

            p->x = e->x;
            p->y = e->y;

            double speed = rand_range(e, e->min_speed, e->max_speed);
            double angle = rand_range(e, e->min_angle, e->max_angle) * M_PI / 180.0;
            p->vx = cos(angle) * speed;
            p->vy = -sin(angle) * speed; // Negative because Y typically increases downward

            p->life = rand_range_i64(e, e->min_life, e->max_life);
            p->max_life = p->life;
            p->size = rand_range(e, e->min_size, e->max_size);
            p->start_size = p->size;
            p->color = e->color;
            p->active = 1;

            e->active_count++;
            return;
        }
    }
}

void rt_particle_emitter_burst(rt_particle_emitter emitter, int64_t count)
{
    if (!emitter || count < 1)
        return;
    for (int64_t i = 0; i < count && emitter->active_count < emitter->max_particles; i++)
    {
        emit_one(emitter);
    }
}

void rt_particle_emitter_update(rt_particle_emitter emitter)
{
    if (!emitter)
        return;

    // Emit new particles if emitting
    if (emitter->emitting)
    {
        emitter->rate_accumulator += emitter->rate;
        while (emitter->rate_accumulator >= 1.0 && emitter->active_count < emitter->max_particles)
        {
            emit_one(emitter);
            emitter->rate_accumulator -= 1.0;
        }
    }

    // Update existing particles
    emitter->active_count = 0;
    for (int64_t i = 0; i < emitter->max_particles; i++)
    {
        struct particle *p = &emitter->particles[i];
        if (!p->active)
            continue;

        // Apply velocity
        p->x += p->vx;
        p->y += p->vy;

        // Apply gravity
        p->vx += emitter->gx;
        p->vy += emitter->gy;

        // Shrink if enabled
        if (emitter->shrink && p->max_life > 0)
        {
            double life_ratio = (double)p->life / (double)p->max_life;
            p->size = p->start_size * life_ratio;
        }

        // Decrease lifetime
        p->life--;
        if (p->life <= 0)
        {
            p->active = 0;
        }
        else
        {
            emitter->active_count++;
        }
    }
}

int64_t rt_particle_emitter_count(rt_particle_emitter emitter)
{
    return emitter ? emitter->active_count : 0;
}

void rt_particle_emitter_clear(rt_particle_emitter emitter)
{
    if (!emitter)
        return;
    for (int64_t i = 0; i < emitter->max_particles; i++)
    {
        emitter->particles[i].active = 0;
    }
    emitter->active_count = 0;
    emitter->rate_accumulator = 0.0;
}

int8_t rt_particle_emitter_get(rt_particle_emitter emitter,
                               int64_t index,
                               double *out_x,
                               double *out_y,
                               double *out_size,
                               int64_t *out_color)
{
    if (!emitter)
        return 0;

    // Find the Nth active particle
    int64_t found = 0;
    for (int64_t i = 0; i < emitter->max_particles; i++)
    {
        struct particle *p = &emitter->particles[i];
        if (!p->active)
            continue;

        if (found == index)
        {
            if (out_x)
                *out_x = p->x;
            if (out_y)
                *out_y = p->y;
            if (out_size)
                *out_size = p->size;

            // Calculate color with fade
            if (out_color)
            {
                int64_t color = p->color;
                if (emitter->fade_out && p->max_life > 0)
                {
                    double life_ratio = (double)p->life / (double)p->max_life;
                    int64_t base_alpha = (color >> 24) & 0xFF;
                    int64_t new_alpha = (int64_t)(base_alpha * life_ratio);
                    color = (color & 0x00FFFFFF) | (new_alpha << 24);
                }
                *out_color = color;
            }
            return 1;
        }
        found++;
    }
    return 0;
}
