# Viper Runtime API - Complete Reference

This document provides comprehensive documentation for all Viper runtime namespaces. It supplements the main runtime reference in `bible/appendices/d-runtime-reference.md`.

**Auto-generated from:** `src/il/runtime/runtime.def`
**Total Functions:** 1,418
**Total Classes:** 98

---

## Table of Contents

- [Viper.Bits](#viperbits) - Bitwise operations
- [Viper.Box](#viperbox) - Value boxing/unboxing
- [Viper.Convert](#viperconvert) - Type conversions
- [Viper.DateTime](#viperdatetime) - Date and time operations
- [Viper.Diagnostics](#viperdiagnostics) - Assertions and debugging
- [Viper.Exec](#viperexec) - Process execution
- [Viper.Fmt](#viperfmt) - Number formatting
- [Viper.IO](#viperio) - File and directory operations
- [Viper.Log](#viperlog) - Logging utilities
- [Viper.Machine](#vipermachine) - System information
- [Viper.Parse](#viperparse) - String parsing
- [Viper.Random](#viperrandom) - Random number generation
- [Viper.Sound](#vipersound) - Audio playback
- [Viper.String](#viperstring) - String manipulation
- [Viper.Strings](#viperstrings) - String utilities
- [Viper.Text](#vipertext) - Text processing utilities
- [Viper.Vec2](#vipervec2) - 2D vector math
- [Viper.Vec3](#vipervec3) - 3D vector math
- [Viper.Collections](#vipercollections) - Data structures (Bytes, Seq, List, Map, etc.)

---

## Viper.Bits

Bitwise operations for low-level integer manipulation. All functions work with 64-bit integers.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `And(a, b)` | `i64(i64, i64)` | Bitwise AND of two integers |
| `Or(a, b)` | `i64(i64, i64)` | Bitwise OR of two integers |
| `Xor(a, b)` | `i64(i64, i64)` | Bitwise XOR of two integers |
| `Not(x)` | `i64(i64)` | Bitwise NOT (complement) |
| `Shl(x, n)` | `i64(i64, i64)` | Shift left by n bits |
| `Shr(x, n)` | `i64(i64, i64)` | Arithmetic shift right by n bits |
| `Ushr(x, n)` | `i64(i64, i64)` | Logical (unsigned) shift right |
| `Rotl(x, n)` | `i64(i64, i64)` | Rotate left by n bits |
| `Rotr(x, n)` | `i64(i64, i64)` | Rotate right by n bits |
| `Get(x, pos)` | `bool(i64, i64)` | Get bit at position (0-63) |
| `Set(x, pos)` | `i64(i64, i64)` | Set bit at position to 1 |
| `Clear(x, pos)` | `i64(i64, i64)` | Clear bit at position to 0 |
| `Toggle(x, pos)` | `i64(i64, i64)` | Toggle bit at position |
| `Flip(x)` | `i64(i64)` | Flip all bits |
| `Swap(x)` | `i64(i64)` | Byte-swap (endian conversion) |
| `Count(x)` | `i64(i64)` | Count number of set bits (popcount) |
| `LeadZ(x)` | `i64(i64)` | Count leading zero bits |
| `TrailZ(x)` | `i64(i64)` | Count trailing zero bits |

### Examples

```zia
// Bitwise operations
var flags = Viper.Bits.Or(0x01, 0x04);  // flags = 0x05
var masked = Viper.Bits.And(flags, 0x0F);

// Bit manipulation
var value = 0;
value = Viper.Bits.Set(value, 3);    // Set bit 3: value = 8
var isSet = Viper.Bits.Get(value, 3); // true

// Counting
var ones = Viper.Bits.Count(0xFF);   // 8 set bits
var leading = Viper.Bits.LeadZ(0x0F); // 60 leading zeros
```

---

## Viper.Box

Boxing and unboxing of primitive values to/from objects. Used for storing primitives in collections.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `I64(value)` | `obj(i64)` | Box an integer |
| `F64(value)` | `obj(f64)` | Box a float |
| `I1(value)` | `obj(i64)` | Box a boolean (as integer) |
| `Str(value)` | `obj(str)` | Box a string |
| `ToI64(box)` | `i64(obj)` | Unbox to integer |
| `ToF64(box)` | `f64(obj)` | Unbox to float |
| `ToI1(box)` | `i64(obj)` | Unbox to boolean |
| `ToStr(box)` | `str(obj)` | Unbox to string |
| `Type(box)` | `i64(obj)` | Get boxed value type |
| `EqI64(box, val)` | `bool(obj, i64)` | Compare box with integer |
| `EqF64(box, val)` | `bool(obj, f64)` | Compare box with float |
| `EqStr(box, val)` | `bool(obj, str)` | Compare box with string |

### Type Constants

- `0` = Integer (i64)
- `1` = Float (f64)
- `2` = Boolean (i1)
- `3` = String (str)

### Examples

```zia
// Boxing values for heterogeneous collections
var boxedInt = Viper.Box.I64(42);
var boxedStr = Viper.Box.Str("hello");

// Type checking
var type = Viper.Box.Type(boxedInt);  // 0 (integer)

// Unboxing
var value = Viper.Box.ToI64(boxedInt);  // 42
```

---

## Viper.Convert

Type conversion functions between strings and numeric types.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `ToInt(s)` | `i64(str)` | Parse string to integer |
| `ToInt64(s)` | `i64(str)` | Parse string to 64-bit integer |
| `ToDouble(s)` | `f64(str)` | Parse string to float |
| `ToString_Int(n)` | `str(i64)` | Convert integer to string |
| `ToString_Double(n)` | `str(f64)` | Convert float to string |
| `NumToInt(n)` | `i64(f64)` | Convert float to integer (truncate) |

### Examples

```zia
var n = Viper.Convert.ToInt("42");        // 42
var f = Viper.Convert.ToDouble("3.14");   // 3.14
var s = Viper.Convert.ToString_Int(100);  // "100"
```

---

## Viper.DateTime

Date and time operations using Unix timestamps (seconds since epoch).

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Now()` | `i64()` | Current Unix timestamp (seconds) |
| `NowMs()` | `i64()` | Current Unix timestamp (milliseconds) |
| `Create(y,m,d,h,min,s)` | `i64(...)` | Create timestamp from components |
| `Year(ts)` | `i64(i64)` | Extract year |
| `Month(ts)` | `i64(i64)` | Extract month (1-12) |
| `Day(ts)` | `i64(i64)` | Extract day (1-31) |
| `Hour(ts)` | `i64(i64)` | Extract hour (0-23) |
| `Minute(ts)` | `i64(i64)` | Extract minute (0-59) |
| `Second(ts)` | `i64(i64)` | Extract second (0-59) |
| `DayOfWeek(ts)` | `i64(i64)` | Day of week (0=Sunday, 6=Saturday) |
| `AddDays(ts, days)` | `i64(i64, i64)` | Add days to timestamp |
| `AddSeconds(ts, secs)` | `i64(i64, i64)` | Add seconds to timestamp |
| `Diff(ts1, ts2)` | `i64(i64, i64)` | Difference in seconds |
| `Format(ts, fmt)` | `str(i64, str)` | Format timestamp |
| `ToISO(ts)` | `str(i64)` | Convert to ISO 8601 string |

### Format Specifiers

- `%Y` - Year (4 digits)
- `%m` - Month (01-12)
- `%d` - Day (01-31)
- `%H` - Hour (00-23)
- `%M` - Minute (00-59)
- `%S` - Second (00-59)

### Examples

```zia
var now = Viper.DateTime.Now();
var year = Viper.DateTime.Year(now);
var formatted = Viper.DateTime.Format(now, "%Y-%m-%d %H:%M:%S");
var iso = Viper.DateTime.ToISO(now);  // "2024-01-15T10:30:00Z"

var tomorrow = Viper.DateTime.AddDays(now, 1);
var future = Viper.DateTime.Create(2025, 6, 15, 12, 0, 0);
```

---

## Viper.Diagnostics

Assertion functions and debugging utilities.

### Assertion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Assert(cond, msg)` | `void(bool, str)` | Assert condition is true |
| `AssertEq(a, b, msg)` | `void(i64, i64, str)` | Assert integers equal |
| `AssertNeq(a, b, msg)` | `void(i64, i64, str)` | Assert integers not equal |
| `AssertEqNum(a, b, msg)` | `void(f64, f64, str)` | Assert floats equal |
| `AssertEqStr(a, b, msg)` | `void(str, str, str)` | Assert strings equal |
| `AssertNull(obj, msg)` | `void(obj, str)` | Assert object is null |
| `AssertNotNull(obj, msg)` | `void(obj, str)` | Assert object is not null |
| `AssertGt(a, b, msg)` | `void(i64, i64, str)` | Assert a > b |
| `AssertLt(a, b, msg)` | `void(i64, i64, str)` | Assert a < b |
| `AssertGte(a, b, msg)` | `void(i64, i64, str)` | Assert a >= b |
| `AssertLte(a, b, msg)` | `void(i64, i64, str)` | Assert a <= b |
| `AssertFail(msg)` | `void(str)` | Always fail with message |
| `Trap(msg)` | `void(str)` | Trigger runtime trap |

### Stopwatch Class

High-precision timing for performance measurement.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create stopped stopwatch |
| `StartNew()` | `obj()` | Create and start stopwatch |
| `Start()` | `void(obj)` | Start timing |
| `Stop()` | `void(obj)` | Stop timing |
| `Reset()` | `void(obj)` | Reset to zero |
| `Restart()` | `void(obj)` | Reset and start |
| `get_ElapsedMs` | `i64(obj)` | Elapsed milliseconds |
| `get_ElapsedUs` | `i64(obj)` | Elapsed microseconds |
| `get_ElapsedNs` | `i64(obj)` | Elapsed nanoseconds |
| `get_IsRunning` | `bool(obj)` | Check if running |

### Examples

```zia
// Assertions
Viper.Diagnostics.Assert(x > 0, "x must be positive");
Viper.Diagnostics.AssertEq(result, expected, "result mismatch");
Viper.Diagnostics.AssertNotNull(obj, "object is null");

// Stopwatch
var sw = Viper.Diagnostics.Stopwatch.StartNew();
// ... code to measure ...
sw.Stop();
var elapsed = sw.get_ElapsedMs;
Viper.Terminal.Say("Took " + elapsed + " ms");
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
Viper.Collections.List.Add(args, "-l");
Viper.Collections.List.Add(args, "-a");
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
| `Int(n)` | `str(i64)` | Format integer |
| `IntRadix(n, radix)` | `str(i64, i64)` | Format in base 2-36 |
| `IntPad(n, width, pad)` | `str(i64, i64, str)` | Format with padding |
| `Num(n)` | `str(f64)` | Format float |
| `NumFixed(n, decimals)` | `str(f64, i64)` | Format with fixed decimals |

### Examples

```zia
var hex = Viper.Fmt.IntRadix(255, 16);     // "ff"
var bin = Viper.Fmt.IntRadix(10, 2);       // "1010"
var padded = Viper.Fmt.IntPad(42, 5, "0"); // "00042"
var fixed = Viper.Fmt.NumFixed(3.14159, 2); // "3.14"
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
Viper.Log.Debug("Processing item " + Viper.Convert.ToString_Int(i));
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
Viper.Terminal.Say("Cores: " + Viper.Convert.ToString_Int(Viper.Machine.get_Cores));
Viper.Terminal.Say("Memory: " + Viper.Convert.ToString_Int(Viper.Machine.get_MemTotal / 1048576) + " MB");
```

---

## Viper.Parse

Safe string parsing with error handling.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `TryInt(s, out)` | `bool(str, ptr)` | Try parse integer |
| `TryNum(s, out)` | `bool(str, ptr)` | Try parse float |
| `TryBool(s, out)` | `bool(str, ptr)` | Try parse boolean |
| `IntOr(s, default)` | `i64(str, i64)` | Parse or return default |
| `NumOr(s, default)` | `f64(str, f64)` | Parse or return default |
| `BoolOr(s, default)` | `bool(str, bool)` | Parse or return default |
| `IsInt(s)` | `bool(str)` | Check if string is valid integer |
| `IsNum(s)` | `bool(str)` | Check if string is valid number |
| `IntRadix(s, radix, default)` | `i64(str, i64, i64)` | Parse in given base |

### Examples

```zia
// Safe parsing with defaults
var port = Viper.Parse.IntOr(portStr, 8080);
var timeout = Viper.Parse.NumOr(timeoutStr, 30.0);
var enabled = Viper.Parse.BoolOr(enabledStr, false);

// Validation
if (Viper.Parse.IsInt(input)) {
    var value = Viper.Convert.ToInt(input);
}

// Parse hex
var color = Viper.Parse.IntRadix("FF00FF", 16, 0);
```

---

## Viper.Random

Random number generation.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Next()` | `f64()` | Random float [0.0, 1.0) |
| `NextInt(max)` | `i64(i64)` | Random integer [0, max) |
| `Seed(value)` | `void(i64)` | Seed the generator |

### Examples

```zia
// Seed for reproducibility
Viper.Random.Seed(12345);

// Random values
var roll = Viper.Random.NextInt(6) + 1;  // Dice roll 1-6
var chance = Viper.Random.Next();         // 0.0 to 1.0
var percent = Viper.Random.NextInt(100);  // 0 to 99
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
| `get_Length(s)` | `i64(str)` | Get string length |
| `get_IsEmpty(s)` | `bool(str)` | Check if empty |
| `Concat(a, b)` | `str(str, str)` | Concatenate strings |
| `Left(s, n)` | `str(str, i64)` | Get leftmost n characters |
| `Right(s, n)` | `str(str, i64)` | Get rightmost n characters |
| `Mid(s, start)` | `str(str, i64)` | Substring from position |
| `MidLen(s, start, len)` | `str(str, i64, i64)` | Substring with length |
| `Substring(s, start, end)` | `str(str, i64, i64)` | Substring by indices |
| `IndexOf(s, sub)` | `i64(str, str)` | Find substring (-1 if not found) |
| `IndexOfFrom(s, start, sub)` | `i64(str, i64, str)` | Find from position |
| `Has(s, sub)` | `bool(str, str)` | Check if contains |
| `StartsWith(s, prefix)` | `bool(str, str)` | Check prefix |
| `EndsWith(s, suffix)` | `bool(str, str)` | Check suffix |
| `Count(s, sub)` | `i64(str, str)` | Count occurrences |
| `ToLower(s)` | `str(str)` | Convert to lowercase |
| `ToUpper(s)` | `str(str)` | Convert to uppercase |
| `Trim(s)` | `str(str)` | Trim whitespace both ends |
| `TrimStart(s)` | `str(str)` | Trim leading whitespace |
| `TrimEnd(s)` | `str(str)` | Trim trailing whitespace |
| `PadLeft(s, width, pad)` | `str(str, i64, str)` | Left-pad to width |
| `PadRight(s, width, pad)` | `str(str, i64, str)` | Right-pad to width |
| `Replace(s, old, new)` | `str(str, str, str)` | Replace all occurrences |
| `Split(s, delim)` | `obj(str, str)` | Split into list |
| `Repeat(s, n)` | `str(str, i64)` | Repeat string n times |
| `Flip(s)` | `str(str)` | Reverse string |
| `Chr(code)` | `str(i64)` | Character from code point |
| `Asc(s)` | `i64(str)` | Code point of first char |
| `Cmp(a, b)` | `i64(str, str)` | Compare (-1, 0, 1) |
| `CmpNoCase(a, b)` | `i64(str, str)` | Case-insensitive compare |

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

## Viper.Strings

Additional string utilities (conversion focus).

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `FromSingle(n)` | `str(f64)` | Float (single precision format) |
| `FromI16(n)` | `str(i16)` | 16-bit int to string |
| `FromI32(n)` | `str(i32)` | 32-bit int to string |
| `FromStr(s)` | `str(str)` | Copy string |
| `Equals(a, b)` | `bool(str, str)` | String equality |
| `Join(sep, list)` | `str(str, obj)` | Join list with separator |
| `SplitFields(s, arr, max)` | `i64(str, ptr, i64)` | Split on whitespace |

> **Note**: For integer/double to string conversion, use `Viper.Convert.ToString_Int` and `Viper.Convert.ToString_Double`.

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
Viper.Collections.Map.Put(vars, "name", "Alice");
Viper.Collections.Map.Put(vars, "count", "5");
var message = Viper.Text.Template.Render(template, vars);
```

---

## Viper.Vec2

2D vector mathematics for games and graphics.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `New(x, y)` | `obj(f64, f64)` | Create vector |
| `Zero()` | `obj()` | Zero vector (0, 0) |
| `One()` | `obj()` | Unit vector (1, 1) |
| `get_X(v)` | `f64(obj)` | Get X component |
| `get_Y(v)` | `f64(obj)` | Get Y component |
| `Add(a, b)` | `obj(obj, obj)` | Vector addition |
| `Sub(a, b)` | `obj(obj, obj)` | Vector subtraction |
| `Mul(v, scalar)` | `obj(obj, f64)` | Scalar multiplication |
| `Div(v, scalar)` | `obj(obj, f64)` | Scalar division |
| `Neg(v)` | `obj(obj)` | Negate vector |
| `Dot(a, b)` | `f64(obj, obj)` | Dot product |
| `Cross(a, b)` | `f64(obj, obj)` | Cross product (2D: scalar) |
| `Len(v)` | `f64(obj)` | Vector length |
| `LenSq(v)` | `f64(obj)` | Length squared |
| `Norm(v)` | `obj(obj)` | Normalize to unit length |
| `Dist(a, b)` | `f64(obj, obj)` | Distance between vectors |
| `Lerp(a, b, t)` | `obj(obj, obj, f64)` | Linear interpolation |
| `Angle(v)` | `f64(obj)` | Angle in radians |
| `Rotate(v, angle)` | `obj(obj, f64)` | Rotate by angle (radians) |

### Examples

```zia
var pos = Viper.Vec2.New(100.0, 200.0);
var vel = Viper.Vec2.New(5.0, -3.0);

// Movement
pos = Viper.Vec2.Add(pos, vel);

// Distance calculation
var target = Viper.Vec2.New(300.0, 400.0);
var dist = Viper.Vec2.Dist(pos, target);

// Normalize velocity
var dir = Viper.Vec2.Norm(vel);
var speed = Viper.Vec2.Len(vel);

// Interpolation
var midpoint = Viper.Vec2.Lerp(pos, target, 0.5);
```

---

## Viper.Vec3

3D vector mathematics.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `New(x, y, z)` | `obj(f64, f64, f64)` | Create vector |
| `Zero()` | `obj()` | Zero vector (0, 0, 0) |
| `One()` | `obj()` | Unit vector (1, 1, 1) |
| `get_X(v)` | `f64(obj)` | Get X component |
| `get_Y(v)` | `f64(obj)` | Get Y component |
| `get_Z(v)` | `f64(obj)` | Get Z component |
| `Add(a, b)` | `obj(obj, obj)` | Vector addition |
| `Sub(a, b)` | `obj(obj, obj)` | Vector subtraction |
| `Mul(v, scalar)` | `obj(obj, f64)` | Scalar multiplication |
| `Div(v, scalar)` | `obj(obj, f64)` | Scalar division |
| `Neg(v)` | `obj(obj)` | Negate vector |
| `Dot(a, b)` | `f64(obj, obj)` | Dot product |
| `Cross(a, b)` | `obj(obj, obj)` | Cross product |
| `Len(v)` | `f64(obj)` | Vector length |
| `LenSq(v)` | `f64(obj)` | Length squared |
| `Norm(v)` | `obj(obj)` | Normalize to unit length |
| `Dist(a, b)` | `f64(obj, obj)` | Distance between vectors |
| `Lerp(a, b, t)` | `obj(obj, obj, f64)` | Linear interpolation |

### Examples

```zia
var pos = Viper.Vec3.New(10.0, 20.0, 30.0);
var forward = Viper.Vec3.New(0.0, 0.0, 1.0);
var up = Viper.Vec3.New(0.0, 1.0, 0.0);

// Cross product for perpendicular vector
var right = Viper.Vec3.Cross(forward, up);

// Normalize
var dir = Viper.Vec3.Norm(pos);
```

---

## Appendix: Type Abbreviations

| Abbreviation | Full Type | Description |
|--------------|-----------|-------------|
| `i1` | `bool` | Boolean |
| `i8` | `byte` | 8-bit integer |
| `i16` | `short` | 16-bit integer |
| `i32` | `int` | 32-bit integer |
| `i64` | `long` | 64-bit integer |
| `f32` | `float` | 32-bit float |
| `f64` | `double` | 64-bit float |
| `str` | `string` | String |
| `obj` | `object` | Object reference |
| `ptr` | `pointer` | Raw pointer |
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
| `New()` | `obj()` | Create empty sequence |
| `WithCapacity(cap)` | `obj(i64)` | Create with capacity |
| `get_Len(seq)` | `i64(obj)` | Get length |
| `get_Cap(seq)` | `i64(obj)` | Get capacity |
| `get_IsEmpty(seq)` | `bool(obj)` | Check if empty |
| `Get(seq, idx)` | `obj(obj, i64)` | Get item at index |
| `Set(seq, idx, val)` | `void(obj, i64, obj)` | Set item at index |
| `Push(seq, val)` | `void(obj, obj)` | Add to end |
| `PushAll(seq, other)` | `void(obj, obj)` | Add all from other seq |
| `Pop(seq)` | `obj(obj)` | Remove and return last |
| `Peek(seq)` | `obj(obj)` | Get last without removing |
| `First(seq)` | `obj(obj)` | Get first item |
| `Last(seq)` | `obj(obj)` | Get last item |
| `Insert(seq, idx, val)` | `void(obj, i64, obj)` | Insert at index |
| `Remove(seq, idx)` | `obj(obj, i64)` | Remove at index |
| `Clear(seq)` | `void(obj)` | Clear all items |
| `Clone(seq)` | `obj(obj)` | Deep copy |
| `Slice(seq, start, end)` | `obj(obj, i64, i64)` | Extract slice |
| `Find(seq, val)` | `i64(obj, obj)` | Find index (-1 if not found) |
| `Has(seq, val)` | `bool(obj, obj)` | Check if contains |
| `Reverse(seq)` | `void(obj)` | Reverse in place |
| `Shuffle(seq)` | `void(obj)` | Shuffle randomly |

### Viper.Collections.List

Ordered list (similar to Seq but with different API).

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty list |
| `get_Count(list)` | `i64(obj)` | Get count |
| `get_Item(list, idx)` | `obj(obj, i64)` | Get item at index |
| `set_Item(list, idx, val)` | `void(obj, i64, obj)` | Set item at index |
| `Add(list, val)` | `void(obj, obj)` | Add to end |
| `Insert(list, idx, val)` | `void(obj, i64, obj)` | Insert at index |
| `Remove(list, val)` | `bool(obj, obj)` | Remove first occurrence |
| `RemoveAt(list, idx)` | `void(obj, i64)` | Remove at index |
| `Clear(list)` | `void(obj)` | Clear all items |
| `Find(list, val)` | `i64(obj, obj)` | Find index (-1 if not found) |
| `Has(list, val)` | `bool(obj, obj)` | Check if contains |

### Viper.Collections.Map

Hash map with string keys.

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create empty map |
| `get_Len(map)` | `i64(obj)` | Get entry count |
| `get_IsEmpty(map)` | `bool(obj)` | Check if empty |
| `Get(map, key)` | `obj(obj, str)` | Get value (null if missing) |
| `GetOr(map, key, default)` | `obj(obj, str, obj)` | Get or return default |
| `Set(map, key, val)` | `void(obj, str, obj)` | Set key-value pair |
| `SetIfMissing(map, key, val)` | `bool(obj, str, obj)` | Set only if key missing |
| `Has(map, key)` | `bool(obj, str)` | Check if key exists |
| `Remove(map, key)` | `bool(obj, str)` | Remove key |
| `Clear(map)` | `void(obj)` | Clear all entries |
| `Keys(map)` | `obj(obj)` | Get all keys as Seq |
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
Viper.Collections.List.Add(numbers, 1);
Viper.Collections.List.Add(numbers, 2);
var count = Viper.Collections.List.get_Count(numbers);

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

## See Also

- [Runtime Reference (Main)](bible/appendices/d-runtime-reference.md) - Terminal, File, Math, Collections, Network, etc.
- [IL Runtime Specification](devdocs/runtime-vm.md) - Low-level VM runtime details
- [Zia Language Guide](zia-guide.md) - Zia programming language reference
