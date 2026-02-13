# Viper Runtime Library Reference

> **Version:** 0.2.0
> **Status:** Pre-Alpha — API subject to change
> **Last updated:** 2026-02-10

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are
implemented in C and exposed through the IL runtime system.

---

## Quick Navigation

| Module                          | Description                                                               |
|---------------------------------|---------------------------------------------------------------------------|
| [Architecture](architecture.md) | Runtime internals, type reference                                         |
| [Audio](audio.md)               | `Sound`, `Music`, `Voice`, `Audio` — audio playback for games and applications |
| [Collections](collections.md)   | `Bag`, `Bytes`, `Deque`, `Heap`, `LazySeq`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Set`, `SortedSet`, `Stack`, `TreeMap`, `WeakMap` |
| [Core Types](core.md)           | `Object`, `Box`, `MessageBus`, `String` — foundational types               |
| [Cryptography](crypto.md)       | `Cipher`, `Hash`, `KeyDerive`, `Password`, `Rand`, `Tls`                  |
| [Diagnostics](diagnostics.md)   | `Assert`, `Trap` — assertion checking and traps                           |
| [Game Utilities](game.md)       | `Grid2D`, `Timer`, `StateMachine`, `Tween`, `ObjectPool`, `Quadtree`, `Physics2D`, `SpriteSheet`, `ParticleEmitter`, `SpriteAnimation`, `CollisionRect`, `Collision`, `ButtonGroup`, `SmoothValue`, `ScreenFX`, `PathFollower` |
| [Graphics](graphics.md)         | `Canvas`, `Color`, `Pixels`, `Sprite`, `Tilemap`, `Camera`, `SceneNode`, `Scene`, `SpriteBatch` |
| [GUI](gui.md)                   | `App`, `Button`, `Label`, widgets — GUI toolkit for applications          |
| [Functional](functional.md)     | `Lazy`, `Option`, `Result` — lazy evaluation, optionals, and result types  |
| [Input](input.md)               | `Action`, `Keyboard`, `KeyChord`, `Manager`, `Mouse`, `Pad` — input for games and interactive apps |
| [Input/Output](io.md)           | `Archive`, `BinFile`, `Compress`, `Dir`, `File`, `Glob`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `TempFile`, `Watcher` |
| [Mathematics](math.md)          | `Bits`, `Math`, `Easing`, `PerlinNoise`, `Quaternion`, `Random`, `Spline`, `Vec2`, `Vec3` |
| [Network](network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `RateLimiter`, `RestClient`, `RetryPolicy`, `Tcp`, `TcpServer`, `Udp`, `Url`, `WebSocket` |
| [System](system.md)             | `Environment`, `Exec`, `Machine`, `Terminal`                              |
| [Text Processing](text.md)      | `Codec`, `Csv`, `Html`, `Json`, `JsonPath`, `JsonStream`, `Markdown`, `Pattern`, `Serialize`, `StringBuilder`, `Template`, `Toml`, `Uuid` |
| [Threads](threads.md)           | `Async`, `Barrier`, `CancelToken`, `ConcurrentMap`, `Debouncer`, `Gate`, `Monitor`, `Parallel`, `Pool`, `Promise`, `Future`, `RwLock`, `SafeI64`, `Scheduler`, `Thread`, `Throttler` |
| [Time & Timing](time.md)        | `Clock`, `Countdown`, `DateOnly`, `DateRange`, `DateTime`, `Duration`, `RelativeTime`, `Stopwatch` |
| [Utilities](utilities.md)       | `Convert`, `Fmt`, `Log`                                                   |

---

## Namespace Overview

### Viper (Root)

| Class                                       | Type     | Description                                   |
|---------------------------------------------|----------|-----------------------------------------------|
| [`Bits`](math.md#viperbits)                 | Static   | Bit manipulation (shifts, rotates, counting)  |
| [`Box`](core.md#viperbox)                   | Static   | Boxing helpers for generic collections         |
| [`Convert`](utilities.md#viperconvert)      | Static   | Type conversion utilities                     |
| [`Environment`](system.md#viperenvironment) | Static   | Command-line args and environment             |
| [`Easing`](math.md#vipermatheasing)         | Static   | Animation easing functions                    |
| [`Exec`](system.md#viperexec)               | Static   | External command execution                    |
| [`Fmt`](utilities.md#viperfmt)              | Static   | String formatting                             |
| [`Lazy`](functional.md#viperlazy)           | Static   | Lazy evaluation wrapper                       |
| [`Log`](utilities.md#viperlog)              | Static   | Logging utilities                             |
| [`Machine`](system.md#vipermachine)         | Static   | System information queries                    |
| [`Math`](math.md#vipermath)                 | Static   | Mathematical functions (trig, pow, abs, etc.) |
| [`MessageBus`](core.md#vipercoremessagebus) | Instance | In-process publish/subscribe messaging        |
| [`Object`](core.md#viperobject)             | Base     | Root type for all reference types             |
| [`Option`](functional.md#viperoption)       | Static   | Optional value type (Some/None)               |
| [`PerlinNoise`](math.md#vipermathperlinnoise)| Instance | Perlin noise for procedural generation       |
| [`Random`](math.md#viperrandom)             | Static   | Random number generation                      |
| [`Result`](functional.md#viperresult)       | Static   | Result type for error handling (Ok/Err)       |
| [`String`](core.md#viperstring)             | Instance | Immutable string with manipulation methods    |
| [`Terminal`](system.md#viperterminal)       | Static   | Terminal input/output                         |
| [`Vec2`](math.md#vipervec2)                 | Instance | 2D vector math                                |
| [`Vec3`](math.md#vipervec3)                 | Instance | 3D vector math                                |
| [`Quaternion`](math.md#viperquaternion)     | Instance | Quaternion math for 3D rotations              |
| [`Spline`](math.md#viperspline)            | Instance | Catmull-Rom spline interpolation              |

### Viper.Collections

| Class                                               | Type     | Description                         |
|-----------------------------------------------------|----------|-------------------------------------|
| [`Bag`](collections.md#vipercollectionsbag)         | Instance | String set with set operations      |
| [`Bytes`](collections.md#vipercollectionsbytes)     | Instance | Byte array for binary data          |
| [`Deque`](collections.md#vipercollectionsdeque)     | Instance | Double-ended queue                  |
| [`Heap`](collections.md#vipercollectionsheap)       | Instance | Priority queue (min/max heap)       |
| [`LazySeq`](collections.md#viperlazyseq)            | Instance | Lazy on-demand sequence             |
| [`List`](collections.md#vipercollectionslist)       | Instance | Dynamic array of objects            |
| [`Map`](collections.md#vipercollectionsmap)         | Instance | String-keyed hash map               |
| [`Queue`](collections.md#vipercollectionsqueue)     | Instance | FIFO collection                     |
| [`Ring`](collections.md#vipercollectionsring)       | Instance | Fixed-size circular buffer          |
| [`Seq`](collections.md#vipercollectionsseq)         | Instance | Growable array with stack/queue ops |
| [`Set`](collections.md#vipercollectionsset)         | Instance | Generic object set with set ops     |
| [`SortedSet`](collections.md#vipercollectionssortedset) | Instance | Sorted string set with range ops |
| [`Stack`](collections.md#vipercollectionsstack)     | Instance | LIFO collection                     |
| [`TreeMap`](collections.md#vipercollectionstreemap) | Instance | Sorted key-value map                |
| [`WeakMap`](collections.md#vipercollectionsweakmap) | Instance | Weak-reference key-value map        |

### Viper.Crypto

| Class                                         | Type     | Description                              |
|-----------------------------------------------|----------|------------------------------------------|
| [`Hash`](crypto.md#vipercryptohash)           | Static   | CRC32, MD5, SHA1, SHA256                 |
| [`KeyDerive`](crypto.md#vipercryptokeyderive) | Static   | PBKDF2-SHA256 key derivation             |
| [`Password`](crypto.md#vipercryptopassword)   | Static   | Password hashing and verification        |
| [`Rand`](crypto.md#vipercryptorand)           | Static   | Cryptographically secure random bytes    |
| [`Tls`](crypto.md#vipercryptotls)             | Instance | TLS 1.3 secure socket connections        |

### Viper.Diagnostics

| Class                                             | Type     | Description        |
|---------------------------------------------------|----------|--------------------|
| [`Assert`](diagnostics.md#viperdiagnostics)       | Static   | Assertion checking |
| [`Trap`](diagnostics.md#viperdiagnostics)         | Static   | Unconditional trap |

### Viper.Game

| Class                                                     | Type     | Description                             |
|-----------------------------------------------------------|----------|-----------------------------------------|
| [`ButtonGroup`](game.md#vipergamebuttongroup)             | Instance | Mutually exclusive button selection      |
| [`Collision`](game.md#vipergamecollision)                 | Static   | Static collision detection helpers       |
| [`CollisionRect`](game.md#vipergamecollisionrect)         | Instance | AABB collision detection                 |
| [`Grid2D`](game.md#vipergamegrid2d)                       | Instance | 2D array for tile maps and grids         |
| [`ObjectPool`](game.md#vipergameobjectpool)               | Instance | Efficient object slot reuse              |
| [`ParticleEmitter`](game.md#vipergameparticleemitter)     | Instance | Particle effects system                  |
| [`PathFollower`](game.md#vipergamepathfollower)           | Instance | Path following along waypoints           |
| [`Physics2D`](game.md#vipergamephysics2d)                 | Compound | 2D rigid body physics (World + Body)     |
| [`Quadtree`](game.md#vipergamequadtree)                   | Instance | Spatial partitioning for collision       |
| [`ScreenFX`](game.md#vipergamescreenfx)                   | Instance | Screen effects (shake, flash, fade)      |
| [`SmoothValue`](game.md#vipergamesmoothvalue)             | Instance | Smooth value interpolation               |
| [`SpriteAnimation`](game.md#vipergamespriteanimation)     | Instance | Frame-based sprite animation controller  |
| [`SpriteSheet`](game.md#vipergamespritesheet)             | Instance | Sprite sheet/atlas with region extraction|
| [`StateMachine`](game.md#vipergamestatemachine)           | Instance | Finite state machine for game states     |
| [`Timer`](game.md#vipergametimer)                         | Instance | Frame-based game timers                  |
| [`Tween`](game.md#vipergametween)                         | Instance | Animation tweening with 19 easing curves |

### Viper.Sound

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Audio`](audio.md#vipersoundaudio)            | Static   | Global audio control             |
| [`Music`](audio.md#vipersoundmusic)            | Instance | Streaming music playback         |
| [`Sound`](audio.md#vipersoundsound)            | Instance | Sound effects for short clips    |
| [`Voice`](audio.md#vipersoundvoice)            | Static   | Voice control for playing sounds |

### Viper.Graphics

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Camera`](graphics.md#vipergraphicscamera)         | Instance | 2D camera for scrolling/zoom     |
| [`Canvas`](graphics.md#vipergraphicscanvas)         | Instance | 2D graphics canvas               |
| [`Color`](graphics.md#vipergraphicscolor)           | Static   | Color creation                   |
| [`Pixels`](graphics.md#vipergraphicspixels)         | Instance | Software image buffer            |
| [`Scene`](graphics.md#vipergraphicsscene)           | Instance | Scene graph container            |
| [`SceneNode`](graphics.md#vipergraphicsscenenode)   | Instance | Hierarchical scene graph node    |
| [`Sprite`](graphics.md#vipergraphicssprite)         | Instance | 2D sprite with flip/animation    |
| [`SpriteBatch`](graphics.md#vipergraphicsspritebatch)| Instance | Batched sprite rendering        |
| [`Tilemap`](graphics.md#vipergraphicstilemap)       | Instance | Tile-based game maps             |

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

| Class                                         | Type     | Description                        |
|-----------------------------------------------|----------|------------------------------------|
| [`Action`](input.md#viperinputaction)         | Static   | Device-agnostic action mapping     |
| [`Keyboard`](input.md#viperinputkeyboard)     | Static   | Keyboard input for games and UI    |
| [`KeyChord`](input.md#viperinputkeychord)     | Instance | Key chord and combo detection      |
| [`Manager`](input.md#viperinputmanager)       | Instance | Unified input with debouncing      |
| [`Mouse`](input.md#viperinputmouse)           | Static   | Mouse input for games and UI       |
| [`Pad`](input.md#viperinputpad)               | Static   | Gamepad/controller input           |

### Viper.IO

| Class                                   | Type     | Description                    |
|-----------------------------------------|----------|--------------------------------|
| [`Archive`](io.md#viperioarchive)       | Instance | ZIP archive read/write         |
| [`BinFile`](io.md#viperiobinfile)       | Instance | Binary file stream             |
| [`Compress`](io.md#viperiocompress)     | Static   | DEFLATE/GZIP compression       |
| [`Dir`](io.md#viperiodir)               | Static   | Directory operations           |
| [`File`](io.md#viperiofile)             | Static   | File read/write/delete         |
| [`Glob`](io.md#viperioglob)             | Static   | File globbing and matching     |
| [`LineReader`](io.md#viperiolinereader) | Instance | Line-by-line text reading      |
| [`LineWriter`](io.md#viperiolinewriter) | Instance | Buffered text writing          |
| [`MemStream`](io.md#viperiomemstream)   | Instance | In-memory binary stream        |
| [`Path`](io.md#viperiopath)             | Static   | Path manipulation              |
| [`TempFile`](io.md#viperiotempfile)     | Static   | Temporary file/dir creation    |
| [`Watcher`](io.md#viperiowatcher)       | Instance | File system event monitoring   |

### Viper.Network

| Class                                             | Type     | Description                           |
|---------------------------------------------------|----------|---------------------------------------|
| [`Dns`](network.md#vipernetworkdns)               | Static   | DNS resolution and validation         |
| [`Http`](network.md#vipernetworkhttp)             | Static   | Simple HTTP helpers                   |
| [`HttpReq`](network.md#vipernetworkhttpreq)       | Instance | HTTP request builder                  |
| [`HttpRes`](network.md#vipernetworkhttpres)       | Instance | HTTP response wrapper                 |
| [`RateLimiter`](network.md#vipernetworkratelimiter)| Instance | Token bucket rate limiting           |
| [`RestClient`](network.md#vipernetworkrestclient) | Instance | REST API client with base URL         |
| [`RetryPolicy`](network.md#vipernetworkretrypolicy)| Instance | Retry with backoff for HTTP requests |
| [`Tcp`](network.md#vipernetworktcp)               | Instance | TCP client connection                 |
| [`TcpServer`](network.md#vipernetworktcpserver)   | Instance | TCP server (listener)                 |
| [`Udp`](network.md#vipernetworkudp)               | Instance | UDP datagram socket                   |
| [`Url`](network.md#vipernetworkurl)               | Instance | URL parsing and building              |
| [`WebSocket`](network.md#vipernetworkwebsocket)   | Instance | WebSocket client (RFC 6455)           |

### Viper.Text

| Class                                             | Type     | Description                |
|---------------------------------------------------|----------|----------------------------|
| [`Codec`](text.md#vipertextcodec)                 | Static   | Base64, Hex, URL encoding       |
| [`Csv`](text.md#vipertextcsv)                     | Static   | CSV parsing and formatting      |
| [`Html`](text.md#vipertexthtml)                   | Static   | HTML stripping and entity decode|
| [`Json`](text.md#vipertextjson)                   | Static   | JSON parsing and formatting     |
| [`JsonPath`](text.md#vipertextjsonpath)           | Static   | JSONPath query evaluation       |
| [`JsonStream`](text.md#vipertextjsonstream)       | Instance | Streaming JSON reader/writer    |
| [`Markdown`](text.md#vipertextmarkdown)           | Static   | Markdown to HTML/text conversion|
| [`Pattern`](text.md#vipertextpattern)             | Static   | Regex pattern matching          |
| [`Serialize`](text.md#vipertextserialize)         | Static   | Unified multi-format serializer |
| [`StringBuilder`](text.md#vipertextstringbuilder) | Instance | Mutable string builder          |
| [`Template`](text.md#vipertexttemplate)           | Static   | Template rendering              |
| [`Toml`](text.md#vipertexttoml)                   | Static   | TOML parsing and formatting     |
| [`Uuid`](text.md#vipertextuuid)                   | Static   | UUID v4 generation              |

### Viper.Threads

| Class                                         | Type     | Description                                  |
|-----------------------------------------------|----------|----------------------------------------------|
| [`Async`](threads.md#viperthreadsasync)                   | Static   | Async task combinators (Delay, All, Any)      |
| [`Barrier`](threads.md#viperthreadsbarrier)               | Instance | Synchronization barrier for N participants    |
| [`CancelToken`](threads.md#viperthreadscanceltoken)       | Instance | Cooperative cancellation token                |
| [`ConcurrentMap`](threads.md#viperthreadsconcurrentmap)   | Instance | Thread-safe string-keyed hash map             |
| [`Debouncer`](threads.md#viperthreadsdebouncer)           | Instance | Debounce rapid calls to single execution      |
| [`Future`](threads.md#viperthreadsfuture)                 | Instance | Read-only handle for async result             |
| [`Gate`](threads.md#viperthreadsgate)                     | Instance | Counting gate/semaphore                       |
| [`Monitor`](threads.md#viperthreadsmonitor)               | Static   | FIFO-fair, re-entrant object monitor          |
| [`Parallel`](threads.md#viperthreadsparallel)             | Static   | Parallel For/ForEach/Map/Reduce               |
| [`Pool`](threads.md#viperthreadspool)                     | Instance | Thread pool for parallel task execution       |
| [`Promise`](threads.md#viperthreadspromise)               | Instance | Write-once async value producer               |
| [`RwLock`](threads.md#viperthreadsrwlock)                 | Instance | Reader-writer lock                            |
| [`SafeI64`](threads.md#viperthreadssafei64)               | Instance | Lock-free atomic integer cell                 |
| [`Scheduler`](threads.md#viperthreadsscheduler)           | Instance | Delayed and periodic task scheduling          |
| [`Thread`](threads.md#viperthreadsthread)                 | Instance | OS thread handle + join/sleep/yield helpers   |
| [`Throttler`](threads.md#viperthreadsthrottler)           | Instance | Rate-limit calls to at most once per interval |

### Viper.Time

| Class                                       | Type     | Description                  |
|---------------------------------------------|----------|------------------------------|
| [`Clock`](time.md#vipertimeclock)               | Static   | Sleep and tick counting          |
| [`Countdown`](time.md#vipertimecountdown)       | Instance | Millisecond-based countdown      |
| [`DateOnly`](time.md#vipertimedateonly)         | Instance | Calendar date without time       |
| [`DateRange`](time.md#vipertimedaterange)       | Instance | Time range with start/end        |
| [`DateTime`](time.md#vipertimedatetime)         | Static   | Date and time operations         |
| [`Duration`](time.md#vipertimeduration)         | Static   | Time span manipulation           |
| [`RelativeTime`](time.md#vipertimerelativetime) | Static   | Human-readable relative times    |
| [`Stopwatch`](time.md#vipertimestopwatch)       | Instance | Performance timing               |

---

## Class Types

- **Instance** — Requires instantiation with `NEW` before use
- **Static** — Methods called directly on the class name (no instantiation)
- **Base** — Cannot be instantiated directly; inherited by other types

---

## Which Collection Should I Use?

| Need                        | Use         | Why                                         |
|-----------------------------|-------------|---------------------------------------------|
| 2D tile maps/grids          | `Grid2D`    | Efficient (x,y) access, bounds checking     |
| Binary data                 | `Bytes`     | Efficient byte manipulation                 |
| Both ends access            | `Deque`     | Push/pop from front and back, indexed       |
| FIFO (first-in-first-out)   | `Queue`     | Enqueue/dequeue interface                   |
| Fixed-size buffer           | `Ring`      | Overwrites oldest when full                 |
| Indexed array               | `Seq`       | Fast random access, push/pop                |
| Key-value pairs             | `Map`       | O(1) lookup by string key                   |
| Large/infinite sequences    | `LazySeq`   | Memory-efficient on-demand generation       |
| Legacy compatibility        | `List`      | Similar to VB6 Collection                   |
| LIFO (last-in-first-out)    | `Stack`     | Simple push/pop interface                   |
| Priority queue              | `Heap`      | Extract min/max by priority                 |
| Sorted key-value            | `TreeMap`   | Keys in sorted order, floor/ceil queries    |
| Sorted unique strings       | `SortedSet` | Sorted order, range queries, floor/ceil     |
| Unique objects              | `Set`       | Object set with set operations              |
| Unique strings              | `Bag`       | String set operations (union, etc.)         |
| Weak references             | `WeakMap`   | GC-friendly caching, avoids memory leaks    |

---

## See Also

- [Zia Language Reference](../zia-reference.md)
- [BASIC Language Reference](../basic-reference.md)
- [IL Guide](../il-guide.md)
- [Getting Started](../getting-started.md)
