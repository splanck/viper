# Viper Runtime Analysis Report
## Comprehensive Assessment of Higher-Level Abstraction Opportunities

**Generated:** 2026-02-04 (Updated with deep header analysis)
**Scope:** All `Viper.*` namespace classes and functions
**Source:** `src/il/runtime/runtime.def`, `src/runtime/*.h`
**Headers Analyzed:** 90+ (including rt_string.h, rt_seq.h, rt_network.h, rt_threads.h, rt_graphics.h, rt_gui.h, etc.)

---

## 1. Executive Summary

### Overview
| Metric | Value |
|--------|-------|
| **Total Functions** | 1,418+ |
| **Total Classes** | 98+ |
| **Top-Level Namespaces** | 33 |
| **Header Files** | 112 |
| **Lines of Header Code** | ~15,000+ |
| **Abstraction Level Distribution** | See below |

### Abstraction Level Distribution

| Level | Description | Count | Percentage |
|-------|-------------|-------|------------|
| Level 1 (Primitive) | Raw operations, manual resource management | 8 | 8% |
| Level 2 (Basic) | Some convenience, significant boilerplate | 21 | 21% |
| Level 3 (Intermediate) | Good balance of control and convenience | 41 | 42% |
| Level 4 (High-Level) | Easy to use correctly, hard to use incorrectly | 23 | 24% |
| Level 5 (Batteries-Included) | Complete solution with sensible defaults | 5 | 5% |

### Top 10 Highest-Priority Expansion Recommendations

| Rank | Namespace/Class | Gap | Priority | Effort |
|------|-----------------|-----|----------|--------|
| 1 | **Viper.Crypto** | High-level Encrypt/Decrypt API (currently raw TLS primitives only) | **Critical** | Small |
| 2 | **Viper.IO** | Abstract Stream interfaces (InputStream/OutputStream) | **Critical** | Large |
| 3 | Viper.Threads | Future/Promise async pattern | **High** | Medium |
| 4 | Viper.Network | HttpClient with sessions/connection reuse | **High** | Medium |
| 5 | **Viper.DateTime** | Duration/TimeSpan class | **High** | Small |
| 6 | Infrastructure | Result<T,E> type for error handling | **High** | Small |
| 7 | Viper.Collections.* | Lazy evaluation (LazySeq) | Medium | Medium |
| 8 | **Viper.Text** | Regex capture groups / CompiledPattern | Medium | Medium |
| 9 | Viper.Graphics | Scene graph abstraction | Medium | Large |
| 10 | Viper.GUI | Dialog helpers (Alert, Confirm, Prompt) | Medium | Medium |

---

## 2. Namespace-by-Namespace Analysis

### 2.1 Viper.Collections

#### Current State
- **Class Count:** 11
- **Average Abstraction Level:** 2.8
- **API Surface Area:** 180+ public methods

#### Classes Inventory (From rt_seq.h, rt_map.h Deep Analysis)

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Seq | **4.5** | **42** | **Excellent** - Has full functional ops (Keep, Reject, Apply, All, Any, Fold, TakeWhile, DropWhile, Sort, SortBy) |
| List | 3 | 16 | Basic CRUD, duplicates Seq functionality |
| Map | 3 | 12 | Good with GetOr, SetIfMissing |
| Set | 4 | 13 | Complete set-theoretic operations |
| Bag | 3 | 11 | String-only Set, has Merge/Common/Diff |
| Stack | 3 | 7 | Simple but complete |
| Queue | 3 | 7 | Simple but complete |
| Ring | 4 | 10 | Good bounded circular buffer |
| Heap | 4 | 12 | Good priority queue with TryPop/TryPeek |
| TreeMap | 4 | 14 | Good ordered map with Floor/Ceil |
| Bytes | 3 | 17 | Good binary data with Base64/Hex |

**IMPORTANT FINDING from rt_seq.h Analysis:**
Seq actually has **42 methods** including excellent functional operations:
- `rt_seq_keep(obj, pred)` - Filter matching
- `rt_seq_reject(obj, pred)` - Filter non-matching
- `rt_seq_apply(obj, fn)` - Map transformation
- `rt_seq_all/any/none(obj, pred)` - Predicates
- `rt_seq_fold(obj, init, fn)` - Reduce
- `rt_seq_take_while/drop_while(obj, pred)` - Conditional slicing
- `rt_seq_sort/sort_by/sort_desc(obj, ...)` - Sorting

**This is much better than initially assessed.** The main gap is exposing these to the Zia runtime.

#### Remaining Gaps

1. **Functional ops exist but need closure support** - The C implementations are ready, just need IL/VM function pointer support
2. **Missing collection conversions** - No ToList(), ToSet(), ToMap() between types
3. **No lazy evaluation** - All operations are eager
4. **Missing typed collections** - All use obj, no compile-time type safety

#### Recommended Higher-Level Abstractions

