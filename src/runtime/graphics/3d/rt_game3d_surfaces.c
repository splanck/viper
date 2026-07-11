//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_surfaces.c
// Purpose: Game3D.Surfaces — a process-global name <-> id registry for
//   physics surface tags (plan 20). Footsteps, impact VFX, and decal tables
//   key on the stable session-scoped ids; data files persist names.
// Key invariants:
//   - Register is idempotent; ids are stable from 1 for the process lifetime.
//   - Thread-safe via the platform once/mutex init pattern (CONC rules).
// Ownership/Lifetime:
//   - Names are copied into process-global storage; never freed (bounded 255).
// Links: misc/plans/thirdpersonupgrade/20-physics-materials.md, ADR 0091.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_game3d.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GAME3D_SURFACES_MAX 255
#define GAME3D_SURFACE_NAME_MAX 64

static char g_surface_names[GAME3D_SURFACES_MAX][GAME3D_SURFACE_NAME_MAX];
static int32_t g_surface_count;

#if defined(_WIN32)
#include <windows.h>
static CRITICAL_SECTION g_surfaces_lock;
static INIT_ONCE g_surfaces_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK surfaces_init_once(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_surfaces_lock);
    return TRUE;
}

static void surfaces_lock(void) {
    InitOnceExecuteOnce(&g_surfaces_once, surfaces_init_once, NULL, NULL);
    EnterCriticalSection(&g_surfaces_lock);
}

static void surfaces_unlock(void) {
    LeaveCriticalSection(&g_surfaces_lock);
}
#else
#include <pthread.h>
static pthread_mutex_t g_surfaces_lock = PTHREAD_MUTEX_INITIALIZER;

static void surfaces_lock(void) {
    pthread_mutex_lock(&g_surfaces_lock);
}

static void surfaces_unlock(void) {
    pthread_mutex_unlock(&g_surfaces_lock);
}
#endif

/// @brief Register (or look up) a surface name; ids are stable from 1.
/// @details Idempotent; names are case-sensitive and truncated at 63 bytes.
///   Traps past the 255-surface budget (a data error, not a runtime state).
int64_t rt_game3d_surfaces_register(rt_string name) {
    const char *cname = name ? rt_string_cstr(name) : NULL;
    if (!cname || !*cname) {
        rt_trap("Game3D.Surfaces.Register: name must not be empty");
        return 0;
    }
    surfaces_lock();
    for (int32_t i = 0; i < g_surface_count; ++i) {
        if (strncmp(g_surface_names[i], cname, GAME3D_SURFACE_NAME_MAX) == 0) {
            surfaces_unlock();
            return i + 1;
        }
    }
    if (g_surface_count >= GAME3D_SURFACES_MAX) {
        surfaces_unlock();
        rt_trap("Game3D.Surfaces.Register: surface budget (255) exceeded");
        return 0;
    }
    int32_t index = g_surface_count++;
    strncpy(g_surface_names[index], cname, GAME3D_SURFACE_NAME_MAX - 1);
    g_surface_names[index][GAME3D_SURFACE_NAME_MAX - 1] = '\0';
    surfaces_unlock();
    return index + 1;
}

/// @brief Name for a surface id, or "" when unknown.
rt_string rt_game3d_surfaces_name_of(int64_t id) {
    rt_string result = NULL;
    surfaces_lock();
    if (id >= 1 && id <= g_surface_count)
        result = rt_const_cstr(g_surface_names[id - 1]);
    surfaces_unlock();
    return result ? rt_string_ref(result) : rt_str_empty();
}

/// @brief Id for a surface name, or 0 when unregistered.
int64_t rt_game3d_surfaces_id_of(rt_string name) {
    const char *cname = name ? rt_string_cstr(name) : NULL;
    int64_t id = 0;
    if (!cname || !*cname)
        return 0;
    surfaces_lock();
    for (int32_t i = 0; i < g_surface_count; ++i) {
        if (strncmp(g_surface_names[i], cname, GAME3D_SURFACE_NAME_MAX) == 0) {
            id = i + 1;
            break;
        }
    }
    surfaces_unlock();
    return id;
}

/// @brief Number of registered surfaces.
int64_t rt_game3d_surfaces_count(void) {
    surfaces_lock();
    int64_t count = g_surface_count;
    surfaces_unlock();
    return count;
}

#else
typedef int rt_game3d_surfaces_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
