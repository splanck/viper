//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_spritesheet.c
/// @brief Sprite sheet/atlas implementation.
///
/// Stores a single atlas Pixels buffer and a map of named regions.
/// Regions can be extracted as independent Pixels buffers.
///
//===----------------------------------------------------------------------===//

#include "rt_spritesheet.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int64_t x, y, w, h;
} ss_region;

typedef struct
{
    void *vptr;
    void *atlas;
    ss_region *regions;
    char **names;
    int64_t count;
    int64_t capacity;
} rt_spritesheet_impl;

#define SS_INITIAL_CAP 16

static void ss_finalizer(void *obj)
{
    rt_spritesheet_impl *ss = (rt_spritesheet_impl *)obj;
    if (ss)
    {
        int64_t i;
        for (i = 0; i < ss->count; i++)
        {
            free(ss->names[i]);
        }
        free(ss->regions);
        free(ss->names);
        ss->regions = NULL;
        ss->names = NULL;
        ss->count = 0;
        if (ss->atlas)
        {
            rt_obj_release_check0(ss->atlas);
            ss->atlas = NULL;
        }
    }
}

static int64_t find_region(rt_spritesheet_impl *ss, const char *name)
{
    int64_t i;
    for (i = 0; i < ss->count; i++)
    {
        if (strcmp(ss->names[i], name) == 0)
            return i;
    }
    return -1;
}

static void ensure_cap(rt_spritesheet_impl *ss)
{
    if (ss->count < ss->capacity)
        return;
    int64_t new_cap = ss->capacity * 2;
    ss_region *new_regions = (ss_region *)realloc(ss->regions, (size_t)new_cap * sizeof(ss_region));
    if (!new_regions)
    {
        rt_trap("SpriteSheet: memory allocation failed");
        return;
    }
    ss->regions = new_regions;
    char **new_names = (char **)realloc(ss->names, (size_t)new_cap * sizeof(char *));
    if (!new_names)
    {
        rt_trap("SpriteSheet: memory allocation failed");
        return;
    }
    ss->names = new_names;
    ss->capacity = new_cap;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_spritesheet_new(void *atlas_pixels)
{
    rt_spritesheet_impl *ss;
    if (!atlas_pixels)
        return NULL;

    ss = (rt_spritesheet_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_spritesheet_impl));
    if (!ss)
    {
        rt_trap("SpriteSheet: memory allocation failed");
        return NULL;
    }
    ss->vptr = NULL;
    ss->atlas = atlas_pixels;
    rt_obj_retain_maybe(atlas_pixels);
    ss->count = 0;
    ss->capacity = SS_INITIAL_CAP;
    ss->regions = (ss_region *)calloc((size_t)SS_INITIAL_CAP, sizeof(ss_region));
    ss->names = (char **)calloc((size_t)SS_INITIAL_CAP, sizeof(char *));
    if (!ss->regions || !ss->names)
    {
        rt_trap("SpriteSheet: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(ss, ss_finalizer);
    return ss;
}

void *rt_spritesheet_from_grid(void *atlas_pixels, int64_t frame_w, int64_t frame_h)
{
    void *sheet;
    int64_t atlas_w, atlas_h, cols, rows, idx, iy, ix;
    if (!atlas_pixels || frame_w <= 0 || frame_h <= 0)
        return NULL;

    sheet = rt_spritesheet_new(atlas_pixels);
    if (!sheet)
        return NULL;

    atlas_w = rt_pixels_width(atlas_pixels);
    atlas_h = rt_pixels_height(atlas_pixels);
    cols = atlas_w / frame_w;
    rows = atlas_h / frame_h;

    idx = 0;
    for (iy = 0; iy < rows; iy++)
    {
        for (ix = 0; ix < cols; ix++)
        {
            char name_buf[32];
            snprintf(name_buf, sizeof(name_buf), "%d", (int)idx);
            rt_spritesheet_set_region(
                sheet, rt_const_cstr(name_buf), ix * frame_w, iy * frame_h, frame_w, frame_h);
            idx++;
        }
    }
    return sheet;
}

void rt_spritesheet_set_region(
    void *obj, rt_string name, int64_t x, int64_t y, int64_t w, int64_t h)
{
    rt_spritesheet_impl *ss;
    const char *cstr;
    int64_t idx;
    if (!obj || !name)
        return;
    ss = (rt_spritesheet_impl *)obj;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return;

    /* Update existing region if name matches */
    idx = find_region(ss, cstr);
    if (idx >= 0)
    {
        ss->regions[idx].x = x;
        ss->regions[idx].y = y;
        ss->regions[idx].w = w;
        ss->regions[idx].h = h;
        return;
    }

    /* Add new region */
    ensure_cap(ss);
    {
        size_t name_len = strlen(cstr);
        ss->names[ss->count] = (char *)malloc(name_len + 1);
        if (!ss->names[ss->count])
            return;
        memcpy(ss->names[ss->count], cstr, name_len + 1);
        ss->regions[ss->count].x = x;
        ss->regions[ss->count].y = y;
        ss->regions[ss->count].w = w;
        ss->regions[ss->count].h = h;
        ss->count++;
    }
}

void *rt_spritesheet_get_region(void *obj, rt_string name)
{
    rt_spritesheet_impl *ss;
    const char *cstr;
    int64_t idx;
    ss_region *r;
    void *dst;
    if (!obj || !name)
        return NULL;
    ss = (rt_spritesheet_impl *)obj;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return NULL;

    idx = find_region(ss, cstr);
    if (idx < 0)
        return NULL;

    r = &ss->regions[idx];
    dst = rt_pixels_new(r->w, r->h);
    if (!dst)
        return NULL;

    rt_pixels_copy(dst, 0, 0, ss->atlas, r->x, r->y, r->w, r->h);
    return dst;
}

int8_t rt_spritesheet_has_region(void *obj, rt_string name)
{
    rt_spritesheet_impl *ss;
    const char *cstr;
    if (!obj || !name)
        return 0;
    ss = (rt_spritesheet_impl *)obj;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    return find_region(ss, cstr) >= 0 ? 1 : 0;
}

int64_t rt_spritesheet_region_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_spritesheet_impl *)obj)->count;
}

int64_t rt_spritesheet_width(void *obj)
{
    if (!obj)
        return 0;
    return rt_pixels_width(((rt_spritesheet_impl *)obj)->atlas);
}

int64_t rt_spritesheet_height(void *obj)
{
    if (!obj)
        return 0;
    return rt_pixels_height(((rt_spritesheet_impl *)obj)->atlas);
}

void *rt_spritesheet_region_names(void *obj)
{
    rt_spritesheet_impl *ss;
    void *seq;
    int64_t i;
    if (!obj)
        return rt_seq_new();
    ss = (rt_spritesheet_impl *)obj;
    seq = rt_seq_new();
    for (i = 0; i < ss->count; i++)
    {
        rt_string s = rt_const_cstr(ss->names[i]);
        rt_seq_push(seq, (void *)s);
    }
    return seq;
}

int8_t rt_spritesheet_remove_region(void *obj, rt_string name)
{
    rt_spritesheet_impl *ss;
    const char *cstr;
    int64_t idx, j;
    if (!obj || !name)
        return 0;
    ss = (rt_spritesheet_impl *)obj;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;

    idx = find_region(ss, cstr);
    if (idx < 0)
        return 0;

    free(ss->names[idx]);
    for (j = idx; j < ss->count - 1; j++)
    {
        ss->names[j] = ss->names[j + 1];
        ss->regions[j] = ss->regions[j + 1];
    }
    ss->count--;
    return 1;
}