**IEnumerable Interface**
```
Purpose: Common iteration protocol for all collections
Would wrap: All collection types
Key methods:
  GetEnumerator() -> IEnumerator

interface IEnumerator:
  MoveNext() -> Boolean
  Current() -> Object
  Reset() -> Void

Example usage:
  var seq = Viper.Collections.Seq.New()
  for item in seq {
    // unified iteration
  }

Priority: High
Effort: Medium
```

**Functional Collection Extensions**
```
Purpose: Enable functional programming patterns
Would wrap: Seq, List (extend to others)
Key methods:
  Keep(predicate) -> Seq        // Filter matching
  Reject(predicate) -> Seq      // Filter non-matching
  Apply(transform) -> Seq       // Map transformation
  Fold(seed, accumulator) -> Object
  All(predicate) -> Boolean
  Any(predicate) -> Boolean
  None(predicate) -> Boolean
  FindWhere(predicate) -> Integer
  TakeWhile(predicate) -> Seq
  DropWhile(predicate) -> Seq

Example usage:
  var evens = numbers.Keep(fn(x) => x % 2 == 0)
  var doubled = numbers.Apply(fn(x) => x * 2)
  var sum = numbers.Fold(0, fn(acc, x) => acc + x)

Priority: Critical
Effort: Large (requires closure protocol design)
```

**Collection Converters**
```
Purpose: Convert between collection types
Would wrap: All collections
Key methods:
  Seq.ToList() -> List
  Seq.ToSet() -> Set
  Seq.ToBag() -> Bag
  List.ToSeq() -> Seq
  Map.ToSeq() -> Seq  // Returns key-value pairs
  Set.ToSeq() -> Seq

Priority: Medium
Effort: Small
```

---

### 2.2 Viper.IO

#### Current State
- **Class Count:** 10
- **Average Abstraction Level:** 3.6
- **API Surface Area:** 120+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| File | 4 | 18 | Excellent static API (ReadAllText, WriteAllText, etc.) |
| Dir | 4 | 15 | Good directory operations with recursive variants |
| Path | 4 | 11 | Complete path manipulation |
| BinFile | 3 | 15 | Random access binary I/O |
| MemStream | 4 | 32 | Comprehensive in-memory stream |
| LineReader | 3 | 7 | Simple line-by-line reading |
| LineWriter | 3 | 8 | Simple line-by-line writing |
| Watcher | 3 | 13 | File system monitoring |
| Compress | 4 | 12 | Deflate/Gzip compression |
| Archive | 3 | - | ZIP archive handling |

#### Identified Gaps

1. **No async file operations** - All I/O is synchronous
2. **Missing directory tree operations** - No recursive copy, find files by pattern
3. **No temporary file abstraction** - No auto-cleanup temp files
4. **Missing config file readers** - No INI/properties support

#### Recommended Higher-Level Abstractions (**CRITICAL PRIORITY**)

**InputStream / OutputStream Interfaces**
```
Purpose: Abstract stream protocol for composable I/O
Would unify: BinFile, MemStream, TcpSocket, compressed streams

interface InputStream:
  Read(buf, len) -> Integer     // Returns bytes read
  ReadByte() -> Integer         // Single byte or -1
  ReadAll() -> Bytes            // Drain to end
  Skip(n) -> Integer            // Skip bytes
  get_Available -> Integer      // Bytes ready
  get_CanSeek -> Boolean
  Seek(pos) -> Void
  Close() -> Void

interface OutputStream:
  Write(buf, len) -> Integer
  WriteByte(b) -> Void
  WriteAll(data) -> Void
  Flush() -> Void
  Close() -> Void

Enables:
  - GzipInputStream wrapping FileInputStream
  - BufferedInputStream wrapping any stream
  - EncryptedOutputStream wrapping network stream
  - TeeOutputStream writing to multiple destinations

Priority: CRITICAL
Effort: Large (requires interface system)
```

**BufferedStream**
```
Purpose: Add buffering to any stream
Key methods:
  WrapRead(stream, bufSize) -> InputStream
  WrapWrite(stream, bufSize) -> OutputStream

Priority: High
Effort: Small (once interfaces exist)
```

**FileGlob**
```
Purpose: Find files by wildcard pattern
Key methods:
  Match(pattern) -> Seq          // "*.txt"
  MatchRecursive(pattern) -> Seq // "**/*.zia"
  Match(pattern, maxDepth) -> Seq

Example usage:
  var sources = Viper.IO.FileGlob.MatchRecursive("src/**/*.zia")
  for file in sources { compile(file) }

Priority: Medium
Effort: Small
```

**TempFile**
```
Purpose: Auto-cleanup temporary files
Key methods:
  New() -> TempFile
  NewWithExtension(ext) -> TempFile
  get_Path -> String
  Write(content) -> Void
  Read() -> String
  // Auto-deleted when disposed

Priority: Low
Effort: Small
```

**DirectoryTree**
```
Purpose: Recursive directory operations
Key methods:
  Copy(src, dst) -> Void
  FindFiles(pattern) -> Seq
  FindFilesRecursive(pattern) -> Seq
  GetSize() -> Integer
  DeleteEmpty() -> Integer  // Returns count deleted

Priority: Low
Effort: Small
```

