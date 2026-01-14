# Viper Runtime Library Reference

> **Version:** 0.1.2
> **Status:** Pre-Alpha — API subject to change

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are
implemented in C and exposed through the IL runtime system.

---

## Quick Navigation

| Module                          | Description                                                               |
|---------------------------------|---------------------------------------------------------------------------|
| [Architecture](architecture.md) | Runtime internals, type reference                                         |
| [Audio](audio.md)               | `Sound`, `Music` — audio playback for games and applications              |
| [Collections](collections.md)   | `Bag`, `Bytes`, `Heap`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Stack`, `TreeMap` |
| [Core Types](core.md)           | `Object`, `Box`, `String` — foundational types                             |
| [Cryptography](crypto.md)       | `Hash`, `KeyDerive`, `Rand`                                               |
| [Diagnostics](diagnostics.md)   | `Assert`, `Trap`, `Stopwatch`                                             |
| [Graphics](graphics.md)         | `Canvas`, `Color`, `Pixels`, `Sprite`, `Tilemap`, `Camera`                |
| [GUI](gui.md)                   | `App`, `Button`, `Label`, widgets — GUI toolkit for applications          |
| [Input](input.md)               | `Keyboard`, `Mouse`, `Pad` — input for games and interactive apps       |
| [Input/Output](io.md)           | `Archive`, `BinFile`, `Compress`, `Dir`, `File`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Watcher` |
| [Mathematics](math.md)          | `Bits`, `Math`, `Random`, `Vec2`, `Vec3`                                  |
| [Network](network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `Tcp`, `TcpServer`, `Udp`, `Url`      |
| [System](system.md)             | `Environment`, `Exec`, `Machine`, `Terminal`                              |
| [Text Processing](text.md)      | `Codec`, `Csv`, `Guid`, `Pattern`, `StringBuilder`, `Template`            |
| [Threads](threads.md)           | `Barrier`, `Gate`, `Monitor`, `RwLock`, `SafeI64`, `Thread`               |
| [Time & Timing](time.md)        | `Clock`, `Countdown`, `DateTime`, `Stopwatch`                             |
| [Utilities](utilities.md)       | `Convert`, `Fmt`, `Log`, `Parse`                                          |

---

## Namespace Overview

### Viper (Root)

| Class                                       | Type     | Description                                   |
|---------------------------------------------|----------|-----------------------------------------------|
| [`Bits`](math.md#viperbits)                 | Static   | Bit manipulation (shifts, rotates, counting)  |
| [`Box`](core.md#viperbox)                   | Static   | Boxing helpers for generic collections         |
| [`Convert`](utilities.md#viperconvert)      | Static   | Type conversion utilities                     |
| [`DateTime`](time.md#viperdatetime)         | Static   | Date and time operations                      |
| [`Environment`](system.md#viperenvironment) | Static   | Command-line args and environment             |
| [`Exec`](system.md#viperexec)               | Static   | External command execution                    |
| [`Fmt`](utilities.md#viperfmt)              | Static   | String formatting                             |
| [`Log`](utilities.md#viperlog)              | Static   | Logging utilities                             |
| [`Machine`](system.md#vipermachine)         | Static   | System information queries                    |
| [`Math`](math.md#vipermath)                 | Static   | Mathematical functions (trig, pow, abs, etc.) |
| [`Object`](core.md#viperobject)             | Base     | Root type for all reference types             |
| [`Parse`](utilities.md#viperparse)          | Static   | String parsing utilities                      |
| [`Random`](math.md#viperrandom)             | Static   | Random number generation                      |
| [`String`](core.md#viperstring)             | Instance | Immutable string with manipulation methods    |
| [`Terminal`](system.md#viperterminal)       | Static   | Terminal input/output                         |
| [`Vec2`](math.md#vipervec2)                 | Instance | 2D vector math                                |
| [`Vec3`](math.md#vipervec3)                 | Instance | 3D vector math                                |

### Viper.Collections

| Class                                               | Type     | Description                         |
|-----------------------------------------------------|----------|-------------------------------------|
| [`Bag`](collections.md#vipercollectionsbag)         | Instance | String set with set operations      |
| [`Bytes`](collections.md#vipercollectionsbytes)     | Instance | Byte array for binary data          |
| [`List`](collections.md#vipercollectionslist)       | Instance | Dynamic array of objects            |
| [`Map`](collections.md#vipercollectionsmap)         | Instance | String-keyed hash map               |
| [`Queue`](collections.md#vipercollectionsqueue)     | Instance | FIFO collection                     |
| [`Heap`](collections.md#vipercollectionsheap)       | Instance | Priority queue (min/max heap)       |
| [`Ring`](collections.md#vipercollectionsring)       | Instance | Fixed-size circular buffer          |
| [`Seq`](collections.md#vipercollectionsseq)         | Instance | Growable array with stack/queue ops |
| [`Stack`](collections.md#vipercollectionsstack)     | Instance | LIFO collection                     |
| [`TreeMap`](collections.md#vipercollectionstreemap) | Instance | Sorted key-value map                |

### Viper.Crypto

| Class                                         | Type   | Description                              |
|-----------------------------------------------|--------|------------------------------------------|
| [`Hash`](crypto.md#vipercryptohash)           | Static | CRC32, MD5, SHA1, SHA256                 |
| [`KeyDerive`](crypto.md#vipercryptokeyderive) | Static | PBKDF2-SHA256 key derivation             |
| [`Rand`](crypto.md#vipercryptorand)           | Static | Cryptographically secure random bytes    |

### Viper.Diagnostics

| Class                                             | Type     | Description        |
|---------------------------------------------------|----------|--------------------|
| [`Assert`](diagnostics.md#viperdiagnostics)       | Static   | Assertion checking |
| [`Trap`](diagnostics.md#viperdiagnostics)         | Static   | Unconditional trap |
| [`Stopwatch`](time.md#viperdiagnosticsstopwatch)  | Instance | Performance timing |

### Viper.Audio

| Class                                     | Type     | Description                      |
|-------------------------------------------|----------|----------------------------------|
| [`Sound`](audio.md#viperaudiosoound)      | Instance | Sound effects for short clips    |
| [`Music`](audio.md#viperaudiomusic)       | Instance | Streaming music playback         |
| [`Audio`](audio.md#viperaudio-static)     | Static   | Global audio control             |

### Viper.Graphics

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Canvas`](graphics.md#vipergraphicscanvas)   | Instance | 2D graphics canvas               |
| [`Color`](graphics.md#vipergraphicscolor)     | Static   | Color creation                   |
| [`Pixels`](graphics.md#vipergraphicspixels)   | Instance | Software image buffer            |
| [`Sprite`](graphics.md#vipergraphicssprite)   | Instance | 2D sprite with animation         |
| [`Tilemap`](graphics.md#vipergraphicstilemap) | Instance | Tile-based game maps             |
| [`Camera`](graphics.md#vipergraphicscamera)   | Instance | 2D camera for scrolling/zoom     |

### Viper.GUI

| Class                                               | Type     | Description                       |
|-----------------------------------------------------|----------|-----------------------------------|
| [`App`](gui.md#vipergui-app)                        | Instance | Main application window           |
| [`Font`](gui.md#vipergui-font)                      | Instance | Font for text rendering           |
| [`Label`](gui.md#vipergui-label)                    | Instance | Text display widget               |
| [`Button`](gui.md#vipergui-button)                  | Instance | Clickable button widget           |
| [`TextInput`](gui.md#vipergui-textinput)            | Instance | Single-line text entry            |
| [`Checkbox`](gui.md#vipergui-checkbox)              | Instance | Boolean toggle widget             |
| [`RadioButton`](gui.md#vipergui-radiobutton)        | Instance | Single-select option widget       |
| [`Slider`](gui.md#vipergui-slider)                  | Instance | Numeric range slider              |
| [`Spinner`](gui.md#vipergui-spinner)                | Instance | Numeric spinner control           |
| [`ProgressBar`](gui.md#vipergui-progressbar)        | Instance | Progress indicator                |
| [`Dropdown`](gui.md#vipergui-dropdown)              | Instance | Drop-down selection               |
| [`ListBox`](gui.md#vipergui-listbox)                | Instance | Scrollable list selection         |
| [`ScrollView`](gui.md#vipergui-scrollview)          | Instance | Scrollable container              |
| [`SplitPane`](gui.md#vipergui-splitpane)            | Instance | Resizable split container         |
| [`TabBar`](gui.md#vipergui-tabbar)                  | Instance | Tabbed container                  |
| [`TreeView`](gui.md#vipergui-treeview)              | Instance | Hierarchical tree widget          |
| [`CodeEditor`](gui.md#vipergui-codeeditor)          | Instance | Code editing with syntax coloring |

### Viper.Input

| Class                                         | Type   | Description                      |
|-----------------------------------------------|--------|----------------------------------|
| [`Keyboard`](input.md#viperinputkeyboard)     | Static | Keyboard input for games and UI  |
| [`Mouse`](input.md#viperinputmouse)           | Static | Mouse input for games and UI     |
| [`Pad`](input.md#viperinputpad)               | Static | Gamepad/controller input         |

### Viper.IO

| Class                                   | Type     | Description                    |
|-----------------------------------------|----------|--------------------------------|
| [`Archive`](io.md#viperioarchive)       | Instance | ZIP archive read/write         |
| [`BinFile`](io.md#viperiobinfile)       | Instance | Binary file stream             |
| [`Compress`](io.md#viperiocompress)     | Static   | DEFLATE/GZIP compression       |
| [`Dir`](io.md#viperiodir)               | Static   | Directory operations           |
| [`File`](io.md#viperiofile)             | Static   | File read/write/delete         |
| [`LineReader`](io.md#viperiolinereader) | Instance | Line-by-line text reading      |
| [`LineWriter`](io.md#viperiolinewriter) | Instance | Buffered text writing          |
| [`MemStream`](io.md#viperiomemstream)   | Instance | In-memory binary stream        |
| [`Path`](io.md#viperiopath)             | Static   | Path manipulation              |
| [`Watcher`](io.md#viperiowatcher)       | Instance | File system event monitoring   |

### Viper.Network

| Class                                           | Type     | Description                           |
|-------------------------------------------------|----------|---------------------------------------|
| [`Dns`](network.md#vipernetworkdns)             | Static   | DNS resolution and validation         |
| [`Http`](network.md#vipernetworkhttp)           | Static   | Simple HTTP helpers                   |
| [`HttpReq`](network.md#vipernetworkhttpreq)     | Instance | HTTP request builder                  |
| [`HttpRes`](network.md#vipernetworkhttpres)     | Instance | HTTP response wrapper                 |
| [`Tcp`](network.md#vipernetworktcp)             | Instance | TCP client connection                 |
| [`TcpServer`](network.md#vipernetworktcpserver) | Instance | TCP server (listener)                 |
| [`Udp`](network.md#vipernetworkudp)             | Instance | UDP datagram socket                   |
| [`Url`](network.md#vipernetworkurl)             | Instance | URL parsing and building              |

### Viper.Text

| Class                                             | Type     | Description                |
|---------------------------------------------------|----------|----------------------------|
| [`Codec`](text.md#vipertextcodec)                 | Static   | Base64, Hex, URL encoding  |
| [`Csv`](text.md#vipertextcsv)                     | Static   | CSV parsing and formatting |
| [`Guid`](text.md#vipertextguid)                   | Static   | UUID v4 generation         |
| [`Pattern`](text.md#vipertextpattern)             | Static   | Regex pattern matching     |
| [`StringBuilder`](text.md#vipertextstringbuilder) | Instance | Mutable string builder     |
| [`Template`](text.md#vipertexttemplate)           | Static   | Template rendering         |

### Viper.Threads

| Class                                         | Type     | Description                                  |
|-----------------------------------------------|----------|----------------------------------------------|
| [`Barrier`](threads.md#viperthreadsbarrier)   | Instance | Synchronization barrier for N participants   |
| [`Gate`](threads.md#viperthreadsgate)         | Instance | Counting gate/semaphore                      |
| [`Monitor`](threads.md#viperthreadsmonitor)   | Static   | FIFO-fair, re-entrant object monitor         |
| [`RwLock`](threads.md#viperthreadsrwlock)     | Instance | Reader-writer lock                           |
| [`SafeI64`](threads.md#viperthreadssafei64)   | Instance | FIFO-serialized safe integer cell            |
| [`Thread`](threads.md#viperthreadsthread)     | Instance | OS thread handle + join/sleep/yield helpers  |

### Viper.Time

| Class                                     | Type     | Description             |
|-------------------------------------------|----------|-------------------------|
| [`Clock`](time.md#vipertimeclock)         | Static   | Sleep and tick counting |
| [`Countdown`](time.md#vipertimecountdown) | Instance | Interval timing         |

---

## Class Types

- **Instance** — Requires instantiation with `NEW` before use
- **Static** — Methods called directly on the class name (no instantiation)
- **Base** — Cannot be instantiated directly; inherited by other types

---

## Which Collection Should I Use?

| Need                      | Use       | Why                                      |
|---------------------------|-----------|------------------------------------------|
| Binary data               | `Bytes`   | Efficient byte manipulation              |
| FIFO (first-in-first-out) | `Queue`   | Enqueue/dequeue interface                |
| Priority queue            | `Heap`    | Extract min/max by priority              |
| Fixed-size buffer         | `Ring`    | Overwrites oldest when full              |
| Indexed array             | `Seq`     | Fast random access, push/pop             |
| Key-value pairs           | `Map`     | O(1) lookup by string key                |
| Legacy compatibility      | `List`    | Similar to VB6 Collection                |
| LIFO (last-in-first-out)  | `Stack`   | Simple push/pop interface                |
| Sorted key-value          | `TreeMap` | Keys in sorted order, floor/ceil queries |
| Unique strings            | `Bag`     | Set operations (union, intersect, diff)  |

---

## See Also

- [BASIC Language Reference](../basic-reference.md)
- [IL Guide](../il-guide.md)
- [Getting Started](../getting-started.md)
