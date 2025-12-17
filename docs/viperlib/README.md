# Viper Runtime Library Reference

> **Version:** 0.1.2
> **Status:** Pre-Alpha — API subject to change

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are implemented in C and exposed through the IL runtime system.

---

## Quick Navigation

| Module | Description |
|--------|-------------|
| [Architecture](architecture.md) | Runtime internals, type reference |
| [Collections](collections.md) | `Bag`, `Bytes`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Stack`, `TreeMap` |
| [Core Types](core.md) | `Object`, `String` — foundational types |
| [Cryptography](crypto.md) | `Hash` (CRC32, MD5, SHA1, SHA256) |
| [Diagnostics](diagnostics.md) | `Assert`, `Stopwatch` |
| [Graphics](graphics.md) | `Canvas`, `Color`, `Pixels` |
| [Input/Output](io.md) | `BinFile`, `Dir`, `File`, `LineReader`, `LineWriter`, `Path` |
| [Mathematics](math.md) | `Bits`, `Math`, `Random`, `Vec2`, `Vec3` |
| [System](system.md) | `Environment`, `Exec`, `Machine`, `Terminal` |
| [Text Processing](text.md) | `Codec`, `Csv`, `Guid`, `StringBuilder` |
| [Time & Timing](time.md) | `Clock`, `Countdown`, `DateTime`, `Stopwatch` |
| [Utilities](utilities.md) | `Convert`, `Fmt`, `Log`, `Parse` |

---

## Namespace Overview

### Viper (Root)

| Class | Type | Description |
|-------|------|-------------|
| [`Bits`](math.md#viperbits) | Static | Bit manipulation (shifts, rotates, counting) |
| [`Convert`](utilities.md#viperconvert) | Static | Type conversion utilities |
| [`DateTime`](time.md#viperdatetime) | Static | Date and time operations |
| [`Environment`](system.md#viperenvironment) | Static | Command-line args and environment |
| [`Exec`](system.md#viperexec) | Static | External command execution |
| [`Fmt`](utilities.md#viperfmt) | Static | String formatting |
| [`Log`](utilities.md#viperlog) | Static | Logging utilities |
| [`Machine`](system.md#vipermachine) | Static | System information queries |
| [`Math`](math.md#vipermath) | Static | Mathematical functions (trig, pow, abs, etc.) |
| [`Object`](core.md#viperobject) | Base | Root type for all reference types |
| [`Parse`](utilities.md#viperparse) | Static | String parsing utilities |
| [`Random`](math.md#viperrandom) | Static | Random number generation |
| [`String`](core.md#viperstring) | Instance | Immutable string with manipulation methods |
| [`Terminal`](system.md#viperterminal) | Static | Terminal input/output |
| [`Vec2`](math.md#vipervec2) | Instance | 2D vector math |
| [`Vec3`](math.md#vipervec3) | Instance | 3D vector math |

### Viper.Collections

| Class | Type | Description |
|-------|------|-------------|
| [`Bag`](collections.md#vipercollectionsbag) | Instance | String set with set operations |
| [`Bytes`](collections.md#vipercollectionsbytes) | Instance | Byte array for binary data |
| [`List`](collections.md#vipercollectionslist) | Instance | Dynamic array of objects |
| [`Map`](collections.md#vipercollectionsmap) | Instance | String-keyed hash map |
| [`Queue`](collections.md#vipercollectionsqueue) | Instance | FIFO collection |
| [`Ring`](collections.md#vipercollectionsring) | Instance | Fixed-size circular buffer |
| [`Seq`](collections.md#vipercollectionsseq) | Instance | Growable array with stack/queue ops |
| [`Stack`](collections.md#vipercollectionsstack) | Instance | LIFO collection |
| [`TreeMap`](collections.md#vipercollectionstreemap) | Instance | Sorted key-value map |

### Viper.Crypto

| Class | Type | Description |
|-------|------|-------------|
| [`Hash`](crypto.md#vipercryptohash) | Static | CRC32, MD5, SHA1, SHA256 |

### Viper.Diagnostics

| Class | Type | Description |
|-------|------|-------------|
| [`Assert`](diagnostics.md#viperdiagnosticsassert) | Static | Assertion checking |
| [`Stopwatch`](time.md#viperdiagnosticsstopwatch) | Instance | Performance timing |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| [`Canvas`](graphics.md#vipergraphicscanvas) | Instance | 2D graphics canvas |
| [`Color`](graphics.md#vipergraphicscolor) | Static | Color creation |
| [`Pixels`](graphics.md#vipergraphicspixels) | Instance | Software image buffer |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| [`BinFile`](io.md#viperiobinfile) | Instance | Binary file stream |
| [`Dir`](io.md#viperiodir) | Static | Directory operations |
| [`File`](io.md#viperiofile) | Static | File read/write/delete |
| [`LineReader`](io.md#viperiolinereader) | Instance | Line-by-line text reading |
| [`LineWriter`](io.md#viperiolinewriter) | Instance | Buffered text writing |
| [`Path`](io.md#viperiopath) | Static | Path manipulation |

### Viper.Text

| Class | Type | Description |
|-------|------|-------------|
| [`Codec`](text.md#vipertextcodec) | Static | Base64, Hex, URL encoding |
| [`Csv`](text.md#vipertextcsv) | Static | CSV parsing and formatting |
| [`Guid`](text.md#vipertextguid) | Static | UUID v4 generation |
| [`StringBuilder`](text.md#vipertextstringbuilder) | Instance | Mutable string builder |

### Viper.Time

| Class | Type | Description |
|-------|------|-------------|
| [`Clock`](time.md#vipertimeclock) | Static | Sleep and tick counting |
| [`Countdown`](time.md#vipertimecountdown) | Instance | Interval timing |

---

## Class Types

- **Instance** — Requires instantiation with `NEW` before use
- **Static** — Methods called directly on the class name (no instantiation)
- **Base** — Cannot be instantiated directly; inherited by other types

---

## Which Collection Should I Use?

| Need | Use | Why |
|------|-----|-----|
| Binary data | `Bytes` | Efficient byte manipulation |
| FIFO (first-in-first-out) | `Queue` | Enqueue/dequeue interface |
| Fixed-size buffer | `Ring` | Overwrites oldest when full |
| Indexed array | `Seq` | Fast random access, push/pop |
| Key-value pairs | `Map` | O(1) lookup by string key |
| Legacy compatibility | `List` | Similar to VB6 Collection |
| LIFO (last-in-first-out) | `Stack` | Simple push/pop interface |
| Sorted key-value | `TreeMap` | Keys in sorted order, floor/ceil queries |
| Unique strings | `Bag` | Set operations (union, intersect, diff) |

---

## See Also

- [BASIC Language Reference](../basic-reference.md)
- [IL Guide](../il-guide.md)
- [Getting Started](../getting-started.md)
