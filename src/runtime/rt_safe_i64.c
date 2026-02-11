//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_safe_i64.c
/// @brief Thread-safe integer cell for Viper.Threads.SafeI64 class.
///
/// This file implements a simple thread-safe container for a single 64-bit
/// integer value. SafeI64 provides atomic-like operations using monitors for
/// synchronization, making it safe to read and modify from multiple threads.
///
/// **What is SafeI64?**
/// SafeI64 is a thread-safe wrapper around a 64-bit integer that provides:
/// - Thread-safe get and set operations
/// - Atomic add (increment/decrement)
/// - Compare-and-exchange for lock-free algorithms
///
/// **Operations:**
/// ```
/// ┌──────────────────────────────────────────────────────────────────────────┐
/// │ Operation                 │ Description                                  │
/// ├──────────────────────────────────────────────────────────────────────────┤
/// │ New(initial)              │ Create with initial value                    │
/// │ Get()                     │ Read current value                           │
/// │ Set(value)                │ Write new value                              │
/// │ Add(delta)                │ Add delta and return new value               │
/// │ CompareExchange(exp, des) │ If value==exp, set to des; return old value  │
/// └──────────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **Usage Examples:**
/// ```
/// ' Thread-safe counter
/// Dim counter = SafeI64.New(0)
///
/// ' Multiple threads can increment safely
/// counter.Add(1)  ' Atomic increment
///
/// ' Read the value
/// Print "Count: " & counter.Get()
///
/// ' Conditional update (compare-and-swap)
/// Dim old = counter.CompareExchange(100, 0)  ' If 100, reset to 0
/// If old = 100 Then Print "Counter was reset"
/// ```
///
/// **Compare-and-Exchange (CAS):**
/// The CompareExchange operation is the building block for lock-free algorithms.
/// It atomically:
/// 1. Reads the current value
/// 2. Compares it to the expected value
/// 3. If equal, sets to the desired value
/// 4. Returns the original value (for success checking)
///
/// ```
/// ' CAS loop example (optimistic concurrency)
/// Sub IncrementIfPositive(cell As SafeI64)
///     Dim old, new As Long
///     Do
///         old = cell.Get()
///         If old <= 0 Then Exit Sub  ' Don't increment non-positive
///         new = old + 1
///     Loop While cell.CompareExchange(old, new) <> old  ' Retry if changed
/// End Sub
/// ```
///
/// **Implementation:**
/// SafeI64 uses monitors (from rt_monitor.c) for thread synchronization.
/// This is simpler than true lock-free atomics but may have higher contention
/// under heavy concurrent access.
///
/// **Platform Support:**
/// | Platform | Status                     |
/// |----------|----------------------------|
/// | macOS    | Full support (via Monitor) |
/// | Linux    | Full support (via Monitor) |
/// | Windows  | Traps (not implemented)    |
///
/// @see rt_monitor.c For the underlying synchronization primitive
/// @see rt_threads.c For thread creation
///
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdint.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct RtSafeI64Win
{
    volatile LONG64 value;
} RtSafeI64Win;

static RtSafeI64Win *require_safe_win(void *obj, const char *what)
{
    if (!obj)
    {
        rt_trap(what ? what : "SafeI64: null object");
        return NULL;
    }
    return (RtSafeI64Win *)obj;
}

void *rt_safe_i64_new(int64_t initial)
{
    RtSafeI64Win *cell =
        (RtSafeI64Win *)rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtSafeI64Win));
    if (!cell)
        rt_trap("SafeI64.New: alloc failed");
    if (!cell)
        return NULL;
    cell->value = (LONG64)initial;
    return cell;
}

int64_t rt_safe_i64_get(void *obj)
{
    RtSafeI64Win *cell = require_safe_win(obj, "SafeI64.Get: null object");
    if (!cell)
        return 0;
    // Acquire read via InterlockedCompareExchange64 (returns current value)
    return (int64_t)InterlockedCompareExchange64(&cell->value, 0, 0);
}

void rt_safe_i64_set(void *obj, int64_t value)
{
    RtSafeI64Win *cell = require_safe_win(obj, "SafeI64.Set: null object");
    if (!cell)
        return;
    InterlockedExchange64(&cell->value, (LONG64)value);
}

int64_t rt_safe_i64_add(void *obj, int64_t delta)
{
    RtSafeI64Win *cell = require_safe_win(obj, "SafeI64.Add: null object");
    if (!cell)
        return 0;
    // InterlockedExchangeAdd64 returns the OLD value; we want the NEW value
    return (int64_t)InterlockedExchangeAdd64(&cell->value, (LONG64)delta) + delta;
}

int64_t rt_safe_i64_compare_exchange(void *obj, int64_t expected, int64_t desired)
{
    RtSafeI64Win *cell = require_safe_win(obj, "SafeI64.CompareExchange: null object");
    if (!cell)
        return 0;
    // Returns the old value (whether exchange happened or not)
    return (int64_t)InterlockedCompareExchange64(&cell->value, (LONG64)desired, (LONG64)expected);
}

#else

/// @brief Internal structure for SafeI64.
///
/// Just holds a single int64 value. Thread safety is provided by using the
/// object's address as a monitor key (implicit monitor association).
typedef struct RtSafeI64
{
    int64_t value; ///< The stored value.
} RtSafeI64;

