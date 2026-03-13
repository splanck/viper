# Viper Runtime Consistency Audit & Remediation Plan

**Date:** 2026-03-13
**Scope:** Naming conventions, behavioral consistency, enum adoption, documentation alignment
**Impact:** ~250+ runtime source files, ~40+ documentation files, ~270+ test files, runtime.def
**Revision:** v3 — triple-validated, all findings cross-checked against source files

---

## Context

The Viper runtime has grown organically across 200+ C source files spanning collections, I/O, networking, graphics, text processing, math, and more. While individually well-implemented, the API surface has accumulated naming inconsistencies, behavioral asymmetries between similar types, and missed opportunities to use enums for type safety. This plan addresses all three dimensions to make the runtime feel deliberately designed and internally consistent.

**Guiding principles:**
- A developer who learns one collection type should be able to predict the API of another
- C function names should follow a single, predictable prefix convention
- Integer constants and magic numbers should be typed enums wherever feasible
- Documentation must exactly match implementation at all times

---

## Table of Contents

1. [Phase 1: C Function Naming Consistency](#phase-1)
2. [Phase 2: runtime.def IL Name & Registration Consistency](#phase-2)
3. [Phase 3: Collection Behavioral Consistency](#phase-3)
4. [Phase 4: Enum Adoption](#phase-4)
5. [Phase 5: Enum Naming Unification](#phase-5)
6. [Phase 6: Parameter & Convention Consistency](#phase-6)
7. [Phase 7: Documentation Alignment](#phase-7)
8. [Verification](#verification)
9. [File Impact Summary](#file-impact)
10. [Full Collection Feature Matrix](#feature-matrix)

---

<a id="phase-1"></a>
## Phase 1: C Function Naming Consistency

### 1.1 — String API Prefix Convention

The `rt_string_` vs `rt_str_` split is **intentional and correct** (validated in passes 2 and 3):
- **`rt_string_*`** (8 functions): Low-level lifecycle and introspection — `ref`, `unref`, `from_bytes`, `is_handle`, `cstr`, `intern`, `intern_drain`, `interned_eq`
- **`rt_str_*`** (45+ functions): User-facing string operations — slicing, searching, comparison, manipulation
- **`rt_sb_*`** (8 functions): Low-level StringBuilder struct operations
- **`rt_text_sb_*`** (6 functions): OOP StringBuilder wrapper (Viper.Text.StringBuilder bridge)

**Action:** No renames needed — keep the split as-is. It reflects a meaningful architectural distinction (internal infra vs public API).

**Previously completed renames (from crashed session) remain correct:**
- ✅ `rt_string_like` → `rt_str_like` (was user-facing, correctly moved to `rt_str_`)
- ✅ `rt_string_like_ci` → `rt_str_like_ci`

### 1.2 — Namespace Bridge Header Rename

**Already done (from crashed session):**
- ✅ `rt_ns_list_new` → `rt_list_new`
- ✅ `rt_ns_stringbuilder_new` → `rt_sb_new`

No remaining `rt_ns_` prefixed functions exist (validated pass 3). However, the header name is stale:

| Action | Detail |
|---|---|
| Rename header | `src/runtime/oop/rt_ns_bridge.h` → `src/runtime/oop/rt_sb_bridge.h` |
| Rename source | `src/runtime/oop/rt_ns_bridge.c` → `src/runtime/oop/rt_sb_bridge.c` |
| Rename internal finalizer | `rt_ns_stringbuilder_finalize()` → `rt_sb_finalize()` (static, internal only) |
| Update all includes | All files with `#include "rt_ns_bridge.h"` |

### 1.3 — Constructor Verb Consistency

Convention:
- **`_new`**: Allocate a new heap object (collections, data structures, OOP objects)
- **`_create`**: Compose a value from components (value types stored as int64_t, or create external resources on filesystem)
- **`_open`**: Open an existing external resource (files, streams, connections)
- **`_from_*`**: Factory from existing data
- **`_load`**: Load a resource from a file path (audio, assets)
- **`_parse`/`_parse_*`**: Factory from a string representation
- **`_today`/`_now`**: Factory for current-time values

**Items to fix:**

| Current Name | Issue | Action |
|---|---|---|
| `rt_exc_create` | Creates a heap-allocated exception object, not a value | Rename → `rt_exc_new` |

**Items that are CORRECT (no action):**

| Name | Rationale |
|---|---|
| `rt_datetime_create(year,month,day,hour,min,sec)` | ✅ Composes a value type (int64_t timestamp) from components |
| `rt_duration_create(days,hours,mins,secs,ms)` | ✅ Composes a value type (int64_t millis) from components |
| `rt_dateonly_create(year,month,day)` | ✅ Creates a heap-managed date object from components — acceptable given it takes component args. **But** if DateOnly is heap-allocated, rename → `rt_dateonly_new` for consistency with other heap objects. Verify. |
| `rt_tempfile_create` | ✅ Creates external resource on filesystem |
| `rt_tempdir_create` | ✅ Creates external resource on filesystem |
| `rt_archive_create` | ✅ Creates external archive file |
| `rt_archive_open` | ✅ Opens existing archive |
| `rt_sound_load` | ✅ Loads audio from file path |
| `rt_music_load` | ✅ Loads audio from file path |
| `rt_soundbank_new` | ✅ Creates empty in-memory bank |
| `rt_playlist_new` | ✅ Creates empty in-memory playlist |
| `rt_datetime_now`, `rt_dateonly_today` | ✅ Current-time factories |
| `rt_datetime_parse_iso`, `rt_dateonly_parse` | ✅ Parse factories |
| `rt_duration_from_*`, `rt_dateonly_from_days` | ✅ From-data factories |

### 1.4 — Destructor Verb Consistency

Convention:
- **`_destroy`**: Tear down a heap-managed object or resource handle
- **`_close`**: Close an external resource (file, socket, connection, window)
- **`_free`**: Release a low-level struct/memory buffer (not heap-managed via GC)

**Items to fix:**

| Current Name | Issue | Action |
|---|---|---|
| `rt_sound_free` | Audio resource handle (not raw memory) | Rename → `rt_sound_destroy` |
| `rt_music_free` | Audio resource handle (not raw memory) | Rename → `rt_music_destroy` |

**Correctly named (no action):**

| Name | Rationale |
|---|---|
| `rt_sb_free` | ✅ Low-level struct builder (not heap-managed) |
| `rt_pool_free` | ✅ Raw memory pool |
| `rt_canvas_close` | ✅ External window resource |
| `rt_tcp_close`, `rt_ws_close`, `rt_tls_close`, `rt_udp_close` | ✅ Network connections |
| `rt_binfile_close`, `rt_linereader_close`, `rt_linewriter_close` | ✅ File handles |
| `rt_channel_close` | ✅ Communication channel resource |
| `rt_threadpool_shutdown`/`rt_threadpool_shutdown_now` | ✅ Domain-specific verb (shutdown, not destroy/close) |

### 1.5 — Collection Verb Standardization

**Already done (from crashed session):**
- ✅ `rt_set_put` → `rt_set_add`
- ✅ `rt_set_drop` → `rt_set_remove`

**Remaining — set-like collections (membership-based, no keys):**

| Collection | Current Insert | → New | Current Delete | → New | Rationale |
|---|---|---|---|---|---|
| Bag | `rt_bag_put` | → `rt_bag_add` | `rt_bag_drop` | → `rt_bag_remove` | Set-like; match Set.Add/Remove |
| SortedSet | `rt_sortedset_put` | → `rt_sortedset_add` | `rt_sortedset_drop` | → `rt_sortedset_remove` | Set-like; match Set.Add/Remove |

**Remaining — map-like collections (key-value pairs):**

| Collection | Current Insert | → New | Rationale |
|---|---|---|---|
| Trie | `rt_trie_put` | → `rt_trie_set` | Map-like; match Map.Set |

**Keep as-is (correct):**

| Collection | Verb | Rationale |
|---|---|---|
| BiMap | `put` | Bidirectional map, `put` is idiomatic for "insert key↔value pair" |
| MultiMap | `put` | Multi-value map, `put` adds without replacing |
| LRUCache | `put` | Cache semantics, `put` is idiomatic |
| CountMap | `inc`/`inc_by`/`set` | Domain-specific counter operations |
| BloomFilter | `add` | ✅ Already consistent with Set |

### 1.6 — Legacy Functions Without Module Prefix

| Current Name | Action | Rationale |
|---|---|---|
| `rt_instr3()` | → `rt_str_instr3()` | BASIC legacy, lacks `rt_str_` prefix |
| `rt_to_int()` | Keep | Top-level converter, acceptable |
| `rt_to_double()` | Keep | Top-level converter, acceptable |
| `rt_val()` | Keep | BASIC VAL() compat |
| `rt_str()` | Keep | BASIC STR$() compat |
| `rt_const_cstr()` | Keep | Low-level utility |

**Corrected from pass 2:** `rt_voice_stop()` is NOT an issue — it's part of a consistent `rt_voice_*` sub-module (`rt_voice_stop`, `rt_voice_set_volume`, `rt_voice_set_pan`, `rt_voice_is_playing`).

### 1.7 — Parameter Naming Standardization

Convention for public APIs:
- **`obj`**: Opaque pointer to collection/object (primary convention)
- **`elem`**: Element being added/removed from linear collections and sets
- **`key`/`value`**: Parameters for map-like types
- **`other`**: Second collection in binary operations (union, intersect, diff)

| Module | Current Param | → Standard | Files |
|---|---|---|---|
| rt_ring | `item` | → `elem` | `rt_ring.h`, `rt_ring.c` |
| rt_stack | `val` | → `elem` | `rt_stack.h`, `rt_stack.c` |
| rt_queue | `val` | → `elem` | `rt_queue.h`, `rt_queue.c` |
| rt_deque | `val`/`item` | → `elem` | `rt_deque.h`, `rt_deque.c` |
| rt_list | `elem` | ✅ Already correct | — |
| rt_set | `elem` | ✅ Already correct | — |
| rt_map | `key`/`value` | ✅ Keep (map semantic) | — |

---

<a id="phase-2"></a>
## Phase 2: runtime.def IL Name & Registration Consistency

### 2.1 — Property Accessor Naming (`get_X` Convention)

**Rule:** Property accessors use `get_`/`set_` prefix. Methods use PascalCase verbs.

**Duration duplicate entries to clean up:**

| Current IL Name | Action |
|---|---|
| `Viper.Time.Duration.Days` | Convert to RT_ALIAS → `Duration.get_Days` |
| `Viper.Time.Duration.Hours` | Convert to RT_ALIAS → `Duration.get_Hours` |
| `Viper.Time.Duration.Minutes` | Convert to RT_ALIAS → `Duration.get_Minutes` |
| `Viper.Time.Duration.Seconds` | Convert to RT_ALIAS → `Duration.get_Seconds` |
| `Viper.Time.Duration.Millis` | Convert to RT_ALIAS → `Duration.get_Millis` |

**Correctly bare (computed methods, not properties):**
- `Vec2.Len`, `Vec3.Len`, `Quat.Len` — computed magnitude, keep bare

### 2.2 — Duration Namespace Duplication

Duration appears in BOTH `Viper.Text.Duration.*` and `Viper.Time.Duration.*`.

**Action:** Canonical namespace is `Viper.Time.Duration.*`. Convert all `Viper.Text.Duration.*` entries to RT_ALIAS.

### 2.3 — Size Property: `Length` vs `Count`

**Rule:** `get_Length` is canonical. `get_Count` is alias.

| Current | Action |
|---|---|
| `BloomFilter.get_Count` | Rename primary → `get_Length`, add `Count` alias |
| `UnionFind.get_Count` | Rename primary → `get_Length`, add `Count` alias |
| `Iterator.get_Count` | Rename primary → `get_Length`, add `Count` alias |
| `BinaryBuffer.get_Len` (via `rt_binbuf_get_len`) | Rename C func → `rt_binbuf_len()`, IL name → `get_Length` |

### 2.4 — CRITICAL: Register Missing Conversion Functions

**Pass 3 discovery:** 20+ conversion functions exist in `rt_convert_coll.c`/`.h` but are NOT registered in runtime.def. Only Stack.ToList, Queue.ToList, and Heap.ToSeq are currently exposed.

**Functions to register as RT_FUNC entries:**

| C Function | Proposed IL Name |
|---|---|
| `rt_list_to_seq` | `Viper.Collections.List.ToSeq` |
| `rt_list_to_set` | `Viper.Collections.List.ToSet` |
| `rt_list_to_stack` | `Viper.Collections.List.ToStack` |
| `rt_list_to_queue` | `Viper.Collections.List.ToQueue` |
| `rt_seq_to_list` | `Viper.Collections.Seq.ToList` |
| `rt_seq_to_set` | `Viper.Collections.Seq.ToSet` |
| `rt_seq_to_stack` | `Viper.Collections.Seq.ToStack` |
| `rt_seq_to_queue` | `Viper.Collections.Seq.ToQueue` |
| `rt_seq_to_deque` | `Viper.Collections.Seq.ToDeque` |
| `rt_seq_to_bag` | `Viper.Collections.Seq.ToBag` |
| `rt_set_to_seq` | `Viper.Collections.Set.ToSeq` |
| `rt_set_to_list` | `Viper.Collections.Set.ToList` |
| `rt_stack_to_seq` | `Viper.Collections.Stack.ToSeq` |
| `rt_queue_to_seq` | `Viper.Collections.Queue.ToSeq` |
| `rt_deque_to_seq` | `Viper.Collections.Deque.ToSeq` |
| `rt_deque_to_list` | `Viper.Collections.Deque.ToList` |
| `rt_ring_to_seq` | `Viper.Collections.Ring.ToSeq` |
| `rt_bag_to_seq` | `Viper.Collections.Bag.ToSeq` |
| `rt_bag_to_set` | `Viper.Collections.Bag.ToSet` |
| `rt_bag_to_list` | `Viper.Collections.Bag.ToList` |

**Also register variadic factory functions (if IL supports variadics):**
- `rt_seq_of`, `rt_list_of`, `rt_set_of`

### 2.5 — Expose Header-Only Functions

Two functions are declared in headers but not registered in runtime.def:

| C Function | Proposed IL Name | File |
|---|---|---|
| `rt_deque_with_capacity(i64)` | `Viper.Collections.Deque.WithCapacity` | `rt_deque.h` |
| `rt_grid2d_find(grid,val,*x,*y)` | Evaluate — may need wrapper for IL | `rt_grid2d.h` |

---

<a id="phase-3"></a>
## Phase 3: Collection Behavioral Consistency

### 3.1 — Missing Methods (Gap Analysis)

Full feature matrix in [appendix](#feature-matrix). Items grouped by priority.

#### CRITICAL — Ring (circular buffer) is severely under-featured

| Method | Signature | Action |
|---|---|---|
| `rt_ring_has` | `int8_t (void *obj, void *elem)` | **Add** |
| `rt_ring_clone` | `void* (void *obj)` | **Add** |
| `rt_ring_reverse` | `void (void *obj)` | **Add** |
| `rt_ring_first` | `void* (void *obj)` | **Add** |
| `rt_ring_last` | `void* (void *obj)` | **Add** |

Note: `rt_ring_to_seq` is already in rt_convert_coll.c — just needs registering (Phase 2.4).

#### HIGH — Clone for core types

| Collection | Method to add |
|---|---|
| Set | `rt_set_clone()` |
| Map | `rt_map_clone()` |
| Stack | `rt_stack_clone()` |
| Queue | `rt_queue_clone()` |
| Ring | `rt_ring_clone()` (above) |

#### HIGH — Items() for iteration export

**Convention:** Linear collections: `Items()` → Seq. Maps: `Keys()`/`Values()`.

| Collection | Current | Action |
|---|---|---|
| List | None | **Add `rt_list_items()` → Seq** |
| Deque | None | **Add `rt_deque_items()` → Seq** |
| Ring | None | Covered by rt_ring_to_seq registration (Phase 2.4) |
| Stack | `to_list()` only | **Add `rt_stack_items()` → Seq** (keep ToList) |
| Queue | `to_list()` only | **Add `rt_queue_items()` → Seq** (keep ToList) |
| PQueue/Heap | `to_seq()` | **Add `items()` alias** |
| Set, Bag | ✅ `items()` | Already correct |

#### MEDIUM — Seq.Slice

| Collection | Action |
|---|---|
| Seq | **Add `rt_seq_slice(obj, start, end)` → Seq** (List and Bytes have it) |

#### MEDIUM — First/Last convenience

| Collection | Current | Action |
|---|---|---|
| Deque | `peek_front`/`peek_back` | **Add `rt_deque_first()`/`rt_deque_last()` aliases** |
| Ring | `peek` only | `first`/`last` covered above |

#### MEDIUM — TryPop for safe empty-collection access

| Collection | Action |
|---|---|
| Stack | **Add `rt_stack_try_pop()` → NULL if empty** |
| Queue | **Add `rt_queue_try_pop()` → NULL if empty** |
| Ring | **Add `rt_ring_try_pop()` → NULL if empty** |
| Deque | **Add `rt_deque_try_pop_front()`/`try_pop_back()`** |
| PQueue | ✅ Already has `try_pop`, `try_peek` |

#### LOW — Missing baseline ops

| Collection | Method | Action |
|---|---|---|
| Bytes | `is_empty` | **Add `rt_bytes_is_empty()`** |
| Grid2D | `is_empty` | **Add `rt_grid2d_is_empty()`** |

### 3.2 — Return Type Notes

| Operation | Current behavior | Assessment |
|---|---|---|
| `Seq.Remove(i64)` → `void*` | Removes by INDEX, returns element | Correct semantics (it's RemoveAt) |
| `List.RemoveAt(i64)` → `void` | Removes by index, no return | Inconsistent with Seq but low risk to change. Document. |
| `List.Remove(obj)` → `int8_t` | Removes by value, returns found? | ✅ Correct |
| `Set.Add(obj)` → `int8_t` | Returns 1=new, 0=duplicate | ✅ Correct |
| `List.Push(obj)` → `void` | No return | ✅ Correct (no duplicate concept for lists) |

### 3.3 — Structural Equality (LOW priority, future phase)

Only FrozenMap/FrozenSet have `equals()`. Adding `equals()` to mutable collections requires defining deep equality semantics. **Defer to a future plan.**

### 3.4 — Functional Operations (no action needed)

Functional ops (`keep`/`reject`/`apply`/`fold`/`all`/`any`/`none`) exist only on Seq — this is intentional. Users convert via `Items()` → Seq → functional ops. **Document this pattern in viperlib docs.**

---

<a id="phase-4"></a>
## Phase 4: Enum Adoption

### 4.1 — `#define` Groups to Convert to Enums

#### HIGH PRIORITY — GUI Constants (`src/runtime/graphics/rt_gui.h`)

10 groups, all confirmed with exact names/values in pass 3:

| Group | Count | Existing Prefix | Enum Typedef |
|---|---|---|---|
| Cursor types | 11 | `RT_CURSOR_*` (ARROW=0..NOT_ALLOWED=10) | `rt_cursor_type_t` |
| StatusBar zones | 3 | `RT_STATUSBAR_ZONE_*` (LEFT=0..RIGHT=2) | `rt_statusbar_zone_t` |
| Toolbar styles | 3 | `RT_TOOLBAR_STYLE_*` (ICON_ONLY=0..ICON_TEXT=2) | `rt_toolbar_style_t` |
| Toolbar icon sizes | 3 | `RT_TOOLBAR_ICON_*` (SMALL=0..LARGE=2) | `rt_toolbar_icon_size_t` |
| Syntax tokens | 11 | `RT_TOKEN_*` (NONE=0..ERROR=10) | `rt_token_type_t` |
| MessageBox types | 4 | `RT_MESSAGEBOX_*` (INFO=0..QUESTION=3) | `rt_messagebox_type_t` |
| File dialog modes | 3 | `RT_FILEDIALOG_*` (OPEN=0..FOLDER=2) | `rt_filedialog_mode_t` |
| Toast types | 4 | `RT_TOAST_*` (INFO=0..ERROR=3) | `rt_toast_type_t` |
| Toast positions | 6 | `RT_TOAST_POSITION_*` (TOP_RIGHT=0..BOTTOM_CENTER=5) | `rt_toast_position_t` |
| Minimap markers | 4 | `RT_MINIMAP_MARKER_*` (ERROR=0..SEARCH=3) | `rt_minimap_marker_t` |

#### HIGH PRIORITY — Gamepad Axes (`src/runtime/graphics/rt_action.h`)

| Group | Count | Existing Prefix | Enum Typedef |
|---|---|---|---|
| Gamepad axes | 7 | `VIPER_AXIS_*` (LEFT_X=0..MAX=6) | `viper_axis_t` |

#### HIGH PRIORITY — Network/Core

| Group | Count | File | Enum Typedef |
|---|---|---|---|
| TLS status codes | 9 | `src/runtime/network/rt_tls.h` | `rt_tls_status_t` |
| Log levels | 5 | `src/runtime/core/rt_log.h` | `rt_log_level_t` |
| Watcher events | 5 | `src/runtime/io/rt_watcher.h` | `rt_watch_event_t` |
| Stream types | 2 | `src/runtime/io/rt_stream.h` | `rt_stream_type_t` |

#### MEDIUM PRIORITY

| Group | Count | File | Enum Typedef |
|---|---|---|---|
| Waveform types | 4 | `src/runtime/audio/rt_synth.h` | `rt_wave_type_t` |
| SFX presets | 6 | `src/runtime/audio/rt_synth.h` | `rt_sfx_preset_t` |
| Seek origins | 3 | `src/runtime/io/rt_binfile.h` (currently BARE magic numbers!) | `rt_seek_origin_t` |
| Huffman block types | 3 | `src/runtime/io/rt_compress.c` (bare switch cases!) | `rt_huffman_type_t` |
| TLS cert verification | 2 | `src/runtime/network/rt_tls.h` | `rt_cert_verify_t` |

#### LOW PRIORITY

| Group | File | Enum Typedef |
|---|---|---|
| Class ID constants (scattered) | Various (`rt_exc.h`, `rt_seq.h`, `rt_map.h`) | Consolidate → `rt_class_id_t` in new `src/runtime/core/rt_class_ids.h` |
| Tile collision types | `src/runtime/graphics/rt_tilemap.c` (internal only) | `rt_tile_collision_t` (internal enum) |

#### KEEP AS `#define` (platform compatibility)

These match external platform values (GLFW/SDL/HID) and must remain `#define`:
- `VIPER_KEY_*` (73 keyboard constants in `rt_input.h`)
- `VIPER_MOUSE_BUTTON_*` (5 mouse constants in `rt_input.h`)
- `VIPER_PAD_*` (16 gamepad constants in `rt_input.h`)

### 4.2 — Enum Style Convention

Pass 3 found two enum declaration patterns in use:

**Style A — Named enum + `_t` typedef (rt_box.h):**
```c
typedef enum rt_box_type {
    RT_BOX_I64 = 0,
    RT_BOX_F64 = 1,
} rt_box_type_t;
```

**Style B — Anonymous enum + `_t` typedef (rt_heap.h, rt_serialize.h):**
```c
typedef enum {
    RT_HEAP_STRING = 1,
    RT_HEAP_ARRAY = 2,
} rt_heap_kind_t;
```

**Recommended convention for ALL new enums: Style B (anonymous enum + `_t` typedef):**
- It's the most common pattern in existing code
- Avoids the redundancy of naming both the enum tag and typedef
- The `_t` typedef is the only name callers use

```c
typedef enum {
    RT_<MODULE>_<VALUE> = <int>,
    ...
} rt_<module>_<concept>_t;
```

- Typedef: `rt_` prefix, lowercase, underscores, `_t` suffix
- Values: `RT_` prefix, UPPER_SNAKE_CASE
- Always include explicit integer values for ABI stability

---

<a id="phase-5"></a>
## Phase 5: Enum Naming Unification

### 5.1 — Add `_t` Suffix to Existing Enums

7 existing enums lack the `_t` suffix (or use wrong conventions):

| Current | → Proposed | File |
|---|---|---|
| `rt_ease_type` | → `rt_ease_type_t` | `src/runtime/collections/rt_tween.h` |
| `rt_screenfx_type` | → `rt_screenfx_type_t` | `src/runtime/collections/rt_screenfx.h` |
| `rt_pathfollow_mode` | → `rt_pathfollow_mode_t` | `src/runtime/collections/rt_pathfollow.h` |
| `rt_sb_status` | → `rt_sb_status_t` | `src/runtime/core/rt_string_builder.h` |
| `rt_input_grow_result` | → `rt_input_grow_result_t` | `src/runtime/rt_internal.h` |
| `rt_json_tok_type` | → `rt_json_tok_type_t` | `src/runtime/text/rt_json_stream.h` |
| `XmlNodeType` | → `rt_xml_node_type_t` | `src/runtime/text/rt_xml.h` |

**Already correct (no action):**
- ✅ `rt_box_type_t` (`src/runtime/oop/rt_box.h`)
- ✅ `rt_heap_kind_t` (`src/runtime/core/rt_heap.h`)
- ✅ `rt_elem_kind_t` (`src/runtime/core/rt_heap.h`)
- ✅ `rt_pool_class_t` (`src/runtime/core/rt_pool.h`)
- ✅ `rt_format_t` (`src/runtime/text/rt_serialize.h`)

---

<a id="phase-6"></a>
## Phase 6: Parameter & Convention Consistency

### 6.1 — Error Handling Convention

| Situation | Convention | Example |
|---|---|---|
| Out-of-bounds index | `rt_trap()` | `rt_list_get(list, -1)` traps |
| Allocation failure | Return `NULL` | `rt_list_new()` returns NULL on OOM |
| Element not found (search) | Return `-1` (i64) | `rt_list_find()` returns -1 |
| Element not found (removal) | Return `0` (i1 false) | `rt_list_remove()` returns 0 |
| Element found (membership) | Return `1` (i1 true) | `rt_set_has()` returns 1 |
| Pop on empty | `rt_trap()` | `rt_stack_pop()` traps |
| Pop on empty (safe) | Return `NULL` | `rt_stack_try_pop()` returns NULL |

**Action:** Add a convention comment block to each collection header.

### 6.2 — Boolean Return Type

**Rule:** All boolean functions return `int8_t` (IL `i1`). Audit for any using `int`, `int32_t`, or `bool`.

### 6.3 — HTTP GET Naming Note

`rt_http_get_bytes()` uses "get" as HTTP verb, not property accessor. Add comment in header: `// Note: "get" here is HTTP GET method, not a property accessor`.

---

<a id="phase-7"></a>
## Phase 7: Documentation Alignment

### 7.1 — viperlib Documentation Updates

| Directory | Files | Updates Needed |
|---|---|---|
| `docs/viperlib/collections/sequential.md` | List, Queue, Stack, Deque, Ring, Heap | New methods, renamed verbs, conversion methods |
| `docs/viperlib/collections/maps-sets.md` | Set, Map, Bag, Trie, SortedSet | Verb renames (Bag, SortedSet, Trie) |
| `docs/viperlib/collections/functional.md` | Seq, Iterator | Conversion methods, `Items()` → Seq pattern |
| `docs/viperlib/collections/specialized.md` | Bag, Trie, BitSet, BloomFilter | Verb renames |
| `docs/viperlib/text/patterns.md` | String.Like/LikeCI | Verify references |
| `docs/viperlib/text/formatting.md` | StringBuilder | Verify references |
| Time docs | Duration | Remove/alias Viper.Text.Duration references |

### 7.2 — Release Notes

Add to current release notes:
- All function renames (old → new)
- New enum types
- New collection methods
- Conversion method registrations
- Behavioral consistency improvements

### 7.3 — runtime.def TOC

Update the TOC comment at top of runtime.def for any new/moved sections.

### 7.4 — Demo Programs and Bug Docs

- Search `demos/` for renamed functions, update
- Update `bugs/runtime_audit_20260212.md` for any renamed references

---

<a id="verification"></a>
## Verification

**After each phase:**

1. `./scripts/build_viper.sh` — full build passes
2. `ctest --test-dir build --output-on-failure` — all 1279+ tests pass
3. Grep for old names — zero hits
4. `./scripts/check_runtime_completeness.sh` — all RT_FUNC entries have implementations
5. Windows build parity check

**After all phases:**

6. `./scripts/build_demos.sh` — all demos build and run
7. Grep viperlib docs for stale function names
8. Verify release notes are complete

---

<a id="file-impact"></a>
## File Impact Summary

| Phase | Description | Estimated Files |
|---|---|---|
| 1 | C function naming | ~60 |
| 2 | runtime.def IL names + registration | ~5 (runtime.def + generated) |
| 3 | New collection methods | ~40 (implementations + tests) |
| 4 | Enum adoption | ~30 |
| 5 | Enum naming unification | ~15 |
| 6 | Convention documentation | ~20 |
| 7 | Documentation alignment | ~50 |
| **Total** | | **~220 files** |

**Execution order:** Phases 1→7 sequentially. Each committed separately.

---

<a id="feature-matrix"></a>
## Appendix: Full Collection Feature Matrix (30 types)

**Legend:** ✅ = present, ❌ = absent/gap, — = not applicable

### Linear Collections

| Feature | List | Seq | Stack | Queue | Ring | Deque |
|---|---|---|---|---|---|---|
| new | ✅ | ✅ (+with_capacity) | ✅ | ✅ | ✅ (+new_default) | ✅ (+with_capacity) |
| len | ✅ | ✅ (+cap) | ✅ | ✅ | ✅ (+cap) | ✅ (+cap) |
| is_empty | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| has | ✅ | ✅ | ✅ | ✅ | ❌ **GAP** | ✅ |
| clear | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| clone | ✅ | ✅ | ❌ **GAP** | ❌ **GAP** | ❌ **GAP** | ✅ |
| push | ✅ | ✅ (+push_all, insert) | ✅ | ✅ | ✅ | ✅ (front+back) |
| pop | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (front+back) |
| remove | ✅ (by value + by idx) | ✅ (by idx only) | — | — | — | — |
| get/set | ✅ | ✅ | — | — | ✅ get | ✅ get+set |
| find | ✅ | ✅ | — | — | — | — |
| first/last | ✅ | ✅ (+peek) | — | — | peek only ❌ | peek_front/back ❌ |
| items/to_seq | ❌ **GAP** | — (is Seq) | to_list only | to_list only | ❌ **GAP** | ❌ **GAP** |
| slice | ✅ | ❌ **GAP** | — | — | — | — |
| reverse | ✅ | ✅ | — | — | ❌ **GAP** | ✅ |
| sort | ✅ (sort, sort_desc) | ✅ (+sort_by) | — | — | — | — |
| shuffle | ✅ | ✅ | — | — | — | — |
| try_pop | — | — | ❌ **GAP** | ❌ **GAP** | ❌ **GAP** | ❌ **GAP** |

### Set-like Collections

| Feature | Set | Bag | SortedSet | FrozenSet | BloomFilter | BitSet |
|---|---|---|---|---|---|---|
| new | ✅ | ✅ | ✅ | ✅ (from_seq) | ✅ | ✅ |
| len | ✅ | ✅ | ✅ | ✅ | count | ✅ |
| is_empty | ✅ | ✅ | ✅ | ✅ | — | ✅ |
| has | ✅ | ✅ | ✅ | ✅ | might_contain | ✅ (get) |
| add | ✅ (add) | `put` **FIX→add** | `put` **FIX→add** | — (immutable) | ✅ (add) | ✅ (set) |
| remove | ✅ (remove) | `drop` **FIX→remove** | `drop` **FIX→remove** | — | — | ✅ (clear bit) |
| clear | ✅ | ✅ | ✅ | — | ✅ | ✅ (clear_all) |
| clone | ❌ **GAP** | — | — | — | — | — |
| items | ✅ | ✅ | ✅ (+take,skip) | ✅ | — | — |
| union/intersect/diff | ✅ | ✅ | ✅ | — | — | ✅ (and,or,xor) |
| equals | — | — | — | ✅ | — | — |

### Map-like Collections

| Feature | Map | OrderedMap | TreeMap | IntMap | BiMap | CountMap | DefaultMap | MultiMap | Trie | WeakMap | LRUCache | FrozenMap |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| new | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | from_seqs |
| len | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| is_empty | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| has | ✅ | ✅ | ✅ | ✅ | ✅ (k+v) | ✅ | ✅ | ✅ | ✅ (+prefix) | ✅ | ✅ | ✅ |
| set/put | ✅ set | ✅ set | ✅ set | ✅ set | ✅ put | ✅ (set,inc) | ✅ set | ✅ put | `put` **FIX→set** | ✅ set | ✅ put | — |
| get | ✅ (+get_or) | ✅ | ✅ | ✅ (+get_or) | ✅ (k+v) | ✅ | ✅ (+default) | ✅ (+first) | ✅ | ✅ | ✅ (+peek) | ✅ (+get_or) |
| remove | ✅ | ✅ | ✅ | ✅ | ✅ (k+v) | ✅ | ✅ | ✅ (all) | ✅ | ✅ | ✅ | — |
| clear | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| clone | ❌ **GAP** | — | — | — | — | — | — | — | — | — | — | — |
| keys/values | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| first/last | — | — | ✅ | — | — | — | — | — | — | — | — | — |
| equals | — | — | — | — | — | — | — | — | — | — | — | ✅ |

### Specialized Collections

| Feature | Bytes | PQueue | Grid2D | BinBuf | SparseArray | UnionFind |
|---|---|---|---|---|---|---|
| new | ✅ (+from_*) | ✅ (+new_max) | ✅ | ✅ (+new_cap) | ✅ | ✅ |
| len | ✅ | ✅ | ✅ (size) | `get_len` **FIX→len** | ✅ | — |
| is_empty | ❌ **GAP** | ✅ | ❌ **GAP** | — | — | — |
| has | — | — | — | — | ✅ | — |
| get/set | ✅ | — | ✅ | read_*/write_* | ✅ | — |
| clear | ✅ (fill) | ✅ | ✅ | ✅ (reset) | ✅ | ✅ (reset) |
| clone | ✅ | — | — | — | — | — |
| find | ✅ | — | ✅ | — | — | ✅ (find) |
| slice | ✅ | — | — | — | — | — |
| items/export | — | ✅ (to_seq) | — | ✅ (to_bytes) | ✅ (indices,values) | — |

---

## Summary of All Changes

| Category | Count | Description |
|---|---|---|
| **C function renames** | ~10 | Verbs (bag/sortedset/trie), constructor (rt_exc), destructors (sound/music), legacy (rt_instr3) |
| **Header renames** | 2 | rt_ns_bridge.h → rt_sb_bridge.h (+.c) |
| **Parameter renames** | ~4 modules | ring/stack/queue/deque: item/val → elem |
| **runtime.def registrations** | ~22 | 20+ conversion functions + 2 header-only functions |
| **runtime.def cleanup** | ~8 | Duration duplicates, namespace aliases, Count→Length |
| **New collection methods** | ~20 | Ring(5), Clone(4), Items(5), Slice(1), TryPop(4), is_empty(2) |
| **Enum conversions** | ~18 groups | GUI(10), network(2), core(2), I/O(4), audio(2) |
| **Enum renames** | 7 | Add `_t` suffix to existing enums |
| **Documentation updates** | ~50 files | viperlib, release notes, demos, bug docs |
