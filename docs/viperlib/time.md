# Time & Timing

> Date/time operations, timing, and performance measurement.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.DateTime](#viperdatetime)
- [Viper.Time.Clock](#vipertimeclock)
- [Viper.Time.Countdown](#vipertimecountdown)
- [Viper.Diagnostics.Stopwatch](#viperdiagnosticsstopwatch)

---

## Viper.DateTime

Date and time operations. Timestamps are Unix timestamps (seconds since January 1, 1970 UTC).

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Now()` | `Integer()` | Returns the current Unix timestamp (seconds) |
| `NowMs()` | `Integer()` | Returns the current timestamp in milliseconds |
| `Year(timestamp)` | `Integer(Integer)` | Extracts the year from a timestamp |
| `Month(timestamp)` | `Integer(Integer)` | Extracts the month (1-12) from a timestamp |
| `Day(timestamp)` | `Integer(Integer)` | Extracts the day of month (1-31) from a timestamp |
| `Hour(timestamp)` | `Integer(Integer)` | Extracts the hour (0-23) from a timestamp |
| `Minute(timestamp)` | `Integer(Integer)` | Extracts the minute (0-59) from a timestamp |
| `Second(timestamp)` | `Integer(Integer)` | Extracts the second (0-59) from a timestamp |
| `DayOfWeek(timestamp)` | `Integer(Integer)` | Returns day of week (0=Sunday, 6=Saturday) |
| `Format(timestamp, format)` | `String(Integer, String)` | Formats a timestamp using strftime-style format |
| `ToISO(timestamp)` | `String(Integer)` | Returns ISO 8601 formatted string |
| `Create(y, m, d, h, min, s)` | `Integer(Integer...)` | Creates a timestamp from components |
| `AddSeconds(timestamp, seconds)` | `Integer(Integer, Integer)` | Adds seconds to a timestamp |
| `AddDays(timestamp, days)` | `Integer(Integer, Integer)` | Adds days to a timestamp |
| `Diff(t1, t2)` | `Integer(Integer, Integer)` | Returns the difference in seconds between two timestamps |

### Example

```basic
' Get current time
DIM now AS INTEGER
now = Viper.DateTime.Now()

' Extract components
PRINT "Year: "; Viper.DateTime.Year(now)
PRINT "Month: "; Viper.DateTime.Month(now)
PRINT "Day: "; Viper.DateTime.Day(now)
PRINT "Hour: "; Viper.DateTime.Hour(now)

' Format as string
PRINT Viper.DateTime.Format(now, "%Y-%m-%d %H:%M:%S")
' Output: "2025-12-06 14:30:00"

PRINT Viper.DateTime.ToISO(now)
' Output: "2025-12-06T14:30:00"

' Create a specific date
DIM birthday AS INTEGER
birthday = Viper.DateTime.Create(1990, 6, 15, 0, 0, 0)

' Date arithmetic
DIM tomorrow AS INTEGER
tomorrow = Viper.DateTime.AddDays(now, 1)

DIM nextWeek AS INTEGER
nextWeek = Viper.DateTime.AddDays(now, 7)

' Calculate difference
DIM age_seconds AS INTEGER
age_seconds = Viper.DateTime.Diff(now, birthday)
```

### Format Specifiers

| Specifier | Description | Example |
|-----------|-------------|---------|
| `%Y` | 4-digit year | 2025 |
| `%m` | 2-digit month | 01-12 |
| `%d` | 2-digit day | 01-31 |
| `%H` | 24-hour hour | 00-23 |
| `%M` | Minute | 00-59 |
| `%S` | Second | 00-59 |
| `%A` | Full weekday name | Monday |
| `%B` | Full month name | January |

---

## Viper.Time.Clock

---

## Viper.Time.Clock

Basic timing utilities for sleeping and measuring elapsed time.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sleep(ms)` | `Void(Integer)` | Pause execution for the specified number of milliseconds |
| `Ticks()` | `Integer()` | Returns monotonic time in milliseconds since an unspecified epoch |
| `TicksUs()` | `Integer()` | Returns monotonic time in microseconds since an unspecified epoch |

### Notes

- `Ticks()` and `TicksUs()` return monotonic, non-decreasing values suitable for measuring elapsed time
- The epoch (starting point) is unspecified - only use these functions for measuring durations, not absolute time
- `TicksUs()` provides microsecond resolution for high-precision timing
- `Sleep(0)` returns immediately without sleeping
- Negative values passed to `Sleep()` are treated as 0

### Example

```basic
' Measure execution time
DIM startMs AS INTEGER = Viper.Time.Clock.Ticks()

' Do some work...
FOR i = 1 TO 1000000
    ' busy loop
NEXT

DIM endMs AS INTEGER = Viper.Time.Clock.Ticks()
PRINT "Elapsed time: "; endMs - startMs; " ms"

' High-precision timing with microseconds
DIM startUs AS INTEGER = Viper.Time.Clock.TicksUs()
' ... fast operation ...
DIM endUs AS INTEGER = Viper.Time.Clock.TicksUs()
PRINT "Elapsed: "; endUs - startUs; " microseconds"

' Sleep for a short delay
Viper.Time.Clock.Sleep(100)  ' Sleep for 100ms
```

---

## Viper.Time.Countdown

---

## Viper.Time.Countdown

Interval timing with expiration detection. Useful for timeouts, rate limiting, and periodic actions.

**Type:** Instance (obj)
**Constructor:** `Viper.Time.Countdown.New(interval_ms)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Elapsed` | `Integer` | Read | Total elapsed time in milliseconds since start/last reset |
| `Remaining` | `Integer` | Read | Time remaining until expiration: max(0, Interval - Elapsed) |
| `Expired` | `Boolean` | Read | True if Elapsed >= Interval |
| `Interval` | `Integer` | Read/Write | Target interval duration in milliseconds |
| `IsRunning` | `Boolean` | Read | True if the countdown is currently running |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Start()` | `Void()` | Start or resume the countdown timer |
| `Stop()` | `Void()` | Stop/pause the timer (preserves elapsed time) |
| `Reset()` | `Void()` | Reset elapsed time to 0 and stop the timer |
| `Wait()` | `Void()` | Block until the countdown expires (starts timer if not running) |

### Notes

- All times are in milliseconds
- `Remaining` never goes negative - it returns 0 when expired
- `Wait()` automatically starts the timer if not already running
- `Wait()` returns immediately if already expired
- Negative intervals passed to `New()` or `set_Interval` are clamped to 0
- Elapsed time accumulates across multiple Start/Stop cycles

### Example

```basic
' Create a 500ms countdown
DIM cd AS OBJECT = Viper.Time.Countdown.New(500)

' Start the countdown
cd.Start()

' Poll for expiration
WHILE NOT cd.Expired
    PRINT "Remaining: "; cd.Remaining; " ms"
    Viper.Time.Clock.Sleep(100)
WEND

PRINT "Countdown expired!"

' Reset and wait (blocking)
cd.Reset()
PRINT "Waiting for 500ms..."
cd.Wait()
PRINT "Done!"

' Change the interval
cd.Interval = 1000
cd.Reset()
cd.Start()
PRINT "New 1-second countdown started"
```

### Use Cases

```basic
' Rate limiting - only allow action every 100ms
DIM rateLimiter AS OBJECT = Viper.Time.Countdown.New(100)
rateLimiter.Start()

SUB DoRateLimitedAction()
    IF rateLimiter.Expired THEN
        ' Perform the action
        PRINT "Action performed!"
        rateLimiter.Reset()
        rateLimiter.Start()
    END IF
END SUB

' Timeout pattern
DIM timeout AS OBJECT = Viper.Time.Countdown.New(5000)  ' 5 second timeout
timeout.Start()

WHILE NOT operationComplete AND NOT timeout.Expired
    ' Keep trying...
WEND

IF timeout.Expired THEN
    PRINT "Operation timed out!"
END IF
```

---

## Viper.Vec2

---

## Viper.Diagnostics.Stopwatch

High-precision stopwatch for benchmarking and performance measurement. Supports pause/resume timing with nanosecond resolution.

**Type:** Instance class (requires `New()` or `StartNew()`)

### Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Stopwatch()` | Create a new stopped stopwatch |
| `StartNew()` | `Stopwatch()` | Create and immediately start a new stopwatch |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ElapsedMs` | `Integer` (read-only) | Total elapsed time in milliseconds |
| `ElapsedUs` | `Integer` (read-only) | Total elapsed time in microseconds |
| `ElapsedNs` | `Integer` (read-only) | Total elapsed time in nanoseconds |
| `IsRunning` | `Boolean` (read-only) | True if stopwatch is currently running |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Start()` | `Void()` | Start or resume timing (no effect if already running) |
| `Stop()` | `Void()` | Pause timing, preserving accumulated time |
| `Reset()` | `Void()` | Stop and clear all accumulated time |
| `Restart()` | `Void()` | Reset and start in one atomic operation |

### Notes

- Stopwatch provides nanosecond resolution on supported platforms
- Time accumulates across multiple Start/Stop cycles until Reset
- Reading `ElapsedMs`/`ElapsedUs`/`ElapsedNs` while running returns current elapsed time
- `Start()` has no effect if already running (doesn't reset)
- `Stop()` has no effect if already stopped

### Example

```basic
' Create and start a stopwatch
DIM sw AS OBJECT = Viper.Diagnostics.Stopwatch.StartNew()

' Code to benchmark
FOR i = 1 TO 1000000
    DIM x AS INTEGER = i * i
NEXT

sw.Stop()
PRINT "Elapsed: "; sw.ElapsedMs; " ms"
PRINT "Elapsed: "; sw.ElapsedUs; " us"
PRINT "Elapsed: "; sw.ElapsedNs; " ns"

' Resume timing for additional work
sw.Start()
FOR i = 1 TO 500000
    DIM x AS INTEGER = i * i
NEXT
sw.Stop()
PRINT "Total: "; sw.ElapsedMs; " ms"

' Reset and restart
sw.Restart()
' More benchmarking...
sw.Stop()
PRINT "New timing: "; sw.ElapsedMs; " ms"
```

---

## Viper.IO.File

