//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_pathfollow.c
/// @brief Implementation of path follower.
///
//===----------------------------------------------------------------------===//

#include "rt_pathfollow.h"

#include <stdlib.h>
#include <string.h>

/// Waypoint structure.
struct waypoint
{
    int64_t x;
    int64_t y;
};

/// Internal path follower structure.
struct rt_pathfollow_impl
{
    struct waypoint points[RT_PATHFOLLOW_MAX_POINTS];
    int64_t point_count;      ///< Number of waypoints.
    rt_pathfollow_mode mode;  ///< Path mode.
    int64_t speed;            ///< Speed (units/sec, fixed-point).
    int8_t active;            ///< Is following active.
    int8_t finished;          ///< Has reached end (ONCE mode).
    int8_t reverse;           ///< Direction for PINGPONG.
    int64_t current_x;        ///< Current X position.
    int64_t current_y;        ///< Current Y position.
    int64_t segment;          ///< Current segment index.
    int64_t segment_progress; ///< Progress within segment (0-1000).
    int64_t total_length;     ///< Total path length (cached).
    int64_t *segment_lengths; ///< Cached segment lengths.
};

/// Integer square root approximation.
static int64_t isqrt(int64_t n)
{
    if (n < 0)
        return 0;
    if (n < 2)
        return n;

    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x)
    {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/// Calculate distance between two points.
static int64_t distance(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    // Scale down to prevent overflow, then scale result back
    int64_t dx_scaled = dx / 100;
    int64_t dy_scaled = dy / 100;
    return isqrt(dx_scaled * dx_scaled + dy_scaled * dy_scaled) * 100;
}

/// Recalculate cached segment lengths.
static void recalculate_lengths(rt_pathfollow path)
{
    if (!path || path->point_count < 2)
    {
        path->total_length = 0;
        return;
    }

    if (path->segment_lengths)
        free(path->segment_lengths);

    int64_t segments = path->point_count - 1;
    path->segment_lengths = malloc((size_t)segments * sizeof(int64_t));
    if (!path->segment_lengths)
    {
        path->total_length = 0;
        return;
    }

    path->total_length = 0;
    for (int64_t i = 0; i < segments; i++)
    {
        path->segment_lengths[i] = distance(
            path->points[i].x, path->points[i].y, path->points[i + 1].x, path->points[i + 1].y);
        path->total_length += path->segment_lengths[i];
    }
}

rt_pathfollow rt_pathfollow_new(void)
{
    struct rt_pathfollow_impl *path = malloc(sizeof(struct rt_pathfollow_impl));
    if (!path)
        return NULL;

    memset(path, 0, sizeof(struct rt_pathfollow_impl));
    path->speed = 100000; // Default: 100 units/sec
    path->mode = RT_PATHFOLLOW_ONCE;
    return path;
}

void rt_pathfollow_destroy(rt_pathfollow path)
{
    if (!path)
        return;

    if (path->segment_lengths)
        free(path->segment_lengths);
    free(path);
}

void rt_pathfollow_clear(rt_pathfollow path)
{
    if (!path)
        return;

    path->point_count = 0;
    path->segment = 0;
    path->segment_progress = 0;
    path->current_x = 0;
    path->current_y = 0;
    path->active = 0;
    path->finished = 0;
    path->reverse = 0;
    path->total_length = 0;

    if (path->segment_lengths)
    {
        free(path->segment_lengths);
        path->segment_lengths = NULL;
    }
}

int8_t rt_pathfollow_add_point(rt_pathfollow path, int64_t x, int64_t y)
{
    if (!path)
        return 0;
    if (path->point_count >= RT_PATHFOLLOW_MAX_POINTS)
        return 0;

    path->points[path->point_count].x = x;
    path->points[path->point_count].y = y;
    path->point_count++;

    // Set initial position if this is the first point
    if (path->point_count == 1)
    {
        path->current_x = x;
        path->current_y = y;
    }

    recalculate_lengths(path);
    return 1;
}

int64_t rt_pathfollow_point_count(rt_pathfollow path)
{
    return path ? path->point_count : 0;
}

void rt_pathfollow_set_mode(rt_pathfollow path, int64_t mode)
{
    if (!path)
        return;
    if (mode >= RT_PATHFOLLOW_ONCE && mode <= RT_PATHFOLLOW_PINGPONG)
        path->mode = (rt_pathfollow_mode)mode;
}

int64_t rt_pathfollow_get_mode(rt_pathfollow path)
{
    return path ? path->mode : 0;
}

void rt_pathfollow_set_speed(rt_pathfollow path, int64_t speed)
{
    if (path && speed > 0)
        path->speed = speed;
}

int64_t rt_pathfollow_get_speed(rt_pathfollow path)
{
    return path ? path->speed : 0;
}

void rt_pathfollow_start(rt_pathfollow path)
{
    if (!path || path->point_count < 2)
        return;

    path->active = 1;
    path->finished = 0;
}

void rt_pathfollow_pause(rt_pathfollow path)
{
    if (path)
        path->active = 0;
}

void rt_pathfollow_stop(rt_pathfollow path)
{
    if (!path)
        return;

    path->active = 0;
    path->finished = 0;
    path->segment = 0;
    path->segment_progress = 0;
    path->reverse = 0;

    if (path->point_count > 0)
    {
        path->current_x = path->points[0].x;
        path->current_y = path->points[0].y;
    }
}

int8_t rt_pathfollow_is_active(rt_pathfollow path)
{
    return path ? path->active : 0;
}

int8_t rt_pathfollow_is_finished(rt_pathfollow path)
{
    return path ? path->finished : 0;
}

void rt_pathfollow_update(rt_pathfollow path, int64_t dt)
{
    if (!path || !path->active || path->finished || path->point_count < 2)
        return;

    if (!path->segment_lengths || path->total_length == 0)
        return;

    // Calculate distance to move this frame
    // speed is units/sec (fixed-point 1000), dt is ms
    int64_t move_dist = (path->speed * dt) / 1000;

    while (move_dist > 0 && !path->finished)
    {
        int64_t seg = path->segment;
        int64_t seg_len = path->segment_lengths[seg];

        // Calculate remaining distance in current segment
        int64_t seg_traveled = (seg_len * path->segment_progress) / 1000;
        int64_t seg_remaining = seg_len - seg_traveled;

        if (path->reverse)
        {
            // Moving backwards
            seg_remaining = seg_traveled;
        }

        if (move_dist >= seg_remaining)
        {
            // Move to next segment
            move_dist -= seg_remaining;

            if (path->reverse)
            {
                // Moving backwards
                if (seg > 0)
                {
                    path->segment--;
                    path->segment_progress = 1000;
                }
                else
                {
                    // Reached start
                    path->segment_progress = 0;
                    if (path->mode == RT_PATHFOLLOW_PINGPONG)
                    {
                        path->reverse = 0;
                    }
                    else if (path->mode == RT_PATHFOLLOW_LOOP)
                    {
                        path->segment = path->point_count - 2;
                        path->segment_progress = 1000;
                    }
                    else
                    {
                        path->finished = 1;
                        path->active = 0;
                    }
                }
            }
            else
            {
                // Moving forward
                if (seg < path->point_count - 2)
                {
                    path->segment++;
                    path->segment_progress = 0;
                }
                else
                {
                    // Reached end
                    path->segment_progress = 1000;
                    if (path->mode == RT_PATHFOLLOW_PINGPONG)
                    {
                        path->reverse = 1;
                    }
                    else if (path->mode == RT_PATHFOLLOW_LOOP)
                    {
                        path->segment = 0;
                        path->segment_progress = 0;
                    }
                    else
                    {
                        path->finished = 1;
                        path->active = 0;
                    }
                }
            }
        }
        else
        {
            // Partial movement within segment
            if (seg_len > 0)
            {
                int64_t progress_delta = (move_dist * 1000) / seg_len;
                if (path->reverse)
                    path->segment_progress -= progress_delta;
                else
                    path->segment_progress += progress_delta;

                // Clamp
                if (path->segment_progress < 0)
                    path->segment_progress = 0;
                if (path->segment_progress > 1000)
                    path->segment_progress = 1000;
            }
            move_dist = 0;
        }
    }

    // Update current position based on segment and progress
    int64_t seg = path->segment;
    int64_t p = path->segment_progress;
    int64_t x1 = path->points[seg].x;
    int64_t y1 = path->points[seg].y;
    int64_t x2 = path->points[seg + 1].x;
    int64_t y2 = path->points[seg + 1].y;

    path->current_x = x1 + ((x2 - x1) * p) / 1000;
    path->current_y = y1 + ((y2 - y1) * p) / 1000;
}

int64_t rt_pathfollow_get_x(rt_pathfollow path)
{
    return path ? path->current_x : 0;
}

int64_t rt_pathfollow_get_y(rt_pathfollow path)
{
    return path ? path->current_y : 0;
}

int64_t rt_pathfollow_get_progress(rt_pathfollow path)
{
    if (!path || path->point_count < 2 || path->total_length == 0)
        return 0;

    // Calculate total distance traveled
    int64_t traveled = 0;
    for (int64_t i = 0; i < path->segment; i++)
    {
        traveled += path->segment_lengths[i];
    }
    traveled += (path->segment_lengths[path->segment] * path->segment_progress) / 1000;

    return (traveled * 1000) / path->total_length;
}

void rt_pathfollow_set_progress(rt_pathfollow path, int64_t progress)
{
    if (!path || path->point_count < 2 || path->total_length == 0)
        return;

    if (progress < 0)
        progress = 0;
    if (progress > 1000)
        progress = 1000;

    // Find the segment for this progress
    int64_t target_dist = (path->total_length * progress) / 1000;
    int64_t accumulated = 0;

    for (int64_t i = 0; i < path->point_count - 1; i++)
    {
        if (accumulated + path->segment_lengths[i] >= target_dist)
        {
            path->segment = i;
            int64_t seg_dist = target_dist - accumulated;
            path->segment_progress = (seg_dist * 1000) / path->segment_lengths[i];
            break;
        }
        accumulated += path->segment_lengths[i];
    }

    // Update position
    int64_t seg = path->segment;
    int64_t p = path->segment_progress;
    path->current_x =
        path->points[seg].x + ((path->points[seg + 1].x - path->points[seg].x) * p) / 1000;
    path->current_y =
        path->points[seg].y + ((path->points[seg + 1].y - path->points[seg].y) * p) / 1000;
}

int64_t rt_pathfollow_get_segment(rt_pathfollow path)
{
    return path ? path->segment : 0;
}

int64_t rt_pathfollow_get_angle(rt_pathfollow path)
{
    if (!path || path->point_count < 2)
        return 0;

    int64_t seg = path->segment;
    int64_t dx = path->points[seg + 1].x - path->points[seg].x;
    int64_t dy = path->points[seg + 1].y - path->points[seg].y;

    if (path->reverse)
    {
        dx = -dx;
        dy = -dy;
    }

    // Approximate atan2 using lookup or simple quadrant detection
    // For simplicity, return angle in 8 cardinal/ordinal directions
    // More precise: use a lookup table or CORDIC

    if (dx == 0 && dy == 0)
        return 0;

    // Simple 8-direction approximation
    // Right=0, Down=90, Left=180, Up=270
    if (dx > 0 && dy == 0)
        return 0;
    if (dx > 0 && dy > 0)
        return 45000; // 45 degrees
    if (dx == 0 && dy > 0)
        return 90000; // 90 degrees
    if (dx < 0 && dy > 0)
        return 135000; // 135 degrees
    if (dx < 0 && dy == 0)
        return 180000; // 180 degrees
    if (dx < 0 && dy < 0)
        return 225000; // 225 degrees
    if (dx == 0 && dy < 0)
        return 270000; // 270 degrees
    if (dx > 0 && dy < 0)
        return 315000; // 315 degrees

    return 0;
}
