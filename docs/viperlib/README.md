---
status: active
audience: public
last-verified: 2026-07-14
---

# Viper Runtime Library Reference

> **Development line:** 0.2.99
> **Status:** Pre-Alpha — API subject to change
> **Last updated:** 2026-07-14

The Viper Runtime Library provides built-in classes and utilities available to all Viper programs. These classes are
implemented in C and exposed through the IL runtime system.

For the exhaustive, source-generated class/member/function inventory, see the
[Runtime API Reference](../generated/runtime/README.md). The pages below are
conceptual guides and curated navigation; canonical signatures and class
descriptions come from the modular `runtime.def` registry.

---

## Quick Navigation

| Module                          | Description                                                               |
|---------------------------------|---------------------------------------------------------------------------|
| [Architecture](architecture.md) | Runtime internals, type reference                                         |
| [Audio](audio.md)               | `Audio`, `Music`, `MusicGen`, `Sound`, `Synth`, `Voice` — audio playback and procedural generation |
| [Collections](collections/README.md)   | `Bag`, `Bytes`, `Deque`, `F64Buffer`, `Heap`, `I64Buffer`, `LazySeq`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Set`, `SortedSet`, `Stack`, `TreeMap`, `Trie`, `WeakMap` |
| [Core Types](core.md)           | `Box`, `Diagnostics`, `MessageBus`, `Object`, `Parse`, `String` — foundational types (`Viper.Core`) |
| [Cryptography](crypto.md)       | `Aes`, `Cipher`, `Hash`, `KeyDerive`, `Password`, `Rand`, `Tls`; compatibility `Legacy.Aes`, `Legacy.Hash` |
| [Diagnostics](diagnostics.md)   | `Diagnostics`, `Log`, `TrapInfo` — assertions, logging, and current-trap inspection |
| [Functional](functional.md)     | `Lazy`, `Option`, `Result` — lazy evaluation, optionals, and result types  |
| [Game Utilities](game/README.md)       | `AchievementTracker`, `AnimStateMachine`, `ButtonGroup`, `Collision`, `CollisionRect`, `Grid2D`, `Viper.Game2D.LevelDocument`, `Viper.Game2D.SceneDocument`, `Lighting2D`, `ObjectPool`, `ParticleEmitter`, `PathFollower`, `Physics2D`, `PlatformerController`, `Quadtree`, `ScreenFX`, `SmoothValue`, `SpriteAnimation`, `StateMachine`, `Timer`, `Tween`, `Typewriter`, `WorldToScreenProjection` |
| [Graphics](graphics/README.md)         | `Camera`, `Canvas`, `Color`, `Pixels`, `Renderer2D`, `RenderTarget2D`, `Viper.Graphics2D.SceneGraph`, `Viper.Graphics2D.SceneNode`, `Sprite`, `SpriteBatch`, `SpriteSheet`, `Texture2D`, `TextureAtlas`, `TileLayer2D`, `Viper.Graphics2D.Tilemap`, production 2D classes |
| [Game3D](graphics/game3d.md) | `World3D`, `Entity3D`, `LayerMask`, `Input3D` — code-first 3D game workflow helpers |
| [Graphics 3D & Physics](graphics/physics3d.md) | `Physics3DWorld`, `PhysicsHit3D`, `PhysicsHitList3D`, `CollisionEvent3D`, `ContactPoint3D`, `Collider3D`, `Physics3DBody`, `Character3D`, `DistanceJoint3D`, `SpringJoint3D` |
| [GUI](gui/README.md)                   | `App`, `Breadcrumb`, `Button`, `ClipboardText`, `CodeEditor`, `CommandPalette`, `Container`, `Cursor`, `FileDialog`, `Label`, `MessageBox`, `Minimap`, `Shortcuts`, `Toast`, `Tooltip`, widgets — GUI toolkit for applications |
| [Input](input.md)               | `Action`, `Key`, `Keyboard`, `KeyChord`, `Manager`, `Mouse`, `Pad` — input for games and interactive apps |
| [Input/Output](io/README.md)           | `Archive`, `BinaryBuffer`, `BinFile`, `Compress`, `Dir`, `File`, `Glob`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Stream`, `TempFile`, `Watcher` |
| [Mathematics](math.md)          | `BigInt`, `Bits`, `Easing`, `Mat3`, `Mat4`, `Math`, `PerlinNoise`, `Quat`, `Random`, `Spline`, `Vec2`, `Vec3` |
| [Network](network.md)           | `Dns`, `Http`, `HttpReq`, `HttpRes`, `RateLimiter`, `RestClient`, `RetryPolicy`, `Tcp`, `TcpServer`, `Udp`, `Url`, `WebSocket` |
| [System](system.md)             | `Environment`, `Clipboard`, `Exec`, `Machine`, `Terminal`; `Viper.Runtime.Unsafe`, `Viper.Runtime.GC`, compatibility `Viper.Memory` |
| [Text & Data](text/README.md)          | `Codec`, `CompiledPattern`, `Csv`, `Diff`, `Html`, `Ini`, `Json`, `JsonPath`, `JsonStream`, `Markdown`, `InvariantNumberFormat`, `Pattern`, `Pluralize`, `Scanner`, `StringBuilder`, `Template`, `TextWrapper`, `Toml`, `Uuid`, `Version`; `Viper.Data`: `Serialize`, `Xml`, `Yaml` |
| [Threads](threads.md)           | `Async`, `Barrier`, `CancelToken`, `Channel`, `ConcurrentMap`, `Debouncer`, `Future`, `Gate`, `Monitor`, `Parallel`, `Pool`, `Promise`, `RwLock`, `SafeI64`, `Scheduler`, `Thread`, `Throttler` |
| [Time & Timing](time.md)        | `Clock`, `Countdown`, `DateOnly`, `DateRange`, `DateTime`, `Duration`, `RelativeTime`, `Stopwatch` |
| [Localization](localization/README.md) | `Locale`, `LocaleInfo`, `LocaleManager`, `NumberFormat`, `DateFormat`, `RelativeTimeFormat`, `MessageBundle`, `PluralRules`, `ListFormat`, `TextDirection` |
| [Utilities](utilities.md)       | `Convert`, `Fmt`, `Log`, `Parse`                                          |
| [Zia Tooling](zia.md)           | `Viper.Zia.Toolchain` structured diagnostics and compile results          |

