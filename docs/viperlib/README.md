# Viper Runtime Library Reference

> **Version:** 0.1.2
> **Status:** Pre-Alpha — API subject to change

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are implemented in C and exposed through the IL runtime system.

---

## Quick Navigation

| Module | Description |
|--------|-------------|
| [Core Types](core.md) | `String`, `Object` — foundational types |
| [Collections](collections.md) | `List`, `Map`, `Seq`, `Stack`, `Queue`, `Bytes`, `Bag`, `Ring`, `TreeMap` |
| [Input/Output](io.md) | `File`, `Path`, `Dir`, `BinFile`, `LineReader`, `LineWriter` |
| [Text Processing](text.md) | `StringBuilder`, `Codec`, `Csv`, `Guid` |
| [Mathematics](math.md) | `Math`, `Bits`, `Random`, `Vec2`, `Vec3` |
| [Time & Timing](time.md) | `DateTime`, `Clock`, `Countdown`, `Stopwatch` |
| [System](system.md) | `Terminal`, `Environment`, `Exec`, `Machine` |
| [Utilities](utilities.md) | `Convert`, `Parse`, `Fmt`, `Log` |
| [Graphics](graphics.md) | `Canvas`, `Color`, `Pixels` |
| [Cryptography](crypto.md) | `Hash` (MD5, SHA1, SHA256, CRC32) |
| [Diagnostics](diagnostics.md) | `Assert` |
| [Architecture](architecture.md) | Runtime internals, type reference |

---

## Namespace Overview

### Viper (Root)

| Class | Type | Description |
|-------|------|-------------|
| [`String`](core.md#viperstring) | Instance | Immutable string with manipulation methods |
| [`Object`](core.md#viperobject) | Base | Root type for all reference types |
| [`Math`](math.md#vipermath) | Static | Mathematical functions (trig, pow, abs, etc.) |
| [`Bits`](math.md#viperbits) | Static | Bit manipulation (shifts, rotates, counting) |
| [`Random`](math.md#viperrandom) | Static | Random number generation |
| [`Vec2`](math.md#vipervec2) | Instance | 2D vector math |
| [`Vec3`](math.md#vipervec3) | Instance | 3D vector math |
| [`Terminal`](system.md#viperterminal) | Static | Terminal input/output |
| [`Environment`](system.md#viperenvironment) | Static | Command-line args and environment |
| [`Exec`](system.md#viperexec) | Static | External command execution |
| [`Machine`](system.md#vipermachine) | Static | System information queries |
| [`DateTime`](time.md#viperdatetime) | Static | Date and time operations |
| [`Convert`](utilities.md#viperconvert) | Static | Type conversion utilities |
| [`Parse`](utilities.md#viperparse) | Static | String parsing utilities |
| [`Fmt`](utilities.md#viperfmt) | Static | String formatting |
| [`Log`](utilities.md#viperlog) | Static | Logging utilities |

### Viper.Text

| Class | Type | Description |
|-------|------|-------------|
| [`StringBuilder`](text.md#vipertextstringbuilder) | Instance | Mutable string builder |
| [`Codec`](text.md#vipertextcodec) | Static | Base64, Hex, URL encoding |
| [`Csv`](text.md#vipertextcsv) | Static | CSV parsing and formatting |
| [`Guid`](text.md#vipertextguid) | Static | UUID v4 generation |

### Viper.Collections

| Class | Type | Description |
|-------|------|-------------|
| [`List`](collections.md#vipercollectionslist) | Instance | Dynamic array of objects |
| [`Map`](collections.md#vipercollectionsmap) | Instance | String-keyed hash map |
| [`Seq`](collections.md#vipercollectionsseq) | Instance | Growable array with stack/queue ops |
| [`Stack`](collections.md#vipercollectionsstack) | Instance | LIFO collection |
| [`Queue`](collections.md#vipercollectionsqueue) | Instance | FIFO collection |
| [`TreeMap`](collections.md#vipercollectionstreemap) | Instance | Sorted key-value map |
| [`Bytes`](collections.md#vipercollectionsbytes) | Instance | Byte array for binary data |
| [`Bag`](collections.md#vipercollectionsbag) | Instance | String set with set operations |
| [`Ring`](collections.md#vipercollectionsring) | Instance | Fixed-size circular buffer |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| [`File`](io.md#viperiofile) | Static | File read/write/delete |
| [`Path`](io.md#viperiopath) | Static | Path manipulation |
| [`Dir`](io.md#viperiodir) | Static | Directory operations |
| [`BinFile`](io.md#viperiobinfile) | Instance | Binary file stream |
| [`LineReader`](io.md#viperiolinereader) | Instance | Line-by-line text reading |
| [`LineWriter`](io.md#viperiolinewriter) | Instance | Buffered text writing |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| [`Canvas`](graphics.md#vipergraphicscanvas) | Instance | 2D graphics canvas |
| [`Color`](graphics.md#vipergraphicscolor) | Static | Color creation |
| [`Pixels`](graphics.md#vipergraphicspixels) | Instance | Software image buffer |

### Viper.Time

| Class | Type | Description |
|-------|------|-------------|
| [`Clock`](time.md#vipertimeclock) | Static | Sleep and tick counting |
| [`Countdown`](time.md#vipertimecountdown) | Instance | Interval timing |

### Viper.Diagnostics

| Class | Type | Description |
|-------|------|-------------|
| [`Assert`](diagnostics.md#viperdiagnosticsassert) | Static | Assertion checking |
| [`Stopwatch`](time.md#viperdiagnosticsstopwatch) | Instance | Performance timing |

### Viper.Crypto

| Class | Type | Description |
|-------|------|-------------|
| [`Hash`](crypto.md#vipercryptohash) | Static | MD5, SHA1, SHA256, CRC32 |

---

## Class Types

- **Instance** — Requires instantiation with `NEW` before use
- **Static** — Methods called directly on the class name (no instantiation)
- **Base** — Cannot be instantiated directly; inherited by other types

---

## Which Collection Should I Use?

| Need | Use | Why |
|------|-----|-----|
| Indexed array | `Seq` | Fast random access, push/pop |
| Key-value pairs | `Map` | O(1) lookup by string key |
| Sorted key-value | `TreeMap` | Keys in sorted order, floor/ceil queries |
| LIFO (last-in-first-out) | `Stack` | Simple push/pop interface |
| FIFO (first-in-first-out) | `Queue` | Enqueue/dequeue interface |
| Unique strings | `Bag` | Set operations (union, intersect, diff) |
| Binary data | `Bytes` | Efficient byte manipulation |
| Fixed-size buffer | `Ring` | Overwrites oldest when full |
| Legacy compatibility | `List` | Similar to VB6 Collection |

---

## See Also

- [BASIC Language Reference](../basic-reference.md)
- [IL Guide](../il-guide.md)
- [Getting Started](../getting-started.md)