---

### 2.3 Viper.Text

#### Current State
- **Class Count:** 9
- **Average Abstraction Level:** 3.8
- **API Surface Area:** 80+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Codec | 4 | 6 | Base64, Hex, URL encoding |
| Csv | 4 | 8 | Parse/format CSV with custom delimiters |
| Json | 4 | 7 | Parse/format JSON |
| Xml | 4 | 30 | Comprehensive DOM-style XML |
| Yaml | 3 | 5 | Basic YAML support |
| Guid | 4 | 5 | UUID generation and validation |
| StringBuilder | 3 | 7 | Efficient string building |
| Pattern | 4 | 9 | Regex matching and replacement |
| Template | 4 | 6 | String template rendering |

#### Identified Gaps

1. **No JsonPath queries** - Can't query JSON with path expressions
2. **No XPath support** - XML navigation is manual
3. **Missing text formatting** - No table formatting, word wrapping

#### Recommended Higher-Level Abstractions

**JsonPath**
```
Purpose: Query JSON with path expressions
Key methods:
  Query(json, path) -> Object
  QueryAll(json, path) -> Seq
  Exists(json, path) -> Boolean

Example usage:
  var name = JsonPath.Query(data, "$.users[0].name")
  var emails = JsonPath.QueryAll(data, "$..email")

Priority: Medium
Effort: Medium
```

---

### 2.4 Viper.Graphics

#### Current State (From rt_graphics.h, rt_sprite.h, rt_tilemap.h, rt_camera.h Deep Analysis)
- **Class Count:** 6
- **Average Abstraction Level:** 4.0
- **API Surface Area:** 115+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Canvas | **4.5** | 52 | **Excellent** - Shapes, text, gradients, Bezier, polygons, flood fill, clip regions |
| Color | 4 | 15 | Good HSL/RGB, lerp, brighten/darken |
| Pixels | 4 | ~25 | Image loading and manipulation |
| Sprite | 4 | 18 | Animation frames, transform, collision detection |
| Tilemap | 4 | 16 | Tileset-based rendering with pixel/tile conversion |
| Camera | 4 | 15 | Viewport, zoom, rotation, world/screen conversion, bounds |

**Highlights from Header Analysis:**
- Canvas has `rt_canvas_bezier` for curves, `rt_canvas_polygon` for arbitrary shapes
- Canvas supports `rt_canvas_set_clip_rect` for clipping regions
- Canvas has `rt_canvas_gradient_h/v` for gradients
- Sprite has `rt_sprite_overlaps` for AABB collision
- Camera has `rt_camera_set_bounds` for limiting camera movement

#### Assessment
**Well-designed namespace.** Covers 2D game graphics needs comprehensively.

#### Gaps Identified

1. **No scene graph** - Manual draw ordering required
2. **No sprite batching** - Each draw call is separate (performance)
3. **No render-to-texture** - Can't render to Pixels buffer
4. **No particle system** - Must build from scratch (though Viper.Game has ParticleEmitter)

#### Recommended Higher-Level Abstractions

**Scene** (For complex games)
```
Purpose: Hierarchical scene graph with automatic ordering
Key methods:
  AddChild(node) -> Void
  RemoveChild(node) -> Void
  SetZIndex(n) -> Void
  get_Transform -> Mat3
  Draw(canvas) -> Void  // Draws all children in order

Priority: Medium (complex games)
Effort: Large
```

**SpriteBatch** (For performance)
```
Purpose: Batched sprite rendering
Key methods:
  Begin() -> Void
  Draw(sprite, x, y) -> Void
  End() -> Void  // Commits all draws efficiently

Priority: Medium (performance-critical games)
Effort: Medium
```

---

### 2.5 Viper.GUI

#### Current State
- **Class Count:** 50+
- **Average Abstraction Level:** 4.2
- **API Surface Area:** 300+ public methods

#### Classes Inventory (Partial)

| Category | Classes | Level | Assessment |
|----------|---------|-------|------------|
| Core | App, Widget, Font, Theme | 4 | Good foundation |
| Layout | VBox, HBox, Container | 4 | Flexbox-style layout |
| Input | Button, TextInput, Checkbox, Slider, Spinner | 4 | Complete input widgets |
| Display | Label, Image, ProgressBar | 4 | Display widgets |
| Selection | Dropdown, ListBox, RadioButton, RadioGroup | 4 | Selection widgets |
| Navigation | TabBar, Tab, TreeView, Breadcrumb | 4 | Navigation widgets |
| Containers | ScrollView, SplitPane | 4 | Container widgets |
| Menus | MenuBar, Menu, MenuItem, ContextMenu | 4 | Menu system |
| Toolbars | Toolbar, StatusBar | 4 | Application chrome |
| Dialogs | MessageBox, FileDialog | 5 | Modal dialogs |
| Advanced | CodeEditor, FindBar, CommandPalette, Minimap | 5 | IDE-grade widgets |
| Utilities | Clipboard, Shortcuts, Cursor, Tooltip, Toast | 4 | UI utilities |
| Drag/Drop | Widget drag/drop support | 4 | Drag and drop |

