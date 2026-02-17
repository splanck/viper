# Utilities

> Conversion, parsing, formatting, and logging utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Convert](#viperconvert)
- [Viper.Fmt](#viperfmt)
- [Viper.Log](#viperlog)
- [Viper.Parse](#viperparse)

---

## Viper.Convert

Type conversion utilities.

**Type:** Static utility class
**Runtime namespace:** `Viper.Core.Convert` (accessible as `Viper.Convert` via bind)

### Methods

| Method                   | Signature         | Description                                      |
|--------------------------|-------------------|--------------------------------------------------|
| `ToInt(text)`            | `Integer(String)` | Parses a string as a 64-bit integer (alias for ToInt64) |
| `ToInt64(text)`          | `Integer(String)` | Parses a string as a 64-bit integer              |
| `ToDouble(text)`         | `Double(String)`  | Parses a string as a double-precision float      |
| `NumToInt(value)`        | `Integer(Double)` | Converts a floating-point value to integer (truncates) |
| `ToString_Int(value)`    | `String(Integer)` | Converts an integer to its string representation |
| `ToString_Double(value)` | `String(Double)`  | Converts a double to its string representation   |

### Zia Example

```zia
module ConvertDemo;

bind Viper.Terminal;
bind Viper.Convert as Convert;

func start() {
    // Parse string to integer
    var n = Convert.ToInt64("12345");
    Say("Parsed: " + Convert.ToString_Int(n + 1));  // Output: Parsed: 12346

    // Parse string to double
    var d = Convert.ToDouble("3.14159");
    Say("Pi: " + Convert.ToString_Double(d));        // Output: Pi: 3.14159

    // Convert back to string
    Say(Convert.ToString_Int(42));                   // Output: 42
    Say(Convert.ToString_Double(2.5));               // Output: 2.5
}
```

### BASIC Example

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

## Viper.Fmt

String formatting utilities for converting values to formatted strings.

**Type:** Static utility class

### Methods

| Method                      | Signature                          | Description                              |
|-----------------------------|------------------------------------|------------------------------------------|
| `Int(value)`                | `String(Integer)`                  | Format integer as decimal string         |
| `IntRadix(value, radix)`    | `String(Integer, Integer)`         | Format integer in specified radix (2-36) |
| `IntPad(value, width, pad)` | `String(Integer, Integer, String)` | Format integer with padding              |
| `Num(value)`                | `String(Double)`                   | Format number with default precision     |
| `NumFixed(value, decimals)` | `String(Double, Integer)`          | Format number with fixed decimal places  |
| `NumSci(value, decimals)`   | `String(Double, Integer)`          | Format number in scientific notation     |
| `NumPct(value, decimals)`   | `String(Double, Integer)`          | Format number as percentage              |
| `Bool(value)`               | `String(Boolean)`                  | Format as "true" or "false"              |
| `BoolYN(value)`             | `String(Boolean)`                  | Format as "Yes" or "No"                  |
| `Size(bytes)`               | `String(Integer)`                  | Format byte count as human-readable size |
| `Hex(value)`                | `String(Integer)`                  | Format as lowercase hex                  |
| `HexPad(value, width)`      | `String(Integer, Integer)`         | Format as hex with zero-padding          |
| `Bin(value)`                | `String(Integer)`                  | Format as binary                         |
| `Oct(value)`                | `String(Integer)`                  | Format as octal                          |
| `IntGrouped(value, sep)`    | `String(Integer, String)`          | Format integer with thousands separator  |
| `Currency(value, dec, sym)` | `String(Double, Integer, String)`  | Format as currency with decimals and symbol |
| `ToWords(value)`            | `String(Integer)`                  | Convert integer to English words         |
| `Ordinal(value)`            | `String(Integer)`                  | Convert integer to ordinal (1st, 2nd, 3rd...) |

### Zia Example

```zia
module FmtDemo;

bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Integer formatting
    Say("Int: " + Fmt.Int(12345));           // Output: Int: 12345
    Say("Hex: " + Fmt.Hex(255));             // Output: Hex: ff
    Say("HexPad: " + Fmt.HexPad(255, 4));   // Output: HexPad: 00ff
    Say("Bin: " + Fmt.Bin(10));              // Output: Bin: 1010
    Say("Oct: " + Fmt.Oct(64));              // Output: Oct: 100
    Say("Padded: " + Fmt.IntPad(42, 5, "0"));  // Output: Padded: 00042

    // Number formatting
    Say("Fixed: " + Fmt.NumFixed(3.14159, 2));  // Output: Fixed: 3.14

    // Boolean formatting
    Say("Bool: " + Fmt.Bool(true));          // Output: Bool: true
    Say("YesNo: " + Fmt.BoolYN(false));      // Output: YesNo: No

    // Size formatting
    Say("Size: " + Fmt.Size(1048576));       // Output: Size: 1.0 MB
}
```

### BASIC Example

```basic
' Integer formatting
PRINT Viper.Fmt.Int(12345)              ' Output: "12345"
PRINT Viper.Fmt.IntRadix(255, 16)       ' Output: "ff"
PRINT Viper.Fmt.IntPad(42, 5, "0")      ' Output: "00042"

' Number formatting
PRINT Viper.Fmt.Num(3.14159)            ' Output: "3.14159"
PRINT Viper.Fmt.NumFixed(3.14159, 2)    ' Output: "3.14"
PRINT Viper.Fmt.NumSci(1234.5, 2)       ' Output: "1.23e+03"
PRINT Viper.Fmt.NumPct(0.756, 1)        ' Output: "75.6%"

' Boolean formatting
PRINT Viper.Fmt.Bool(TRUE)              ' Output: "true"
PRINT Viper.Fmt.BoolYN(FALSE)           ' Output: "No"

' Size formatting (auto-scales to KB, MB, GB, etc.)
PRINT Viper.Fmt.Size(1024)              ' Output: "1.0 KB"
PRINT Viper.Fmt.Size(1048576)           ' Output: "1.0 MB"
PRINT Viper.Fmt.Size(1234567890)        ' Output: "1.1 GB"

' Radix formatting
PRINT Viper.Fmt.Hex(255)                ' Output: "ff"
PRINT Viper.Fmt.HexPad(255, 4)          ' Output: "00ff"
PRINT Viper.Fmt.Bin(10)                 ' Output: "1010"
PRINT Viper.Fmt.Oct(64)                 ' Output: "100"
```

---

## Viper.Log

Structured logging with configurable log levels.

**Type:** Static utility class

### Log Levels

| Constant | Value | Description                    |
|----------|-------|--------------------------------|
| `DEBUG`  | 0     | Detailed debugging information |
| `INFO`   | 1     | General informational messages |
| `WARN`   | 2     | Warning conditions             |
| `ERROR`  | 3     | Error conditions               |
| `OFF`    | 4     | Disable all logging            |

### Properties

| Property | Type                   | Description                                                    |
|----------|------------------------|----------------------------------------------------------------|
| `Level`  | `Integer` (read/write) | Current minimum log level (messages below this are suppressed) |

### Methods

| Method           | Signature          | Description                     |
|------------------|--------------------|---------------------------------|
| `Debug(message)` | `Void(String)`     | Log a debug message             |
| `Info(message)`  | `Void(String)`     | Log an info message             |
| `Warn(message)`  | `Void(String)`     | Log a warning message           |
| `Error(message)` | `Void(String)`     | Log an error message            |
| `Enabled(level)` | `Boolean(Integer)` | Check if a log level is enabled |

### Notes

- Default log level is `INFO` (debug messages suppressed)
- Log output goes to stderr with timestamp and level prefix
- Format: `[LEVEL] YYYY-MM-DD HH:MM:SS message`
- Set `Level = Viper.Log.OFF` to disable all logging

### Zia Example

```zia
module LogDemo;

bind Viper.Terminal;
bind Viper.Log;

func start() {
    // Log at various levels (output goes to stderr)
    Info("Application started");
    Warn("Configuration file not found, using defaults");
    Error("Failed to connect to database");

    // Debug messages are suppressed by default (level < INFO)
    Debug("This won't appear");

    Say("Log demo complete");
}
```

### BASIC Example

```basic
' Basic logging
Viper.Log.Info("Application started")
Viper.Log.Warn("Configuration file not found, using defaults")
Viper.Log.Error("Failed to connect to database")

' Debug logging (suppressed by default)
Viper.Log.Debug("Entering function processData")

' Enable debug logging
Viper.Log.Level = Viper.Log.DEBUG
Viper.Log.Debug("Now this message appears")

' Check if level is enabled before expensive formatting
IF Viper.Log.Enabled(Viper.Log.DEBUG) THEN
    Viper.Log.Debug("User count: " + Viper.Fmt.Int(userCount))
END IF

' Suppress all logging
Viper.Log.Level = Viper.Log.OFF
```

---

## Viper.Parse

Safe parsing utilities with error handling. Unlike `Viper.Convert` which traps on invalid input, these methods allow
graceful error handling.

**Type:** Static utility class
**Runtime namespace:** `Viper.Core.Parse` (accessible as `Viper.Parse` via bind)

### Methods

| Method                           | Signature                           | Description                               |
|----------------------------------|-------------------------------------|-------------------------------------------|
| `TryInt(text, outPtr)`           | `Boolean(String, Ptr)`              | Try to parse integer, return success      |
| `TryNum(text, outPtr)`           | `Boolean(String, Ptr)`              | Try to parse double, return success       |
| `TryBool(text, outPtr)`          | `Boolean(String, Ptr)`              | Try to parse boolean, return success      |
| `IntOr(text, default)`           | `Integer(String, Integer)`          | Parse integer or return default           |
| `NumOr(text, default)`           | `Double(String, Double)`            | Parse double or return default            |
| `BoolOr(text, default)`          | `Boolean(String, Boolean)`          | Parse boolean or return default           |
| `IsInt(text)`                    | `Boolean(String)`                   | Check if string is a valid integer        |
| `IsNum(text)`                    | `Boolean(String)`                   | Check if string is a valid number         |
| `IntRadix(text, radix, default)` | `Integer(String, Integer, Integer)` | Parse integer in given radix with default |

### Notes

- **Boolean parsing** accepts (case-insensitive):
    - True: `"true"`, `"yes"`, `"1"`, `"on"`
    - False: `"false"`, `"no"`, `"0"`, `"off"`
- `IsInt` and `IsNum` accept optional leading `+` or `-`
- `TryInt`, `TryNum`, and `TryBool` require an output pointer; frontends without pointer/out parameters should use
  `IntOr`, `NumOr`, `BoolOr`, `IsInt`, or `IsNum` instead.
- `IntRadix` supports radix 2-36 (digits 0-9, letters a-z)
- Leading/trailing whitespace is trimmed before parsing

### Zia Example

```zia
module ParseDemo;

bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Parse with default fallback
    var port = Viper.Parse.IntOr("8080", 3000);
    Say("Port: " + Fmt.Int(port));             // Output: Port: 8080

    var timeout = Viper.Parse.NumOr("invalid", 30.0);
    Say("Timeout: " + Fmt.Num(timeout));       // Output: Timeout: 30

    // Validate input
    if Viper.Parse.IsInt("42") {
        Say("42 is a valid integer");          // Output: 42 is a valid integer
    }
    if !Viper.Parse.IsInt("hello") {
        Say("hello is not a valid integer");   // Output: hello is not a valid integer
    }

    // Boolean parsing
    var enabled = Viper.Parse.BoolOr("yes", false);
    Say("Enabled: " + Fmt.Bool(enabled));      // Output: Enabled: true

    // Hex parsing with default
    var color = Viper.Parse.IntRadix("ff", 16, 0);
    Say("Color: " + Fmt.Int(color));           // Output: Color: 255
}
```

### BASIC Example

```basic
' Parse with default fallback
DIM port AS INTEGER = Viper.Parse.IntOr(portStr, 8080)
DIM timeout AS DOUBLE = Viper.Parse.NumOr(timeoutStr, 30.0)
DIM verbose AS INTEGER = Viper.Parse.BoolOr(verboseStr, FALSE)

' Validate before use
IF Viper.Parse.IsInt(userInput) THEN
    DIM value AS INTEGER = Viper.Convert.ToInt64(userInput)
    PRINT "Valid integer: "; value
ELSE
    PRINT "Invalid input"
END IF

' Parse hex value with default
DIM color AS INTEGER = Viper.Parse.IntRadix("ff0000", 16, 0)

' Parse boolean from config
DIM enabled AS INTEGER = Viper.Parse.BoolOr("yes", FALSE)
PRINT enabled  ' Output: 1 (true)
```

### Comparison with Viper.Convert

| Scenario                      | Use               |
|-------------------------------|-------------------|
| Trusted input (internal data) | `Viper.Convert`   |
| User input (may be invalid)   | `Viper.Parse`     |
| Need default value            | `Viper.Parse.*Or` |
| Need validation only          | `Viper.Parse.Is*` |

---

## See Also

- [Core Types](core.md) - `String` manipulation methods
- [Text Processing](text.md) - `Codec` for encoding, `StringBuilder` for building strings