/// @brief Validates and casts a SafeI64 object pointer.
///
/// @param obj The object to validate.
/// @param what Error message context for trap.
///
/// @return The cast pointer, or NULL after trapping.
static RtSafeI64 *require_safe(void *obj, const char *what)
{
    if (!obj)
    {
        rt_trap(what ? what : "SafeI64: null object");
        return NULL;
    }
    return (RtSafeI64 *)obj;
}

/// @brief Creates a new SafeI64 with an initial value.
///
/// Allocates and initializes a new thread-safe integer container. The returned
/// object is managed by Viper's garbage collector.
///
/// **Example:**
/// ```
/// Dim counter = SafeI64.New(0)        ' Start at zero
/// Dim limit = SafeI64.New(1000)       ' Set a limit
/// Dim flags = SafeI64.New(&HFF)       ' Bit flags
/// ```
///
/// @param initial The initial value to store.
///
/// @return A new SafeI64 object, or NULL after trapping on allocation failure.
///
/// @note Thread-safe to call from any thread.
///
/// @see rt_safe_i64_get For reading the value
/// @see rt_safe_i64_set For writing the value
void *rt_safe_i64_new(int64_t initial)
{
    RtSafeI64 *cell = (RtSafeI64 *)rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtSafeI64));
    if (!cell)
        rt_trap("SafeI64.New: alloc failed");
    if (!cell)
        return NULL;
    cell->value = initial;
    return cell;
}

/// @brief Reads the current value thread-safely.
///
/// Acquires the monitor, reads the value, and releases the monitor.
///
/// **Example:**
/// ```
/// Dim value = cell.Get()
/// Print "Current value: " & value
/// ```
///
/// @param obj The SafeI64 object. Must not be NULL.
///
/// @return The current value, or 0 after trapping if obj is NULL.
///
/// @note Thread-safe.
/// @note Traps if obj is NULL.
int64_t rt_safe_i64_get(void *obj)
{
    RtSafeI64 *cell = require_safe(obj, "SafeI64.Get: null object");
    if (!cell)
        return 0;
    rt_monitor_enter(cell);
    const int64_t v = cell->value;
    rt_monitor_exit(cell);
    return v;
}

/// @brief Sets the value thread-safely.
///
/// Acquires the monitor, sets the value, and releases the monitor.
///
/// **Example:**
/// ```
/// cell.Set(42)
/// cell.Set(0)  ' Reset
/// ```
///
/// @param obj The SafeI64 object. Must not be NULL.
/// @param value The new value to store.
///
/// @note Thread-safe.
/// @note Traps if obj is NULL.
void rt_safe_i64_set(void *obj, int64_t value)
{
    RtSafeI64 *cell = require_safe(obj, "SafeI64.Set: null object");
    if (!cell)
        return;
    rt_monitor_enter(cell);
    cell->value = value;
    rt_monitor_exit(cell);
}

/// @brief Atomically adds to the value and returns the new value.
///
/// Acquires the monitor, adds delta to the current value, and returns the
/// new value. This is useful for counters that need to know the new value.
///
/// **Example:**
/// ```
/// ' Thread-safe increment
/// Dim newCount = counter.Add(1)
/// Print "Incremented to " & newCount
///
/// ' Decrement
/// counter.Add(-1)
///
/// ' Add multiple
/// counter.Add(10)
/// ```
///
/// @param obj The SafeI64 object. Must not be NULL.
/// @param delta The value to add (can be negative for subtraction).
///
/// @return The new value after addition, or 0 after trapping if obj is NULL.
///
/// @note Thread-safe.
/// @note Traps if obj is NULL.
/// @note Overflow follows standard signed integer semantics.
int64_t rt_safe_i64_add(void *obj, int64_t delta)
{
    RtSafeI64 *cell = require_safe(obj, "SafeI64.Add: null object");
    if (!cell)
        return 0;
    rt_monitor_enter(cell);
    cell->value += delta;
    const int64_t out = cell->value;
    rt_monitor_exit(cell);
    return out;
}

/// @brief Atomically compares and conditionally exchanges the value.
///
/// If the current value equals `expected`, sets it to `desired`. Always
/// returns the value that was read (before any potential modification).
///
/// **Success check:** If the returned value equals `expected`, the exchange
/// happened. If not, another thread modified the value first.
///
/// **Example:**
/// ```
/// ' Try to increment from 5 to 6
/// Dim old = cell.CompareExchange(5, 6)
/// If old = 5 Then
///     Print "Successfully changed 5 to 6"
/// Else
///     Print "Value was " & old & ", not 5"
/// End If
///
/// ' CAS loop for complex updates
/// Dim current, newVal As Long
/// Do
///     current = cell.Get()
///     newVal = Transform(current)
/// Loop While cell.CompareExchange(current, newVal) <> current
/// ```
///
/// @param obj The SafeI64 object. Must not be NULL.
/// @param expected The value to compare against.
/// @param desired The value to set if comparison succeeds.
///
/// @return The value that was read (whether exchange happened or not).
///         Returns 0 after trapping if obj is NULL.
///
/// @note Thread-safe.
/// @note Traps if obj is NULL.
/// @note This is the building block for many lock-free algorithms.
int64_t rt_safe_i64_compare_exchange(void *obj, int64_t expected, int64_t desired)
{
    RtSafeI64 *cell = require_safe(obj, "SafeI64.CompareExchange: null object");
    if (!cell)
        return 0;
    rt_monitor_enter(cell);
    const int64_t old = cell->value;
    if (old == expected)
        cell->value = desired;
    rt_monitor_exit(cell);
    return old;
}

#endif
