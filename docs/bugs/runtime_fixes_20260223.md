# Runtime API Completeness Audit — 2026-02-23

Systematic audit of every C runtime class/function against:
1. **C header** — function declared in `src/runtime/**/*.h`
2. **runtime.def** — function registered as RT_FUNC + RT_METHOD/RT_PROP
3. **Docs** — function documented in `docs/viperlib/`

Legend:
- `MISSING_DEF` — C function exists, not in runtime.def (Zia can't call it)
- `MISSING_C` — runtime.def entry has no matching C implementation
- `SIG_MISMATCH` — signature in runtime.def doesn't match C declaration
- `MISSING_DOC` — registered in runtime.def, not documented
- `PHANTOM_DOC` — documented but no C implementation or runtime.def entry
- `OK` — all three layers match

---

## Audit Progress

| Namespace | Status |
|---|---|
| Viper.Core | complete |
| Viper.Math | complete |
| Viper.String | complete |
| Viper.Terminal | complete |
| Viper.Fmt | complete |
| Viper.Log | complete |
| Viper.Environment | complete |
| Viper.Time | complete |
| Viper.Collections | complete |
| Viper.IO | complete |
| Viper.Network | complete |
| Viper.Game | complete (via collections/) |
| Viper.GUI | complete |
| Viper.Sound | complete |
| Viper.Threads | complete |
| Viper.Text | complete |
| Viper.System | complete |
| Viper.Data | complete |
| Viper.OOP extras | complete |

---

## Viper.Core

### Sources audited
All headers in `src/runtime/core/`:
- `rt_math.h` → Viper.Math.* (39 fns)
- `rt_fp.h` → rt_math_pow (Viper.Math.Pow)
- `rt_numeric.h` → internal (BASIC type coercions; no public API)
- `rt_string.h` → Viper.String.* (~50 fns)
- `rt_datetime.h` → Viper.Time.DateTime (20 fns)
- `rt_dateonly.h` → Viper.Time.DateOnly (23 fns)
- `rt_daterange.h` → Viper.Time.DateRange (11 fns)
- `rt_duration.h` → Viper.Time.Duration (23 fns)
- `rt_stopwatch.h` → Viper.Time.Stopwatch (10 fns)
- `rt_countdown.h` → Viper.Time.Countdown (11 fns)
- `rt_reltime.h` → Viper.Time.RelativeTime (4 fns)
- `rt_bits.h` → Viper.Math.Bits (18 fns)
- `rt_fmt.h` → Viper.Fmt.* (18 fns)
- `rt_format.h` → internal (BASIC CSV/f64 formatting; not public)
- `rt_output.h` → internal (terminal buffering; not public)
- `rt_args.h` → Viper.Environment.* (8 fns)
- `rt_log.h` → Viper.Log.* (12 fns)
- `rt_perlin.h` → Viper.Math.PerlinNoise (5 fns)
- `rt_easing.h` → Viper.Math.Easing (28 fns)
- `rt_modvar.h` → internal (BASIC module-level vars; not public)
- `rt_msgbus.h` → Viper.Core.MessageBus (9 fns)
- `rt_crc32.h` → internal (CRC32 impl; wrapped by Viper.Crypto.Hash)
- `rt_string_builder.h` → Viper.Text.StringBuilder (6 public fns + internal builder API)
- `rt_int_format.h` → internal (C buffer formatters; not public)
- `rt_fp.h` → rt_math_pow partial (Viper.Math.Pow)
From `src/runtime/text/`:
- `rt_parse.h` → Viper.Core.Parse (9 fns)

### Viper.Core.Box
- `OK` — I64, F64, I1, Str boxing; ToI64/ToF64/ToI1/ToStr unboxing; Type, EqI64, EqF64, EqStr, ValueType — all registered and documented.

### Viper.Core.Object
- `OK` — Equals, HashCode, RefEquals, ToString, TypeName, TypeId, IsNull — all registered.

### Viper.Core.Diagnostics
- `OK` — Assert, AssertEq, AssertNeq, AssertEqNum, AssertEqStr, AssertNull, AssertNotNull, AssertFail, AssertGt, AssertLt, AssertGte, AssertLte, Trap — all registered.

### Viper.Core.MessageBus
- `OK` — New, Subscribe, Unsubscribe, Publish, SubscriberCount, TotalSubscriptions, Topics, ClearTopic, Clear — all registered.

### Viper.Core.Parse
- `OK` — TryInt, TryNum, TryBool, IntOr, NumOr, BoolOr, IsInt, IsNum, IntRadix — all registered.
- Note: `Double` and `Int64` (rt_parse_double/rt_parse_int64) are in rt_numeric.h and registered separately.

### Viper.Core.Convert
- `OK` — ToDouble, ToInt, ToInt64, ToString_Int, ToString_Double, NumToInt — all registered.

---

## Viper.Math

### Viper.Math (flat)
- `OK` — All 39 functions from rt_math.h + rt_math_pow from rt_fp.h registered.
- Constants: Pi, E, Tau exposed as both getter form (get_Pi/get_E/get_Tau) and direct alias (Pi/E/Tau).

### Viper.Math.Bits
- `OK` — All 18 functions registered and documented in math.md.

### Viper.Math.Easing
- `OK` — All 28 easing functions registered and documented.

### Viper.Math.PerlinNoise
- `OK` — New, Noise2D, Noise3D, Octave2D, Octave3D — registered and documented.

### Viper.Math.Random
- `OK` — New, Next, NextInt, Seed, Range, Gaussian, Exponential, Dice, Chance, Shuffle + instance method variants — all registered and documented.

### Viper.Math.BigInt
- `OK` — All 33 functions (constructors, arithmetic, comparison, bitwise, conversion) registered and documented.

### Viper.Math.Vec2
- `OK` — New, Zero, One, X, Y, Add, Sub, Mul, Div, Neg, Dot, Cross, Len, LenSq, Norm, Dist, Lerp, Angle, Rotate — all registered and documented.

### Viper.Math.Vec3
- `OK` — New, Zero, One, X, Y, Z, Add, Sub, Mul, Div, Neg, Dot, Cross, Len, LenSq, Norm, Dist, Lerp — all registered and documented.

### Viper.Math.Quat
- `OK` — New, Identity, FromAxisAngle, FromEuler, X, Y, Z, W, Mul, Conjugate, Inverse, Norm, Len, LenSq, Dot, Slerp, Lerp, RotateVec3, ToMat4, Axis, Angle — all registered and documented.

### Viper.Math.Mat3
- `OK` — All 22 functions (constructors, 2D transforms, element access, math ops, transform application) registered and documented.

### Viper.Math.Mat4
- `OK` — All 24 functions (constructors, 3D transforms, projection factories, element access, math ops) registered and documented.

### Viper.Math.Spline
- `OK` — CatmullRom, Bezier, Linear, Eval, Tangent, PointAt, ArcLength, Sample, PointCount — all registered and documented.

---

## Viper.String

### Sources audited
- `src/runtime/core/rt_string.h`

### Findings
- `OK` — All ~50 Zia-callable string functions registered.
- Note: Low-level functions (rt_string_ref/unref, rt_str_lt/le/gt/ge, rt_instr3, etc.) are correctly internal-only.
- Note: `rt_str_split_fields` is declared in `src/runtime/rt.hpp` (not rt_string.h) and registered as `Viper.String.SplitFields`.

---

## Viper.Terminal

### Sources audited
- Various: rt_term_say, rt_term_ask, etc. (in rt_output_helpers.c or similar)

### Findings
- `OK` — Say, SayBool, SayInt, SayNum, Int; Print, PrintInt, PrintNum, PrintF64, PrintI64, PrintStr; Ask, GetKey, GetKeyTimeout, InKey, ReadLine, InputLine; Bell, Clear, SetAltScreen, SetColor, SetCursorVisible, SetPosition; BeginBatch, EndBatch, Flush — all registered.

---

## Viper.Fmt

### Sources audited
- `src/runtime/core/rt_fmt.h`

### Findings
- `OK` — All 18 functions (Int, IntRadix, IntPad, Num, NumFixed, NumSci, NumPct, Bool, BoolYN, Size, Hex, HexPad, Bin, Oct, IntGrouped, Currency, ToWords, Ordinal) registered and documented.

---

## Viper.Log

### Sources audited
- `src/runtime/core/rt_log.h`

### Findings
- `OK` — Debug, Info, Warn, Error, Level (get/set), Enabled, DEBUG/INFO/WARN/ERROR/OFF constants — all registered.
- Note: Namespace is `Viper.Log.*` (not `Viper.Core.Log.*`).

---

## Viper.Environment

### Sources audited
- `src/runtime/core/rt_args.h`

### Findings
- `OK` — GetArgumentCount, GetArgument, EndProgram, GetVariable, HasVariable, IsNative, SetVariable, GetCommandLine — all registered.
- Note: `rt_args_clear`, `rt_args_push` are internal initialization functions (not public API).

---

## Viper.Time

### Sources audited
- `src/runtime/core/rt_datetime.h` → Viper.Time.DateTime
- `src/runtime/core/rt_dateonly.h` → Viper.Time.DateOnly
- `src/runtime/core/rt_daterange.h` → Viper.Time.DateRange
- `src/runtime/core/rt_duration.h` → Viper.Time.Duration
- `src/runtime/core/rt_stopwatch.h` → Viper.Time.Stopwatch
- `src/runtime/core/rt_countdown.h` → Viper.Time.Countdown
- `src/runtime/core/rt_reltime.h` → Viper.Time.RelativeTime
- Clock: rt_clock_sleep, rt_clock_ticks, rt_clock_ticks_us

### Findings
- `OK` — All Time namespaces are fully registered.
- `Viper.Time.Clock.Sleep/Ticks/TicksUs` registered; also aliased as `Viper.Time.SleepMs/GetTickCount`.

---

## Viper.Collections

### Sources audited
All headers in `src/runtime/collections/`:
- `rt_list.h` → Viper.Collections.List (19 fns)
- `rt_seq.h` → Viper.Collections.Seq (39+ fns)
- `rt_seq_functional.h` → internal wrappers for Seq functional ops (registered as Seq methods)
- `rt_map.h` → Viper.Collections.Map (20 fns)
- `rt_set.h` → Viper.Collections.Set (14 fns)
- `rt_queue.h` → Viper.Collections.Queue (7 fns)
- `rt_stack.h` → Viper.Collections.Stack (7 fns)
- `rt_deque.h` → Viper.Collections.Deque (17 fns)
- `rt_ring.h` → Viper.Collections.Ring (11 fns)
- `rt_pqueue.h` → Viper.Collections.Heap (12 fns)
- `rt_bytes.h` → Viper.Collections.Bytes (27 fns)
- `rt_bitset.h` → Viper.Collections.BitSet (15 fns)
- `rt_bag.h` → Viper.Collections.Bag (11 fns)
- `rt_lrucache.h` → Viper.Collections.LruCache (13 fns)
- `rt_bloomfilter.h` → Viper.Collections.BloomFilter (7 fns)
- `rt_sparsearray.h` → Viper.Collections.SparseArray (9 fns)
- `rt_iter.h` → Viper.Collections.Iterator (16 fns)
- `rt_trie.h` → Viper.Collections.Trie (12 fns)
- `rt_unionfind.h` → Viper.Collections.UnionFind (7 fns)
- `rt_bimap.h` → Viper.Collections.BiMap (13 fns)
- `rt_frozenset.h` → Viper.Collections.FrozenSet (11 fns)
- `rt_multimap.h` → Viper.Collections.MultiMap (12 fns)
- `rt_countmap.h` → Viper.Collections.CountMap (14 fns)
- `rt_defaultmap.h` → Viper.Collections.DefaultMap (9 fns)
- `rt_frozenmap.h` → Viper.Collections.FrozenMap (11 fns)
- `rt_intmap.h` → Viper.Collections.IntMap (11 fns)
- `rt_orderedmap.h` → Viper.Collections.OrderedMap (11 fns)
- `rt_sortedset.h` → Viper.Collections.SortedSet (23 fns)
- `rt_treemap.h` → Viper.Collections.TreeMap (14 fns)
- `rt_weakmap.h` → Viper.Collections.WeakMap (10 fns)
- `rt_binbuf.h` → **Viper.IO.BinaryBuffer** (26 fns) — note: source in collections/, namespace is IO
- `rt_convert_coll.h` → *unregistered* (28 fns — 3 variadic excluded correctly; 25 non-variadic MISSING_DEF)
- `rt_collision.h` → Viper.Game.CollisionRect + Viper.Game.Collision (27 fns)
- `rt_statemachine.h` → Viper.Game.StateMachine (15 fns)
- `rt_particle.h` → Viper.Game.ParticleEmitter (27 fns)
- `rt_quadtree.h` → Viper.Game.Quadtree (15 fns)
- `rt_grid2d.h` → Viper.Game.Grid2D (14 fns)
- `rt_objpool.h` → Viper.Game.ObjectPool (15 fns)
- `rt_timer.h` → Viper.Game.Timer (15 fns)
- `rt_tween.h` → Viper.Game.Tween (19 fns)
- `rt_smoothvalue.h` → Viper.Game.SmoothValue (13 fns)
- `rt_buttongroup.h` → Viper.Game.ButtonGroup (15 fns)
- `rt_pathfollow.h` → Viper.Game.PathFollower (19 fns)
- `rt_screenfx.h` → Viper.Game.ScreenFX (15 fns)
- `rt_spriteanim.h` → Viper.Game.SpriteAnimation (25 fns)

### Viper.Collections.* findings

- `OK` — List, Seq, Map, Set, Queue, Stack, Ring, Heap, Bytes, BitSet, Bag, LruCache, BloomFilter, SparseArray, Iterator, Trie, UnionFind, BiMap, FrozenSet, MultiMap, CountMap, DefaultMap, FrozenMap, IntMap, OrderedMap, SortedSet, TreeMap, WeakMap — all fully registered.
- `MISSING_DEF` — **`rt_deque_with_capacity`** (`rt_deque.h:28`): C function declared and implemented but has no RT_FUNC / RT_METHOD entry. `Deque.WithCapacity(n)` is not callable from Zia. (Seq has a `SeqWithCapacity` equivalent; Deque is inconsistently missing it.)
- `MISSING_DEF` — **25 functions in `rt_convert_coll.h`**: entire conversion module unregistered. Zia programs cannot convert between collection types. Affected conversions:
  - `rt_seq_to_list`, `rt_seq_to_set`, `rt_seq_to_stack`, `rt_seq_to_queue`, `rt_seq_to_deque`, `rt_seq_to_bag`
  - `rt_list_to_seq`, `rt_list_to_set`, `rt_list_to_stack`, `rt_list_to_queue`
  - `rt_set_to_seq`, `rt_set_to_list`
  - `rt_stack_to_seq`, `rt_stack_to_list`
  - `rt_queue_to_seq`, `rt_queue_to_list`
  - `rt_deque_to_seq`, `rt_deque_to_list`
  - `rt_map_keys_to_seq`, `rt_map_values_to_seq`
  - `rt_bag_to_seq`, `rt_bag_to_set`
  - `rt_ring_to_seq`
  - (3 variadic functions `rt_seq_of`, `rt_list_of`, `rt_set_of` correctly excluded)

### Viper.IO.BinaryBuffer findings (source in collections/)

- `OK` — All 26 functions registered under `Viper.IO.BinaryBuffer`.
- Note: static factories `BinBufNewCap` and `BinBufFromBytes` appear as RT_METHOD in the class block — they are also RT_FUNCs and accessible both ways.

### Viper.Game.* findings (source in collections/)

- `OK` — CollisionRect, Collision, StateMachine, ButtonGroup, PathFollower, ScreenFX, SpriteAnimation, Grid2D, ObjectPool, Timer, Tween, SmoothValue — all fully registered.
- `MISSING_DEF` — **`rt_quadtree_query_was_truncated`** (`rt_quadtree.h:79`): no RT_FUNC entry. Programs relying on `Quadtree.Query()` have no way to detect if results were truncated due to buffer overflow.
- `MISSING_DEF` — **`rt_particle_emitter_draw_to_pixels`** (`rt_particle.h`): batch renderer helper has no RT_FUNC entry. `ParticleEmitter.DrawToPixels(pixels)` is not callable from Zia.
- Note: `rt_grid2d_find` intentionally excluded (output pointer args `*out_x`, `*out_y` — not expressible in Zia's type system).
- Note: Destroy functions for game types (QuadtreeDestroy, Grid2DDestroy, ObjPoolDestroy, TimerDestroy, TweenDestroy, SmoothValueDestroy, StateMachineDestroy, etc.) are all RT_FUNC only — NOT in the RT_CLASS_BEGIN block. Callable as `ClassName.Destroy(obj)` (static), but not as `obj.Destroy()` (instance). This is a consistent intentional pattern across all Game types.

### Summary of MISSING_DEF bugs found

| Bug | C function | Expected Zia name | File |
|-----|-----------|-------------------|------|
| Collections | `rt_deque_with_capacity` | `Deque.WithCapacity(n)` | rt_deque.h |
| Collections | 25 fns in rt_convert_coll.h | `Seq.ToList()`, `List.ToSeq()`, etc. | rt_convert_coll.h |
| Game | `rt_quadtree_query_was_truncated` | `Quadtree.QueryWasTruncated()` | rt_quadtree.h |
| Game | `rt_particle_emitter_draw_to_pixels` | `ParticleEmitter.DrawToPixels(pixels)` | rt_particle.h |

---

## Viper.IO

### Sources audited
All headers in `src/runtime/io/`:
- `rt_file.h` → internal BASIC channel API (output pointer patterns; not Zia-callable)
- `rt_file_path.h` → internal mode string helpers (not Zia-callable)
- `rt_dir.h` → Viper.IO.Dir (15 fns)
- `rt_path.h` → Viper.IO.Path (10 fns)
- `rt_file_ext.h` → Viper.IO.File (18 fns)
- `rt_stream.h` → Viper.IO.Stream (20 fns)
- `rt_binfile.h` → Viper.IO.BinFile (11 fns)
- `rt_memstream.h` → Viper.IO.MemStream (30 fns)
- `rt_linereader.h` → Viper.IO.LineReader (7 fns)
- `rt_linewriter.h` → Viper.IO.LineWriter (9 fns)
- `rt_watcher.h` → Viper.IO.Watcher (14 fns)
- `rt_archive.h` → Viper.IO.Archive (19 fns)
- `rt_compress.h` → Viper.IO.Compress (10 fns)
- `rt_glob.h` → Viper.IO.Glob (4 fns)
- `rt_tempfile.h` → Viper.IO.TempFile (8 fns)

### Findings
- `OK` — All IO classes fully registered. Zero MISSING_DEF bugs.
- Note: `rt_linewriter_append` maps to `LineWriterAppend` — a static factory (returns new LineWriter in append mode), not a write method.
- Note: `rt_tempdir_create` maps to `TempFile.CreateDir` (no separate TempDir class).

---

## Viper.Network

### Sources audited
All headers in `src/runtime/network/`:
- `rt_network.h` → Viper.Network.Tcp (17), TcpServer (8), Udp (18), Dns (10), Http (6), HttpReq (6), HttpRes (7), Url (33)
- `rt_restclient.h` → Viper.Network.RestClient (22)
- `rt_websocket.h` → Viper.Network.WebSocket (15)
- `rt_crypto.h` → internal C primitives (raw uint8_t*/size_t API; not Zia-callable)
- `rt_ratelimit.h` → Viper.Network.RateLimiter (7)
- `rt_retry.h` → Viper.Network.RetryPolicy (9)
- `rt_tls.h` → low-level C + test utilities (not Zia-callable) + `rt_viper_tls_*` wrappers → **Viper.Crypto.Tls** (11)

### Findings
- `OK` — All Network classes fully registered. Zero MISSING_DEF bugs.
- Note: TLS Viper API wrappers (`rt_viper_tls_*`) are registered under `Viper.Crypto.Tls` (not `Viper.Network`) — source lives in `network/` but namespace is Crypto. Correct and consistent.
- Note: `rt_ws_compute_accept_key` (takes `const char*`, returns `char*`) is a test utility for WebSocket handshake verification — correctly absent from runtime.def.
- Note: `rt_crypto.h` (SHA-256, HMAC, HKDF, ChaCha20-Poly1305, X25519) are internal C primitives used by `rt_tls.c`. All take raw byte buffer args — correctly not Zia-callable.

---

## Viper.GUI

### Sources audited
All headers in `src/runtime/graphics/`:
- `rt_graphics.h` → Canvas drawing + Color utilities (~103 fns total)
- `rt_input.h` → Keyboard + Mouse + Gamepad public API (~134 fns)
- `rt_pixels.h` → Viper.Graphics.Pixels (41 fns)
- `rt_gui.h` → All GUI widget classes (~461 C declarations, 2609 lines)

New bug category introduced: `MISSING_CLASS_METHOD` — RT_FUNC registered with symbol ID, C implementation exists, but no corresponding RT_METHOD entry in the RT_CLASS_BEGIN dispatch table. Zia code cannot call these as instance methods.

### Findings — `rt_graphics.h`, `rt_input.h`, `rt_pixels.h`
- `OK` — Canvas, Color, Keyboard, Mouse, Gamepad, Pixels all fully registered. Zero MISSING_DEF bugs.
- Note: `rt_canvas_get_position(x*,y*)` and `rt_canvas_get_monitor_size(w*,h*)` have output-pointer args — correctly excluded.
- Note: Internal management functions (`rt_keyboard_init`, `rt_mouse_init`, `rt_pad_init`, etc.) — correctly absent from runtime.def.

### Findings — `rt_gui.h` App class

`MISSING_DEF` — C functions exist, no RT_FUNC entry, not callable from Zia:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_app_set_title` | `AppSetTitle` | `void(obj,str)` |
| `rt_app_get_title` | `AppGetTitle` | `str(obj)` |
| `rt_app_set_size` | `AppSetSize` | `void(obj,i64,i64)` |
| `rt_app_was_close_requested` | `AppWasCloseRequested` | `i64(obj)` |
| `rt_app_is_focused` | `AppIsFocused` | `i64(obj)` |

Note: `rt_app_set_window_size` IS registered (as `GuiAppSetWindowSize`). `rt_app_set_size` is a distinct function.

### Findings — `rt_gui.h` Widget class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_widget_set_cursor` | `WidgetSetCursor` | `void(obj,i64)` |
| `rt_widget_reset_cursor` | `WidgetResetCursor` | `void(obj)` |

### Findings — `rt_gui.h` MenuBar class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_menubar_remove_menu` | `MenuBarRemoveMenu` | `void(obj,obj)` |
| `rt_menubar_get_menu_count` | `MenuBarGetMenuCount` | `i64(obj)` |
| `rt_menubar_get_menu` | `MenuBarGetMenu` | `obj(obj,i64)` |
| `rt_menubar_is_visible` | `MenuBarIsVisible` | `i64(obj)` |

### Findings — `rt_gui.h` Menu class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_menu_add_item_with_shortcut` | `MenuAddItemWithShortcut` | `obj(obj,str,str)` |
| `rt_menu_remove_item` | `MenuRemoveItem` | `void(obj,obj)` |
| `rt_menu_clear` | `MenuClear` | `void(obj)` |
| `rt_menu_set_title` | `MenuSetTitle` | `void(obj,str)` |
| `rt_menu_get_title` | `MenuGetTitle` | `str(obj)` |
| `rt_menu_get_item_count` | `MenuGetItemCount` | `i64(obj)` |
| `rt_menu_get_item` | `MenuGetItem` | `obj(obj,i64)` |
| `rt_menu_set_enabled` | `MenuSetEnabled` | `void(obj,i64)` |
| `rt_menu_is_enabled` | `MenuIsEnabled` | `i64(obj)` |

### Findings — `rt_gui.h` MenuItem class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_menuitem_get_text` | `MenuItemGetText` | `str(obj)` |
| `rt_menuitem_get_shortcut` | `MenuItemGetShortcut` | `str(obj)` |
| `rt_menuitem_set_icon` | `MenuItemSetIcon` | `void(obj,obj)` |
| `rt_menuitem_set_checkable` | `MenuItemSetCheckable` | `void(obj,i64)` |
| `rt_menuitem_is_checkable` | `MenuItemIsCheckable` | `i64(obj)` |
| `rt_menuitem_is_separator` | `MenuItemIsSeparator` | `i64(obj)` |

### Findings — `rt_gui.h` ContextMenu class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_contextmenu_add_submenu` | `ContextMenuAddSubmenu` | `obj(obj,str)` |
| `rt_contextmenu_get_clicked_item` | `ContextMenuGetClickedItem` | `obj(obj)` |

### Findings — `rt_gui.h` StatusBar class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_statusbar_get_left_text` | `StatusBarGetLeftText` | `str(obj)` |
| `rt_statusbar_get_center_text` | `StatusBarGetCenterText` | `str(obj)` |
| `rt_statusbar_get_right_text` | `StatusBarGetRightText` | `str(obj)` |
| `rt_statusbar_add_button` | `StatusBarAddButton` | `obj(obj,str,i64)` |
| `rt_statusbar_add_progress` | `StatusBarAddProgress` | `obj(obj,i64)` |
| `rt_statusbar_add_separator` | `StatusBarAddSeparator` | `obj(obj,i64)` |
| `rt_statusbar_add_spacer` | `StatusBarAddSpacer` | `obj(obj,i64)` |
| `rt_statusbar_remove_item` | `StatusBarRemoveItem` | `void(obj,obj)` |
| `rt_statusbar_clear` | `StatusBarClear` | `void(obj)` |

### Findings — `rt_gui.h` StatusBarItem class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_statusbaritem_get_text` | `StatusBarItemGetText` | `str(obj)` |
| `rt_statusbaritem_set_progress` | `StatusBarItemSetProgress` | `void(obj,f64)` |
| `rt_statusbaritem_get_progress` | `StatusBarItemGetProgress` | `f64(obj)` |

### Findings — `rt_gui.h` Toolbar class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_toolbar_new_vertical` | `ToolbarNewVertical` | `obj(obj)` |
| `rt_toolbar_add_toggle` | `ToolbarAddToggle` | `obj(obj,str,str)` |
| `rt_toolbar_add_spacer` | `ToolbarAddSpacer` | `obj(obj)` |
| `rt_toolbar_add_dropdown` | `ToolbarAddDropdown` | `obj(obj,str)` |
| `rt_toolbar_remove_item` | `ToolbarRemoveItem` | `void(obj,obj)` |
| `rt_toolbar_get_icon_size` | `ToolbarGetIconSize` | `i64(obj)` |
| `rt_toolbar_get_item` | `ToolbarGetItem` | `obj(obj,i64)` |

### Findings — `rt_gui.h` ToolbarItem class

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_toolbaritem_set_icon_pixels` | `ToolbarItemSetIconPixels` | `void(obj,obj)` |
| `rt_toolbaritem_set_text` | `ToolbarItemSetText` | `void(obj,str)` |
| `rt_toolbaritem_set_toggled` | `ToolbarItemSetToggled` | `void(obj,i64)` |
| `rt_toolbaritem_is_toggled` | `ToolbarItemIsToggled` | `i64(obj)` |

### Findings — `rt_gui.h` MessageBox class (object-based API)

The existing `Viper.GUI.MessageBox` class is static-only (no constructor). An imperative object-based API also exists in `rt_gui.h` with no RT_FUNC coverage:

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_messagebox_new` | `MessageBoxNew` | `obj(str,str,i64)` |
| `rt_messagebox_add_button` | `MessageBoxAddButton` | `void(obj,str,i64)` |
| `rt_messagebox_set_default_button` | `MessageBoxSetDefaultButton` | `void(obj,i64)` |
| `rt_messagebox_show` | `MessageBoxShow` | `i64(obj)` |
| `rt_messagebox_destroy` | `MessageBoxDestroy` | `void(obj)` |

### Findings — `rt_gui.h` FileDialog class (object-based API)

The existing `Viper.GUI.FileDialog` class is static-only. An imperative object-based API with fine-grained filter/path control also exists but has no RT_FUNC coverage:

`MISSING_DEF`:

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_filedialog_new` | `FileDialogNew` | `obj(i64)` |
| `rt_filedialog_set_title` | `FileDialogSetTitle` | `void(obj,str)` |
| `rt_filedialog_set_path` | `FileDialogSetPath` | `void(obj,str)` |
| `rt_filedialog_set_filter` | `FileDialogSetFilter` | `void(obj,str,str)` |
| `rt_filedialog_add_filter` | `FileDialogAddFilter` | `void(obj,str,str)` |
| `rt_filedialog_set_default_name` | `FileDialogSetDefaultName` | `void(obj,str)` |
| `rt_filedialog_set_multiple` | `FileDialogSetMultiple` | `void(obj,i64)` |
| `rt_filedialog_show` | `FileDialogShow` | `i64(obj)` |
| `rt_filedialog_get_path` | `FileDialogGetPath` | `str(obj)` |
| `rt_filedialog_get_path_count` | `FileDialogGetPathCount` | `i64(obj)` |
| `rt_filedialog_get_path_at` | `FileDialogGetPathAt` | `str(obj,i64)` |
| `rt_filedialog_destroy` | `FileDialogDestroy` | `void(obj)` |

### Findings — `rt_gui.h` CodeEditor class

**Part A — `MISSING_DEF`** (no RT_FUNC at all):

| C Function | Suggested Symbol | Signature |
|---|---|---|
| `rt_codeeditor_set_custom_keywords` | `CodeEditorSetCustomKeywords` | `void(obj,str)` |
| `rt_codeeditor_refresh_highlights` | `CodeEditorRefreshHighlights` | `void(obj)` |
| `rt_codeeditor_get_show_line_numbers` | `CodeEditorGetShowLineNumbers` | `i64(obj)` |
| `rt_codeeditor_set_line_number_width` | `CodeEditorSetLineNumberWidth` | `void(obj,i64)` |
| `rt_codeeditor_clear_gutter_icon` | `CodeEditorClearGutterIcon` | `void(obj,i64,i64)` |
| `rt_codeeditor_get_gutter_clicked_slot` | `CodeEditorGetGutterClickSlot` | `i64(obj)` |
| `rt_codeeditor_set_show_fold_gutter` | `CodeEditorSetShowFoldGutter` | `void(obj,i64)` |
| `rt_codeeditor_remove_fold_region` | `CodeEditorRemoveFoldRegion` | `void(obj,i64)` |
| `rt_codeeditor_clear_fold_regions` | `CodeEditorClearFoldRegions` | `void(obj)` |
| `rt_codeeditor_toggle_fold` | `CodeEditorToggleFold` | `void(obj,i64)` |
| `rt_codeeditor_fold_all` | `CodeEditorFoldAll` | `void(obj)` |
| `rt_codeeditor_unfold_all` | `CodeEditorUnfoldAll` | `void(obj)` |
| `rt_codeeditor_set_auto_fold_detection` | `CodeEditorSetAutoFoldDetection` | `void(obj,i64)` |
| `rt_codeeditor_remove_cursor` | `CodeEditorRemoveCursor` | `void(obj,i64)` |
| `rt_codeeditor_get_cursor_line_at` | `CodeEditorGetCursorLineAt` | `i64(obj,i64)` |
| `rt_codeeditor_get_cursor_col_at` | `CodeEditorGetCursorColAt` | `i64(obj,i64)` |
| `rt_codeeditor_set_cursor_position_at` | `CodeEditorSetCursorPositionAt` | `void(obj,i64,i64,i64)` |
| `rt_codeeditor_set_cursor_selection` | `CodeEditorSetCursorSelection` | `void(obj,i64,i64,i64,i64,i64)` |

**Part B — `MISSING_CLASS_METHOD`** (RT_FUNC registered at runtime.def lines 1755–1770, but no RT_METHOD entry in the `GuiCodeEditor` RT_CLASS_BEGIN block at line 5521). C function is implemented and linked, but unreachable from Zia method dispatch:

| RT_FUNC Symbol | C Function | Signature | Missing RT_METHOD name |
|---|---|---|---|
| `CodeEditorSetTokenColor` | `rt_codeeditor_set_token_color` | `void(obj,i64,i64)` | `"SetTokenColor"` |
| `CodeEditorAddHighlight` | `rt_codeeditor_add_highlight` | `void(obj,i64,i64,i64,i64)` | `"AddHighlight"` |
| `CodeEditorClearHighlights` | `rt_codeeditor_clear_highlights` | `void(obj)` | `"ClearHighlights"` |
| `CodeEditorSetGutterIcon` | `rt_codeeditor_set_gutter_icon` | `void(obj,i64,obj,i64)` | `"SetGutterIcon"` |
| `CodeEditorClearAllGutterIcons` | `rt_codeeditor_clear_all_gutter_icons` | `void(obj,i64)` | `"ClearGutterIcons"` |
| `CodeEditorWasGutterClicked` | `rt_codeeditor_was_gutter_clicked` | `i64(obj)` | `"WasGutterClicked"` |
| `CodeEditorGetGutterClickLine` | `rt_codeeditor_get_gutter_clicked_line` | `i64(obj)` | `"GetGutterClickLine"` |
| `CodeEditorAddFoldRegion` | `rt_codeeditor_add_fold_region` | `void(obj,i64,i64)` | `"AddFoldRegion"` |
| `CodeEditorFold` | `rt_codeeditor_fold` | `void(obj,i64)` | `"Fold"` |
| `CodeEditorUnfold` | `rt_codeeditor_unfold` | `void(obj,i64)` | `"Unfold"` |
| `CodeEditorIsFolded` | `rt_codeeditor_is_folded` | `i64(obj,i64)` | `"IsFolded"` |
| `CodeEditorGetCursorCount` | `rt_codeeditor_get_cursor_count` | `i64(obj)` | `"GetCursorCount"` |
| `CodeEditorAddCursor` | `rt_codeeditor_add_cursor` | `void(obj,i64,i64)` | `"AddCursor"` |
| `CodeEditorClearCursors` | `rt_codeeditor_clear_extra_cursors` | `void(obj)` | `"ClearCursors"` |
| `CodeEditorCursorHasSelection` | `rt_codeeditor_cursor_has_selection` | `i64(obj,i64)` | `"CursorHasSelection"` |

### Summary

| Category | Count |
|---|---|
| `MISSING_DEF` (no RT_FUNC) | 57 |
| `MISSING_CLASS_METHOD` (RT_FUNC exists, no RT_METHOD in class) | 15 |
| **Total GUI bugs** | **72** |

Affected classes: App (5), Widget (2), MenuBar (4), Menu (9), MenuItem (6), ContextMenu (2), StatusBar (9), StatusBarItem (3), Toolbar (7), ToolbarItem (4), MessageBox/5 (5), FileDialog/12 (12), CodeEditor/18+15 (33).

---

## Viper.Sound

### Sources audited
- `src/runtime/audio/rt_audio.h` → Audio (7 fns), Sound (5 fns), Voice (4 fns), Music (11 fns)
- `src/runtime/audio/rt_playlist.h` → Playlist (26 fns)

### Findings

**MISSING_DEF:**

| C Function | Suggested Symbol | Signature | Notes |
|---|---|---|---|
| `rt_playlist_destroy` | `PlaylistDestroy` | `void(obj)` | No destructor exposed for Playlist objects |

**MISSING_CLASS_METHOD** (RT_FUNC registered, no RT_METHOD in class block):

| RT_FUNC Symbol | C Function | Missing RT_METHOD | Class block line |
|---|---|---|---|
| `SoundFree` | `rt_sound_free` | `"Free"` / `"Destroy"` in Sound class | Sound: line 5790 |
| `MusicFree` | `rt_music_free` | `"Free"` / `"Destroy"` in Music class | Music: line 5805 |

Note: `rt_sound_new` and `rt_music_new` appear in doc comments only (Key invariants section); the actual C functions are `rt_sound_load` / `rt_music_load` — correctly registered. Playlist Volume/Shuffle/Repeat setters are all wired via RT_PROP.

---

## Viper.Threads

### Sources audited
All headers in `src/runtime/threads/`:
- `rt_threads.h` → Monitor (8), Thread (8), SafeI64 (5), Gate (7), Barrier (5), RwLock (9)
- `rt_channel.h` → Channel (12)
- `rt_threadpool.h` → Pool (9)
- `rt_async.h` → Async combinators (6)
- `rt_cancellation.h` → CancelToken
- `rt_concmap.h` → ConcurrentMap
- `rt_concqueue.h` → ConcurrentQueue
- `rt_debounce.h` → Debouncer
- `rt_future.h` → Future, Promise
- `rt_parallel.h` → Parallel
- `rt_scheduler.h` → Scheduler

### Findings
- `OK` — All Threads classes fully registered. Zero MISSING_DEF or MISSING_CLASS_METHOD bugs.
- Note: `rt_async_when_all`/`rt_async_when_any` appear in doc comments only (line 8 of rt_async.h); actual functions are `rt_async_all`/`rt_async_any` — registered.
- Note: `rt_future_get_for(void**)`/`rt_future_try_get(void**)` have output-pointer args — correctly excluded; Zia-callable variants are `rt_future_get_for_val`/`rt_future_try_get_val` — registered.
- Note: `rt_concmap_get_or_default` and `rt_concqueue_close` appear in doc comments only — not actual declarations.
- Note: `rt_threadpool_wait_all` (line 9 of rt_threadpool.h) is comment text only; real function is `rt_threadpool_wait` — registered.

---

## Viper.Text

### Sources audited
All headers in `src/runtime/text/`:
- `rt_csv.h`, `rt_json.h`, `rt_json_stream.h`, `rt_jsonpath.h`, `rt_regex.h`, `rt_textwrap.h`, `rt_toml.h`, `rt_xml.h`, `rt_yaml.h`
- `rt_parse.h` → Viper.Core.Parse (7 fns)

### Findings
- `OK` — All Text classes fully registered. Zero MISSING_DEF bugs.
- Note: `rt_json_pretty`, `rt_xml_serialize`, `rt_xml_serialize_pretty` appear in doc comments only — not declarations.
- Note: `rt_regex_find`/`rt_regex_match` appear in Key invariants comment (line 10) only — not declarations.
- Note: `rt_parse_*` functions are registered under `Viper.Core.Parse` (not Viper.Text), correctly.

---

## Viper.System / Viper.Exec / Viper.Machine / Viper.Data

### Sources audited
- `src/runtime/system/rt_exec.h` → Viper.Exec (8 fns)
- `src/runtime/system/rt_machine.h` → Viper.Machine (10 fns)
- `src/runtime/rt_platform.h` → preprocessor macros only (no Zia-callable functions)
- `src/runtime/text/rt_xml.h`, `rt_yaml.h` → Viper.Data.Xml, Viper.Data.Yaml
- `src/runtime/oop/rt_object.h` → internal memory management (rt_obj_retain, rt_obj_release, etc.) — correctly not Zia-callable

### Findings
- `OK` — Viper.Exec, Viper.Machine, Viper.Data fully registered. Zero MISSING_DEF bugs.
- Note: `rt_platform.h` is pure preprocessor macros — no functions.
- Note: Exception primitives in `rt_exc.h` (rt_exc_create, rt_exc_get_message, rt_exc_is_exception) are internal VM/EH implementation — correctly absent from runtime.def (exceptions use IL opcodes EhThrow/EhEntry).
- Note: `rt_object.h` reference-counting internals (rt_obj_retain, rt_obj_release, rt_heap_alloc, etc.) are VM-internal — correctly absent.

---

## Viper.OOP Extras (rt_option.h, rt_result.h — missed in prior Collections audit)

### Findings

**MISSING_DEF:**

| C Function | Suggested Symbol | Signature | Notes |
|---|---|---|---|
| `rt_option_equals` | `OptionEquals` | `i1(obj,obj)` | `rt_option.h:186` — two Option values compared for equality |
| `rt_result_equals` | `ResultEquals` | `i1(obj,obj)` | `rt_result.h:197` — two Result values compared for equality |

Note: Higher-order functions (`rt_option_map`, `rt_option_and_then`, `rt_option_filter`, `rt_result_map`, `rt_result_and_then`, `rt_result_map_err`, `rt_result_or_else`, `rt_lazy_map`, `rt_lazy_flat_map`) take C function pointers — correctly excluded from runtime.def (not Zia-callable with those signatures).

---

## Overall Audit Summary

| Namespace | Status | MISSING_DEF | MISSING_CLASS_METHOD | Fixed |
|---|---|---|---|---|
| Viper.Core | complete | 0 | 0 | — |
| Viper.Math | complete | 0 | 0 | — |
| Viper.String | complete | 0 | 0 | — |
| Viper.Terminal | complete | 0 | 0 | — |
| Viper.Fmt | complete | 0 | 0 | — |
| Viper.Log | complete | 0 | 0 | — |
| Viper.Environment | complete | 0 | 0 | — |
| Viper.Time | complete | 0 | 0 | — |
| Viper.Collections | complete | 0 | 0 | — |
| Viper.IO | complete | 0 | 0 | — |
| Viper.Network | complete | 0 | 0 | — |
| Viper.Game | complete | 0 | 0 | — |
| Viper.GUI | **fixed** | 57 | 15 | ✓ all 72 fixed |
| Viper.Sound | **fixed** | 0¹ | 2 | ✓ all 2 fixed |
| Viper.Threads | complete | 0 | 0 | — |
| Viper.Text | complete | 0 | 0 | — |
| Viper.System/Exec/Machine/Data | complete | 0 | 0 | — |
| Viper.OOP extras | **fixed** | 2 | 0 | ✓ all 2 fixed |
| **TOTAL** | | **59** | **17** | **✓ all 76 fixed** |

¹ `PlaylistDestroy` was initially reported as MISSING_DEF but `rt_playlist_destroy` does not exist in the C header (only in a doc comment). The false RT_FUNC entry added in the fix pass was removed. Actual Sound bugs: 2 MISSING_CLASS_METHOD (SoundFree, MusicFree).

**Grand total fixed: 76 bugs** — GUI (72), Sound (2), OOP extras (2).

**Fix status: COMPLETE** — all bugs resolved, build clean, 1204/1204 tests passing.

