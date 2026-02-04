# Viper Runtime API Review

> Deep dive analysis of organizational issues before public API release

## Executive Summary

The Viper runtime has **127 classes** organized across **15 namespaces**. While generally well-structured, there are several organizational inconsistencies that should be addressed before shipping a public API.

**Critical Issues:** 3
**Moderate Issues:** 7
**Minor Issues:** 5

---

## 1. Critical Issues

### 1.1 Game Utilities in Root Namespace (CRITICAL)

**Problem:** 14 game-specific classes are in the root `Viper.*` namespace instead of a dedicated namespace.

| Class | Purpose |
|-------|---------|
| `Viper.Grid2D` | 2D tile/game grids |
| `Viper.Timer` | Frame-based game timers |
| `Viper.StateMachine` | Game state management |
| `Viper.Tween` | Animation tweening |
| `Viper.ButtonGroup` | Mutually exclusive selection |
| `Viper.SmoothValue` | Value interpolation |
| `Viper.ParticleEmitter` | Particle effects |
| `Viper.SpriteAnimation` | Frame-based animation |
| `Viper.CollisionRect` | AABB collision |
| `Viper.Collision` | Static collision helpers |
| `Viper.ObjectPool` | Object slot reuse |
| `Viper.ScreenFX` | Screen shake/flash/fade |
| `Viper.PathFollower` | Waypoint following |
| `Viper.Quadtree` | Spatial partitioning |

**Recommendation:** Move to `Viper.Game.*` namespace:
- `Viper.Game.Grid2D`
- `Viper.Game.Timer`
- `Viper.Game.StateMachine`
- etc.

**Impact:** Breaking change for existing code, but establishes proper separation of concerns.

---

### 1.2 DateTime Namespace Inconsistency (CRITICAL)

**Problem:** Time-related classes are split inconsistently:

| Current Location | Class |
|------------------|-------|
| `Viper.DateTime` | Date/time operations (root namespace) |
| `Viper.Time.Clock` | Sleep and tick functions |
| `Viper.Time.Countdown` | Millisecond-based countdown |
| `Viper.Diagnostics.Stopwatch` | Performance timing |

**Recommendation:** Consolidate all time-related classes:

Option A (Expand Time namespace):
```
Viper.Time.DateTime      (move from Viper.DateTime)
Viper.Time.Clock         (keep)
Viper.Time.Countdown     (keep)
Viper.Time.Stopwatch     (move from Viper.Diagnostics.Stopwatch)
```

Option B (Flatten to root):
```
Viper.DateTime           (keep)
Viper.Clock              (move from Viper.Time.Clock)
Viper.Countdown          (move from Viper.Time.Countdown)
Viper.Stopwatch          (move from Viper.Diagnostics.Stopwatch)
```

**Recommendation:** Option A is cleaner and follows the pattern of other namespaces.

---

### 1.3 String vs Strings Confusion (CRITICAL)

**Problem:** Two similarly-named namespaces for string operations:

| Namespace | Purpose | Methods |
|-----------|---------|---------|
| `Viper.String.*` | Instance string methods | `Length`, `Substring`, `Split`, `Trim`, `ToUpper`, etc. |
| `Viper.Strings.*` | Static conversion helpers | `FromInt`, `FromDouble`, `Equals`, `Join` |

This is confusing for users: "Do I use String or Strings?"

**Recommendation:** Merge `Viper.Strings.*` into `Viper.String.*`:
- `Viper.String.FromInt(i64) -> str`
- `Viper.String.FromDouble(f64) -> str`
- `Viper.String.Join(delimiter, seq) -> str`

The existing `Viper.Strings.Equals` should become `Viper.String.Equals` (or rely on the `==` operator).

---

## 2. Moderate Issues

### 2.1 Diagnostics Namespace Too Small

**Current contents:**
- `Viper.Diagnostics.Assert*` (standalone functions)
- `Viper.Diagnostics.Stopwatch` (class)
- `Viper.Diagnostics.Trap` (standalone function)

**Problem:**
- Stopwatch is really about timing, not diagnostics
- Only 3 things in the namespace

**Recommendation:**
- Move `Stopwatch` to `Viper.Time.Stopwatch`
- Keep `Assert` and `Trap` as `Viper.Diagnostics.Assert` and `Viper.Diagnostics.Trap`
- Or flatten: `Viper.Assert`, `Viper.Trap`