---

## Namespace Overview

Class links in this overview point to the source-generated API reference when
the conceptual guide does not provide a class-specific section.

### Viper (Root)

| Class                                        | Type     | Description                                   |
|----------------------------------------------|----------|-----------------------------------------------|
| [`BigInt`](math.md#vipermathbigint)          | Static   | Arbitrary-precision integer arithmetic        |
| [`Bits`](math.md#vipermathbits)              | Static   | Bit manipulation (shifts, rotates, counting)  |
| [`Box`](core.md#vipercorebox)                | Static   | Boxing helpers for generic collections        |
| [`Convert`](../generated/runtime/core.md#viper-core-convert)       | Static   | Type conversion utilities                     |
| [`Easing`](math.md#vipermatheasing)          | Static   | Animation easing functions                    |
| [`Environment`](../generated/runtime/system.md#viper-system-environment)  | Static   | Command-line args and environment             |
| [`Exec`](../generated/runtime/system.md#viper-system-exec)                | Static   | External command execution                    |
| [`Fmt`](../generated/runtime/text.md#viper-text-fmt)               | Static   | String formatting                             |
| [`Lazy`](../generated/runtime/functional.md#viper-functional-lazy)            | Static   | Lazy evaluation wrapper                       |
| [`Log`](../generated/runtime/diagnostics.md#viper-diagnostics-log)               | Static   | Logging utilities                             |
| [`Machine`](../generated/runtime/system.md#viper-system-machine)          | Static   | System information queries                    |
| [`System.Clipboard`](system.md#vipersystemclipboard) | Static   | UTF-8 desktop clipboard access                 |
| [`Mat3`](math.md#vipermathmat3)              | Static   | 3×3 matrix for 2D affine transforms           |
| [`Mat4`](math.md#vipermathmat4)              | Static   | 4×4 matrix for 3D transforms and projection   |
| [`Math`](math.md#vipermath)                  | Static   | Mathematical functions (trig, pow, abs, etc.) |
| [`MessageBus`](core.md#vipercoremessagebus)  | Instance | In-process publish/subscribe messaging        |
| [`Object`](core.md#vipercoreobject)          | Base     | Root type for all reference types             |
| [`Option`](functional.md#viperoption)        | Static   | Optional value type (Some/None)               |
| [`PerlinNoise`](math.md#vipermathperlinnoise) | Instance | Perlin noise for procedural generation       |
| [`Quat`](math.md#vipermathquat)              | Instance | Quaternion math for 3D rotations              |
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
| [`Bag`](../generated/runtime/collections.md#viper-collections-bag)                   | Instance | String set with set operations             |
| [`BiMap`](../generated/runtime/collections.md#viper-collections-bimap)               | Instance | Bidirectional key-value map                |
| [`BitSet`](../generated/runtime/collections.md#viper-collections-bitset)             | Instance | Compact fixed-size set of bits             |
| [`BloomFilter`](../generated/runtime/collections.md#viper-collections-bloomfilter)   | Instance | Probabilistic membership test              |
| [`Bytes`](../generated/runtime/collections.md#viper-collections-bytes)               | Instance | Byte array for binary data                 |
| [`CountMap`](../generated/runtime/collections.md#viper-collections-countmap)         | Instance | Frequency counter map                      |
| [`DefaultMap`](../generated/runtime/collections.md#viper-collections-defaultmap)     | Instance | Map with auto-initialized default values   |
| [`Deque`](../generated/runtime/collections.md#viper-collections-deque)               | Instance | Double-ended queue                         |
| [`FrozenMap`](../generated/runtime/collections.md#viper-collections-frozenmap)       | Instance | Immutable key-value map                    |
| [`FrozenSet`](../generated/runtime/collections.md#viper-collections-frozenset)       | Instance | Immutable string set                       |
| [`F64Buffer`](collections/specialized.md#packed-numeric-buffers)      | Instance | Packed 64-bit float buffer                 |
| [`Heap`](../generated/runtime/collections.md#viper-collections-heap)                 | Instance | Priority queue (min/max heap)              |
| [`I64Buffer`](collections/specialized.md#packed-numeric-buffers)      | Instance | Packed 64-bit integer buffer               |
| [`IntMap`](../generated/runtime/collections.md#viper-collections-intmap)             | Instance | Integer-keyed hash map                     |
| [`Iterator`](../generated/runtime/collections.md#viper-collections-iterator)         | Instance | Generic forward iterator                   |
| [`LazySeq`](../generated/runtime/functional.md#viper-functional-lazyseq)                      | Instance | Lazy on-demand sequence (`Viper.Functional.LazySeq`)  |
| [`List`](../generated/runtime/collections.md#viper-collections-list)                 | Instance | Dynamic array of objects                   |
| [`LruCache`](../generated/runtime/collections.md#viper-collections-lrucache)         | Instance | Least-recently-used cache                  |
| [`Map`](../generated/runtime/collections.md#viper-collections-map)                   | Instance | String-keyed hash map                      |
| [`MultiMap`](../generated/runtime/collections.md#viper-collections-multimap)         | Instance | Map allowing multiple values per key       |
| [`OrderedMap`](../generated/runtime/collections.md#viper-collections-orderedmap)     | Instance | Insertion-ordered key-value map            |
| [`Queue`](../generated/runtime/collections.md#viper-collections-queue)               | Instance | FIFO collection                            |
| [`Ring`](../generated/runtime/collections.md#viper-collections-ring)                 | Instance | Fixed-size circular buffer                 |
| [`Seq`](../generated/runtime/collections.md#viper-collections-seq)                   | Instance | Growable array with stack/queue ops        |
| [`Set`](../generated/runtime/collections.md#viper-collections-set)                   | Instance | Generic object set with set ops            |
| [`SortedSet`](../generated/runtime/collections.md#viper-collections-sortedset)       | Instance | Sorted string set with range ops           |
| [`SparseArray`](../generated/runtime/collections.md#viper-collections-sparsearray)   | Instance | Sparse integer-indexed array               |
| [`Stack`](../generated/runtime/collections.md#viper-collections-stack)               | Instance | LIFO collection                            |
| [`TreeMap`](../generated/runtime/collections.md#viper-collections-treemap)           | Instance | Sorted key-value map                       |
| [`Trie`](../generated/runtime/collections.md#viper-collections-trie)                 | Instance | Prefix-tree for string lookups             |
| [`UnionFind`](../generated/runtime/collections.md#viper-collections-unionfind)       | Instance | Disjoint-set union-find structure          |
| [`WeakMap`](../generated/runtime/collections.md#viper-collections-weakmap)           | Instance | Weak-reference key-value map               |

### Viper.Crypto

| Class                                         | Type     | Description                              |
|-----------------------------------------------|----------|------------------------------------------|
| [`Aes`](crypto.md#vipercryptoaes)             | Static   | AES-GCM and password encryption with Result/Option decrypt helpers |
| [`Cipher`](crypto.md#vipercryptocipher)       | Static   | High-level authenticated encryption with explicit failure APIs |
| [`Hash`](crypto.md#vipercryptohash)           | Static   | SHA-256, HMAC-SHA256, fast hashes, and constant-time comparison |
| [`KeyDerive`](crypto.md#vipercryptokeyderive) | Static   | PBKDF2-SHA256 key derivation             |
| [`Legacy.Aes`](crypto.md#vipercryptolegacyaes) | Static  | AES-CBC compatibility helpers            |
| [`Legacy.Hash`](crypto.md#vipercryptolegacyhash) | Static | CRC32, MD5, SHA1, and legacy HMAC compatibility helpers |
| [`Password`](crypto.md#vipercryptopassword)   | Static   | Password hashing and verification        |
| [`Rand`](crypto.md#vipercryptorand)           | Static   | Cryptographically secure random bytes    |
| [`Tls`](crypto.md#vipercryptotls)             | Instance | TLS 1.3 secure socket connections        |

### Viper.Data

| Class                                               | Type   | Description                                           |
|-----------------------------------------------------|--------|-------------------------------------------------------|
| [`Serialize`](../generated/runtime/data.md#viper-data-serialize)           | Static | Unified multi-format serializer (JSON/XML/YAML/TOML/CSV) |
| [`Xml`](../generated/runtime/data.md#viper-data-xml)                       | Static | XML document model — parse, navigate, mutate          |
| [`Yaml`](../generated/runtime/data.md#viper-data-yaml)                     | Static | YAML parse and format                                 |

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
| [`CollisionRect`](../generated/runtime/game.md#viper-game-collisionrect)         | Instance | AABB collision detection                 |
| [`Grid2D`](game.md#vipergamegrid2d)                       | Instance | 2D array for tile maps and grids         |
| [`ObjectPool`](game.md#vipergameobjectpool)               | Instance | Efficient object slot reuse              |
| [`ParticleEmitter`](game/effects.md#vipergameparticleemitter) | Instance | Particle effects with batch rendering |
| [`PathFollower`](game/animation.md#vipergamepathfollower) | Instance | Path following along waypoints |
| [`Physics2D`](game.md#vipergamephysics2d-class-family)    | Compound | 2D rigid body physics (World + Body)     |
| [`Quadtree`](game.md#vipergamequadtree)                   | Instance | Spatial partitioning for collision       |
| [`ScreenFX`](game.md#vipergamescreenfx)                   | Instance | Screen effects (shake, flash, fade)      |
| [`SmoothValue`](game.md#vipergamesmoothvalue)             | Instance | Smooth value interpolation               |
| [`SpriteAnimation`](game/animation.md#vipergamespriteanimation) | Instance | Frame-based sprite animation controller |
| [`StateMachine`](game.md#vipergamestatemachine)           | Instance | Finite state machine for game states     |
| [`Timer`](game.md#vipergametimer)                         | Instance | Frame-based game timers                  |
| [`Tween`](../generated/runtime/game.md#viper-game-tween)                         | Instance | Animation tweening with 19 easing curves |
| [`WorldToScreenProjection`](game.md#vipergameworldtoscreenprojection) | Static | Linear, isometric, and perspective world-to-screen helpers |

### Viper.Game2D

| Class | Type | Description |
|-------|------|-------------|
| [`LevelDocument`](game/leveldata.md#vipergame2dleveldocument) | Instance | Legacy JSON level loader with tilemap and object spawn data |
| [`SceneDocument`](game/scene.md#editable-scene-documents) | Instance | Editable JSON scene document with layers, objects, properties, diagnostics, and tilemap-copy export |

### Viper.Sound

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Audio`](../generated/runtime/sound.md#viper-sound-audio)            | Static   | Global audio control             |
| [`Music`](audio.md#vipersoundmusic)            | Instance | Streaming music playback         |
| [`MusicGen`](audio.md#vipersoundmusicgen)      | Instance | Procedural music composition     |
| [`Playlist`](../generated/runtime/sound.md#viper-sound-playlist)      | Instance | Sequential/shuffle track queue   |
| [`Sound`](audio.md#vipersoundsound)            | Instance | Sound effects for short clips    |
| [`SoundBank`](audio.md#vipersoundsoundbank)    | Instance | Named sound registry             |
| [`Synth`](audio.md#vipersoundsynth)            | Static   | Procedural sound synthesis       |
| [`Voice`](audio.md#vipersoundvoice)            | Static   | Voice control for playing sounds |

### Viper.Graphics

| Class                                         | Type     | Description                      |
|-----------------------------------------------|----------|----------------------------------|
| [`Camera`](../generated/runtime/graphics.md#viper-graphics-camera)         | Instance | 2D camera for scrolling/zoom/parallax |
| [`Canvas`](../generated/runtime/graphics.md#viper-graphics-canvas)         | Instance | 2D graphics canvas with delta time    |
| [`Color`](../generated/runtime/graphics.md#viper-graphics-color)           | Static   | Color creation                   |
| [`DebugDraw2D`](graphics/shapes2d.md#classes)              | Instance | Retained debug drawing queue     |
| [`Material2D`](graphics/rendering2d.md#classes)            | Instance | 2D tint, alpha, and blend state  |
| [`NineSlice2D`](graphics/shapes2d.md#classes)              | Instance | Stretchable UI image drawing |
| [`ObjectLayer2D`](graphics/tilemaps2d.md#classes)          | Instance | Rect object map layer       |
| [`ParticleSystem2D`](graphics/game2d.md#classes)           | Instance | Graphics alias for particle effects |
| [`Pixels`](../generated/runtime/graphics.md#viper-graphics-pixels)         | Instance | Software image buffer            |
| [`PostProcess2D`](graphics/rendering2d.md#classes)         | Instance | CPU image post-processing pass   |
| [`RenderTarget2D`](graphics/rendering2d.md#render-targets-textures-and-renderer) | Instance | Offscreen alpha-aware render surface |
| [`Renderer2D`](graphics/rendering2d.md#render-targets-textures-and-renderer) | Instance | Retained 2D draw command stream |
| [`Shader2D`](graphics/rendering2d.md#classes)              | Instance | CPU 2D image effect wrapper      |
| [`ShapeRenderer2D`](graphics/shapes2d.md#classes)          | Instance | Lines, rectangles, circles, paths |
| [`SdfFont`](graphics/shapes2d.md#classes)                  | Instance | SDF-ready bitmap font wrapper    |
| [`Sprite`](../generated/runtime/graphics.md#viper-graphics-sprite)         | Instance | 2D sprite with flip/animation    |
| [`SpriteAnimator`](graphics/pixels.md#vipergraphicsspriteanimator) | Instance | Named sprite clip controller |
| [`SpriteBatch`](../generated/runtime/graphics.md#viper-graphics-spritebatch)| Instance | Batched sprite rendering        |
| [`SpriteSheet`](../generated/runtime/graphics.md#viper-graphics-spritesheet)| Instance | Sprite sheet/atlas with named region extraction |
| [`TextRenderer2D`](graphics/shapes2d.md#classes)           | Instance | Text measurement and Canvas draw wrapper |
| [`Texture2D`](graphics/rendering2d.md#classes)             | Instance | Retained texture handle over Pixels |
| [`TileLayer2D`](graphics/tilemaps2d.md#classes)            | Instance | Dense tile ID layer          |
| [`TileSet2D`](graphics/tilemaps2d.md#classes)              | Instance | Uniform grid tileset         |
| [`Viewport2D`](graphics/game2d.md#viewport-scale)          | Instance | Virtual-to-screen scaling and transforms |

### Viper.Graphics2D

| Class | Type | Description |
|-------|------|-------------|
| [`SceneGraph`](graphics/scene.md#vipergraphics2dscenegraph) | Instance | 2D scene graph container |
| [`SceneNode`](graphics/scene.md#vipergraphics2dscenenode) | Instance | Hierarchical 2D scene graph node |
| [`Tilemap`](graphics/pixels.md#vipergraphics2dtilemap) | Instance | Tile-based map rendering, collision, JSON save/load, and CSV import |

### Viper.Graphics3D

| Class | Type | Description |
|-------|------|-------------|
| [`Character3D`](graphics/physics3d.md#vipergraphics3dcharacter3d) | Instance | Slide-and-step character controller |
| [`AnimController3D`](../graphics3d-guide.md#animcontroller3d) | Instance | Stateful skeletal animation controller with transitions, events, root motion, and scene-node binding support |
| [`CollisionEvent3D`](graphics/physics3d.md#vipergraphics3dcollisionevent3d) | Instance | Structured collision pair event with contact, speed, and impulse data |
| [`Collider3D`](graphics/physics3d.md#vipergraphics3dcollider3d) | Instance | Reusable 3D collision shape including compound, mesh, and heightfield variants |
| [`ContactPoint3D`](graphics/physics3d.md#vipergraphics3dcontactpoint3d) | Instance | Contact manifold point with position, normal, and signed separation |
| [`DistanceJoint3D`](graphics/physics3d.md#vipergraphics3ddistancejoint3d) | Instance | Fixed-distance constraint between bodies |
| [`SceneAsset`](../graphics3d-guide.md#sceneasset) | Instance | Unified imported asset with shared resources and scene instantiation |
| [`SceneGraph`](../graphics3d-guide.md#scenegraph) | Instance | 3D scene graph with culling, spatial queries, `.vscn` save/load, and binding sync |
| [`SceneNode`](../graphics3d-guide.md#scenenode) | Instance | Hierarchical 3D scene node with transform, mesh, material, LOD, and bindings |
| [`Physics3DBody`](graphics/physics3d.md#vipergraphics3dphysics3dbody) | Instance | 3D rigid body with linear/angular motion, sleep, and CCD |
| [`PhysicsHit3D`](graphics/physics3d.md#vipergraphics3dphysicshit3d) | Instance | World-query hit result with body, collider, point, normal, and fraction |
| [`PhysicsHitList3D`](graphics/physics3d.md#vipergraphics3dphysicshitlist3d) | Instance | List of `PhysicsHit3D` results returned by overlap and multi-hit queries |
| [`Physics3DWorld`](graphics/physics3d.md#vipergraphics3dphysics3dworld) | Instance | 3D simulation world with contact and joint access |
| [`SpringJoint3D`](graphics/physics3d.md#vipergraphics3dspringjoint3d) | Instance | Spring constraint with stiffness and damping |

### Viper.GUI

| Class                                               | Type     | Description                                      |
|-----------------------------------------------------|----------|--------------------------------------------------|
| [`App`](../generated/runtime/gui.md#viper-gui-app)                        | Instance | Main application window                          |
| [`Breadcrumb`](../generated/runtime/gui.md#viper-gui-breadcrumb)                   | Instance | Breadcrumb navigation widget                     |
| [`Button`](../generated/runtime/gui.md#viper-gui-button)                  | Instance | Clickable button widget                          |
| [`Container`](../generated/runtime/gui.md#viper-gui-container)            | Instance | Generic layout container widget                  |
| [`Checkbox`](../generated/runtime/gui.md#viper-gui-checkbox)              | Instance | Boolean toggle widget                            |
| [`ClipboardText`](gui/application.md#clipboardtext)         | Static   | System clipboard access                          |
| [`CodeEditor`](../generated/runtime/gui.md#viper-gui-codeeditor)          | Instance | Code editing with syntax coloring                |
| [`CommandPalette`](../generated/runtime/gui.md#viper-gui-commandpalette)           | Instance | Searchable command palette overlay               |
| [`Cursor`](../generated/runtime/gui.md#viper-gui-cursor)                           | Static   | System cursor type and visibility                |
| [`Dropdown`](../generated/runtime/gui.md#viper-gui-dropdown)              | Instance | Drop-down selection                              |
| [`FileDialog`](../generated/runtime/gui.md#viper-gui-filedialog)                   | Static   | Native open/save file dialogs                    |
| [`Font`](../generated/runtime/gui.md#viper-gui-font)                      | Instance | Font for text rendering                          |
| [`Label`](../generated/runtime/gui.md#viper-gui-label)                    | Instance | Text display widget                              |
| [`ListBox`](../generated/runtime/gui.md#viper-gui-listbox)                | Instance | Scrollable list selection                        |
| [`MessageBox`](../generated/runtime/gui.md#viper-gui-messagebox)                   | Static   | Native info/warning/error/question dialogs       |
| [`Minimap`](../generated/runtime/gui.md#viper-gui-minimap)                         | Instance | Code minimap sidebar bound to a CodeEditor       |
| [`ProgressBar`](../generated/runtime/gui.md#viper-gui-progressbar)        | Instance | Progress indicator                               |
| [`RadioButton`](../generated/runtime/gui.md#viper-gui-radiobutton)        | Instance | Single-select option widget                      |
| [`ScrollView`](../generated/runtime/gui.md#viper-gui-scrollview)          | Instance | Scrollable container                             |
| [`Shortcuts`](../generated/runtime/gui.md#viper-gui-shortcuts)                     | Static   | Global keyboard shortcut registration            |
| [`Slider`](../generated/runtime/gui.md#viper-gui-slider)                  | Instance | Numeric range slider                             |
| [`Spinner`](../generated/runtime/gui.md#viper-gui-spinner)                | Instance | Numeric spinner control                          |
| [`SplitPane`](../generated/runtime/gui.md#viper-gui-splitpane)            | Instance | Resizable split container                        |
| [`TabBar`](../generated/runtime/gui.md#viper-gui-tabbar)                  | Instance | Tabbed container                                 |
| [`TextInput`](../generated/runtime/gui.md#viper-gui-textinput)            | Instance | Single-line text entry                           |
| [`Toast`](../generated/runtime/gui.md#viper-gui-toast)                             | Instance | Transient notification toasts                    |
| [`Tooltip`](../generated/runtime/gui.md#viper-gui-tooltip)                         | Static   | Floating tooltip display                         |
| [`TreeView`](../generated/runtime/gui.md#viper-gui-treeview)              | Instance | Hierarchical tree widget                         |

### Viper.Input

| Class                                         | Type     | Description                        |
|-----------------------------------------------|----------|------------------------------------|
| [`Action`](input.md#viperinputaction)         | Static   | Device-agnostic action mapping     |
| [`Key`](input.md#viperinputkey)               | Static   | Canonical keyboard key codes       |
| [`Keyboard`](input.md#viperinputkeyboard)     | Static   | Keyboard input for games and UI    |
| [`KeyChord`](input.md#viperinputkeychord)     | Instance | Key chord and combo detection      |
| [`Manager`](input.md#viperinputmanager)       | Instance | Unified input with debouncing      |
| [`Mouse`](input.md#viperinputmouse)           | Static   | Mouse input for games and UI       |
| [`Pad`](input.md#viperinputpad)               | Static   | Gamepad/controller input           |

### Viper.IO

| Class                                           | Type     | Description                         |
|-------------------------------------------------|----------|-------------------------------------|
| [`Archive`](../generated/runtime/io.md#viper-io-archive)               | Instance | ZIP archive read/write              |
| [`BinaryBuffer`](../generated/runtime/io.md#viper-io-binarybuffer)     | Instance | Positioned binary read/write buffer |
| [`BinFile`](../generated/runtime/io.md#viper-io-binfile)               | Instance | Binary file stream                  |
| [`Compress`](../generated/runtime/io.md#viper-io-compress)             | Static   | DEFLATE/GZIP compression            |
| [`Dir`](../generated/runtime/io.md#viper-io-dir)                       | Static   | Directory operations                |
| [`File`](../generated/runtime/io.md#viper-io-file)                     | Static   | File read/write/delete              |
| [`Glob`](../generated/runtime/io.md#viper-io-glob)                     | Static   | File globbing and matching          |
| [`LineReader`](../generated/runtime/io.md#viper-io-linereader)         | Instance | Line-by-line text reading           |
| [`LineWriter`](../generated/runtime/io.md#viper-io-linewriter)         | Instance | Buffered text writing               |
| [`MemStream`](../generated/runtime/io.md#viper-io-memstream)           | Instance | In-memory binary stream             |
| [`Path`](../generated/runtime/io.md#viper-io-path)                     | Static   | Path manipulation                   |
| [`Stream`](../generated/runtime/io.md#viper-io-stream)                 | Instance | Unified stream interface            |
| [`TempFile`](../generated/runtime/io.md#viper-io-tempfile)             | Static   | Temporary file/dir creation         |
| [`Watcher`](../generated/runtime/io.md#viper-io-watcher)               | Instance | File system event monitoring        |

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
| [`Codec`](../generated/runtime/text.md#viper-text-codec)                         | Static   | Base64, Hex, URL encoding          |
| [`CompiledPattern`](../generated/runtime/text.md#viper-text-compiledpattern)     | Instance | Pre-compiled regex pattern         |
| [`Csv`](../generated/runtime/text.md#viper-text-csv)                             | Static   | CSV parsing and formatting         |
| [`Diff`](../generated/runtime/text.md#viper-text-diff)                           | Static   | Text diff and patch                |
| [`Html`](../generated/runtime/text.md#viper-text-html)                           | Static   | HTML stripping and entity decode   |
| [`Ini`](../generated/runtime/text.md#viper-text-ini)                             | Static   | INI file parsing and formatting    |
| [`Json`](../generated/runtime/text.md#viper-text-json)                           | Static   | JSON parsing and formatting        |
| [`JsonPath`](../generated/runtime/text.md#viper-text-jsonpath)                   | Static   | JSONPath query evaluation          |
| [`JsonStream`](../generated/runtime/text.md#viper-text-jsonstream)               | Instance | Streaming JSON reader/writer       |
| [`Markdown`](../generated/runtime/text.md#viper-text-markdown)                   | Static   | Markdown to HTML/text conversion   |
| [`InvariantNumberFormat`](text/formatting.md#vipertextinvariantnumberformat) | Static   | C-locale number formatting       |
| [`Pattern`](../generated/runtime/text.md#viper-text-pattern)                     | Static   | Regex pattern matching             |
| [`Pluralize`](../generated/runtime/text.md#viper-text-pluralize)                 | Static   | English pluralization utilities    |
| [`Scanner`](../generated/runtime/text.md#viper-text-scanner)                     | Instance | Token-based string scanner         |
| [`Serialize`](../generated/runtime/data.md#viper-data-serialize)                 | Static   | Unified multi-format serializer    |
| [`StringBuilder`](../generated/runtime/text.md#viper-text-stringbuilder)         | Instance | Mutable string builder             |
| [`Template`](../generated/runtime/text.md#viper-text-template)                   | Static   | Template rendering                 |
| [`TextWrapper`](../generated/runtime/text.md#viper-text-textwrapper)             | Static   | Word-wrap and text formatting      |
| [`Toml`](../generated/runtime/text.md#viper-text-toml)                           | Static   | TOML parsing and formatting        |
| [`Uuid`](../generated/runtime/text.md#viper-text-uuid)                           | Static   | UUID v4 generation                 |
| [`Version`](../generated/runtime/text.md#viper-text-version)                     | Static   | Semantic version parsing/comparison|

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

### Viper.Runtime

| Class                                           | Type   | Description                                     |
|-------------------------------------------------|--------|-------------------------------------------------|
| [`Unsafe`](system.md#viperruntimeunsafe)       | Static | Explicit retain/release for runtime handles      |
| [`GC`](system.md#viperruntimegc)               | Static | Garbage collector controls and statistics        |

### Viper.Memory

| Class                                           | Type   | Description                                     |
|-------------------------------------------------|--------|-------------------------------------------------|
| [`Memory`](system.md#vipermemory)              | Static | Compatibility namespace for `Runtime.Unsafe`     |
| [`GC`](../generated/runtime/memory.md#viper-memory-gc)               | Static | Compatibility namespace for `Runtime.GC`         |

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
| Packed numeric batches      | `F64Buffer` / `I64Buffer` | Dense primitive storage without per-element boxing |
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