#### Assessment
**Excellent namespace.** This is one of the most comprehensive GUI libraries, comparable to Qt or GTK. The CodeEditor with syntax highlighting, multi-cursor, code folding, and minimap is particularly impressive.

#### Identified Gaps (From rt_gui.h Deep Analysis)

While GUI is comprehensive, several convenience abstractions are missing:

1. **No simple Dialog helpers** - MessageBox exists but no one-line Alert/Confirm/Prompt
2. **No ListView widget** - TreeView exists but flat list needs custom implementation
3. **Missing RadioButton/RadioGroup** - Only Checkbox currently
4. **No ProgressBar** - Must build from Canvas or Box
5. **No ToolTip helpers** - Widget tooltips not exposed
6. **No data binding** - Manual state synchronization required

#### Recommended Higher-Level Abstractions

**Dialog Helpers** (**HIGH PRIORITY**)
```
Purpose: One-call modal dialogs
Key methods:
  Alert(title, message) -> Void
  Confirm(title, message) -> Boolean
  Prompt(title, default) -> String?
  FileOpen(filter) -> String?
  FileSave(filter, defaultName) -> String?
  FolderSelect() -> String?
  ColorPick(initial) -> Integer?

Example usage:
  if Viper.GUI.Dialog.Confirm("Delete?", "Are you sure?") {
    deleteFile()
  }

  var name = Viper.GUI.Dialog.Prompt("Name", "Untitled")
  if name != null {
    createFile(name)
  }

Priority: HIGH
Effort: Medium
```

**ListView**
```
Purpose: Scrollable item list (simpler than TreeView)
Key methods:
  New(parent) -> ListView
  SetItems(seq) -> Void
  SetRenderer(fn) -> Void
  get_SelectedIndex -> Integer
  get_SelectedItem -> Object
  OnSelect(fn) -> Void

Priority: Medium
Effort: Medium
```

**DataGrid** (Lower Priority)
```
Purpose: Tabular data display
Key methods:
  New(parent) -> DataGrid
  SetColumns(names) -> Void
  SetRows(data) -> Void
  SetCellRenderer(fn) -> Void
  get_SelectedRow -> Integer
```

---

### 2.6 Viper.Sound

#### Current State
- **Class Count:** 4
- **Average Abstraction Level:** 3.8
- **API Surface Area:** 35 public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Audio | 4 | 7 | Global audio system control |
| Sound | 4 | 5 | Sound effect loading and playback |
| Voice | 3 | 4 | Individual sound instance control |
| Music | 4 | 13 | Streaming music with seek |

#### Assessment
**Good namespace.** Covers typical game audio needs.

#### Minor Recommendations
- Consider `AudioGroup` for volume control by category
- Consider `SoundPool` for frequently-played effects

---

### 2.7 Viper.Input

#### Current State
- **Class Count:** 4
- **Average Abstraction Level:** 4.0
- **API Surface Area:** 120+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Keyboard | 4 | 70+ | Complete keyboard handling with key constants |
| Mouse | 4 | 28 | Mouse position, buttons, wheel |
| Pad | 4 | 35 | Gamepad with vibration support |
| Action | 5 | 40 | Input abstraction/mapping system |

#### Assessment
**Excellent namespace.** The Action system for input mapping is particularly well-designed, supporting keyboard, mouse, and gamepad bindings with axis support.

#### No Major Gaps
Consider:
- `Gesture` recognition for touch patterns
- `InputRecorder` for replay systems

---

### 2.8 Viper.Game

#### Current State
- **Class Count:** 11
- **Average Abstraction Level:** 4.0
- **API Surface Area:** 150+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Grid2D | 4 | 13 | 2D array container |
| Timer | 4 | 16 | Frame-based timer |
| StateMachine | 4 | 17 | Game state management |
| Tween | 4 | 20 | Animation/interpolation |
| ButtonGroup | 4 | 17 | Mutually exclusive selection |
| SmoothValue | 4 | 13 | Value smoothing |
| ParticleEmitter | 4 | 26 | Particle effects |
| SpriteAnimation | 4 | 24 | Frame-based animation |
| CollisionRect | 4 | 22 | AABB collision |
| Collision | 4 | 7 | Static collision helpers |
| ObjectPool | 3 | - | Object pooling |

#### Assessment
**Excellent namespace.** Comprehensive game development utilities covering common patterns.

#### Minor Recommendations
- Consider `EntityManager` for ECS-lite pattern
- Consider `Scene` abstraction for level management
- Consider `SaveGame` serialization helper

---

### 2.9 Viper.Threads