---

### 2.2 Vec2/Vec3 in Root vs Math Namespace

**Current:** `Viper.Vec2`, `Viper.Vec3` (root namespace)
**Related:** `Viper.Math` (math functions in root)

**Analysis:** Vector types are mathematical constructs but also fundamental game types.

**Options:**
1. Keep in root (current) - Easier access for common use
2. Move to `Viper.Math.Vec2`, `Viper.Math.Vec3` - More logical grouping

**Recommendation:** Keep in root. They're used frequently enough to warrant short names.

---

### 2.3 Bits in Root vs Math Namespace

**Current:** `Viper.Bits` (root namespace)
**Related:** `Viper.Math` (also root)

**Analysis:** Bit manipulation is mathematical in nature.

**Recommendation:** Keep in root. It's commonly used and the short name `Viper.Bits` is appropriate.

---

### 2.4 Input.Manager Placement

**Current:** `Viper.Input.Manager` (in Input namespace)
**Related:** Game utilities in root namespace

**Analysis:** `Input.Manager` is primarily a game development utility (debounced input, unified keyboard/mouse/pad handling).

**Recommendation:** Keep in `Viper.Input.*`. It fits logically with the other input classes and isn't purely game-specific.

---

### 2.5 Box Not a Class

**Current:** `Viper.Box.*` is a set of standalone functions, not a class:
- `Viper.Box.I64(i64) -> obj`
- `Viper.Box.ToI64(obj) -> i64`
- etc.

**Analysis:** This is actually correct design - boxing functions don't need instance state.

**Recommendation:** Keep as-is. Document clearly that Box is a static utility namespace, not an instantiable class.

---

### 2.6 GUI.MessageBox and GUI.FileDialog Not Classes

**Current:** These are standalone function sets:
- `Viper.GUI.MessageBox.Info(title, message)`
- `Viper.GUI.FileDialog.Open(title, path, filter)`

**Analysis:** These are modal dialogs that don't need instance state.

**Recommendation:** Keep as-is. This follows standard patterns in other frameworks.

---

### 2.7 Sound.Audio vs Sound.Sound Naming

**Current:**
- `Viper.Sound.Audio` - Global audio control (static)
- `Viper.Sound.Sound` - Sound effect instances

**Analysis:** The naming `Sound.Sound` is slightly redundant but unambiguous.

**Recommendation:** Keep as-is. The alternative (`Sound.Effect`) would break from common terminology.

---

## 3. Minor Issues

### 3.1 Inconsistent Property Naming

Some properties use `get_` prefix pattern, others don't:
- `Viper.Collections.Seq.get_Len` vs `Viper.Collections.Seq.Len`
- `Viper.Timer.get_IsExpired` vs direct property access

**Recommendation:** This is an implementation detail. The public API should expose these as properties without the `get_` prefix. Verify all classes follow the same pattern.

---

### 3.2 Missing Heap/Set in Collections

**Current:** `Viper.Collections.Heap` and `Viper.Collections.Set` exist.

**Note:** These are properly documented and placed. No issue here.

---

### 3.3 Crypto.Rand vs Random

**Current:**
- `Viper.Random` - General-purpose PRNG (fast, seedable)
- `Viper.Crypto.Rand` - Cryptographically secure random

**Analysis:** This is correct! Two different use cases. The naming is clear.

**Recommendation:** Keep as-is. Document the difference clearly.

---

### 3.4 Network Namespace Organization

**Current:** All network classes in `Viper.Network.*`:
- `Tcp`, `TcpServer`, `Udp` - Socket-level
- `Http`, `HttpReq`, `HttpRes` - HTTP-level
- `Dns` - DNS resolution
- `Url` - URL parsing
- `WebSocket` - WebSocket client

**Analysis:** This is well-organized. Everything network-related is together.

**Recommendation:** Keep as-is.

---

### 3.5 IO Namespace Organization

**Current:** File and stream classes in `Viper.IO.*`:
- `File`, `Dir`, `Path` - File system
- `BinFile`, `LineReader`, `LineWriter`, `MemStream` - Streams
- `Archive`, `Compress` - Compression
- `Watcher` - File system events

