# Utilities

> Conversion, parsing, formatting, and logging utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Convert](#viperconvert)
- [Viper.Parse](#viperparse)
- [Viper.Fmt](#viperfmt)
- [Viper.Log](#viperlog)

---

## Viper.Convert

Type conversion utilities.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `ToInt64(text)` | `Integer(String)` | Parses a string as a 64-bit integer |
| `ToDouble(text)` | `Double(String)` | Parses a string as a double-precision float |
| `ToString_Int(value)` | `String(Integer)` | Converts an integer to its string representation |
| `ToString_Double(value)` | `String(Double)` | Converts a double to its string representation |

### Example

```basic
DIM s AS STRING
s = "12345"

DIM n AS INTEGER
n = Viper.Convert.ToInt64(s)
PRINT n + 1  ' Output: 12346

DIM d AS DOUBLE
d = Viper.Convert.ToDouble("3.14159")
PRINT d * 2  ' Output: 6.28318

' Convert back to string
PRINT Viper.Convert.ToString_Int(42)      ' Output: "42"
PRINT Viper.Convert.ToString_Double(2.5)  ' Output: "2.5"
```

---

## Viper.Parse

---

## Viper.Parse

Safe parsing utilities that never throw errors.

**Type:** Static utility class

### Philosophy

The `Viper.Parse` namespace provides safe parsing functions that handle invalid input gracefully:

- **TryXxx**: Parse and store result at pointer, return true/false
- **XxxOr**: Return parsed value or a default on failure
- **IsXxx**: Return true if string is valid for the type

These functions never trap or throw - they always return a usable result.

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `TryInt(text, outPtr)` | `Boolean(String, Ptr)` | Parse integer, store at pointer, return success |
| `TryNum(text, outPtr)` | `Boolean(String, Ptr)` | Parse number, store at pointer, return success |
| `TryBool(text, outPtr)` | `Boolean(String, Ptr)` | Parse boolean, store at pointer, return success |
| `IntOr(text, default)` | `Integer(String, Integer)` | Parse integer or return default |
| `NumOr(text, default)` | `Double(String, Double)` | Parse number or return default |
| `BoolOr(text, default)` | `Boolean(String, Boolean)` | Parse boolean or return default |
| `IsInt(text)` | `Boolean(String)` | Check if string is a valid integer |
| `IsNum(text)` | `Boolean(String)` | Check if string is a valid number |
| `IntRadix(text, radix, default)` | `Integer(String, Integer, Integer)` | Parse integer with base 2-36 |

### Boolean Parsing

`TryBool` and `BoolOr` accept the following values (case-insensitive):

- **True values:** `true`, `yes`, `1`, `on`
- **False values:** `false`, `no`, `0`, `off`

### Example

```basic
' Safe parsing with defaults
DIM age AS INTEGER
age = Viper.Parse.IntOr(userInput$, 0)  ' Returns 0 if invalid

' Check before parsing
IF Viper.Parse.IsInt(text$) THEN
    n = VAL(text$)
ELSE
    PRINT "Not a valid integer"
END IF

' Parse with explicit success check
DIM value AS INTEGER
IF Viper.Parse.TryInt(text$, @value) THEN
    PRINT "Parsed: "; value
ELSE
    PRINT "Parse failed"
END IF

' Parse hexadecimal
DIM hex AS INTEGER
hex = Viper.Parse.IntRadix("FF", 16, 0)  ' Returns 255

' Parse binary
DIM bin AS INTEGER
bin = Viper.Parse.IntRadix("1010", 2, 0)  ' Returns 10

' Parse boolean from config
DIM enabled AS BOOLEAN
enabled = Viper.Parse.BoolOr(config$, FALSE)  ' Default to false
```

### Notes

- Leading and trailing whitespace is trimmed before parsing
- Empty strings always fail to parse
- `IntRadix` supports radix values from 2 to 36
- Invalid radix values cause `IntRadix` to return the default

---

## Viper.Random

---

## Viper.Fmt

Value formatting utilities for converting numbers, booleans, and sizes to formatted strings.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Int(value)` | `String(Integer)` | Format integer as decimal string |
| `IntRadix(value, radix)` | `String(Integer, Integer)` | Format integer in specified base (2-36) |
| `IntPad(value, width, pad)` | `String(Integer, Integer, String)` | Format integer with minimum width and padding character |
| `Num(value)` | `String(Double)` | Format number as decimal string (default precision) |
| `NumFixed(value, decimals)` | `String(Double, Integer)` | Format number with fixed decimal places |
| `NumSci(value, decimals)` | `String(Double, Integer)` | Format number in scientific notation |
| `NumPct(value, decimals)` | `String(Double, Integer)` | Format number as percentage (0.5 = "50%") |
| `Bool(value)` | `String(Boolean)` | Format boolean as "true" or "false" |
| `BoolYN(value)` | `String(Boolean)` | Format boolean as "Yes" or "No" |
| `Size(bytes)` | `String(Integer)` | Format byte count as human-readable size |
| `Hex(value)` | `String(Integer)` | Format integer as lowercase hexadecimal |
| `HexPad(value, width)` | `String(Integer, Integer)` | Format integer as padded hexadecimal |
| `Bin(value)` | `String(Integer)` | Format integer as binary |
| `Oct(value)` | `String(Integer)` | Format integer as octal |

### Special Values

- `Num`, `NumFixed`, `NumSci`: Return `"NaN"` for NaN, `"Infinity"` or `"-Infinity"` for infinities
- `NumPct`: Appends `%` suffix (e.g., `"NaN%"` for NaN)
- `IntRadix`: Returns empty string for invalid radix (< 2 or > 36)

### Example

```basic
' Integer formatting
PRINT Viper.Fmt.Int(42)           ' "42"
PRINT Viper.Fmt.Int(-123)         ' "-123"
PRINT Viper.Fmt.IntPad(42, 5, "0") ' "00042"
PRINT Viper.Fmt.IntPad(42, 5, " ") ' "   42"

' Number formatting
PRINT Viper.Fmt.Num(3.14159)      ' "3.14159"
PRINT Viper.Fmt.NumFixed(3.14159, 2) ' "3.14"
PRINT Viper.Fmt.NumSci(1234.5, 2)  ' "1.23e+03"
PRINT Viper.Fmt.NumPct(0.75, 1)    ' "75.0%"

' Boolean formatting
PRINT Viper.Fmt.Bool(true)        ' "true"
PRINT Viper.Fmt.Bool(false)       ' "false"
PRINT Viper.Fmt.BoolYN(true)      ' "Yes"
PRINT Viper.Fmt.BoolYN(false)     ' "No"

' Size formatting
PRINT Viper.Fmt.Size(1024)        ' "1.0 KB"
PRINT Viper.Fmt.Size(1048576)     ' "1.0 MB"
PRINT Viper.Fmt.Size(1073741824)  ' "1.0 GB"

' Hex/Bin/Oct formatting
PRINT Viper.Fmt.Hex(255)          ' "ff"
PRINT Viper.Fmt.HexPad(255, 4)    ' "00ff"
PRINT Viper.Fmt.Bin(10)           ' "1010"
PRINT Viper.Fmt.Oct(63)           ' "77"

' Custom radix
PRINT Viper.Fmt.IntRadix(255, 16) ' "ff"
PRINT Viper.Fmt.IntRadix(10, 2)   ' "1010"
PRINT Viper.Fmt.IntRadix(35, 36)  ' "z"
```

---

## Viper.Log

---

## Viper.Log

Simple logging utilities for outputting diagnostic messages to stderr with automatic timestamps.

**Type:** Static utility class

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Level` | `Integer` (read/write) | Current log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=OFF) |
| `DEBUG` | `Integer` (read-only) | DEBUG level constant (0) |
| `INFO` | `Integer` (read-only) | INFO level constant (1) |
| `WARN` | `Integer` (read-only) | WARN level constant (2) |
| `ERROR` | `Integer` (read-only) | ERROR level constant (3) |
| `OFF` | `Integer` (read-only) | OFF level constant (4) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Debug(message)` | `Void(String)` | Log a debug message (level 0) |
| `Info(message)` | `Void(String)` | Log an info message (level 1) |
| `Warn(message)` | `Void(String)` | Log a warning message (level 2) |
| `Error(message)` | `Void(String)` | Log an error message (level 3) |
| `Enabled(level)` | `Boolean(Integer)` | Check if a level would be logged |

### Output Format

Messages are written to stderr with the format:
```
[LEVEL] HH:MM:SS message
```

### Notes

- Default level is INFO (1)
- Messages below the current level are suppressed
- Setting level to OFF (4) disables all logging
- Log output goes to stderr, not stdout
- Timestamps use local time in 24-hour format

### Example

```basic
' Log messages at different levels
Viper.Log.Debug("Starting initialization...")
Viper.Log.Info("Server started on port 8080")
Viper.Log.Warn("Configuration file not found, using defaults")
Viper.Log.Error("Failed to connect to database")

' Check current level
PRINT "Current level: "; Viper.Log.Level

' Set to DEBUG for verbose output
Viper.Log.Level = Viper.Log.DEBUG
Viper.Log.Debug("This will now appear")

' Set to ERROR to suppress most messages
Viper.Log.Level = Viper.Log.ERROR
Viper.Log.Info("This will be suppressed")
Viper.Log.Error("This will appear")

' Disable all logging
Viper.Log.Level = Viper.Log.OFF

' Check if a level is enabled before expensive formatting
IF Viper.Log.Enabled(Viper.Log.DEBUG) THEN
    DIM msg AS STRING = "Complex data: " + expensive_format()
    Viper.Log.Debug(msg)
END IF
```

### Example Output

```
[INFO] 14:23:45 Server started on port 8080
[WARN] 14:23:45 Configuration file not found, using defaults
[ERROR] 14:23:46 Failed to connect to database
```

---

## Viper.Machine