#### Current State (From rt_threads.h, rt_channel.h, rt_threadpool.h Deep Analysis)
- **Class Count:** 9
- **Average Abstraction Level:** 3.8 (Channel is excellent)
- **API Surface Area:** 91 public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Thread | 3 | 8 | Basic threads with join/tryJoin/joinFor |
| Monitor | 4 | 9 | **Excellent** - re-entrant, FIFO-fair, with timeout |
| SafeI64 | 4 | 5 | Complete atomic ops including CAS |
| Gate | 4 | 7 | Good semaphore with leaveMany |
| Barrier | 3 | 5 | Basic but functional |
| RwLock | 4 | 10 | Good with reader/writer preference control |
| Pool | 3 | 9 | Thread pool with graceful/immediate shutdown |
| Channel | **5** | 12 | **Excellent** Go-style bounded/unbounded channels |
| TLS | 3 | 4 | Thread-local storage |

**Highlights from Header Analysis:**
- Channel supports both bounded (blocking) and synchronous (capacity=0) modes
- Monitor has `rt_monitor_wait_for(obj, ms)` with timeout
- RwLock is writer-preferring to avoid starvation
- Pool distinguishes `shutdown()` (graceful) vs `shutdown_now()` (immediate)

#### Identified Gaps

1. **No Future/Promise** - No async result handling
2. **No Task abstraction** - No high-level async operation wrapper
3. **No async/await pattern** - Callbacks require manual coordination
4. **No ParallelFor** - No easy parallel loop execution

#### Recommended Higher-Level Abstractions

**Future**
```
Purpose: Represent async computation result
Key methods:
  Get() -> Object         // Blocking wait
  TryGet() -> Object?     // Non-blocking, returns null if not ready
  GetFor(ms) -> Object?   // Wait with timeout
  get_IsComplete -> Boolean
  get_IsCancelled -> Boolean
  Cancel() -> Boolean

Example usage:
  var future = doAsyncWork()
  // ... do other work ...
  var result = future.Get()

Priority: High
Effort: Medium
```

**Task**
```
Purpose: High-level async operation wrapper
Key methods:
  Run(callback) -> Future
  Delay(ms) -> Future
  WhenAll(futures) -> Future
  WhenAny(futures) -> Future

Example usage:
  var f1 = Task.Run(fn() => computeA())
  var f2 = Task.Run(fn() => computeB())
  var all = Task.WhenAll([f1, f2])
  var results = all.Get()

Priority: High
Effort: Medium
```

---

### 2.10 Viper.Network

#### Current State (From rt_network.h Deep Analysis - 694 lines)
- **Class Count:** 10
- **Average Abstraction Level:** 4.0 (better than initially assessed)
- **API Surface Area:** 98 public methods (very comprehensive!)

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Tcp | 4 | 18 | Complete TCP client with timeout support |
| TcpServer | 4 | 7 | Accept with timeout, address binding |
| Udp | 4 | 18 | Full UDP with multicast, broadcast |
| Dns | 4 | 11 | IPv4/IPv6 resolution, reverse lookup |
| Http | 4 | 6 | Static Get/Post/Download helpers |
| HttpReq | 4 | 8 | Builder pattern with chaining |
| HttpRes | 4 | 8 | Full response access |
| Url | **5** | 28 | **Batteries-included** URL parsing/building |
| WebSocket | 4 | 13 | RFC 6455 compliant client |

**Note:** This namespace is much stronger than initially assessed. The Url class (28 methods) is particularly comprehensive with query parameter manipulation, URL resolution, encoding/decoding.

#### Actually Good Features (From Header Analysis)
- `HttpReq.SetHeader/SetBody/SetTimeout` return `self` for chaining
- `rt_url_set_query_param` / `rt_url_get_query_param` for query manipulation
- `rt_url_resolve` for relative URL resolution
- `rt_tcp_recv_line` for line-based protocols
- `rt_udp_join_group` / `rt_udp_leave_group` for multicast

#### Remaining Gaps

1. **No HTTPS support** - HTTP only (TLS exists separately but not integrated)
2. **No connection pooling** - Each request creates new connection
3. **No retry/backoff** - Manual retry logic needed
4. **No cookie jar** - No automatic cookie management
5. **No SSE (Server-Sent Events)** - Only WebSocket for real-time

#### Recommended Higher-Level Abstractions

**HttpClient**
```
Purpose: Fluent HTTP client with method chaining
Key methods:
  New() -> HttpClient
  SetTimeout(ms) -> HttpClient
  SetHeader(name, value) -> HttpClient
  SetBaseUrl(url) -> HttpClient
  Get(url) -> HttpRes
  Post(url, body) -> HttpRes
  Put(url, body) -> HttpRes
  Delete(url) -> HttpRes
  GetJson(url) -> Object      // Parse response as JSON
  PostJson(url, data) -> Object

Example usage:
  var client = HttpClient.New()
    .SetBaseUrl("https://api.example.com")
    .SetHeader("Authorization", "Bearer " + token)
    .SetTimeout(5000)

  var user = client.GetJson("/users/1")
  client.PostJson("/users", newUser)

Priority: High
Effort: Medium
```

---

### 2.11 Viper.Time / Viper.DateTime

#### Current State
- **Class Count:** 4
- **Average Abstraction Level:** 3.5
- **API Surface Area:** 45+ public methods

