---
status: active
audience: public
last-verified: 2026-04-05
---

# Viper Runtime Library Reference

> **Version:** 0.2.0
> **Status:** Pre-Alpha — API subject to change
> **Last updated:** 2026-02-19

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are
implemented in C and exposed through the IL runtime system.

---

## Quick Navigation

| Module                          | Description                                                               |
|---------------------------------|---------------------------------------------------------------------------|
| [Architecture](architecture.md) | Runtime internals, type reference                                         |
| [Audio](audio.md)               | `Audio`, `Music`, `MusicGen`, `Sound`, `Synth`, `Voice` — audio playback and procedural generation |
| [Collections](collections/README.md)   | `Bag`, `Bytes`, `Deque`, `Heap`, `LazySeq`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Set`, `SortedSet`, `Stack`, `TreeMap`, `Trie`, `WeakMap` |
| [Core Types](core.md)           | `Box`, `Diagnostics`, `MessageBus`, `Object`, `Parse`, `String` — foundational types (`Viper.Core`) |
| [Cryptography](crypto.md)       | `Aes`, `Cipher`, `Hash`, `KeyDerive`, `Password`, `Rand`, `Tls`           |
| [Diagnostics](diagnostics.md)   | `Assert`, `Trap` — assertion checking and traps                           |
| [Functional](functional.md)     | `Lazy`, `Option`, `Result` — lazy evaluation, optionals, and result types  |
| [Game Utilities](game/README.md)       | `AchievementTracker`, `AnimStateMachine`, `ButtonGroup`, `Collision`, `CollisionRect`, `Grid2D`, `Lighting2D`, `ObjectPool`, `ParticleEmitter`, `PathFollower`, `Physics2D`, `PlatformerController`, `Quadtree`, `ScreenFX`, `SmoothValue`, `SpriteAnimation`, `StateMachine`, `Timer`, `Tween`, `Typewriter` |
| [Graphics](graphics/README.md)         | `Camera`, `Canvas`, `Color`, `Pixels`, `Scene`, `SceneNode`, `Sprite`, `SpriteBatch`, `SpriteSheet`, `TextureAtlas`, `Tilemap` |
| [GUI](gui/README.md)                   | `App`, `Breadcrumb`, `Button`, `Clipboard`, `CodeEditor`, `CommandPalette`, `Container`, `Cursor`, `FileDialog`, `Label`, `MessageBox`, `Minimap`, `Shortcuts`, `Toast`, `Tooltip`, widgets — GUI toolkit for applications |
| [Input](input.md)               | `Action`, `Keyboard`, `KeyChord`, `Manager`, `Mouse`, `Pad` — input for games and interactive apps |
| [Input/Output](io/README.md)           | `Archive`, `BinaryBuffer`, `BinFile`, `Compress`, `Dir`, `File`, `Glob`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Stream`, `TempFile`, `Watcher` |
| [Mathematics](math.md)          | `BigInt`, `Bits`, `Easing`, `Mat3`, `Mat4`, `Math`, `PerlinNoise`, `Quaternion`, `Random`, `Spline`, `Vec2`, `Vec3` |
| [Network](network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `RateLimiter`, `RestClient`, `RetryPolicy`, `Tcp`, `TcpServer`, `Udp`, `Url`, `WebSocket` |
| [System](system.md)             | `Environment`, `Exec`, `Machine`, `Terminal`; `Viper.Memory`: `GC`       |
| [Text & Data](text/README.md)          | `Codec`, `CompiledPattern`, `Csv`, `Diff`, `Html`, `Ini`, `Json`, `JsonPath`, `JsonStream`, `Markdown`, `NumberFormat`, `Pattern`, `Pluralize`, `Scanner`, `StringBuilder`, `Template`, `TextWrapper`, `Toml`, `Uuid`, `Version`; `Viper.Data`: `Serialize`, `Xml`, `Yaml` |
| [Threads](threads.md)           | `Async`, `Barrier`, `CancelToken`, `Channel`, `ConcurrentMap`, `Debouncer`, `Future`, `Gate`, `Monitor`, `Parallel`, `Pool`, `Promise`, `RwLock`, `SafeI64`, `Scheduler`, `Thread`, `Throttler` |
| [Time & Timing](time.md)        | `Clock`, `Countdown`, `DateOnly`, `DateRange`, `DateTime`, `Duration`, `RelativeTime`, `Stopwatch` |
| [Utilities](utilities.md)       | `Convert`, `Fmt`, `Log`, `Parse`                                          |

---

## Namespace Overview

### Viper (Root)

| Class                                        | Type     | Description                                   |
|----------------------------------------------|----------|-----------------------------------------------|
| [`BigInt`](math.md#vipermathbigint)          | Static   | Arbitrary-precision integer arithmetic        |
| [`Bits`](math.md#vipermathbits)              | Static   | Bit manipulation (shifts, rotates, counting)  |
| [`Box`](core.md#vipercorebox)                | Static   | Boxing helpers for generic collections        |
| [`Convert`](utilities.md#viperconvert)       | Static   | Type conversion utilities                     |
| [`Easing`](math.md#vipermatheasing)          | Static   | Animation easing functions                    |
| [`Environment`](system.md#viperenvironment)  | Static   | Command-line args and environment             |
| [`Exec`](system.md#viperexec)                | Static   | External command execution                    |
| [`Fmt`](utilities.md#viperfmt)               | Static   | String formatting                             |
| [`Lazy`](functional.md#viperlazy)            | Static   | Lazy evaluation wrapper                       |
| [`Log`](utilities.md#viperlog)               | Static   | Logging utilities                             |
| [`Machine`](system.md#vipermachine)          | Static   | System information queries                    |
| [`Mat3`](math.md#vipermathmat3)              | Static   | 3×3 matrix for 2D affine transforms           |
| [`Mat4`](math.md#vipermathmat4)              | Static   | 4×4 matrix for 3D transforms and projection   |
| [`Math`](math.md#vipermath)                  | Static   | Mathematical functions (trig, pow, abs, etc.) |
| [`MessageBus`](core.md#vipercoremessagebus)  | Instance | In-process publish/subscribe messaging        |
| [`Object`](core.md#vipercoreobject)          | Base     | Root type for all reference types             |
| [`Option`](functional.md#viperoption)        | Static   | Optional value type (Some/None)               |
| [`PerlinNoise`](math.md#vipermathperlinnoise) | Instance | Perlin noise for procedural generation       |
| [`Quaternion`](math.md#vipermathquaternion)  | Instance | Quaternion math for 3D rotations              |
| [`Random`](math.md#vipermathrandom)          | Static/Instance | Random number generation                 |
| [`Result`](functional.md#viperresult)        | Static   | Result type for error handling (Ok/Err)       |
| [`Spline`](math.md#vipermathspline)          | Instance | Catmull-Rom spline interpolation              |
| [`String`](core.md#viperstring)              | Instance | Immutable string with manipulation methods    |
| [`Terminal`](system.md#viperterminal)        | Static   | Terminal input/output                         |
| [`Vec2`](math.md#vipermathvec2)              | Instance | 2D vector math                                |
| [`Vec3`](math.md#vipermathvec3)              | Instance | 3D vector math                                |

### Viper.Collections

| Class                                                         | Type     | Description                                |
|---------------------------------------------------------------|----------|--------------------------------------------|
| [`Bag`](collections/README.md#vipercollectionsbag)                   | Instance | String set with set operations             |
| [`BiMap`](collections/README.md#vipercollectionsbimap)               | Instance | Bidirectional key-value map                |
| [`BitSet`](collections/README.md#vipercollectionsbitset)             | Instance | Compact fixed-size set of bits             |
| [`BloomFilter`](collections/README.md#vipercollectionsbloomfilter)   | Instance | Probabilistic membership test              |
| [`Bytes`](collections/README.md#vipercollectionsbytes)               | Instance | Byte array for binary data                 |
| [`CountMap`](collections/README.md#vipercollectionscountmap)         | Instance | Frequency counter map                      |
| [`DefaultMap`](collections/README.md#vipercollectionsdefaultmap)     | Instance | Map with auto-initialized default values   |
| [`Deque`](collections/README.md#vipercollectionsdeque)               | Instance | Double-ended queue                         |
| [`FrozenMap`](collections/README.md#vipercollectionsfrozenmap)       | Instance | Immutable key-value map                    |
| [`FrozenSet`](collections/README.md#vipercollectionsfrozenset)       | Instance | Immutable string set                       |
| [`Heap`](collections/README.md#vipercollectionsheap)                 | Instance | Priority queue (min/max heap)              |
| [`IntMap`](collections/README.md#vipercollectionsintmap)             | Instance | Integer-keyed hash map                     |
| [`Iterator`](collections/README.md#vipercollectionsiterator)         | Instance | Generic forward iterator                   |
| [`LazySeq`](collections/README.md#viperlazyseq)                      | Instance | Lazy on-demand sequence (`Viper.LazySeq`)  |
| [`List`](collections/README.md#vipercollectionslist)                 | Instance | Dynamic array of objects                   |
| [`LruCache`](collections/README.md#vipercollectionslrucache)         | Instance | Least-recently-used cache                  |
| [`Map`](collections/README.md#vipercollectionsmap)                   | Instance | String-keyed hash map                      |
| [`MultiMap`](collections/README.md#vipercollectionsmultimap)         | Instance | Map allowing multiple values per key       |
| [`OrderedMap`](collections/README.md#vipercollectionsorderedmap)     | Instance | Insertion-ordered key-value map            |
| [`Queue`](collections/README.md#vipercollectionsqueue)               | Instance | FIFO collection                            |
| [`Ring`](collections/README.md#vipercollectionsring)                 | Instance | Fixed-size circular buffer                 |
| [`Seq`](collections/README.md#vipercollectionsseq)                   | Instance | Growable array with stack/queue ops        |
| [`Set`](collections/README.md#vipercollectionsset)                   | Instance | Generic object set with set ops            |
| [`SortedSet`](collections/README.md#vipercollectionssortedset)       | Instance | Sorted string set with range ops           |
| [`SparseArray`](collections/README.md#vipercollectionssparsearray)   | Instance | Sparse integer-indexed array               |
| [`Stack`](collections/README.md#vipercollectionsstack)               | Instance | LIFO collection                            |
| [`TreeMap`](collections/README.md#vipercollectionstreemap)           | Instance | Sorted key-value map                       |
| [`Trie`](collections/README.md#vipercollectionstrie)                 | Instance | Prefix-tree for string lookups             |
| [`UnionFind`](collections/README.md#vipercollectionsunionfind)       | Instance | Disjoint-set union-find structure          |
| [`WeakMap`](collections/README.md#vipercollectionsweakmap)           | Instance | Weak-reference key-value map               |

### Viper.Crypto

| Class                                         | Type     | Description                              |
|-----------------------------------------------|----------|------------------------------------------|
| [`Aes`](crypto.md#vipercryptoaes)             | Static   | AES-128/256 CBC encryption               |
| [`Cipher`](crypto.md#vipercryptocipher)       | Static   | ChaCha20-Poly1305 high-level encryption  |
| [`Hash`](crypto.md#vipercryptohash)           | Static   | CRC32, MD5, SHA1, SHA256                 |
| [`KeyDerive`](crypto.md#vipercryptokeyderive) | Static   | PBKDF2-SHA256 key derivation             |
| [`Password`](crypto.md#vipercryptopassword)   | Static   | Password hashing and verification        |
| [`Rand`](crypto.md#vipercryptorand)           | Static   | Cryptographically secure random bytes    |
| [`Tls`](crypto.md#vipercryptotls)             | Instance | TLS 1.3 secure socket connections        |

### Viper.Data

| Class                                               | Type   | Description                                           |
|-----------------------------------------------------|--------|-------------------------------------------------------|
| [`Serialize`](text/README.md#viperdataserialize)           | Static | Unified multi-format serializer (JSON/XML/YAML/TOML/CSV) |
| [`Xml`](text/README.md#viperdataxml)                       | Static | XML document model — parse, navigate, mutate          |
| [`Yaml`](text/README.md#viperdatayaml)                     | Static | YAML parse and format                                 |

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
| [`ParticleEmitter`](game.md#vipergameparticleemitter)     | Instance | Particle effects with batch rendering    |
| [`PathFollower`](game.md#vipergamepathfollower)           | Instance | Path following along waypoints           |
| [`Physics2D`](game.md#vipergamephysics2d)                 | Compound | 2D rigid body physics (World + Body)     |
| [`Quadtree`](game.md#vipergamequadtree)                   | Instance | Spatial partitioning for collision       |
| [`ScreenFX`](game.md#vipergamescreenfx)                   | Instance | Screen effects (shake, flash, fade)      |
| [`SmoothValue`](game.md#vipergamesmoothvalue)             | Instance | Smooth value interpolation               |
| [`SpriteAnimation`](game.md#vipergamespriteanimation)     | Instance | Frame-based sprite animation controller  |
| [`StateMachine`](game.md#vipergamestatemachine)           | Instance | Finite state machine for game states     |
| [`Timer`](game.md#vipergametimer)                         | Instance | Frame-based game timers                  |
| [`Tween`](game.md#vipergametween)                         | Instance | Animation tweening with 19 easing curves |

### Viper.Sound

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Audio`](audio.md#vipersoundaudio)            | Static   | Global audio control             |
| [`Music`](audio.md#vipersoundmusic)            | Instance | Streaming music playback         |
| [`MusicGen`](audio.md#vipersoundmusicgen)      | Instance | Procedural music composition     |
| [`Playlist`](audio.md#vipersoundplaylist)      | Instance | Sequential/shuffle track queue   |
| [`Sound`](audio.md#vipersoundsound)            | Instance | Sound effects for short clips    |
| [`SoundBank`](audio.md#vipersoundsoundbank)    | Instance | Named sound registry             |
| [`Synth`](audio.md#vipersoundsynth)            | Static   | Procedural sound synthesis       |
| [`Voice`](audio.md#vipersoundvoice)            | Static   | Voice control for playing sounds |

### Viper.Graphics

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Camera`](graphics/README.md#vipergraphicscamera)         | Instance | 2D camera for scrolling/zoom/parallax |
| [`Canvas`](graphics/README.md#vipergraphicscanvas)         | Instance | 2D graphics canvas with delta time    |
| [`Color`](graphics/README.md#vipergraphicscolor)           | Static   | Color creation                   |
| [`Pixels`](graphics/README.md#vipergraphicspixels)         | Instance | Software image buffer            |
| [`Scene`](graphics/README.md#vipergraphicsscene)           | Instance | Scene graph container            |
| [`SceneNode`](graphics/README.md#vipergraphicsscenenode)   | Instance | Hierarchical scene graph node    |
| [`Sprite`](graphics/README.md#vipergraphicssprite)         | Instance | 2D sprite with flip/animation    |
| [`SpriteBatch`](graphics/README.md#vipergraphicsspritebatch)| Instance | Batched sprite rendering        |
| [`SpriteSheet`](graphics/README.md#vipergraphicsspritesheet)| Instance | Sprite sheet/atlas with named region extraction |
| [`Tilemap`](graphics/README.md#vipergraphicstilemap)       | Instance | Tile-based game maps             |

### Viper.GUI

| Class                                               | Type     | Description                                      |
|-----------------------------------------------------|----------|--------------------------------------------------|
| [`App`](gui/README.md#vipergui-app)                        | Instance | Main application window                          |
| [`Breadcrumb`](gui/README.md#breadcrumb)                   | Instance | Breadcrumb navigation widget                     |
| [`Button`](gui/README.md#vipergui-button)                  | Instance | Clickable button widget                          |
| [`Container`](gui/README.md#vipergui-container)            | Instance | Generic layout container widget                  |
| [`Checkbox`](gui/README.md#vipergui-checkbox)              | Instance | Boolean toggle widget                            |
| [`Clipboard`](gui/README.md#clipboard)                     | Static   | System clipboard access                          |
| [`CodeEditor`](gui/README.md#vipergui-codeeditor)          | Instance | Code editing with syntax coloring                |
| [`CommandPalette`](gui/README.md#commandpalette)           | Instance | Searchable command palette overlay               |
| [`Cursor`](gui/README.md#cursor)                           | Static   | System cursor type and visibility                |
| [`Dropdown`](gui/README.md#vipergui-dropdown)              | Instance | Drop-down selection                              |
| [`FileDialog`](gui/README.md#filedialog)                   | Static   | Native open/save file dialogs                    |
| [`Font`](gui/README.md#vipergui-font)                      | Instance | Font for text rendering                          |
| [`Label`](gui/README.md#vipergui-label)                    | Instance | Text display widget                              |
| [`ListBox`](gui/README.md#vipergui-listbox)                | Instance | Scrollable list selection                        |
| [`MessageBox`](gui/README.md#messagebox)                   | Static   | Native info/warning/error/question dialogs       |
| [`Minimap`](gui/README.md#minimap)                         | Instance | Code minimap sidebar bound to a CodeEditor       |
| [`ProgressBar`](gui/README.md#vipergui-progressbar)        | Instance | Progress indicator                               |
| [`RadioButton`](gui/README.md#vipergui-radiobutton)        | Instance | Single-select option widget                      |
| [`ScrollView`](gui/README.md#vipergui-scrollview)          | Instance | Scrollable container                             |
| [`Shortcuts`](gui/README.md#shortcuts)                     | Static   | Global keyboard shortcut registration            |
| [`Slider`](gui/README.md#vipergui-slider)                  | Instance | Numeric range slider                             |
| [`Spinner`](gui/README.md#vipergui-spinner)                | Instance | Numeric spinner control                          |
| [`SplitPane`](gui/README.md#vipergui-splitpane)            | Instance | Resizable split container                        |
| [`TabBar`](gui/README.md#vipergui-tabbar)                  | Instance | Tabbed container                                 |
| [`TextInput`](gui/README.md#vipergui-textinput)            | Instance | Single-line text entry                           |
| [`Toast`](gui/README.md#toast)                             | Instance | Transient notification toasts                    |
| [`Tooltip`](gui/README.md#tooltip)                         | Static   | Floating tooltip display                         |
| [`TreeView`](gui/README.md#vipergui-treeview)              | Instance | Hierarchical tree widget                         |

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

| Class                                           | Type     | Description                         |
|-------------------------------------------------|----------|-------------------------------------|
| [`Archive`](io/README.md#viperioarchive)               | Instance | ZIP archive read/write              |
| [`BinaryBuffer`](io/README.md#viperiobinarybuffer)     | Instance | Positioned binary read/write buffer |
| [`BinFile`](io/README.md#viperiobinfile)               | Instance | Binary file stream                  |
| [`Compress`](io/README.md#viperiocompress)             | Static   | DEFLATE/GZIP compression            |
| [`Dir`](io/README.md#viperiodir)                       | Static   | Directory operations                |
| [`File`](io/README.md#viperiofile)                     | Static   | File read/write/delete              |
| [`Glob`](io/README.md#viperioglob)                     | Static   | File globbing and matching          |
| [`LineReader`](io/README.md#viperiolinereader)         | Instance | Line-by-line text reading           |
| [`LineWriter`](io/README.md#viperiolinewriter)         | Instance | Buffered text writing               |
| [`MemStream`](io/README.md#viperiomemstream)           | Instance | In-memory binary stream             |
| [`Path`](io/README.md#viperiopath)                     | Static   | Path manipulation                   |
| [`Stream`](io/README.md#viperiostream)                 | Instance | Unified stream interface            |
| [`TempFile`](io/README.md#viperiotempfile)             | Static   | Temporary file/dir creation         |
| [`Watcher`](io/README.md#viperiowatcher)               | Instance | File system event monitoring        |

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

| Class                                                     | Type     | Description                        |
|-----------------------------------------------------------|----------|------------------------------------|
| [`Codec`](text/README.md#vipertextcodec)                         | Static   | Base64, Hex, URL encoding          |
| [`CompiledPattern`](text/README.md#vipertextcompiledpattern)     | Instance | Pre-compiled regex pattern         |
| [`Csv`](text/README.md#vipertextcsv)                             | Static   | CSV parsing and formatting         |
| [`Diff`](text/README.md#vipertextdiff)                           | Static   | Text diff and patch                |
| [`Html`](text/README.md#vipertexthtml)                           | Static   | HTML stripping and entity decode   |
| [`Ini`](text/README.md#vipertextini)                             | Static   | INI file parsing and formatting    |
| [`Json`](text/README.md#vipertextjson)                           | Static   | JSON parsing and formatting        |
| [`JsonPath`](text/README.md#vipertextjsonpath)                   | Static   | JSONPath query evaluation          |
| [`JsonStream`](text/README.md#vipertextjsonstream)               | Instance | Streaming JSON reader/writer       |
| [`Markdown`](text/README.md#vipertextmarkdown)                   | Static   | Markdown to HTML/text conversion   |
| [`NumberFormat`](text/README.md#vipertextnumberformat)           | Static   | Locale-aware number formatting     |
| [`Pattern`](text/README.md#vipertextpattern)                     | Static   | Regex pattern matching             |
| [`Pluralize`](text/README.md#vipertextpluralize)                 | Static   | English pluralization utilities    |
| [`Scanner`](text/README.md#vipertextscanner)                     | Instance | Token-based string scanner         |
| [`Serialize`](text/README.md#vipertextserialize)                 | Static   | Unified multi-format serializer    |
| [`StringBuilder`](text/README.md#vipertextstringbuilder)         | Instance | Mutable string builder             |
| [`Template`](text/README.md#vipertexttemplate)                   | Static   | Template rendering                 |
| [`TextWrapper`](text/README.md#vipertexttextwrapper)             | Static   | Word-wrap and text formatting      |
| [`Toml`](text/README.md#vipertexttoml)                           | Static   | TOML parsing and formatting        |
| [`Uuid`](text/README.md#vipertextuuid)                           | Static   | UUID v4 generation                 |
| [`Version`](text/README.md#vipertextversion)                     | Static   | Semantic version parsing/comparison|

### Viper.Threads

| Class                                         | Type     | Description                                  |
|-----------------------------------------------|----------|----------------------------------------------|
| [`Async`](threads.md#viperthreadsasync)                   | Static   | Async task combinators (Delay, All, Any)      |
| [`Barrier`](threads.md#viperthreadsbarrier)               | Instance | Synchronization barrier for N participants    |
| [`CancelToken`](threads.md#viperthreadscanceltoken)       | Instance | Cooperative cancellation token                |
| [`Channel`](threads.md#viperthreadschannel)               | Instance | Typed buffered/unbuffered message channel     |
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

### Viper.Memory

| Class                                           | Type   | Description                                     |
|-------------------------------------------------|--------|-------------------------------------------------|
| [`GC`](core.md#vipermemory)                     | Static | Garbage collector controls (hint, collect, stat)|

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
| FIFO (first-in-first-out)   | `Queue`     | Push/Pop interface                   |
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