**Analysis:** Well-organized.

**Recommendation:** Keep as-is.

---

## 4. Recommended Changes Summary

### High Priority (Should fix before public release)

| Change | Impact | Difficulty |
|--------|--------|------------|
| Create `Viper.Game.*` namespace for game utilities | Breaking | Medium |
| Move `DateTime` to `Viper.Time.DateTime` | Breaking | Low |
| Move `Stopwatch` to `Viper.Time.Stopwatch` | Breaking | Low |
| Merge `Viper.Strings.*` into `Viper.String.*` | Breaking | Medium |

### Medium Priority (Consider for future release)

| Change | Impact | Difficulty |
|--------|--------|------------|
| Verify all properties follow consistent naming | Non-breaking | Low |
| Add documentation about Box being static functions | Non-breaking | Low |

### Low Priority (Optional)

| Change | Impact | Difficulty |
|--------|--------|------------|
| Consider `Viper.Math.Vec2` (currently root) | Breaking | Low |
| Consider `Viper.Math.Bits` (currently root) | Breaking | Low |

---

## 5. Proposed Final Namespace Structure

```
Viper (root)
├── Bits                    # Bit manipulation (static)
├── Box                     # Boxing/unboxing (static functions)
├── Convert                 # Type conversion (static)
├── Environment             # Env vars, args (static)
├── Exec                    # Command execution (static)
├── Fmt                     # Formatting (static)
├── Log                     # Logging (static)
├── Machine                 # System info (static)
├── Math                    # Math functions (static)
├── Object                  # Base type
├── Random                  # PRNG (static)
├── String                  # String operations (instance + static)
├── Terminal                # Terminal I/O (static)
├── Vec2                    # 2D vector (instance)
├── Vec3                    # 3D vector (instance)
│
├── Collections
│   ├── Bag                 # String set
│   ├── Bytes               # Byte array
│   ├── Heap                # Priority queue
│   ├── List                # Dynamic array
│   ├── Map                 # String-keyed hash map
│   ├── Queue               # FIFO
│   ├── Ring                # Circular buffer
│   ├── Seq                 # Growable array
│   ├── Set                 # Object set
│   ├── Stack               # LIFO
│   └── TreeMap             # Sorted map
│
├── Crypto
│   ├── Hash                # Hashing (static)
│   ├── KeyDerive           # Key derivation (static)
│   ├── Rand                # Secure random (static)
│   └── Tls                 # TLS connections
│
├── Diagnostics
│   ├── Assert              # Assertions (static)
│   └── Trap                # Trap/abort (static)
│
├── Game                    # ** NEW NAMESPACE **
│   ├── ButtonGroup         # Mutually exclusive selection
│   ├── Collision           # Static collision helpers
│   ├── CollisionRect       # AABB collision
│   ├── Grid2D              # 2D tile grids
│   ├── ObjectPool          # Object slot reuse
│   ├── ParticleEmitter     # Particle effects
│   ├── PathFollower        # Waypoint following
│   ├── Quadtree            # Spatial partitioning
│   ├── ScreenFX            # Screen effects
│   ├── SmoothValue         # Value interpolation
│   ├── SpriteAnimation     # Frame-based animation
│   ├── StateMachine        # State management
│   ├── Timer               # Frame-based timers
│   └── Tween               # Animation tweening
│
├── Graphics
│   ├── Camera              # 2D camera
│   ├── Canvas              # Drawing surface
│   ├── Color               # Color utilities (static)
│   ├── Pixels              # Pixel buffer
│   ├── Sprite              # 2D sprite
│   └── Tilemap             # Tile-based maps
│
├── GUI
│   ├── App                 # Application window
│   ├── Button, Label, etc. # Widgets (28 classes)
│   ├── MessageBox          # Modal dialogs (static)
│   └── FileDialog          # File dialogs (static)
│
├── Input
│   ├── Action              # Action mapping (static)
│   ├── Keyboard            # Keyboard input (static)
│   ├── Manager             # Unified input
│   ├── Mouse               # Mouse input (static)
│   └── Pad                 # Gamepad input (static)
│
├── IO
│   ├── Archive             # ZIP archives
│   ├── BinFile             # Binary file stream
│   ├── Compress            # Compression (static)
│   ├── Dir                 # Directory ops (static)
│   ├── File                # File ops (static)
│   ├── LineReader          # Line-by-line reader
│   ├── LineWriter          # Buffered writer
│   ├── MemStream           # In-memory stream
│   ├── Path                # Path manipulation (static)
│   └── Watcher             # File system events
│
├── Network
│   ├── Dns                 # DNS resolution (static)
│   ├── Http                # HTTP helpers (static)
│   ├── HttpReq             # HTTP request builder
│   ├── HttpRes             # HTTP response
│   ├── Tcp                 # TCP client
│   ├── TcpServer           # TCP server
│   ├── Udp                 # UDP socket
│   ├── Url                 # URL parsing
│   └── WebSocket           # WebSocket client
│
├── Sound
│   ├── Audio               # Global audio (static)
│   ├── Music               # Streaming music
│   ├── Sound               # Sound effects
│   └── Voice               # Voice control (static)
│
├── Text
│   ├── Codec               # Base64/Hex/URL encoding (static)
│   ├── Csv                 # CSV parsing (static)
│   ├── Guid                # UUID generation (static)
│   ├── Json                # JSON parsing (static)
│   ├── Pattern             # Regex (static)
│   ├── StringBuilder       # Mutable string builder
│   └── Template            # Template rendering (static)
│
├── Threads
│   ├── Barrier             # N-party barrier
│   ├── Gate                # Semaphore
│   ├── Monitor             # Object monitor (static)
│   ├── RwLock              # Reader-writer lock
│   ├── SafeI64             # Thread-safe integer
│   └── Thread              # Thread handle
│
└── Time                    # ** REORGANIZED **
    ├── Clock               # Sleep, ticks (static)
    ├── Countdown           # Millisecond countdown
    ├── DateTime            # Date/time operations (static) ** MOVED **
    └── Stopwatch           # Performance timing ** MOVED **
```