#### Classes Inventory (From rt_datetime.h Analysis)

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| DateTime | 3 | 15 | Unix timestamps only, no Duration type |
| Stopwatch | 4 | 11 | High-precision timing |
| Clock | 4 | 3 | System time and sleep |
| Countdown | 4 | 11 | Countdown timer |

#### Identified Gaps

1. **No Duration/TimeSpan type** - Must do manual second arithmetic
2. **No timezone support** - Everything is UTC only
3. **No date-only type** - All timestamps include time component
4. **Missing recurring schedules** - No cron-like patterns

#### Recommended Higher-Level Abstractions

**Duration** (**HIGH PRIORITY**)
```
Purpose: Represent time spans with clear semantics
Key methods:
  FromSeconds(n) -> Duration
  FromMinutes(n) -> Duration
  FromHours(n) -> Duration
  FromDays(n) -> Duration
  get_TotalSeconds -> Integer
  get_TotalMinutes -> Integer
  get_TotalHours -> Integer
  Add(other) -> Duration
  Sub(other) -> Duration
  Multiply(n) -> Duration

Example usage:
  var timeout = Duration.FromMinutes(5)
  var deadline = DateTime.Now() + timeout.get_TotalSeconds

  var elapsed = Duration.FromSeconds(datetime1 - datetime2)
  Say("Took " + elapsed.get_TotalMinutes + " minutes")

Priority: HIGH
Effort: Small
```

**DateOnly**
```
Purpose: Date without time component
Key methods:
  Today() -> DateOnly
  FromParts(y, m, d) -> DateOnly
  AddDays(n) -> DateOnly
  DaysBetween(other) -> Integer
  ToDateTime() -> Integer  // Midnight UTC

Priority: Low
Effort: Small
```

**Schedule**
```
Purpose: Cron-like scheduling
Key methods:
  Parse(cronExpr) -> Schedule
  NextOccurrence(from) -> Integer
  NextN(from, n) -> Seq

Priority: Low
Effort: Medium
```

---

### 2.12 Viper.Math

#### Current State
- **Class Count:** 5
- **Average Abstraction Level:** 4.0
- **API Surface Area:** 120+ public methods

#### Classes Inventory

| Class | Level | Methods | Assessment |
|-------|-------|---------|------------|
| Math | 4 | 40 | Comprehensive math functions |
| Vec2 | 4 | 18 | 2D vector |
| Vec3 | 4 | 18 | 3D vector |
| Mat3 | 4 | 22 | 3x3 matrix for 2D transforms |
| Mat4 | 4 | 24 | 4x4 matrix for 3D transforms |
| BigInt | 4 | 35 | Arbitrary precision integers |

#### Assessment
**Excellent namespace.** Complete coverage of mathematical operations including arbitrary precision.

---

### 2.13 Viper.Crypto

#### Current State
- **Class Count:** 1 (static utilities at C level)
- **Average Abstraction Level:** 1.5 (**CRITICAL GAP**)
- **API Surface Area:** 11 methods (very low-level)

#### Classes Inventory (From rt_crypto.h Analysis)

| Function | Level | Assessment |
|----------|-------|------------|
| rt_sha256(data, len, digest) | 1 | Raw bytes, no string wrapper |
| rt_hmac_sha256(...) | 1 | Raw key/data handling |
| rt_hkdf_extract/expand(...) | 1 | TLS internals, not user-facing |
| rt_chacha20_poly1305_encrypt(...) | 1 | Requires nonce/AAD management |
| rt_chacha20_poly1305_decrypt(...) | 1 | Manual authentication handling |
| rt_x25519_keygen(...) | 2 | Key exchange only |
| rt_crypto_random_bytes(...) | 2 | Raw bytes output |

#### Identified Gaps (**SEVERE**)

1. **No high-level encryption API** - Users must manually compose HKDF + nonce + ChaCha20
2. **No password hashing** - Missing bcrypt/scrypt/argon2 for user passwords
3. **No digital signatures** - Only X25519 key exchange, no Ed25519/RSA signing
4. **No secure string generation** - Only raw bytes, no Base64/hex tokens
5. **No certificate handling** - X.509 not supported
6. **No hash streaming** - Must hash entire data at once
7. **AES mentioned in docs but only ChaCha20 in rt_crypto.h** - API mismatch

#### Recommended Higher-Level Abstractions (**CRITICAL PRIORITY**)

**Simple Encryption API**
```
Purpose: One-call encrypt/decrypt with password
Key methods:
  Encrypt(data, password) -> Bytes
    - Internally: derive key with HKDF, generate nonce, encrypt with ChaCha20-Poly1305
    - Output: nonce || ciphertext || tag
  Decrypt(data, password) -> Bytes
    - Validates authentication tag, returns plaintext or traps

Example usage:
  var encrypted = Viper.Crypto.Encrypt(secretData, "user-password")
  var decrypted = Viper.Crypto.Decrypt(encrypted, "user-password")

Priority: CRITICAL
Effort: Small (wraps existing primitives)
```

