//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_manager_internal.h
// Purpose: Shared arena allocator type + the CLDR plural-rule parser boundary
//   between rt_locale_manager.c (locale loader) and rt_locale_plural.c (parser).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>

typedef struct loc_arena_alloc {
    void *ptr;
    struct loc_arena_alloc *next;
} loc_arena_alloc_t;

typedef struct loc_arena {
    loc_arena_alloc_t *allocs;
} loc_arena_t;

void *loc_arena_alloc(loc_arena_t *arena, size_t size);
rt_plural_rule_node_t *parse_rule(loc_arena_t *arena, const char *rule);
