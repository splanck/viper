//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_scheduler.c
// Purpose: Implements a poll-based task scheduler for the Viper.Threads.Scheduler
//          class. Tasks are registered with a string name and a delay in
//          milliseconds; Poll returns a sequence of names whose due times have
//          elapsed. Does not use background threads.
//
// Key invariants:
//   - Tasks are stored as a singly-linked list; Poll scans and removes due ones.
//   - Due timestamps are computed from CLOCK_MONOTONIC to avoid wall-clock skew.
//   - Scheduling the same name twice creates two separate entries.
//   - Poll removes and returns all tasks due at or before the current time.
//   - The scheduler is not thread-safe; external synchronization is required.
//   - Task name strings are retained by the scheduler until the task fires.
//
// Ownership/Lifetime:
//   - The scheduler object is heap-allocated and managed by the runtime GC.
//   - Each task entry retains a reference to its name string; the reference is
//     released when the task fires (transferred to the returned sequence).
//
// Links: src/runtime/threads/rt_scheduler.h (public API),
//        src/runtime/threads/rt_debounce.h (related delayed-execution concept)
//
//===----------------------------------------------------------------------===//

#include "rt_scheduler.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

extern void rt_trap(const char *msg);

//=============================================================================
// Time Helper
//=============================================================================

/// @brief Get current time in milliseconds from a monotonic clock.
static int64_t current_time_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000LL) / freq.QuadPart);
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    return 0;
#endif
}

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief A single scheduled task entry.
typedef struct sched_entry
{
    rt_string name;           ///< Retained task name string.
    int64_t due_time_ms;      ///< Absolute time when this task is due.
    struct sched_entry *next; ///< Next entry in linked list.
} sched_entry;

/// @brief Internal scheduler data.
typedef struct
{
    sched_entry *head; ///< Head of the linked list of entries.
    int64_t count;     ///< Number of entries in the list.
} rt_scheduler_data;

/// @brief Finalizer for scheduler objects. Frees all entries.
static void scheduler_finalizer(void *obj)
{
    if (!obj)
        return;
    rt_scheduler_data *data = (rt_scheduler_data *)obj;
    sched_entry *e = data->head;
    while (e)
    {
        sched_entry *next = e->next;
        if (e->name)
            rt_string_unref(e->name);
        free(e);
        e = next;
    }
    data->head = NULL;
    data->count = 0;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Creates a new empty scheduler.
///
/// Allocates and initializes a Scheduler object with no pending tasks.
///
/// @return A new Scheduler object. Traps on allocation failure.
void *rt_scheduler_new(void)
{
    rt_scheduler_data *data =
        (rt_scheduler_data *)rt_obj_new_i64(0, (int64_t)sizeof(rt_scheduler_data));
    if (!data)
    {
        rt_trap("Scheduler: memory allocation failed");
        return NULL;
    }
    data->head = NULL;
    data->count = 0;
    rt_obj_set_finalizer(data, scheduler_finalizer);
    return data;
}

/// @brief Schedules a named task with a delay in milliseconds.
///
/// Records a task that will become due after the specified delay. If a task
/// with the same name already exists, it is replaced with the new delay.
///
/// @param sched Scheduler pointer.
/// @param name Task name string. Ignored if NULL.
/// @param delay_ms Delay in milliseconds from now. Negative values treated as 0.
void rt_scheduler_schedule(void *sched, rt_string name, int64_t delay_ms)
{
    if (!sched || !name)
        return;
    rt_scheduler_data *data = (rt_scheduler_data *)sched;

    if (delay_ms < 0)
        delay_ms = 0;

    int64_t due = current_time_ms() + delay_ms;
    const char *name_cstr = rt_string_cstr(name);

    // Check for existing entry with the same name and update it
    sched_entry *e = data->head;
    while (e)
    {
        if (strcmp(rt_string_cstr(e->name), name_cstr) == 0)
        {
            e->due_time_ms = due;
            return;
        }
        e = e->next;
    }

    // Create new entry
    sched_entry *entry = (sched_entry *)malloc(sizeof(sched_entry));
    if (!entry)
    {
        rt_trap("Scheduler.Schedule: memory allocation failed");
        return;
    }
    entry->name = rt_string_ref(name);
    entry->due_time_ms = due;
    entry->next = data->head;
    data->head = entry;
    data->count++;
}

/// @brief Cancels a scheduled task by name.
///
/// Removes the first task matching the given name from the scheduler.
///
/// @param sched Scheduler pointer.
/// @param name Task name to cancel.
/// @return 1 if a task was found and cancelled, 0 if not found.
int8_t rt_scheduler_cancel(void *sched, rt_string name)
{
    if (!sched || !name)
        return 0;
    rt_scheduler_data *data = (rt_scheduler_data *)sched;
    const char *name_cstr = rt_string_cstr(name);

    sched_entry **pp = &data->head;
    while (*pp)
    {
        if (strcmp(rt_string_cstr((*pp)->name), name_cstr) == 0)
        {
            sched_entry *e = *pp;
            *pp = e->next;
            rt_string_unref(e->name);
            free(e);
            data->count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/// @brief Checks if a named task is due.
///
/// Returns true if the named task exists and its due time has passed.
///
/// @param sched Scheduler pointer.
/// @param name Task name to check.
/// @return 1 if due, 0 if not due or not found.
int8_t rt_scheduler_is_due(void *sched, rt_string name)
{
    if (!sched || !name)
        return 0;
    rt_scheduler_data *data = (rt_scheduler_data *)sched;
    const char *name_cstr = rt_string_cstr(name);
    int64_t now = current_time_ms();

    sched_entry *e = data->head;
    while (e)
    {
        if (strcmp(rt_string_cstr(e->name), name_cstr) == 0)
            return now >= e->due_time_ms ? 1 : 0;
        e = e->next;
    }
    return 0;
}

/// @brief Polls for all due tasks.
///
/// Returns a Seq of task name strings for all tasks whose due time has
/// passed. Due tasks are removed from the scheduler.
///
/// @param sched Scheduler pointer.
/// @return Seq of due task name strings. Empty seq if none due.
void *rt_scheduler_poll(void *sched)
{
    void *result = rt_seq_new();
    if (!sched)
        return result;
    rt_scheduler_data *data = (rt_scheduler_data *)sched;
    int64_t now = current_time_ms();

    sched_entry **pp = &data->head;
    while (*pp)
    {
        if (now >= (*pp)->due_time_ms)
        {
            sched_entry *e = *pp;
            *pp = e->next;
            // Transfer name ownership to the result seq
            rt_seq_push(result, (void *)e->name);
            free(e);
            data->count--;
        }
        else
        {
            pp = &(*pp)->next;
        }
    }
    return result;
}

/// @brief Gets the number of pending tasks.
///
/// @param sched Scheduler pointer.
/// @return Count of tasks in the scheduler (both due and not-yet-due).
int64_t rt_scheduler_pending(void *sched)
{
    if (!sched)
        return 0;
    return ((rt_scheduler_data *)sched)->count;
}

/// @brief Clears all scheduled tasks.
///
/// Removes all tasks from the scheduler, freeing associated memory.
///
/// @param sched Scheduler pointer.
void rt_scheduler_clear(void *sched)
{
    if (!sched)
        return;
    rt_scheduler_data *data = (rt_scheduler_data *)sched;
    sched_entry *e = data->head;
    while (e)
    {
        sched_entry *next = e->next;
        rt_string_unref(e->name);
        free(e);
        e = next;
    }
    data->head = NULL;
    data->count = 0;
}
