# Time & Timing

> Date/time operations, timing, and performance measurement.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Time.Clock](#vipertimeclock)
- [Viper.Time.Countdown](#vipertimecountdown)
- [Viper.Time.DateOnly](#vipertimedateonly)
- [Viper.Time.DateRange](#vipertimedaterange)
- [Viper.Time.DateTime](#vipertimedatetime)
- [Viper.Time.Duration](#vipertimeduration)
- [Viper.Time.RelativeTime](#vipertimerelativetime)
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

### Zia Example

```zia
module ClockDemo;

bind Viper.Terminal;
bind Viper.Time.Clock as Clock;
bind Viper.Fmt as Fmt;

func start() {
    var t1 = Clock.Ticks();
    // ... some work ...
    var t2 = Clock.Ticks();
    Say("Elapsed: " + Fmt.Int(t2 - t1) + " ms");
    Say("Ticks (us): " + Fmt.Int(Clock.TicksUs()));
}
```

### BASIC Example

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

### Zia Example

> Countdown is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

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
| `ToISO(timestamp)`               | `String(Integer)`           | Returns ISO 8601 formatted string in UTC (with Z suffix) |
| `ToLocal(timestamp)`             | `String(Integer)`           | Returns ISO 8601 formatted string in local time (no Z)   |
| `Create(y, m, d, h, min, s)`     | `Integer(Integer...)`       | Creates a timestamp from local time components           |
| `AddSeconds(timestamp, seconds)` | `Integer(Integer, Integer)` | Adds seconds to a timestamp                              |
| `AddDays(timestamp, days)`       | `Integer(Integer, Integer)` | Adds days to a timestamp                                 |
| `Diff(t1, t2)`                   | `Integer(Integer, Integer)` | Returns the difference in seconds between two timestamps |
| `ParseISO(str)`                  | `Integer(String)`           | Parse an ISO 8601 datetime string to Unix timestamp      |
| `ParseDate(str)`                 | `Integer(String)`           | Parse a "YYYY-MM-DD" string to Unix timestamp            |
| `ParseTime(str)`                 | `Integer(String)`           | Parse a "HH:MM:SS" string to seconds since midnight      |
| `TryParse(str)`                  | `Integer(String)`           | Auto-detect format and parse; returns 0 on failure       |

### Notes

- `Create` interprets components in the **local time zone**
- `Year`, `Month`, `Day`, `Hour`, `Minute`, `Second` extract components in the **local time zone**
- `ToISO` formats in **UTC** (appends `Z` suffix)
- `ToLocal` formats in the **local time zone** (no `Z` suffix) — use this for consistent round-trips with `Create`
- `Format` uses the **local time zone** with strftime-style format strings
- `Diff` returns absolute seconds regardless of time zones

### Parsing Methods

| Method       | Input Format                          | Returns                                |
|--------------|---------------------------------------|----------------------------------------|
| `ParseISO`   | `"2025-06-15T14:30:00Z"` or `"2025-06-15T14:30:00"` | Unix timestamp (seconds) |
| `ParseDate`  | `"2025-06-15"`                        | Unix timestamp (seconds, midnight)     |
| `ParseTime`  | `"14:30:00"`                          | Seconds since midnight (0-86399)       |
| `TryParse`   | Any of the above formats              | Unix timestamp, or 0 on failure        |

- `ParseISO` accepts ISO 8601 datetime strings with or without the `Z` suffix
- `ParseDate` parses date-only strings and returns the timestamp at midnight
- `ParseTime` returns seconds since midnight (not a Unix timestamp)
- `TryParse` auto-detects the input format and returns 0 if the string cannot be parsed

### Zia Example

```zia
module DateTimeDemo;

bind Viper.Terminal;
bind Viper.Time.DateTime as DateTime;
bind Viper.Fmt as Fmt;

func start() {
    var now = DateTime.Now();
    Say("Year: " + Fmt.Int(DateTime.Year(now)));
    Say("Month: " + Fmt.Int(DateTime.Month(now)));
    Say("Day: " + Fmt.Int(DateTime.Day(now)));
    Say("Hour: " + Fmt.Int(DateTime.Hour(now)));

    // Local ISO format (consistent with Create)
    Say("Local: " + DateTime.ToLocal(now));

    // UTC ISO format
    Say("UTC: " + DateTime.ToISO(now));

    // Create a specific date and format it
    var christmas = DateTime.Create(2025, 12, 25, 12, 0, 0);
    Say("Christmas: " + DateTime.ToLocal(christmas));
}
```

### BASIC Example

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

' Local ISO (no Z suffix, consistent with Create)
PRINT Viper.Time.DateTime.ToLocal(now)
' Output: "2025-12-06T14:30:00"

' UTC ISO (with Z suffix)
PRINT Viper.Time.DateTime.ToISO(now)
' Output: "2025-12-06T19:30:00Z"

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

### Zia Example

> Stopwatch is not yet available as a constructible type in Zia. Use `Viper.Time.Clock.Ticks()` for timing measurements.

### BASIC Example

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

## Viper.Time.DateOnly

Date-only type for working with calendar dates without time components. Represents a year, month, and day.

**Type:** Instance class (requires `Create(year, month, day)`, `Today()`, `Parse(str)`, or `FromDays(i64)`)

### Constructors

| Method                   | Signature                       | Description                                            |
|--------------------------|---------------------------------|--------------------------------------------------------|
| `Create(year, month, day)` | `DateOnly(Integer, Integer, Integer)` | Create a date from year, month, and day components |
| `Today()`                | `DateOnly()`                    | Returns today's date                                   |
| `Parse(str)`             | `DateOnly(String)`              | Parse a date string (e.g., "2025-06-15")               |
| `FromDays(days)`         | `DateOnly(Integer)`             | Create a date from a day count (days since epoch)      |

### Properties

| Property     | Type                   | Description                                     |
|--------------|------------------------|-------------------------------------------------|
| `Year`       | `Integer` (read-only)  | The year component                              |
| `Month`      | `Integer` (read-only)  | The month component (1-12)                      |
| `Day`        | `Integer` (read-only)  | The day of month component (1-31)               |
| `DayOfWeek`  | `Integer` (read-only)  | Day of week (0=Sunday, 6=Saturday)              |
| `DayOfYear`  | `Integer` (read-only)  | Day of year (1-366)                             |
| `IsLeapYear` | `Boolean` (read-only)  | True if the year is a leap year                 |
| `DaysInMonth`| `Integer` (read-only)  | Number of days in the current month             |

### Methods

| Method              | Signature              | Description                                              |
|---------------------|------------------------|----------------------------------------------------------|
| `ToDays()`          | `Integer()`            | Convert to day count (days since epoch)                  |
| `AddDays(n)`        | `DateOnly(Integer)`    | Returns a new date with n days added                     |
| `AddMonths(n)`      | `DateOnly(Integer)`    | Returns a new date with n months added                   |
| `AddYears(n)`       | `DateOnly(Integer)`    | Returns a new date with n years added                    |
| `DiffDays(other)`   | `Integer(DateOnly)`    | Returns the number of days between this date and other   |
| `StartOfMonth()`    | `DateOnly()`           | Returns the first day of this date's month               |
| `EndOfMonth()`      | `DateOnly()`           | Returns the last day of this date's month                |
| `StartOfYear()`     | `DateOnly()`           | Returns January 1 of this date's year                    |
| `EndOfYear()`       | `DateOnly()`           | Returns December 31 of this date's year                  |
| `Cmp(other)`        | `Integer(DateOnly)`    | Compares dates, returns -1, 0, or 1                      |
| `Equals(other)`     | `Boolean(DateOnly)`    | Returns true if the two dates are equal                  |
| `ToString()`        | `String()`             | Returns date as "YYYY-MM-DD" string                      |
| `Format(fmt)`       | `String(String)`       | Formats the date using a format string                   |

### Notes

- DateOnly is immutable -- all mutation methods return new DateOnly instances
- `DiffDays(other)` returns a negative value if `other` is after this date
- `AddMonths` and `AddYears` clamp the day to the last valid day of the resulting month
- `Parse` expects ISO 8601 date format: "YYYY-MM-DD"

### Zia Example

```zia
module DateOnlyDemo;

bind Viper.Terminal;
bind Viper.Time.DateOnly as DateOnly;
bind Viper.Fmt as Fmt;

func start() {
    var d = DateOnly.Create(2025, 6, 15);
    Say("Year: " + Fmt.Int(d.get_Year()));           // 2025
    Say("Month: " + Fmt.Int(d.get_Month()));         // 6
    Say("Day: " + Fmt.Int(d.get_Day()));             // 15
    Say("ToString: " + d.ToString());                // 2025-06-15
    Say("IsLeapYear: " + Fmt.Bool(d.get_IsLeapYear())); // false

    var d2 = d.AddDays(10);
    Say("AddDays(10): " + d2.ToString());            // 2025-06-25

    var diff = d.DiffDays(d2);
    Say("DiffDays: " + Fmt.Int(diff));               // -10
}
```

### BASIC Example

```basic
' Create a specific date
DIM d AS OBJECT = Viper.Time.DateOnly.Create(2025, 6, 15)

PRINT "Year: "; d.Year           ' Output: 2025
PRINT "Month: "; d.Month         ' Output: 6
PRINT "Day: "; d.Day             ' Output: 15
PRINT "ToString: "; d.ToString() ' Output: 2025-06-15
PRINT "IsLeapYear: "; d.IsLeapYear ' Output: 0

' Date arithmetic
DIM d2 AS OBJECT = d.AddDays(10)
PRINT "AddDays(10): "; d2.ToString() ' Output: 2025-06-25

DIM diff AS INTEGER = d.DiffDays(d2)
PRINT "DiffDays: "; diff             ' Output: -10

' Start/end of month
DIM som AS OBJECT = d.StartOfMonth()
DIM eom AS OBJECT = d.EndOfMonth()
PRINT "Start of month: "; som.ToString()  ' Output: 2025-06-01
PRINT "End of month: "; eom.ToString()    ' Output: 2025-06-30

' Parse from string
DIM parsed AS OBJECT = Viper.Time.DateOnly.Parse("2025-12-25")
PRINT "Parsed: "; parsed.ToString()  ' Output: 2025-12-25

' Get today's date
DIM today AS OBJECT = Viper.Time.DateOnly.Today()
PRINT "Today: "; today.ToString()
```

---

## Viper.Time.Duration

Duration type for representing and manipulating time spans. Duration is a static value type -- it operates on `i64` millisecond values rather than heap-allocated objects.

**Type:** Static utility class (operates on i64 millisecond values)

### Factory Methods

| Method                              | Signature                                       | Description                                  |
|-------------------------------------|-------------------------------------------------|----------------------------------------------|
| `FromMillis(ms)`                    | `Integer(Integer)`                              | Create duration from milliseconds            |
| `FromSeconds(s)`                    | `Integer(Integer)`                              | Create duration from seconds                 |
| `FromMinutes(m)`                    | `Integer(Integer)`                              | Create duration from minutes                 |
| `FromHours(h)`                      | `Integer(Integer)`                              | Create duration from hours                   |
| `FromDays(d)`                       | `Integer(Integer)`                              | Create duration from days                    |
| `Create(days, hours, min, sec, ms)` | `Integer(Integer, Integer, Integer, Integer, Integer)` | Create from individual components    |
| `Zero()`                            | `Integer()`                                     | Returns zero duration (0)                    |

### Total Extraction Methods

| Method              | Signature          | Description                                       |
|---------------------|--------------------|---------------------------------------------------|
| `TotalMillis(dur)`  | `Integer(Integer)` | Returns total milliseconds (identity)             |
| `TotalSeconds(dur)` | `Integer(Integer)` | Returns total whole seconds                       |
| `TotalMinutes(dur)` | `Integer(Integer)` | Returns total whole minutes                       |
| `TotalHours(dur)`   | `Integer(Integer)` | Returns total whole hours                         |
| `TotalDays(dur)`    | `Integer(Integer)` | Returns total whole days                          |
| `TotalSecondsF(dur)`| `Double(Integer)`  | Returns total seconds as floating-point           |

### Component Extraction Methods

| Method          | Signature          | Description                                            |
|-----------------|--------------------|--------------------------------------------------------|
| `Days(dur)`     | `Integer(Integer)` | Days component (remaining after extracting larger)     |
| `Hours(dur)`    | `Integer(Integer)` | Hours component (0-23)                                 |
| `Minutes(dur)`  | `Integer(Integer)` | Minutes component (0-59)                               |
| `Seconds(dur)`  | `Integer(Integer)` | Seconds component (0-59)                               |
| `Millis(dur)`   | `Integer(Integer)` | Milliseconds component (0-999)                         |

### Arithmetic Methods

| Method            | Signature                   | Description                          |
|-------------------|-----------------------------|--------------------------------------|
| `Add(a, b)`       | `Integer(Integer, Integer)` | Add two durations                    |
| `Sub(a, b)`       | `Integer(Integer, Integer)` | Subtract duration b from a           |
| `Mul(dur, factor)` | `Integer(Integer, Integer)` | Multiply duration by integer factor  |
| `Div(dur, divisor)`| `Integer(Integer, Integer)` | Divide duration by integer divisor   |
| `Abs(dur)`        | `Integer(Integer)`          | Absolute value of a duration         |
| `Neg(dur)`        | `Integer(Integer)`          | Negate a duration                    |
| `Cmp(a, b)`       | `Integer(Integer, Integer)` | Compare two durations (-1, 0, or 1)  |

### Formatting Methods

| Method          | Signature          | Description                                      |
|-----------------|--------------------|--------------------------------------------------|
| `ToString(dur)` | `String(Integer)` | Format as "HH:MM:SS" (e.g., "02:00:00")         |
| `ToISO(dur)`    | `String(Integer)` | Format as ISO 8601 duration (e.g., "PT2H")       |

### Notes

- Duration is a value type -- all methods take and return `i64` millisecond values
- No heap allocation is needed; durations are plain integers
- Negative durations are supported and represent time spans in the past
- `TotalSecondsF` returns a `f64` for sub-second precision

### Zia Example

```zia
module DurationDemo;

bind Viper.Terminal;
bind Viper.Time.Duration as Duration;
bind Viper.Fmt as Fmt;

func start() {
    var d = Duration.FromHours(2);
    Say("Millis: " + Fmt.Int(d));                            // 7200000
    Say("TotalMinutes: " + Fmt.Int(Duration.TotalMinutes(d))); // 120
    Say("ToString: " + Duration.ToString(d));                // 02:00:00
    Say("ToISO: " + Duration.ToISO(d));                      // PT2H
}
```

### BASIC Example

```basic
' Create a duration of 2 hours
DIM d AS INTEGER = Viper.Time.Duration.FromHours(2)

PRINT "Millis: "; d                                    ' Output: 7200000
PRINT "TotalMinutes: "; Viper.Time.Duration.TotalMinutes(d) ' Output: 120
PRINT "ToString: "; Viper.Time.Duration.ToString(d)    ' Output: 02:00:00
PRINT "ToISO: "; Viper.Time.Duration.ToISO(d)          ' Output: PT2H

' Create from components
DIM d2 AS INTEGER = Viper.Time.Duration.Create(1, 2, 30, 0, 0)
PRINT "Complex: "; Viper.Time.Duration.ToString(d2)    ' Output: 26:30:00

' Arithmetic
DIM sum AS INTEGER = Viper.Time.Duration.Add(d, d2)
DIM diff AS INTEGER = Viper.Time.Duration.Sub(d2, d)
PRINT "Sum: "; Viper.Time.Duration.ToString(sum)
PRINT "Diff: "; Viper.Time.Duration.ToString(diff)

' Component extraction
PRINT "Hours: "; Viper.Time.Duration.Hours(d2)         ' Output: 2
PRINT "Minutes: "; Viper.Time.Duration.Minutes(d2)     ' Output: 30
```

---

## Viper.Time.DateRange

A time range defined by start and end timestamps (in seconds). Useful for representing time intervals, scheduling windows, and date-based queries.

**Type:** Instance class (requires `New(startTimestamp, endTimestamp)`)

### Constructor

| Method              | Signature                   | Description                                               |
|---------------------|-----------------------------|-----------------------------------------------------------|
| `New(start, end)`   | `DateRange(Integer, Integer)` | Create a date range from start and end Unix timestamps  |

### Properties

| Property | Type                  | Description                                   |
|----------|-----------------------|-----------------------------------------------|
| `Start`  | `Integer` (read-only) | Start timestamp in seconds                    |
| `End`    | `Integer` (read-only) | End timestamp in seconds                      |

### Methods

| Method              | Signature                | Description                                                  |
|---------------------|--------------------------|--------------------------------------------------------------|
| `Contains(ts)`      | `Boolean(Integer)`       | Returns true if timestamp is within the range                |
| `Days()`            | `Integer()`              | Returns the number of whole days in the range                |
| `Hours()`           | `Integer()`              | Returns the number of whole hours in the range               |
| `Duration()`        | `Integer()`              | Returns the duration in seconds                              |
| `Overlaps(other)`   | `Boolean(DateRange)`     | Returns true if this range overlaps with another             |
| `Intersection(other)`| `DateRange(DateRange)`  | Returns the overlapping portion of two ranges                |
| `Union(other)`      | `DateRange(DateRange)`   | Returns the smallest range covering both ranges              |
| `ToString()`        | `String()`               | Returns a string representation of the range                 |

### Notes

- Timestamps are Unix timestamps in seconds (not milliseconds)
- `Contains` checks if `Start <= ts <= End`
- `Intersection` traps if the ranges do not overlap
- `Union` returns a contiguous range from the earliest start to the latest end

### Zia Example

```zia
module DateRangeDemo;

bind Viper.Terminal;
bind Viper.Time.DateRange as DateRange;
bind Viper.Fmt as Fmt;

func start() {
    var r = DateRange.New(1000, 5000);
    Say("Start: " + Fmt.Int(r.get_Start()));        // 1000
    Say("End: " + Fmt.Int(r.get_End()));             // 5000
    Say("Contains 3000: " + Fmt.Bool(r.Contains(3000))); // true
    Say("Duration: " + Fmt.Int(r.Duration()));       // 4000
}
```

### BASIC Example

```basic
' Create a date range
DIM r AS OBJECT = Viper.Time.DateRange.New(1000, 5000)

PRINT "Start: "; r.Start        ' Output: 1000
PRINT "End: "; r.End            ' Output: 5000
PRINT "Contains 3000: "; r.Contains(3000)  ' Output: 1
PRINT "Contains 6000: "; r.Contains(6000)  ' Output: 0
PRINT "Duration: "; r.Duration()           ' Output: 4000
PRINT "Days: "; r.Days()

' Check overlap with another range
DIM r2 AS OBJECT = Viper.Time.DateRange.New(4000, 8000)
PRINT "Overlaps: "; r.Overlaps(r2)  ' Output: 1

' Get intersection
DIM inter AS OBJECT = r.Intersection(r2)
PRINT "Intersection start: "; inter.Start  ' Output: 4000
PRINT "Intersection end: "; inter.End      ' Output: 5000

' Get union
DIM u AS OBJECT = r.Union(r2)
PRINT "Union start: "; u.Start  ' Output: 1000
PRINT "Union end: "; u.End      ' Output: 8000

PRINT r.ToString()
```

---

## Viper.Time.RelativeTime

Formats time durations and timestamps into human-readable relative descriptions (e.g., "5m", "2h", "3d").

**Type:** Static utility class

### Methods

| Method                  | Signature                   | Description                                                        |
|-------------------------|-----------------------------|--------------------------------------------------------------------|
| `Format(timestamp)`     | `String(Integer)`           | Format a timestamp relative to the current time                    |
| `FormatFrom(ts, base)`  | `String(Integer, Integer)`  | Format a timestamp relative to a given base timestamp              |
| `FormatDuration(ms)`    | `String(Integer)`           | Format a duration in milliseconds as a human-readable string       |
| `FormatShort(timestamp)`| `String(Integer)`           | Format a timestamp relative to now using short notation            |

### Notes

- `FormatDuration` produces compact output: "1h", "30m", "5s", "1d"
- `Format` and `FormatShort` compare against the current system time
- Output is always in the largest appropriate unit (e.g., 3600000ms becomes "1h", not "60m")

### Zia Example

```zia
module RelativeTimeDemo;

bind Viper.Terminal;
bind Viper.Time.RelativeTime as RT;

func start() {
    Say(RT.FormatDuration(3600000));   // 1h
    Say(RT.FormatDuration(86400000));  // 1d
    Say(RT.FormatDuration(60000));     // 1m
}
```

### BASIC Example

```basic
' Format durations as human-readable strings
PRINT Viper.Time.RelativeTime.FormatDuration(3600000)   ' Output: 1h
PRINT Viper.Time.RelativeTime.FormatDuration(86400000)  ' Output: 1d
PRINT Viper.Time.RelativeTime.FormatDuration(60000)     ' Output: 1m
PRINT Viper.Time.RelativeTime.FormatDuration(5000)      ' Output: 5s

' Format relative to a base timestamp
DIM now AS INTEGER = Viper.Time.DateTime.Now()
DIM past AS INTEGER = now - 3600  ' 1 hour ago
PRINT Viper.Time.RelativeTime.FormatFrom(past, now)
```

---

## See Also

- [Threads](threads.md) - `Thread.Sleep()` and synchronization with timeouts
- [Graphics](graphics.md) - Frame timing with `Canvas` game loops
