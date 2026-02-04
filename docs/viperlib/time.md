# Time & Timing

> Date/time operations, timing, and performance measurement.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Time.Clock](#vipertimeclock)
- [Viper.Time.Countdown](#vipertimecountdown)
- [Viper.Time.DateTime](#vipertimedatetime)
- [Viper.Time.Stopwatch](#vipertimestopwatch)

---

## Viper.Time.Clock

Basic timing utilities for sleeping and measuring elapsed time.

**Type:** Static utility class

### Methods

| Method      | Signature       | Description                                                       |
|-------------|-----------------|-------------------------------------------------------------------|
| `Sleep(ms)` | `Void(Integer)` | Pause execution for the specified number of milliseconds          |
| `Ticks()`   | `Integer()`     | Returns monotonic time in milliseconds since an unspecified epoch |
| `TicksUs()` | `Integer()`     | Returns monotonic time in microseconds since an unspecified epoch |

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

A countdown timer for implementing timeouts, delays, and timed operations. Tracks remaining time and signals when the
interval has expired.

**Type:** Instance class (requires `New(interval)`)

### Constructor

| Method          | Signature            | Description                                                      |
|-----------------|----------------------|------------------------------------------------------------------|
| `New(interval)` | `Countdown(Integer)` | Create a countdown timer with specified interval in milliseconds |

### Properties

| Property    | Type                   | Description                                            |
|-------------|------------------------|--------------------------------------------------------|
| `Elapsed`   | `Integer` (read-only)  | Milliseconds elapsed since start                       |
| `Remaining` | `Integer` (read-only)  | Milliseconds remaining until expiration (0 if expired) |
| `Expired`   | `Boolean` (read-only)  | True if the countdown has finished                     |
| `Interval`  | `Integer` (read/write) | The countdown duration in milliseconds                 |
| `IsRunning` | `Boolean` (read-only)  | True if the countdown is currently running             |

### Methods

| Method    | Signature | Description                                    |
|-----------|-----------|------------------------------------------------|
| `Start()` | `Void()`  | Start or resume the countdown                  |
| `Stop()`  | `Void()`  | Pause the countdown, preserving remaining time |
| `Reset()` | `Void()`  | Stop and reset to full interval                |
| `Wait()`  | `Void()`  | Block until the countdown expires              |

### Notes

- Unlike Stopwatch (which counts up), Countdown counts down from an interval
- `Remaining` returns 0 once expired (never negative)
- `Expired` becomes true when elapsed time reaches or exceeds the interval
- `Wait()` blocks the current thread until expiration; returns immediately if already expired
- Changing `Interval` while running affects when `Expired` becomes true

### Example

```basic
' Create a 5-second countdown
DIM timer AS OBJECT = Viper.Time.Countdown.New(5000)
timer.Start()

' Game loop with timeout
WHILE NOT timer.Expired
    PRINT "Time remaining: "; timer.Remaining; " ms"
    Viper.Time.Clock.Sleep(500)
WEND
PRINT "Time's up!"

' Use for operation timeout
DIM timeout AS OBJECT = Viper.Time.Countdown.New(1000)
timeout.Start()

WHILE NOT operationComplete AND NOT timeout.Expired
    ' Try operation...
WEND

IF timeout.Expired THEN
    PRINT "Operation timed out"
END IF

' Blocking wait
DIM delay AS OBJECT = Viper.Time.Countdown.New(2000)
delay.Start()
PRINT "Waiting 2 seconds..."
delay.Wait()
PRINT "Done!"
```

### Comparison with Clock.Sleep

| Use Case                      | Recommended       |
|-------------------------------|-------------------|
| Simple fixed delay            | `Clock.Sleep(ms)` |
| Timeout with early exit       | `Countdown`       |
| Progress tracking during wait | `Countdown`       |
| Pause/resume timing           | `Countdown`       |

---

## Viper.Time.DateTime

Date and time operations. Timestamps are Unix timestamps (seconds since January 1, 1970 UTC).

**Type:** Static utility class

### Methods

| Method                           | Signature                   | Description                                              |
|----------------------------------|-----------------------------|----------------------------------------------------------|
| `Now()`                          | `Integer()`                 | Returns the current Unix timestamp (seconds)             |
| `NowMs()`                        | `Integer()`                 | Returns the current timestamp in milliseconds            |
| `Year(timestamp)`                | `Integer(Integer)`          | Extracts the year from a timestamp                       |
| `Month(timestamp)`               | `Integer(Integer)`          | Extracts the month (1-12) from a timestamp               |
| `Day(timestamp)`                 | `Integer(Integer)`          | Extracts the day of month (1-31) from a timestamp        |
| `Hour(timestamp)`                | `Integer(Integer)`          | Extracts the hour (0-23) from a timestamp                |
| `Minute(timestamp)`              | `Integer(Integer)`          | Extracts the minute (0-59) from a timestamp              |
| `Second(timestamp)`              | `Integer(Integer)`          | Extracts the second (0-59) from a timestamp              |
| `DayOfWeek(timestamp)`           | `Integer(Integer)`          | Returns day of week (0=Sunday, 6=Saturday)               |
| `Format(timestamp, format)`      | `String(Integer, String)`   | Formats a timestamp using strftime-style format          |
| `ToISO(timestamp)`               | `String(Integer)`           | Returns ISO 8601 formatted string                        |
| `Create(y, m, d, h, min, s)`     | `Integer(Integer...)`       | Creates a timestamp from components                      |
| `AddSeconds(timestamp, seconds)` | `Integer(Integer, Integer)` | Adds seconds to a timestamp                              |
| `AddDays(timestamp, days)`       | `Integer(Integer, Integer)` | Adds days to a timestamp                                 |
| `Diff(t1, t2)`                   | `Integer(Integer, Integer)` | Returns the difference in seconds between two timestamps |

### Example

```basic
' Get current time
DIM now AS INTEGER
now = Viper.Time.DateTime.Now()

' Extract components
PRINT "Year: "; Viper.Time.DateTime.Year(now)
PRINT "Month: "; Viper.Time.DateTime.Month(now)
PRINT "Day: "; Viper.Time.DateTime.Day(now)
PRINT "Hour: "; Viper.Time.DateTime.Hour(now)

' Format as string
PRINT Viper.Time.DateTime.Format(now, "%Y-%m-%d %H:%M:%S")
' Output: "2025-12-06 14:30:00"

PRINT Viper.Time.DateTime.ToISO(now)
' Output: "2025-12-06T14:30:00"

' Create a specific date
DIM birthday AS INTEGER
birthday = Viper.Time.DateTime.Create(1990, 6, 15, 0, 0, 0)

' Date arithmetic
DIM tomorrow AS INTEGER
tomorrow = Viper.Time.DateTime.AddDays(now, 1)

DIM nextWeek AS INTEGER
nextWeek = Viper.Time.DateTime.AddDays(now, 7)

' Calculate difference
DIM age_seconds AS INTEGER
age_seconds = Viper.Time.DateTime.Diff(now, birthday)
```

### Format Specifiers

| Specifier | Description       | Example |
|-----------|-------------------|---------|
| `%Y`      | 4-digit year      | 2025    |
| `%m`      | 2-digit month     | 01-12   |
| `%d`      | 2-digit day       | 01-31   |
| `%H`      | 24-hour hour      | 00-23   |
| `%M`      | Minute            | 00-59   |
| `%S`      | Second            | 00-59   |
| `%A`      | Full weekday name | Monday  |
| `%B`      | Full month name   | January |

---

## Viper.Time.Stopwatch

High-precision stopwatch for benchmarking and performance measurement. Supports pause/resume timing with nanosecond
resolution.

**Type:** Instance class (requires `New()` or `StartNew()`)

### Constructors

| Method       | Signature     | Description                                  |
|--------------|---------------|----------------------------------------------|
| `New()`      | `Stopwatch()` | Create a new stopped stopwatch               |
| `StartNew()` | `Stopwatch()` | Create and immediately start a new stopwatch |

### Properties

| Property    | Type                  | Description                            |
|-------------|-----------------------|----------------------------------------|
| `ElapsedMs` | `Integer` (read-only) | Total elapsed time in milliseconds     |
| `ElapsedUs` | `Integer` (read-only) | Total elapsed time in microseconds     |
| `ElapsedNs` | `Integer` (read-only) | Total elapsed time in nanoseconds      |
| `IsRunning` | `Boolean` (read-only) | True if stopwatch is currently running |

### Methods

| Method      | Signature | Description                                           |
|-------------|-----------|-------------------------------------------------------|
| `Start()`   | `Void()`  | Start or resume timing (no effect if already running) |
| `Stop()`    | `Void()`  | Pause timing, preserving accumulated time             |
| `Reset()`   | `Void()`  | Stop and clear all accumulated time                   |
| `Restart()` | `Void()`  | Reset and start in one atomic operation               |

### Notes

- Stopwatch provides nanosecond resolution on supported platforms
- Time accumulates across multiple Start/Stop cycles until Reset
- Reading `ElapsedMs`/`ElapsedUs`/`ElapsedNs` while running returns current elapsed time
- `Start()` has no effect if already running (doesn't reset)
- `Stop()` has no effect if already stopped

### Example

```basic
' Create and start a stopwatch
DIM sw AS OBJECT = Viper.Time.Stopwatch.StartNew()

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

## See Also

- [Threads](threads.md) - `Thread.Sleep()` and synchronization with timeouts
- [Graphics](graphics.md) - Frame timing with `Canvas` game loops
