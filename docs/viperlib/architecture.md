# Runtime Architecture

> Internal architecture and type system reference.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Runtime Architecture](#runtime-architecture)
- [Type Reference](#type-reference)

---

## Runtime Architecture

### Overview

The Viper runtime is defined in `src/il/runtime/runtime.def` using X-macros. This single source of truth generates:

- `RuntimeNameMap.inc` — Maps canonical names to C symbols
- `RuntimeClasses.inc` — OOP class catalog for the type system

### RT_FUNC Syntax

```
RT_FUNC(id, c_symbol, "canonical_name", "signature")
```

- **id**: Unique C++ identifier used in generated code
- **c_symbol**: The C function name (rt_* prefix by convention)
- **canonical_name**: The Viper namespace path (e.g., "Viper.Math.Sin")
- **signature**: IL type signature using type abbreviations

### RT_CLASS Syntax

```
RT_CLASS_BEGIN("canonical_name", type_id, "layout", ctor_id)
    RT_PROP("name", "type", getter_id, setter_id_or_none)
    RT_METHOD("name", "signature", target_id)
RT_CLASS_END()
```

Classes define the OOP interface exposed to Viper languages. Method signatures omit the receiver (arg0).

### Type Abbreviations

| Abbrev | Type | Size |
|--------|------|------|
| `void` | No value | 0 |
| `i1` | Boolean | 1 bit |
| `i8` | Signed byte | 8 bits |
| `i16` | Short integer | 16 bits |
| `i32` | Integer | 32 bits |
| `i64` | Long integer | 64 bits |
| `f32` | Single float | 32 bits |
| `f64` | Double float | 64 bits |
| `str` | String | pointer |
| `obj` | Object | pointer |
| `ptr` | Raw pointer | pointer |

### Quick Reference

| Class | Description |
|-------|-------------|
| `Viper.Object` | Base class with Equals, GetHashCode, ToString |
| `Viper.String` | String manipulation (Substring, Trim, Replace, etc.) |
| `Viper.Strings` | Static string utilities (Join, FromInt, etc.) |
| `Viper.Math` | Math functions (Sin, Cos, Sqrt, etc.) and constants (Pi, E) |
| `Viper.Terminal` | Terminal I/O (Say, Print, Ask, ReadLine) |
| `Viper.Convert` | Type conversion (ToInt, ToDouble) |
| `Viper.Environment` | Command-line args, environment variables, process exit |
| `Viper.Random` | Random number generation |
| `Viper.Collections.Seq` | Dynamic array with Push, Pop, Get, Set |
| `Viper.Collections.Stack` | LIFO with Push, Pop, Peek |
| `Viper.Collections.Queue` | FIFO with Add, Take, Peek |
| `Viper.Collections.Map` | String-keyed dictionary |
| `Viper.Collections.Bytes` | Efficient byte array |
| `Viper.Collections.Bag` | String set with union, intersection, difference |
| `Viper.Collections.Ring` | Fixed-size circular buffer (overwrites oldest) |
| `Viper.Collections.List` | Dynamic list of objects |
| `Viper.IO.File` | File read/write/copy/delete |
| `Viper.IO.Dir` | Directory create/list/delete |
| `Viper.IO.Path` | Path join/split/normalize |
| `Viper.IO.BinFile` | Binary file stream with random access |
| `Viper.IO.LineReader` | Line-by-line text file reading |
| `Viper.IO.LineWriter` | Buffered text file writing |
| `Viper.Text.StringBuilder` | Efficient string concatenation |
| `Viper.Text.Codec` | Base64, Hex, URL encoding/decoding |
| `Viper.Text.Guid` | UUID v4 generation |
| `Viper.Crypto.Hash` | MD5, SHA1, SHA256, CRC32 hashing |
| `Viper.Graphics.Canvas` | Window and 2D drawing |
| `Viper.Graphics.Color` | RGB/RGBA color creation |
| `Viper.Graphics.Pixels` | Software image buffer for pixel manipulation |
| `Viper.Time.Clock` | Sleep and tick counting |
| `Viper.DateTime` | Date/time creation and formatting |
| `Viper.Diagnostics.Stopwatch` | Benchmarking timer |

---

## Type Reference

| Viper Type | IL Type | Description |
|------------|---------|-------------|
| `Integer` | `i64` | 64-bit signed integer |
| `Double` | `f64` | 64-bit floating point |
| `Boolean` | `i1` | Boolean (0 or 1) |
| `String` | `str` | Immutable string |
| `Object` | `obj` | Reference to any object |
| `Void` | `void` | No return value |