---

## 6. Migration Strategy

If implementing breaking changes:

1. **Version 0.3.0**: Add new locations with aliases
   - `Viper.Game.Timer` = `Viper.Timer` (deprecated)
   - `Viper.Time.DateTime` = `Viper.DateTime` (deprecated)

2. **Version 0.4.0**: Remove deprecated aliases

3. **Documentation**: Update all examples to use new namespaces

4. **Migration tool**: Provide a script to update user code

---

## 7. Alias Analysis

There are several runtime aliases that indicate API evolution or convenience shortcuts:

| Alias | Target | Notes |
|-------|--------|-------|
| `Viper.Console.PrintStr` | `Terminal.Say` | Undocumented `Console` namespace |
| `Viper.Console.PrintI64` | `Terminal.PrintI64` | Undocumented `Console` namespace |
| `Viper.Console.ReadLine` | `Terminal.ReadLine` | Undocumented `Console` namespace |
| `Viper.Time.SleepMs` | `Clock.Sleep` | Convenience alias |
| `Viper.Time.GetTickCount` | `Clock.Ticks` | Legacy name |
| `Viper.Strings.Len` | `String.Length` | Duplicate functionality |
| `Viper.Strings.Concat` | `String.Concat` | Duplicate functionality |

**Issues Found:**
1. `Viper.Console.*` exists as aliases but isn't documented - should be removed or documented
2. `Viper.Time.SleepMs` and `Viper.Time.GetTickCount` are aliases that bypass `Clock` - inconsistent
3. `Viper.Strings.Len/Concat` duplicate `Viper.String.*` methods

**Recommendation:**
- Remove undocumented `Viper.Console.*` aliases (or document them)
- Deprecate `Viper.Time.SleepMs` and `Viper.Time.GetTickCount` (use `Clock.Sleep`/`Clock.Ticks`)
- Clean up `Viper.Strings.*` duplication when merging into `Viper.String.*`

---

## 8. Questions for Consideration

1. **Is `Viper.Game.*` the right name?** Alternatives:
   - `Viper.Gaming.*`
   - `Viper.GameDev.*`
   - `Viper.Engine.*`

2. **Should Vec2/Vec3 stay in root?** They're mathematical but heavily used in games.

3. **Is the current `Viper.GUI.*` organization good?** 28 classes is a lot but all are related.

4. **Should `Viper.String` be renamed to `Viper.Str`?** Matches IL type `str`.
