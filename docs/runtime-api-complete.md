# Viper Runtime API - Complete Reference

This document provides comprehensive documentation for all Viper runtime namespaces. It supplements the main runtime reference in `bible/appendices/d-runtime-reference.md`.

**Auto-generated from:** `src/il/runtime/runtime.def`
**Total Functions:** 2,902
**Total Classes:** 197

> **Note:** This document covers a subset of the runtime API. Many newer namespaces and functions
> are not yet documented here. See `src/il/runtime/runtime.def` for the complete list.

---

## Table of Contents

- [Viper.Collections](#vipercollections) - Data structures (Bytes, Seq, List, Map, etc.)
- [Viper.Core.Box](#vipercorebox) - Value boxing/unboxing
- [Viper.Core.Convert](#vipercoreconvert) - Type conversions
- [Viper.Core.Diagnostics](#vipercorediagnostics) - Assertions and debugging
- [Viper.Core.Parse](#vipercoreparse) - String parsing
- [Viper.Exec](#viperexec) - Process execution
- [Viper.Fmt](#viperfmt) - Number formatting
- [Viper.IO](#viperio) - File and directory operations
- [Viper.Log](#viperlog) - Logging utilities
- [Viper.Machine](#vipermachine) - System information
- [Viper.Math.Bits](#vipermathbits) - Bitwise operations
- [Viper.Math.Random](#vipermathrandom) - Random number generation
- [Viper.Math.Vec2](#vipermathvec2) - 2D vector math
- [Viper.Math.Vec3](#vipermathvec3) - 3D vector math
- [Viper.Sound](#vipersound) - Audio playback
- [Viper.String](#viperstring) - String manipulation
- [Viper.Text](#vipertext) - Text processing utilities
- [Viper.Time.DateTime](#vipertimedatetime) - Date and time operations
- [Viper.Time.Stopwatch](#vipertimestopwatch) - High-precision timing

> **Note:** The table of contents lists sections alphabetically by namespace. Sections in the document
> body appear in the original authoring order; use the links above to navigate.

---

## Viper.Math.Bits

Bitwise operations for low-level integer manipulation. All functions work with 64-bit integers.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `And(a, b)` | `i64(i64, i64)` | Bitwise AND of two integers |
| `Clear(x, pos)` | `i64(i64, i64)` | Clear bit at position to 0 |
| `Count(x)` | `i64(i64)` | Count number of set bits (popcount) |
| `Flip(x)` | `i64(i64)` | Flip all bits (bitwise complement) |
| `Get(x, pos)` | `i1(i64, i64)` | Get bit at position (0-63) |
| `LeadZ(x)` | `i64(i64)` | Count leading zero bits |
| `Not(x)` | `i64(i64)` | Bitwise NOT (one's complement) |
| `Or(a, b)` | `i64(i64, i64)` | Bitwise OR of two integers |
| `Rotl(x, n)` | `i64(i64, i64)` | Rotate left by n bits |
| `Rotr(x, n)` | `i64(i64, i64)` | Rotate right by n bits |
| `Set(x, pos)` | `i64(i64, i64)` | Set bit at position to 1 |
| `Shl(x, n)` | `i64(i64, i64)` | Shift left by n bits |
| `Shr(x, n)` | `i64(i64, i64)` | Arithmetic shift right by n bits |
| `Swap(x)` | `i64(i64)` | Byte-swap (endian conversion) |
| `Toggle(x, pos)` | `i64(i64, i64)` | Toggle bit at position |
| `TrailZ(x)` | `i64(i64)` | Count trailing zero bits |
| `Ushr(x, n)` | `i64(i64, i64)` | Logical (unsigned) shift right |
| `Xor(a, b)` | `i64(i64, i64)` | Bitwise XOR of two integers |

### Examples

```zia
// Bitwise operations
var flags = Viper.Math.Bits.Or(0x01, 0x04);  // flags = 0x05
var masked = Viper.Math.Bits.And(flags, 0x0F);

// Bit manipulation
var value = 0;
value = Viper.Math.Bits.Set(value, 3);    // Set bit 3: value = 8
var isSet = Viper.Math.Bits.Get(value, 3); // true

// Counting
var ones = Viper.Math.Bits.Count(0xFF);   // 8 set bits
var leading = Viper.Math.Bits.LeadZ(0x0F); // 60 leading zeros
```

---

## Viper.Core.Box

Boxing and unboxing of primitive values to/from objects. Used for storing primitives in collections.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `EqF64(box, val)` | `i1(obj, f64)` | Compare box with float |
| `EqI64(box, val)` | `i1(obj, i64)` | Compare box with integer |
| `EqStr(box, val)` | `i1(obj, str)` | Compare box with string |
| `F64(value)` | `obj(f64)` | Box a float |
| `I1(value)` | `obj(i64)` | Box a boolean (as integer) |
| `I64(value)` | `obj(i64)` | Box an integer |
| `Str(value)` | `obj(str)` | Box a string |
| `ToF64(box)` | `f64(obj)` | Unbox to float |
| `ToI1(box)` | `i64(obj)` | Unbox to boolean |
| `ToI64(box)` | `i64(obj)` | Unbox to integer |
| `ToStr(box)` | `str(obj)` | Unbox to string |
| `Type(box)` | `i64(obj)` | Get boxed value type tag |
| `ValueType(size)` | `obj(i64)` | Allocate heap memory for a value type |

### Type Constants

- `0` = Integer (i64)
- `1` = Float (f64)
- `2` = Boolean (i1)
- `3` = String (str)

### Examples

```zia
// Boxing values for heterogeneous collections
var boxedInt = Viper.Core.Box.I64(42);
var boxedStr = Viper.Core.Box.Str("hello");

// Type checking
var type = Viper.Core.Box.Type(boxedInt);  // 0 (integer)

// Unboxing
var value = Viper.Core.Box.ToI64(boxedInt);  // 42
```

---

## Viper.Core.Convert

Type conversion functions between strings and numeric types.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `NumToInt(n)` | `i64(f64)` | Convert float to integer (truncate toward zero) |
| `ToDouble(s)` | `f64(str)` | Parse string to float |
| `ToInt(s)` | `i64(str)` | Parse string to integer |
| `ToInt64(s)` | `i64(str)` | Parse string to 64-bit integer |
| `ToString_Double(n)` | `str(f64)` | Convert float to string |
| `ToString_Int(n)` | `str(i64)` | Convert integer to string |

### Examples

```zia
var n = Viper.Core.Convert.ToInt("42");        // 42
var f = Viper.Core.Convert.ToDouble("3.14");   // 3.14
var s = Viper.Core.Convert.ToString_Int(100);  // "100"
```

---

## Viper.Time.DateTime

Date and time operations using Unix timestamps (seconds since epoch).

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `AddDays(ts, days)` | `i64(i64, i64)` | Add days to timestamp |
| `AddSeconds(ts, secs)` | `i64(i64, i64)` | Add seconds to timestamp |
| `Create(y,m,d,h,min,s)` | `i64(i64,i64,i64,i64,i64,i64)` | Create timestamp from components |
| `Day(ts)` | `i64(i64)` | Extract day (1-31) |
| `DayOfWeek(ts)` | `i64(i64)` | Day of week (0=Sunday, 6=Saturday) |
| `Diff(ts1, ts2)` | `i64(i64, i64)` | Difference in seconds |
| `Format(ts, fmt)` | `str(i64, str)` | Format timestamp using strftime specifiers |
| `Hour(ts)` | `i64(i64)` | Extract hour (0-23) |
| `Minute(ts)` | `i64(i64)` | Extract minute (0-59) |
| `Month(ts)` | `i64(i64)` | Extract month (1-12) |
| `Now()` | `i64()` | Current Unix timestamp (seconds) |
| `NowMs()` | `i64()` | Current Unix timestamp (milliseconds) |
| `ParseDate(s)` | `i64(str)` | Parse date string to timestamp |
| `ParseISO(s)` | `i64(str)` | Parse ISO 8601 string to timestamp |
| `ParseTime(s)` | `i64(str)` | Parse time string to timestamp |
| `Second(ts)` | `i64(i64)` | Extract second (0-59) |
| `ToISO(ts)` | `str(i64)` | Convert to ISO 8601 string |
| `ToLocal(ts)` | `str(i64)` | Convert to local time string |
| `TryParse(s)` | `i64(str)` | Try parse date/time string, return timestamp or -1 |
| `Year(ts)` | `i64(i64)` | Extract year |

### Format Specifiers

- `%Y` - Year (4 digits)
- `%m` - Month (01-12)
- `%d` - Day (01-31)
- `%H` - Hour (00-23)
- `%M` - Minute (00-59)
- `%S` - Second (00-59)

### Examples

```zia
var now = Viper.Time.DateTime.Now();
var year = Viper.Time.DateTime.Year(now);
var formatted = Viper.Time.DateTime.Format(now, "%Y-%m-%d %H:%M:%S");
var iso = Viper.Time.DateTime.ToISO(now);  // "2024-01-15T10:30:00Z"

var tomorrow = Viper.Time.DateTime.AddDays(now, 1);
var future = Viper.Time.DateTime.Create(2025, 6, 15, 12, 0, 0);
```

---

## Viper.Core.Diagnostics

Assertion functions and debugging utilities. Stopwatch (timing) is in `Viper.Time.Stopwatch`.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Assert(cond, msg)` | `void(i1, str)` | Assert condition is true |
| `AssertEq(a, b, msg)` | `void(i64, i64, str)` | Assert integers equal |
| `AssertEqNum(a, b, msg)` | `void(f64, f64, str)` | Assert floats equal |
| `AssertEqStr(a, b, msg)` | `void(str, str, str)` | Assert strings equal |
| `AssertFail(msg)` | `void(str)` | Always fail with message |
| `AssertGt(a, b, msg)` | `void(i64, i64, str)` | Assert a > b |
| `AssertGte(a, b, msg)` | `void(i64, i64, str)` | Assert a >= b |
| `AssertLt(a, b, msg)` | `void(i64, i64, str)` | Assert a < b |
| `AssertLte(a, b, msg)` | `void(i64, i64, str)` | Assert a <= b |
| `AssertNeq(a, b, msg)` | `void(i64, i64, str)` | Assert integers not equal |
| `AssertNotNull(obj, msg)` | `void(obj, str)` | Assert object is not null |
| `AssertNull(obj, msg)` | `void(obj, str)` | Assert object is null |
| `Trap(msg)` | `void(str)` | Trigger runtime trap with message |

### Examples

```zia
// Assertions
Viper.Core.Diagnostics.Assert(x > 0, "x must be positive");
Viper.Core.Diagnostics.AssertEq(result, expected, "result mismatch");
Viper.Core.Diagnostics.AssertNotNull(obj, "object is null");

// Trigger a fatal trap
Viper.Core.Diagnostics.Trap("unreachable code reached");
```

---

## Viper.Exec

Execute external processes and capture output.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Run(cmd)` | `i64(str)` | Run command, return exit code |
| `RunArgs(cmd, args)` | `i64(str, obj)` | Run with arguments list |
| `Capture(cmd)` | `str(str)` | Run and capture stdout |
| `CaptureArgs(cmd, args)` | `str(str, obj)` | Run with args, capture stdout |
| `Shell(cmd)` | `i64(str)` | Run via shell (sh -c) |
| `ShellCapture(cmd)` | `str(str)` | Shell command, capture output |

### Examples

```zia
// Run command
var exitCode = Viper.Exec.Run("ls");

// Capture output
var output = Viper.Exec.Capture("date");
Viper.Terminal.Say("Date: " + output);

// With arguments
var args = Viper.Collections.List.New();
Viper.Collections.List.Push(args, "-l");
Viper.Collections.List.Push(args, "-a");
var listing = Viper.Exec.CaptureArgs("ls", args);

// Shell command (supports pipes, redirects)
var result = Viper.Exec.ShellCapture("cat /etc/passwd | grep root");
```

---

## Viper.Fmt

Number formatting utilities.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Bin(n)` | `str(i64)` | Format integer as binary string (no prefix) |
| `Bool(v)` | `str(i1)` | Format boolean as "true" or "false" |
| `BoolYN(v)` | `str(i1)` | Format boolean as "Yes" or "No" |
| `Currency(n, decimals, symbol)` | `str(f64, i64, str)` | Format as currency (e.g., "$1,234.56") |
| `Hex(n)` | `str(i64)` | Format integer as lowercase hex (no prefix) |
| `HexPad(n, width)` | `str(i64, i64)` | Format as padded hex (e.g., "00ff") |
| `Int(n)` | `str(i64)` | Format integer as decimal string |
| `IntGrouped(n, sep)` | `str(i64, str)` | Format with thousands separator |
| `IntPad(n, width, pad)` | `str(i64, i64, str)` | Format with left padding |
| `IntRadix(n, radix)` | `str(i64, i64)` | Format in base 2-36 |
| `Num(n)` | `str(f64)` | Format float with default precision |
| `NumFixed(n, decimals)` | `str(f64, i64)` | Format with fixed decimal places |
| `NumPct(n, decimals)` | `str(f64, i64)` | Format as percentage (e.g., "50.00%") |
| `NumSci(n, decimals)` | `str(f64, i64)` | Format in scientific notation |
| `Oct(n)` | `str(i64)` | Format integer as octal string (no prefix) |
| `Ordinal(n)` | `str(i64)` | Format as ordinal (e.g., "1st", "2nd") |
| `Size(n)` | `str(i64)` | Format byte count as human-readable size |
| `ToWords(n)` | `str(i64)` | Convert integer to English words |

### Examples

```zia
var hex = Viper.Fmt.IntRadix(255, 16);      // "ff"
var bin = Viper.Fmt.IntRadix(10, 2);        // "1010"
var padded = Viper.Fmt.IntPad(42, 5, "0");  // "00042"
var fixed = Viper.Fmt.NumFixed(3.14159, 2); // "3.14"
var size = Viper.Fmt.Size(1536);            // "1.5 KB"
var grp = Viper.Fmt.IntGrouped(1234567, ","); // "1,234,567"
var words = Viper.Fmt.ToWords(42);          // "forty-two"
```

---

## Viper.IO

File and directory operations.

### Viper.IO.File

| Function | Signature | Description |
|----------|-----------|-------------|
| `Exists(path)` | `bool(str)` | Check if file exists |
| `Delete(path)` | `void(str)` | Delete file |
| `Copy(src, dst)` | `void(str, str)` | Copy file |
| `Move(src, dst)` | `void(str, str)` | Move/rename file |
| `Touch(path)` | `void(str)` | Create empty file or update mtime |
| `Size(path)` | `i64(str)` | Get file size in bytes |
| `Modified(path)` | `i64(str)` | Get modification timestamp |
| `ReadAllText(path)` | `str(str)` | Read entire file as string |
| `ReadAllBytes(path)` | `obj(str)` | Read file as byte array |
| `ReadAllLines(path)` | `obj(str)` | Read file as list of lines |
| `WriteAllText(path, text)` | `void(str, str)` | Write string to file |
| `WriteAllBytes(path, data)` | `void(str, obj)` | Write bytes to file |
| `Append(path, text)` | `void(str, str)` | Append text to file |
| `AppendLine(path, text)` | `void(str, str)` | Append line to file |

### Viper.IO.Dir

| Function | Signature | Description |
|----------|-----------|-------------|
| `Exists(path)` | `bool(str)` | Check if directory exists |
| `Make(path)` | `void(str)` | Create directory |
| `MakeAll(path)` | `void(str)` | Create directory and parents |
| `Remove(path)` | `void(str)` | Remove empty directory |
| `RemoveAll(path)` | `void(str)` | Remove directory recursively |
| `Move(src, dst)` | `void(str, str)` | Move/rename directory |
| `Current()` | `str()` | Get current working directory |
| `SetCurrent(path)` | `void(str)` | Change working directory |
| `List(path)` | `ptr(str)` | List all entries |
| `Files(path)` | `ptr(str)` | List files only |
| `Dirs(path)` | `ptr(str)` | List subdirectories only |
| `Entries(path)` | `obj(str)` | List entries as sequence |

### Viper.IO.Path

| Function | Signature | Description |
|----------|-----------|-------------|
| `Join(a, b)` | `str(str, str)` | Join path components |
| `Dir(path)` | `str(str)` | Get directory part |
| `Name(path)` | `str(str)` | Get filename with extension |
| `Stem(path)` | `str(str)` | Get filename without extension |
| `Ext(path)` | `str(str)` | Get extension (with dot) |
| `WithExt(path, ext)` | `str(str, str)` | Replace extension |
| `Abs(path)` | `str(str)` | Get absolute path |
| `Norm(path)` | `str(str)` | Normalize path (remove . and ..) |
| `IsAbs(path)` | `bool(str)` | Check if path is absolute |
| `Sep()` | `str()` | Get path separator ("/" or "\\") |

### Viper.IO.BinFile

Binary file operations with random access.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Open(path, mode)` | `obj(str, str)` | Open file ("r", "w", "rw") |
| `Close(file)` | `void(obj)` | Close file |
| `Flush(file)` | `void(obj)` | Flush buffers |
| `get_Eof` | `bool(obj)` | Check if at end of file |
| `get_Pos` | `i64(obj)` | Get current position |
| `Seek(file, pos)` | `void(obj, i64)` | Seek to position |
| `ReadI8/I16/I32/I64` | various | Read integers |
| `ReadF32/F64` | various | Read floats |
| `WriteI8/I16/I32/I64` | various | Write integers |
| `WriteF32/F64` | various | Write floats |

### Examples

```zia
// File operations
if (Viper.IO.File.Exists("data.txt")) {
    var content = Viper.IO.File.ReadAllText("data.txt");
    Viper.IO.File.WriteAllText("backup.txt", content);
}

// Directory operations
Viper.IO.Dir.MakeAll("/tmp/myapp/data");
var files = Viper.IO.Dir.Files("/tmp/myapp");

// Path manipulation
var full = Viper.IO.Path.Join("/home/user", "documents/file.txt");
var dir = Viper.IO.Path.Dir(full);   // "/home/user/documents"
var name = Viper.IO.Path.Name(full); // "file.txt"
var stem = Viper.IO.Path.Stem(full); // "file"
var ext = Viper.IO.Path.Ext(full);   // ".txt"
```

---

## Viper.Log

Logging utilities with configurable log levels.

### Log Levels

| Constant | Value | Description |
|----------|-------|-------------|
| `DEBUG` | 0 | Debug messages |
| `INFO` | 1 | Informational messages |
| `WARN` | 2 | Warnings |
| `ERROR` | 3 | Errors |
| `OFF` | 4 | Disable logging |

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Debug(msg)` | `void(str)` | Log debug message |
| `Info(msg)` | `void(str)` | Log info message |
| `Warn(msg)` | `void(str)` | Log warning message |
| `Error(msg)` | `void(str)` | Log error message |
| `get_Level` | `i64()` | Get current log level |
| `set_Level(level)` | `void(i64)` | Set log level |
| `Enabled(level)` | `bool(i64)` | Check if level is enabled |

### Examples

```zia
// Set log level
Viper.Log.set_Level(Viper.Log.get_DEBUG);

// Log messages
Viper.Log.Debug("Processing item " + Viper.Core.Convert.ToString_Int(i));
Viper.Log.Info("Server started on port 8080");
Viper.Log.Warn("Connection pool running low");
Viper.Log.Error("Failed to connect to database");
```

---

## Viper.Machine

System and hardware information.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `get_OS` | `str()` | Operating system name |
| `get_OSVer` | `str()` | OS version string |
| `get_Cores` | `i64()` | Number of CPU cores |
| `get_MemTotal` | `i64()` | Total system memory (bytes) |
| `get_MemFree` | `i64()` | Available memory (bytes) |
| `get_Endian` | `str()` | Byte order ("little" or "big") |
| `get_Host` | `str()` | Hostname |
| `get_User` | `str()` | Current username |
| `get_Home` | `str()` | User home directory |
| `get_Temp` | `str()` | Temp directory path |

### Examples

```zia
Viper.Terminal.Say("OS: " + Viper.Machine.get_OS);
Viper.Terminal.Say("Cores: " + Viper.Core.Convert.ToString_Int(Viper.Machine.get_Cores));
Viper.Terminal.Say("Memory: " + Viper.Core.Convert.ToString_Int(Viper.Machine.get_MemTotal / 1048576) + " MB");
```

---

## Viper.Core.Parse

Safe string parsing with error handling.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `BoolOr(s, default)` | `i1(str, i1)` | Parse boolean or return default |
| `IntOr(s, default)` | `i64(str, i64)` | Parse integer or return default |
| `IntRadix(s, radix, default)` | `i64(str, i64, i64)` | Parse integer in given base |
| `IsInt(s)` | `i1(str)` | Check if string is a valid integer |
| `IsNum(s)` | `i1(str)` | Check if string is a valid number |
| `NumOr(s, default)` | `f64(str, f64)` | Parse float or return default |
| `TryBool(s, out)` | `i1(str, ptr)` | Try parse boolean, write result to ptr |
| `TryInt(s, out)` | `i1(str, ptr)` | Try parse integer, write result to ptr |
| `TryNum(s, out)` | `i1(str, ptr)` | Try parse float, write result to ptr |

### Examples

```zia
// Safe parsing with defaults
var port = Viper.Core.Parse.IntOr(portStr, 8080);
var timeout = Viper.Core.Parse.NumOr(timeoutStr, 30.0);
var enabled = Viper.Core.Parse.BoolOr(enabledStr, false);

// Validation
if (Viper.Core.Parse.IsInt(input)) {
    var value = Viper.Core.Convert.ToInt(input);
}

// Parse hex
var color = Viper.Core.Parse.IntRadix("FF00FF", 16, 0);
```

---

## Viper.Math.Random

Deterministic pseudo-random number generation using a 64-bit LCG (Knuth MMIX constants). Produces identical sequences across all platforms for a given seed.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Chance(p)` | `i64(f64)` | Return 1 with probability p (0.0-1.0), else 0 |
| `Dice(sides)` | `i64(i64)` | Random integer in [1, sides] |
| `Exponential(lambda)` | `f64(f64)` | Random value from exponential distribution |
| `Gaussian(mean, stddev)` | `f64(f64, f64)` | Random value from normal distribution |
| `New(seed)` | `obj(i64)` | Create a Random instance seeded with given value |
| `Next()` | `f64()` | Random float in [0.0, 1.0) |
| `NextInt(max)` | `i64(i64)` | Random integer in [0, max) |
| `Range(min, max)` | `i64(i64, i64)` | Random integer in [min, max] (inclusive) |
| `Seed(value)` | `void(i64)` | Seed the global generator |
| `Shuffle(seq)` | `void(obj)` | Fisher-Yates shuffle a Seq in place |

### Examples

```zia
// Seed for reproducibility
Viper.Math.Random.Seed(12345);

// Random values
var roll = Viper.Math.Random.Dice(6);           // Dice roll 1-6
var chance = Viper.Math.Random.Next();           // 0.0 to 1.0
var percent = Viper.Math.Random.NextInt(100);    // 0 to 99
var range = Viper.Math.Random.Range(10, 20);     // 10 to 20
var coin = Viper.Math.Random.Chance(0.5);        // 50% true
```

---

## Viper.Sound

Audio playback for sound effects and music.

### Viper.Sound.Audio (Global)

| Function | Signature | Description |
|----------|-----------|-------------|
| `Init()` | `i64()` | Initialize audio system |
| `Shutdown()` | `void()` | Shutdown audio |
| `SetMasterVolume(vol)` | `void(i64)` | Set master volume (0-100) |
| `GetMasterVolume()` | `i64()` | Get master volume |
| `PauseAll()` | `void()` | Pause all playback |
| `ResumeAll()` | `void()` | Resume all playback |
| `StopAllSounds()` | `void()` | Stop all sounds |

### Viper.Sound.Sound (Effects)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Load(path)` | `obj(str)` | Load sound file |
| `Free(sound)` | `void(obj)` | Free sound |
| `Play(sound)` | `i64(obj)` | Play sound, return voice ID |
| `PlayEx(sound, vol, pan)` | `i64(obj, i64, i64)` | Play with volume/pan |
| `PlayLoop(sound, vol, pan)` | `i64(obj, i64, i64)` | Play looping |

### Viper.Sound.Voice (Playback Control)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Stop(voice)` | `void(i64)` | Stop voice |
| `SetVolume(voice, vol)` | `void(i64, i64)` | Set voice volume |
| `SetPan(voice, pan)` | `void(i64, i64)` | Set voice pan (-100 to 100) |
| `IsPlaying(voice)` | `bool(i64)` | Check if playing |

### Viper.Sound.Music (Streaming)

| Method | Signature | Description |
|--------|-----------|-------------|
| `Load(path)` | `obj(str)` | Load music file |
| `Free(music)` | `void(obj)` | Free music |
| `Play(music, loops)` | `void(obj, i64)` | Play music (-1=infinite) |
| `Stop(music)` | `void(obj)` | Stop music |
| `Pause(music)` | `void(obj)` | Pause music |
| `Resume(music)` | `void(obj)` | Resume music |
| `SetVolume(music, vol)` | `void(obj, i64)` | Set volume |
| `IsPlaying(music)` | `bool(obj)` | Check if playing |
| `Seek(music, ms)` | `void(obj, i64)` | Seek to position |
| `get_Position` | `i64(obj)` | Get position in ms |
| `get_Duration` | `i64(obj)` | Get duration in ms |

### Examples

```zia
// Initialize audio
Viper.Sound.Audio.Init();

// Load and play sound effect
var explosion = Viper.Sound.Sound.Load("assets/explosion.wav");
Viper.Sound.Sound.Play(explosion);

// Background music
var bgm = Viper.Sound.Music.Load("assets/music.ogg");
Viper.Sound.Music.Play(bgm, -1);  // Loop forever
Viper.Sound.Music.SetVolume(bgm, 50);
```

---

## Viper.String

Core string manipulation functions.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Asc(s)` | `i64(str)` | Code point of first character |
| `Chr(code)` | `str(i64)` | Character from Unicode code point |
| `Cmp(a, b)` | `i64(str, str)` | Lexicographic compare (-1, 0, 1) |
| `CmpNoCase(a, b)` | `i64(str, str)` | Case-insensitive compare (-1, 0, 1) |
| `Concat(a, b)` | `str(str, str)` | Concatenate two strings |
| `Count(s, sub)` | `i64(str, str)` | Count non-overlapping occurrences of sub |
| `EndsWith(s, suffix)` | `i1(str, str)` | Check if string ends with suffix |
| `Flip(s)` | `str(str)` | Reverse string |
| `get_IsEmpty(s)` | `i1(str)` | Check if empty (length == 0) |
| `get_Length(s)` | `i64(str)` | Get string length in characters |
| `Has(s, sub)` | `i1(str, str)` | Check if string contains substring |
| `IndexOf(s, sub)` | `i64(str, str)` | Find first occurrence (-1 if not found) |
| `IndexOfFrom(s, start, sub)` | `i64(str, i64, str)` | Find from position |
| `Left(s, n)` | `str(str, i64)` | Get leftmost n characters |
| `Mid(s, start)` | `str(str, i64)` | Substring from position to end |
| `MidLen(s, start, len)` | `str(str, i64, i64)` | Substring with explicit length |
| `PadLeft(s, width, pad)` | `str(str, i64, str)` | Left-pad to minimum width |
| `PadRight(s, width, pad)` | `str(str, i64, str)` | Right-pad to minimum width |
| `Repeat(s, n)` | `str(str, i64)` | Repeat string n times |
| `Replace(s, old, new)` | `str(str, str, str)` | Replace all occurrences of old with new |
| `Right(s, n)` | `str(str, i64)` | Get rightmost n characters |
| `Split(s, delim)` | `obj(str, str)` | Split into Seq of strings |
| `StartsWith(s, prefix)` | `i1(str, str)` | Check if string starts with prefix |
| `Substring(s, start, end)` | `str(str, i64, i64)` | Substring by start/end indices |
| `ToLower(s)` | `str(str)` | Convert to lowercase |
| `ToUpper(s)` | `str(str)` | Convert to uppercase |
| `Trim(s)` | `str(str)` | Trim whitespace from both ends |
| `TrimEnd(s)` | `str(str)` | Trim trailing whitespace |
| `TrimStart(s)` | `str(str)` | Trim leading whitespace |

### Examples

```zia
var s = "  Hello, World!  ";
var trimmed = Viper.String.Trim(s);        // "Hello, World!"
var upper = Viper.String.ToUpper(trimmed); // "HELLO, WORLD!"
var words = Viper.String.Split(trimmed, " ");
var has = Viper.String.Has(s, "World");    // true
var pos = Viper.String.IndexOf(s, "World"); // 9

var padded = Viper.String.PadLeft("42", 5, "0");  // "00042"
var repeated = Viper.String.Repeat("-", 20);      // "--------------------"
```

---

## Viper.String (Additional Utilities)

Additional string utilities and conversion functions. All are part of the `Viper.String` namespace.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Equals(a, b)` | `i1(str, str)` | String equality check |
| `FromI16(n)` | `str(i16)` | 16-bit integer to string |
| `FromI32(n)` | `str(i32)` | 32-bit integer to string |
| `FromSingle(n)` | `str(f64)` | Format float using single-precision display format |
| `FromStr(s)` | `str(str)` | Clone/copy string |
| `Join(sep, list)` | `str(str, obj)` | Join a Seq of strings with separator |
| `SplitFields(s, arr, max)` | `i64(str, ptr, i64)` | Split on whitespace fields into raw array |

> **Note**: For integer/double to string conversion, use `Viper.Core.Convert.ToString_Int` and `Viper.Core.Convert.ToString_Double`.

---

## Viper.Text

Text processing utilities including encoding, CSV, GUID, templates, and patterns.

### Viper.Text.Codec

| Function | Signature | Description |
|----------|-----------|-------------|
| `Base64Enc(s)` | `str(str)` | Encode to Base64 |
| `Base64Dec(s)` | `str(str)` | Decode from Base64 |
| `HexEnc(s)` | `str(str)` | Encode to hex string |
| `HexDec(s)` | `str(str)` | Decode from hex string |
| `UrlEncode(s)` | `str(str)` | URL-encode string |
| `UrlDecode(s)` | `str(str)` | URL-decode string |

### Viper.Text.Csv

| Function | Signature | Description |
|----------|-----------|-------------|
| `Parse(text)` | `obj(str)` | Parse CSV to list of lists |
| `ParseWith(text, delim)` | `obj(str, str)` | Parse with custom delimiter |
| `ParseLine(line)` | `obj(str)` | Parse single CSV line |
| `ParseLineWith(line, delim)` | `obj(str, str)` | Parse line with delimiter |
| `Format(data)` | `str(obj)` | Format list of lists to CSV |
| `FormatWith(data, delim)` | `str(obj, str)` | Format with custom delimiter |
| `FormatLine(row)` | `str(obj)` | Format single row |
| `FormatLineWith(row, delim)` | `str(obj, str)` | Format row with delimiter |

### Viper.Text.Guid

| Function | Signature | Description |
|----------|-----------|-------------|
| `New()` | `str()` | Generate new UUID v4 |
| `get_Empty` | `str()` | Get empty GUID (all zeros) |
| `IsValid(s)` | `bool(str)` | Validate GUID format |
| `ToBytes(s)` | `obj(str)` | Convert to 16 bytes |
| `FromBytes(b)` | `str(obj)` | Convert from 16 bytes |

### Viper.Text.StringBuilder

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create new StringBuilder |
| `Append(sb, s)` | `obj(obj, str)` | Append string |
| `AppendLine(sb, s)` | `obj(obj, str)` | Append string + newline |
| `Clear(sb)` | `void(obj)` | Clear contents |
| `ToString(sb)` | `str(obj)` | Get built string |
| `get_Length` | `i64(obj)` | Current length |
| `get_Capacity` | `i64(obj)` | Current capacity |

### Viper.Text.Pattern (Regex)

| Function | Signature | Description |
|----------|-----------|-------------|
| `IsMatch(text, pattern)` | `bool(str, str)` | Test if pattern matches |
| `Find(text, pattern)` | `str(str, str)` | Find first match |
| `FindFrom(text, pattern, start)` | `str(str, str, i64)` | Find from position |
| `FindPos(text, pattern)` | `i64(str, str)` | Find position of match |
| `FindAll(text, pattern)` | `obj(str, str)` | Find all matches |
| `Replace(text, pattern, repl)` | `str(str, str, str)` | Replace all matches |
| `ReplaceFirst(text, pattern, repl)` | `str(str, str, str)` | Replace first match |
| `Split(text, pattern)` | `obj(str, str)` | Split by pattern |
| `Escape(s)` | `str(str)` | Escape regex metacharacters |

### Viper.Text.Template

Simple string templating with `{{key}}` placeholders.

| Function | Signature | Description |
|----------|-----------|-------------|
| `Render(template, map)` | `str(str, obj)` | Render with map |
| `RenderSeq(template, seq)` | `str(str, obj)` | Render with sequence |
| `RenderWith(template, map, open, close)` | `str(str, obj, str, str)` | Custom delimiters |
| `Has(template, key)` | `bool(str, str)` | Check for placeholder |
| `Keys(template)` | `obj(str)` | Get all placeholder names |
| `Escape(s)` | `str(str)` | Escape template syntax |

### Examples

```zia
// Base64 encoding
var encoded = Viper.Text.Codec.Base64Enc("Hello, World!");
var decoded = Viper.Text.Codec.Base64Dec(encoded);

// CSV parsing
var csv = "name,age,city\nAlice,30,NYC\nBob,25,LA";
var data = Viper.Text.Csv.Parse(csv);

// GUID generation
var id = Viper.Text.Guid.New();  // "550e8400-e29b-41d4-a716-446655440000"

// StringBuilder for efficient string building
var sb = Viper.Text.StringBuilder.New();
Viper.Text.StringBuilder.Append(sb, "Hello");
Viper.Text.StringBuilder.AppendLine(sb, " World!");
var result = Viper.Text.StringBuilder.ToString(sb);

// Pattern matching
var emails = Viper.Text.Pattern.FindAll(text, "[a-z]+@[a-z]+\\.[a-z]+");
var cleaned = Viper.Text.Pattern.Replace(html, "<[^>]+>", "");

// Templating
var template = "Hello, {{name}}! You have {{count}} messages.";
var vars = Viper.Collections.Map.New();
Viper.Collections.Map.SetStr(vars, "name", "Alice");
Viper.Collections.Map.SetStr(vars, "count", "5");
var message = Viper.Text.Template.Render(template, vars);
```

---

## Viper.Math.Vec2

2D vector mathematics for games and graphics.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Add(a, b)` | `obj(obj, obj)` | Vector addition |
| `Angle(v)` | `f64(obj)` | Angle in radians |
| `Cross(a, b)` | `f64(obj, obj)` | Cross product (2D: scalar result) |
| `Dist(a, b)` | `f64(obj, obj)` | Distance between vectors |
| `Div(v, scalar)` | `obj(obj, f64)` | Scalar division |
| `Dot(a, b)` | `f64(obj, obj)` | Dot product |
| `get_X(v)` | `f64(obj)` | Get X component |
| `get_Y(v)` | `f64(obj)` | Get Y component |
| `Len(v)` | `f64(obj)` | Vector length (magnitude) |
| `LenSq(v)` | `f64(obj)` | Length squared |
| `Lerp(a, b, t)` | `obj(obj, obj, f64)` | Linear interpolation |
| `Mul(v, scalar)` | `obj(obj, f64)` | Scalar multiplication |
| `Neg(v)` | `obj(obj)` | Negate vector |
| `New(x, y)` | `obj(f64, f64)` | Create vector |
| `Norm(v)` | `obj(obj)` | Normalize to unit length |
| `One()` | `obj()` | Unit vector (1, 1) |
| `Rotate(v, angle)` | `obj(obj, f64)` | Rotate by angle (radians) |
| `Sub(a, b)` | `obj(obj, obj)` | Vector subtraction |
| `Zero()` | `obj()` | Zero vector (0, 0) |

### Examples

```zia
var pos = Viper.Math.Vec2.New(100.0, 200.0);
var vel = Viper.Math.Vec2.New(5.0, -3.0);

// Movement
pos = Viper.Math.Vec2.Add(pos, vel);

// Distance calculation
var target = Viper.Math.Vec2.New(300.0, 400.0);
var dist = Viper.Math.Vec2.Dist(pos, target);

// Normalize velocity
var dir = Viper.Math.Vec2.Norm(vel);
var speed = Viper.Math.Vec2.Len(vel);

// Interpolation
var midpoint = Viper.Math.Vec2.Lerp(pos, target, 0.5);
```

---

## Viper.Math.Vec3

3D vector mathematics.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Add(a, b)` | `obj(obj, obj)` | Vector addition |
| `Cross(a, b)` | `obj(obj, obj)` | Cross product (3D: vector result) |
| `Dist(a, b)` | `f64(obj, obj)` | Distance between vectors |
| `Div(v, scalar)` | `obj(obj, f64)` | Scalar division |
| `Dot(a, b)` | `f64(obj, obj)` | Dot product |
| `get_X(v)` | `f64(obj)` | Get X component |
| `get_Y(v)` | `f64(obj)` | Get Y component |
| `get_Z(v)` | `f64(obj)` | Get Z component |
| `Len(v)` | `f64(obj)` | Vector length (magnitude) |
| `LenSq(v)` | `f64(obj)` | Length squared |
| `Lerp(a, b, t)` | `obj(obj, obj, f64)` | Linear interpolation |
| `Mul(v, scalar)` | `obj(obj, f64)` | Scalar multiplication |
| `Neg(v)` | `obj(obj)` | Negate vector |
| `New(x, y, z)` | `obj(f64, f64, f64)` | Create vector |
| `Norm(v)` | `obj(obj)` | Normalize to unit length |
| `One()` | `obj()` | Unit vector (1, 1, 1) |
| `Sub(a, b)` | `obj(obj, obj)` | Vector subtraction |
| `Zero()` | `obj()` | Zero vector (0, 0, 0) |

### Examples

```zia
var pos = Viper.Math.Vec3.New(10.0, 20.0, 30.0);
var forward = Viper.Math.Vec3.New(0.0, 0.0, 1.0);
var up = Viper.Math.Vec3.New(0.0, 1.0, 0.0);

// Cross product for perpendicular vector
var right = Viper.Math.Vec3.Cross(forward, up);

// Normalize
var dir = Viper.Math.Vec3.Norm(pos);
```

---

## Appendix: Type Abbreviations

These abbreviations appear in IL function signatures throughout this document.

> **Note:** `f32` does not exist as a primitive in the IL type system (see `src/il/core/Type.hpp`).
> BASIC `SINGLE` values are widened to `f64` during lowering. The `i8` type also does not appear
> in the IL type enum; it is used only in C runtime return values for boolean-style results.

| Abbreviation | Full Type | Description |
|--------------|-----------|-------------|
| `f64` | `double` | 64-bit IEEE 754 float |
| `i1` | `bool` | Boolean (1-bit integer) |
| `i16` | `short` | 16-bit signed integer |
| `i32` | `int` | 32-bit signed integer |
| `i64` | `long` | 64-bit signed integer |
| `obj` | `object` | Object reference (heap pointer with refcount) |
| `ptr` | `pointer` | Raw unmanaged pointer |
| `str` | `string` | Interned string value |
| `void` | `unit` | No return value |

---

---

## Viper.Collections

Comprehensive collection types for data storage and manipulation.

### Viper.Collections.Bytes

Raw byte array for binary data manipulation.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(size)` | `obj(i64)` | Create byte array of size |
| `get_Len(bytes)` | `i64(obj)` | Get length |
| `Get(bytes, idx)` | `i64(obj, i64)` | Get byte at index (0-255) |
| `Set(bytes, idx, val)` | `void(obj, i64, i64)` | Set byte at index |
| `Clone(bytes)` | `obj(obj)` | Deep copy |
| `Slice(bytes, start, end)` | `obj(obj, i64, i64)` | Extract slice |
| `Copy(dst, dstOff, src, srcOff, len)` | `void(...)` | Copy bytes |
| `Fill(bytes, value)` | `void(obj, i64)` | Fill with value |
| `Find(bytes, value)` | `i64(obj, i64)` | Find byte (-1 if not found) |
| `FromStr(s)` | `obj(str)` | Create from string (UTF-8) |
| `ToStr(bytes)` | `str(obj)` | Convert to string |
| `FromBase64(s)` | `obj(str)` | Decode from Base64 |
| `ToBase64(bytes)` | `str(obj)` | Encode to Base64 |
| `FromHex(s)` | `obj(str)` | Decode from hex string |
| `ToHex(bytes)` | `str(obj)` | Encode to hex string |

### Viper.Collections.Seq

Dynamic sequence (array list) that can hold any object type.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Clear(seq)` | `void(obj)` | Clear all items |
| `Clone(seq)` | `obj(obj)` | Deep copy |
| `Find(seq, val)` | `i64(obj, obj)` | Find index (-1 if not found) |
| `First(seq)` | `obj(obj)` | Get first item |
| `get_Cap(seq)` | `i64(obj)` | Get capacity |
| `get_IsEmpty(seq)` | `i1(obj)` | Check if empty |
| `get_Len(seq)` | `i64(obj)` | Get length |
| `Get(seq, idx)` | `obj(obj, i64)` | Get item at index |
| `Has(seq, val)` | `i1(obj, obj)` | Check if contains |
| `Insert(seq, idx, val)` | `void(obj, i64, obj)` | Insert at index |
| `Last(seq)` | `obj(obj)` | Get last item |
| `New()` | `obj()` | Create empty sequence |
| `Peek(seq)` | `obj(obj)` | Get last without removing |
| `Pop(seq)` | `obj(obj)` | Remove and return last |
| `Push(seq, val)` | `void(obj, obj)` | Add to end |
| `PushAll(seq, other)` | `void(obj, obj)` | Add all from another sequence |
| `Remove(seq, idx)` | `obj(obj, i64)` | Remove at index |
| `Reverse(seq)` | `void(obj)` | Reverse in place |
| `Set(seq, idx, val)` | `void(obj, i64, obj)` | Set item at index |
| `Shuffle(seq)` | `void(obj)` | Shuffle randomly using global RNG |
| `Slice(seq, start, end)` | `obj(obj, i64, i64)` | Extract slice as new sequence |
| `Sort(seq)` | `void(obj)` | Sort ascending in place |
| `SortDesc(seq)` | `void(obj)` | Sort descending in place |
| `WithCapacity(cap)` | `obj(i64)` | Create with pre-allocated capacity |

### Viper.Collections.List

Ordered list with value-semantics API.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Clear(list)` | `void(obj)` | Clear all items |
| `Find(list, val)` | `i64(obj, obj)` | Find index (-1 if not found) |
| `First(list)` | `obj(obj)` | Get first item |
| `Flip(list)` | `void(obj)` | Reverse list in place |
| `get_IsEmpty(list)` | `i1(obj)` | Check if list is empty |
| `get_Len(list)` | `i64(obj)` | Get item count |
| `Get(list, idx)` | `obj(obj, i64)` | Get item at index |
| `Has(list, val)` | `i1(obj, obj)` | Check if contains value |
| `Insert(list, idx, val)` | `void(obj, i64, obj)` | Insert at index |
| `Last(list)` | `obj(obj)` | Get last item |
| `New()` | `obj()` | Create empty list |
| `Pop(list)` | `obj(obj)` | Remove and return last item |
| `Push(list, val)` | `void(obj, obj)` | Add to end |
| `Remove(list, val)` | `i1(obj, obj)` | Remove first occurrence |
| `RemoveAt(list, idx)` | `void(obj, i64)` | Remove at index |
| `Set(list, idx, val)` | `void(obj, i64, obj)` | Set item at index |
| `Slice(list, start, end)` | `obj(obj, i64, i64)` | Extract slice as new list |
| `Sort(list)` | `void(obj)` | Sort ascending in place |
| `SortDesc(list)` | `void(obj)` | Sort descending in place |

### Viper.Collections.Map

Hash map with string keys.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Clear(map)` | `void(obj)` | Clear all entries |
| `Get(map, key)` | `obj(obj, str)` | Get value (null if missing) |
| `GetFloat(map, key)` | `f64(obj, str)` | Get float value |
| `GetFloatOr(map, key, default)` | `f64(obj, str, f64)` | Get float or default |
| `GetInt(map, key)` | `i64(obj, str)` | Get integer value |
| `GetIntOr(map, key, default)` | `i64(obj, str, i64)` | Get integer or default |
| `GetOr(map, key, default)` | `obj(obj, str, obj)` | Get or return default |
| `GetStr(map, key)` | `str(obj, str)` | Get string value |
| `GetStrOr(map, key, default)` | `str(obj, str, str)` | Get string or default |
| `get_IsEmpty(map)` | `i1(obj)` | Check if empty |
| `get_Len(map)` | `i64(obj)` | Get entry count |
| `Has(map, key)` | `i1(obj, str)` | Check if key exists |
| `Keys(map)` | `obj(obj)` | Get all keys as Seq |
| `New()` | `obj()` | Create empty map |
| `Remove(map, key)` | `i1(obj, str)` | Remove key |
| `Set(map, key, val)` | `void(obj, str, obj)` | Set key-value pair (boxed) |
| `SetFloat(map, key, val)` | `void(obj, str, f64)` | Set float value |
| `SetIfMissing(map, key, val)` | `i1(obj, str, obj)` | Set only if key absent |
| `SetInt(map, key, val)` | `void(obj, str, i64)` | Set integer value |
| `SetStr(map, key, val)` | `void(obj, str, str)` | Set string value |
| `Values(map)` | `obj(obj)` | Get all values as Seq |

### Viper.Collections.Stack

LIFO (Last In, First Out) stack.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty stack |
| `get_Len(stack)` | `i64(obj)` | Get size |
| `get_IsEmpty(stack)` | `bool(obj)` | Check if empty |
| `Push(stack, val)` | `void(obj, obj)` | Push item |
| `Pop(stack)` | `obj(obj)` | Pop and return top |
| `Peek(stack)` | `obj(obj)` | Get top without removing |
| `Clear(stack)` | `void(obj)` | Clear all items |

### Viper.Collections.Queue

FIFO (First In, First Out) queue.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty queue |
| `get_Len(queue)` | `i64(obj)` | Get size |
| `get_IsEmpty(queue)` | `bool(obj)` | Check if empty |
| `Add(queue, val)` | `void(obj, obj)` | Enqueue item |
| `Take(queue)` | `obj(obj)` | Dequeue and return front |
| `Peek(queue)` | `obj(obj)` | Get front without removing |
| `Clear(queue)` | `void(obj)` | Clear all items |

### Viper.Collections.Ring

Fixed-capacity circular buffer.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(capacity)` | `obj(i64)` | Create with fixed capacity |
| `get_Len(ring)` | `i64(obj)` | Get current size |
| `get_Cap(ring)` | `i64(obj)` | Get capacity |
| `get_IsEmpty(ring)` | `bool(obj)` | Check if empty |
| `get_IsFull(ring)` | `bool(obj)` | Check if full |
| `Push(ring, val)` | `void(obj, obj)` | Add (overwrites oldest if full) |
| `Pop(ring)` | `obj(obj)` | Remove and return oldest |
| `Peek(ring)` | `obj(obj)` | Get oldest without removing |
| `Get(ring, idx)` | `obj(obj, i64)` | Get item at index |
| `Clear(ring)` | `void(obj)` | Clear all items |

### Viper.Collections.Bag

Unordered set of unique strings.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty bag |
| `get_Len(bag)` | `i64(obj)` | Get size |
| `get_IsEmpty(bag)` | `bool(obj)` | Check if empty |
| `Put(bag, item)` | `bool(obj, str)` | Add item (returns false if existed) |
| `Drop(bag, item)` | `bool(obj, str)` | Remove item |
| `Has(bag, item)` | `bool(obj, str)` | Check if contains |
| `Clear(bag)` | `void(obj)` | Clear all items |
| `Items(bag)` | `obj(obj)` | Get all items as Seq |
| `Merge(bag1, bag2)` | `obj(obj, obj)` | Union of two bags |
| `Common(bag1, bag2)` | `obj(obj, obj)` | Intersection |
| `Diff(bag1, bag2)` | `obj(obj, obj)` | Difference |

### Viper.Collections.Heap

Priority queue (min-heap or max-heap).

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create min-heap |
| `NewMax(isMax)` | `obj(bool)` | Create max-heap if true |
| `get_Len(heap)` | `i64(obj)` | Get size |
| `get_IsEmpty(heap)` | `bool(obj)` | Check if empty |
| `get_IsMax(heap)` | `bool(obj)` | Check if max-heap |
| `Push(heap, priority, val)` | `void(obj, i64, obj)` | Add with priority |
| `Pop(heap)` | `obj(obj)` | Remove and return highest priority |
| `Peek(heap)` | `obj(obj)` | Get highest priority without removing |
| `TryPop(heap)` | `obj(obj)` | Pop or return null |
| `TryPeek(heap)` | `obj(obj)` | Peek or return null |
| `Clear(heap)` | `void(obj)` | Clear all items |
| `ToSeq(heap)` | `obj(obj)` | Convert to sequence |

### Viper.Collections.TreeMap

Sorted map with string keys (red-black tree).

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty tree map |
| `get_Len(map)` | `i64(obj)` | Get entry count |
| `get_IsEmpty(map)` | `bool(obj)` | Check if empty |
| `Get(map, key)` | `obj(obj, str)` | Get value |
| `Set(map, key, val)` | `void(obj, str, obj)` | Set key-value pair |
| `Has(map, key)` | `bool(obj, str)` | Check if key exists |
| `Drop(map, key)` | `bool(obj, str)` | Remove key |
| `Clear(map)` | `void(obj)` | Clear all entries |
| `Keys(map)` | `obj(obj)` | Get keys (sorted) |
| `Values(map)` | `obj(obj)` | Get values (key-sorted) |
| `First(map)` | `str(obj)` | Get smallest key |
| `Last(map)` | `str(obj)` | Get largest key |
| `Floor(map, key)` | `str(obj, str)` | Largest key <= given |
| `Ceil(map, key)` | `str(obj, str)` | Smallest key >= given |

### Collection Examples

```zia
// Bytes for binary data
var data = Viper.Collections.Bytes.New(1024);
Viper.Collections.Bytes.Set(data, 0, 0xFF);
var hex = Viper.Collections.Bytes.ToHex(data);

// Seq for dynamic arrays
var items = Viper.Collections.Seq.New();
Viper.Collections.Seq.Push(items, "apple");
Viper.Collections.Seq.Push(items, "banana");
var first = Viper.Collections.Seq.First(items);

// List for simple collections
var numbers = Viper.Collections.List.New();
Viper.Collections.List.Push(numbers, Viper.Core.Box.I64(1));
Viper.Collections.List.Push(numbers, Viper.Core.Box.I64(2));
var count = Viper.Collections.List.get_Len(numbers);

// Map for key-value storage
var config = Viper.Collections.Map.New();
Viper.Collections.Map.Set(config, "host", "localhost");
Viper.Collections.Map.Set(config, "port", "8080");
var host = Viper.Collections.Map.Get(config, "host");

// Stack for LIFO operations
var stack = Viper.Collections.Stack.New();
Viper.Collections.Stack.Push(stack, "first");
Viper.Collections.Stack.Push(stack, "second");
var top = Viper.Collections.Stack.Pop(stack);  // "second"

// Queue for FIFO operations
var queue = Viper.Collections.Queue.New();
Viper.Collections.Queue.Add(queue, "task1");
Viper.Collections.Queue.Add(queue, "task2");
var next = Viper.Collections.Queue.Take(queue);  // "task1"

// Heap for priority queue
var pq = Viper.Collections.Heap.New();
Viper.Collections.Heap.Push(pq, 3, "low priority");
Viper.Collections.Heap.Push(pq, 1, "high priority");
var urgent = Viper.Collections.Heap.Pop(pq);  // "high priority"

// TreeMap for sorted keys
var sorted = Viper.Collections.TreeMap.New();
Viper.Collections.TreeMap.Set(sorted, "zebra", "last");
Viper.Collections.TreeMap.Set(sorted, "apple", "first");
var smallest = Viper.Collections.TreeMap.First(sorted);  // "apple"
```

---

## Viper.Time.Stopwatch

High-precision timing for performance measurement. Stopwatch objects are heap-allocated.

### Functions and Methods

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_ElapsedMs(sw)` | `i64(obj)` | Elapsed time in milliseconds |
| `get_ElapsedNs(sw)` | `i64(obj)` | Elapsed time in nanoseconds |
| `get_ElapsedUs(sw)` | `i64(obj)` | Elapsed time in microseconds |
| `get_IsRunning(sw)` | `i1(obj)` | Check if the stopwatch is running |
| `New()` | `obj()` | Create a new stopped stopwatch |
| `Reset(sw)` | `void(obj)` | Reset accumulated time to zero (stops) |
| `Restart(sw)` | `void(obj)` | Reset and immediately start |
| `Start(sw)` | `void(obj)` | Start or resume timing |
| `StartNew()` | `obj()` | Create and immediately start a stopwatch |
| `Stop(sw)` | `void(obj)` | Stop (pause) timing |

### Examples

```zia
// Measure execution time
var sw = Viper.Time.Stopwatch.StartNew();
// ... code to measure ...
sw.Stop();
var elapsed = sw.get_ElapsedMs;
Viper.Terminal.Say("Took " + elapsed + " ms");

// Reusable stopwatch
var timer = Viper.Time.Stopwatch.New();
timer.Start();
// ... first section ...
timer.Stop();
var section1 = timer.get_ElapsedUs;
timer.Restart();
// ... second section ...
timer.Stop();
var section2 = timer.get_ElapsedUs;
```

---

## See Also

- [IL Runtime Specification](devdocs/runtime-vm.md) - Low-level VM runtime details
- [Zia Language Reference](zia-reference.md) - Zia programming language reference
- [Zia Getting Started](zia-getting-started.md) - Zia quick-start guide