**Hash Convenience API**
```
Purpose: Hash strings/bytes with one call
Key methods:
  Sha256(data) -> String (hex)
  Sha256Bytes(data) -> Bytes
  Sha256Stream() -> HashStream
    - Update(data) -> HashStream
    - Finish() -> String

Priority: High
Effort: Small
```

**PasswordHash**
```
Purpose: Secure password storage
Key methods:
  Hash(password) -> String    // Returns salted hash
  Verify(password, hash) -> Boolean

Priority: High
Effort: Medium (needs PBKDF2/Argon2 implementation)
```

**SecureRandom**
```
Purpose: Cryptographic random utilities
Key methods:
  Bytes(n) -> Bytes
  String(n) -> String (Base64)
  Hex(n) -> String
  Int(min, max) -> Integer
  Token(n) -> String (URL-safe)
  Uuid() -> String

Priority: Medium
Effort: Small
```

---

## 3. Cross-Cutting Concerns

### 3.1 Patterns That Should Be Consistent

| Pattern | Current State | Recommendation |
|---------|---------------|----------------|
| Property naming | Mixed: `get_Len`, `get_Count`, `get_Length` | Standardize on `Len` with aliases |
| Error handling | Mixed: trap vs TryX | Provide both consistently |
| Constructor naming | Mostly `New()` | Keep consistent |
| Boolean properties | Mixed: `IsX`, `get_IsX` | Standardize on `get_IsX` |

### 3.2 Missing Infrastructure Classes

| Type | Priority | Description |
|------|----------|-------------|
| **Result<T,E>** | Medium | Either success value or error |
| **Option<T>** | Medium | Present or absent value |
| **Lazy<T>** | Low | Deferred initialization |
| **Tuple2/3/4** | Low | Generic tuple types |
| **Range** | Low | Numeric range (start, end, step) |

### 3.3 Recommended Coding Conventions

1. **All collections should implement ToSeq()** for uniform conversion
2. **Error-prone operations should have TryX variants** returning boolean
3. **Properties should use get_/set_ prefix** consistently
4. **Destructive operations should return void**, non-destructive return new object

---

## 4. Proposed New Namespaces

### 4.1 Viper.Async (Recommended)

**Purpose:** Centralize async/await patterns

**Classes:**
- `Future<T>` - Async result container
- `Task` - Async operation utilities
- `Promise` - Manually resolved future
- `AsyncSeq` - Async enumeration

**Rationale:** Threading primitives exist but high-level async patterns are missing.

### 4.2 Viper.Linq (Optional)

**Purpose:** LINQ-style query operations

**Classes:**
- `Enumerable` - Static query methods
- `Queryable` - Composable query builder

**Rationale:** Functional operations on collections are highly desired but require closure support first.

---

## 5. Prioritized Action Plan

| Priority | Action | Affected | Dependencies | Complexity |
|----------|--------|----------|--------------|------------|
| 1 | Design closure/callback protocol | IL, VM | None | Large |
| 2 | Expose Seq functional ops in runtime.def | Collections | #1 | Small |
| 3 | Implement Future/Promise | Threads | None | Medium |
| 4 | Add HttpClient builder | Network | None | Medium |
| 5 | Add collection conversion methods | Collections | None | Small |
| 6 | Implement Task wrapper | Threads | #3 | Medium |
| 7 | Add functional ops to List, Map, Set | Collections | #1 | Medium |
| 8 | Add Result<T,E> infrastructure type | Core | None | Small |
| 9 | Add JsonPath support | Text | None | Medium |
| 10 | Standardize property naming with aliases | All | None | Small |

---

## 6. Appendix: Raw Data

### 6.1 Complete Class Listing by Abstraction Level

#### Level 5 (Batteries-Included)
- Viper.GUI.CodeEditor
- Viper.GUI.MessageBox
- Viper.GUI.FileDialog
- Viper.GUI.CommandPalette
- Viper.Input.Action
- Viper.GUI.FindBar

#### Level 4 (High-Level)
- Viper.Collections.Set
- Viper.Collections.TreeMap
- Viper.IO.File
- Viper.IO.Dir
- Viper.IO.Path
- Viper.IO.MemStream
- Viper.IO.Compress
- Viper.Text.Codec
- Viper.Text.Csv
- Viper.Text.Json
- Viper.Text.Xml
- Viper.Text.Guid
- Viper.Text.Pattern
- Viper.Text.Template
- Viper.Graphics.Canvas
- Viper.Graphics.Color
- Viper.Graphics.Pixels
- Viper.Graphics.Sprite
- Viper.Graphics.Tilemap
- Viper.Graphics.Camera
- Viper.GUI.* (most widgets)
- Viper.Sound.Audio
- Viper.Sound.Sound
- Viper.Sound.Music
- Viper.Input.Keyboard
- Viper.Input.Mouse
- Viper.Input.Pad
- Viper.Game.* (most classes)
- Viper.Threads.SafeI64
- Viper.Threads.Channel
- Viper.Time.DateTime
- Viper.Time.Stopwatch
- Viper.Time.Countdown
- Viper.Math.*
- Viper.Crypto.Hash
- Viper.Crypto.Aes
- Viper.Network.Dns
- Viper.Network.Url

#### Level 3 (Intermediate)
- Viper.Collections.Seq
- Viper.Collections.List
- Viper.Collections.Map
- Viper.Collections.Bag
- Viper.Collections.Ring
- Viper.Collections.Heap
- Viper.Collections.Bytes
- Viper.IO.BinFile
- Viper.IO.LineReader
- Viper.IO.LineWriter
- Viper.IO.Watcher
- Viper.Text.StringBuilder
- Viper.Text.Yaml
- Viper.Sound.Voice
- Viper.Threads.Thread
- Viper.Threads.Monitor
- Viper.Threads.Gate
- Viper.Threads.Barrier
- Viper.Threads.RwLock
- Viper.Threads.Pool
- Viper.Network.Tcp
- Viper.Network.TcpServer
- Viper.Network.Udp
- Viper.Network.HttpReq
- Viper.Network.HttpRes
- Viper.Network.WebSocket
- Viper.Crypto.Tls

#### Level 2 (Basic)
- Viper.Collections.Stack
- Viper.Collections.Queue
- Viper.Network.Http

### 6.2 Methods Needing Exposure (Already in C)

**Seq (rt_seq.h) - Not in runtime.def:**
```c
rt_seq_keep(seq, predicate)      // Filter matching
rt_seq_reject(seq, predicate)    // Filter non-matching
rt_seq_apply(seq, transform)     // Map transformation
rt_seq_all(seq, predicate)       // All match
rt_seq_any(seq, predicate)       // Any match
rt_seq_none(seq, predicate)      // None match
rt_seq_count_where(seq, pred)    // Count matching
rt_seq_find_where(seq, pred)     // Find first matching index
rt_seq_take_while(seq, pred)     // Take while predicate true
rt_seq_drop_while(seq, pred)     // Drop while predicate true
rt_seq_fold(seq, seed, acc)      // Reduce with accumulator
```

**Note:** These require function pointer parameters which the IL/VM currently cannot pass.

---

## Summary

The Viper runtime is a **comprehensive, well-designed library** with 1,418+ functions across 98+ classes. After deep header analysis, the average abstraction level is higher than initially assessed (3.5+), with several namespaces being excellent:

### Namespace Quality Assessment

| Namespace | Quality | Notes |
|-----------|---------|-------|
| **Viper.GUI** | Excellent (Level 4-5) | 50+ widgets including CodeEditor, very comprehensive |
| **Viper.Network** | Excellent (Level 4) | 98 methods, Url class is batteries-included |
| **Viper.Graphics** | Excellent (Level 4) | 115+ methods, full 2D rendering with Bezier, polygons |
| **Viper.Threads.Channel** | Excellent (Level 5) | Go-style channels, bounded/unbounded |
| **Viper.Collections.Seq** | Very Good (Level 4.5) | 42 methods with functional ops (Keep, Reject, Fold) |
| **Viper.Input** | Excellent (Level 4-5) | Action mapping system is particularly well-designed |
| **Viper.Game** | Very Good (Level 4) | StateMachine, Tween, Collision, ParticleEmitter |
| **Viper.Crypto** | **NEEDS WORK** (Level 1-2) | Only raw TLS primitives, no high-level API |

### Key Findings from Deep Analysis

1. **Seq is better than initially thought** - Has 42 methods including Keep/Reject/Apply/Fold/Sort in C, just needs closure support to expose
2. **Network is comprehensive** - 98 methods, Url class has 28 methods alone
3. **Crypto is severely lacking** - Only raw ChaCha20/HKDF/X25519, needs high-level Encrypt/Decrypt
4. **GUI is batteries-included** - CodeEditor with syntax highlighting, TreeView, TabBar, SplitPane
5. **Channel is excellent** - True Go-style CSP with bounded/unbounded modes

### Top 5 Critical Improvements

| Priority | Gap | Impact | Effort |
|----------|-----|--------|--------|
| 1 | **High-level Crypto API** | Security-critical, currently unusable | Small |
| 2 | **Stream Interfaces** | Enables composable I/O | Large |
| 3 | **Future/Promise** | Modern async patterns | Medium |
| 4 | **Duration type** | Date arithmetic UX | Small |
| 5 | **Expose Seq functional ops** | Already implemented in C | Small (needs closure support) |

### Estimated Total Effort

| Category | Items | Effort |
|----------|-------|--------|
| Critical (must-have) | 5 | ~3 weeks |
| High Priority | 8 | ~4 weeks |
| Medium Priority | 12 | ~6 weeks |
| Low Priority | 10 | ~4 weeks |
| **Total** | 35 items | ~17 weeks |

The runtime embodies the Amiga-inspired batteries-included philosophy well. The Crypto namespace is the only severe gap. Most other improvements are incremental enhancements to an already solid foundation.
