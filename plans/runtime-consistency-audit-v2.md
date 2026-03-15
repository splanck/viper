# Viper Runtime Consistency Audit — Comprehensive Report & Implementation Plan

## Context

Comprehensive review of the entire Viper runtime library focused on naming consistency, behavioral uniformity across collections, enum opportunities, missing foundational pieces, and documentation accuracy. The goal is to make Viper feel like a thoughtfully designed language where naming, behavior, and API patterns are consistent and predictable.

Every documentation mismatch is treated as a bug. Every missing `#pragma once` is a defect. Every magic number is a missed enum opportunity.

**Scope:** `src/runtime/`, `src/il/runtime/runtime.def`, `src/il/runtime/signatures/`, `include/viper/runtime/rt.h`, all viperlib docs, all runtime tests, bible docs, release notes.

**Prior Work:** A previous runtime consistency audit (`plans/runtime-consistency-audit.md`) implemented most C-level code changes (verb renames, missing methods, enum types, conversion functions). This plan addresses ALL remaining gaps discovered through deep investigation.

---

## Part 1: Audit Findings

### 1.1 Documentation Property `Len` vs `Length` — 40+ Bugs (CRITICAL)

The runtime.def canonical property is `Length` (e.g., `get_Length`). Every doc file uses `Len` in property tables. This is WRONG — no property named `Len` exists. Users reading docs will try `.Len` and fail.

**Affected files and occurrence counts:**

| File | `Len` Occurrences | Collections Affected |
|------|-------------------|---------------------|
| `docs/viperlib/collections/maps-sets.md` | 7 | Map, Set, OrderedMap, SortedSet, IntMap, FrozenSet, FrozenMap |
| `docs/viperlib/collections/multi-maps.md` | 10 (8 tables + 2 in text) | BiMap, MultiMap, CountMap, DefaultMap, LruCache, WeakMap, SparseArray |
| `docs/viperlib/collections/sequential.md` | 6 | List, Queue, Stack, Deque, Ring, Heap |
| `docs/viperlib/collections/specialized.md` | 4 | Bag, Trie, BitSet, Bytes |
| `docs/viperlib/collections/functional.md` | 1 | Seq |
| `docs/viperlib/text/patterns.md` | 1 | Scanner |
| `docs/viperlib/io/streams.md` | 5 (3 tables + 2 notes) | MemStream, BinaryBuffer, ByteStream |
| `docs/viperlib/threads.md` | 3 | ConcurrentMap, ConcurrentQueue, Channel |

**Also:** `docs/viperlib/collections/sequential.md:28` says "`Count` is available as an alias for `Len`" — should say "alias for `Length`".

**Also:** `docs/viperlib/io/streams.md:271,712` have notes referencing `Len` property — should reference `Length`.

### 1.2 Documentation Method Verb Mismatches — 60+ Bugs (CRITICAL)

C code was renamed (Bag: `Put→Add`, `Drop→Remove`; SortedSet: `Put→Add`, `Drop→Remove`; Trie: `Put→Set`) with backward-compatibility `RT_ALIAS` entries. Documentation and code examples still use the OLD names.

**Bag `Put()`→`Add()` and `Drop()`→`Remove()`:**

| File | Line(s) | Issue |
|------|---------|-------|
| `docs/viperlib/collections/specialized.md` | 33-34 | Method table: `Put(str)`, `Drop(str)` |
| `docs/viperlib/collections/specialized.md` | 61-63, 71, 84-86, 90 | Zia/BASIC examples: `fruits.Put(...)` |
| `docs/viperlib/collections/specialized.md` | 74, 98 | Zia/BASIC examples: `fruits.Drop(...)` |
| `docs/viperlib/collections/specialized.md` | 104-106, 109-111 | BASIC examples: `bagA.Put(...)`, `bagB.Put(...)` |
| `docs/bible/part2-building-blocks/13-stdlib.md` | 727-729 | `tags.Put("important")`, `tags.Put("urgent")` |
| `docs/bible/part2-building-blocks/13-stdlib.md` | 734 | `tags.Drop("urgent")` |

**SortedSet `Put()`→`Add()` and `Drop()`→`Remove()`:**

| File | Line(s) | Issue |
|------|---------|-------|
| `docs/viperlib/collections/maps-sets.md` | 444-445 | Method table: `Put(str)`, `Drop(str)` |
| `docs/viperlib/collections/maps-sets.md` | 475-478, 510-517, 533-534 | Code examples: `set.Put(...)` |

**Trie `Put()`→`Set()`:**

| File | Line(s) | Issue |
|------|---------|-------|
| `docs/viperlib/collections/specialized.md` | 276 | Method table: `Put(key, value)` |
| `docs/viperlib/collections/specialized.md` | 307-311, 346-350, 379 | Code examples: `t.Put(...)` |

**DO NOT change:** BiMap/MultiMap/LruCache/CountMap `.Put()` — these are CORRECT canonical names.
**DO NOT change:** `Seq.Drop(n)` — correct functional operation (skip first N elements).
**DO NOT change:** `Http.Put()` — correct HTTP verb.

### 1.3 Documentation Missing Methods — 40+ Gaps (HIGH)

Methods implemented in runtime.def but completely undocumented in viperlib docs:

| Collection | Missing from Docs | runtime.def Entry |
|-----------|-------------------|-------------------|
| **List** | `Shuffle()`, `Clone()`, `ToSeq()`, `ToSet()`, `ToStack()`, `ToQueue()` | Lines 484-491 |
| **Set** | `ToSeq()`, `ToList()` | Lines 454-455 |
| **Queue** | `ToList()`, `ToSeq()` | Lines 628, 631 |
| **Stack** | `ToList()`, `ToSeq()` | Lines 753, 756 |
| **Deque** | `ToSeq()`, `ToList()` | Lines 8333-8334 |
| **Ring** | `ToSeq()` | Line 676 |
| **Bag** | `ToSeq()`, `ToSet()` | Lines 372-373 |
| **Seq** | `GetStr()`, `ToList()`, `ToSet()`, `ToStack()`, `ToQueue()`, `ToDeque()`, `ToBag()` | Lines 691, 734-739 |
| **Heap** | (complete) | — |

### 1.4 Release Notes Stale References (HIGH)

| File | Line(s) | Issue |
|------|---------|-------|
| `docs/release_notes/Viper_Release_Notes_0_2_3.md` | 156-159 | Section title says "`.Len → .Length Rename`" but body still references `.Len` |
| `docs/release_notes/Viper_Release_Notes_0_2_3.md` | 715 | Says `Count→Len` — should say `Count→Length` |
| `docs/release_notes/Viper_Release_Notes_0_2_3.md` | 811 | Says `Count→Len, Size→Len` — should say `Count→Length, Size→Length` |
| `docs/release_notes/Viper_Release_Notes_0_2_3.md` | 813-814 | References `.Len` retained as alias — correct but confusing in context |

### 1.5 Test Files Using Non-Canonical Method Names — 18 Bugs (HIGH)

Tests work (aliases exist) but should use canonical names:

**Bag `Put`→`Add`, `Drop`→`Remove`:**

| File | Line(s) | Old | New |
|------|---------|-----|-----|
| `tests/runtime_sweep/basic/collections.bas` | 127-129 | `bag.Put(...)` | `bag.Add(...)` |
| `tests/runtime_sweep/basic/collections.bas` | 132 | `bag.Drop("a")` | `bag.Remove("a")` |
| `tests/runtime_sweep/basic/collections.bas` | 137-138 | `bag2.Put(...)` | `bag2.Add(...)` |
| `tests/runtime_sweep/basic/edge_collections.bas` | 203 | `bag.Drop("nonexistent")` | `bag.Remove("nonexistent")` |
| `tests/runtime_sweep/basic/edge_collections.bas` | 206 | `bag.Put("")` | `bag.Add("")` |
| `tests/rt_api/test_map_bag.bas` | 20-23 | `b.Put(...)` | `b.Add(...)` |
| `tests/rt_api/test_map_bag.bas` | 27 | `b.Drop("x")` | `b.Remove("x")` |
| `tests/rt_api/test_map_bag.zia` | 26-29 | `b.Put(...)` | `b.Add(...)` |
| `tests/rt_api/test_map_bag.zia` | 33 | `b.Drop("x")` | `b.Remove("x")` |

**TreeMap `Drop`→`Remove`:**

| File | Line(s) | Old | New |
|------|---------|-----|-----|
| `tests/rt_api/test_treemap_trie.bas` | 14 | `tm.Drop("apple")` | `tm.Remove("apple")` |
| `tests/rt_api/test_treemap_trie.zia` | 17 | `tm.Drop("apple")` | `tm.Remove("apple")` |

**DO NOT change:** `bm.Put(...)` in test_treemap_trie — BiMap `.Put()` is canonical.
**DO NOT change:** `lru.Put(...)` and `mm.Put(...)` in test_special_collections — correct canonical names.
**DO NOT change:** `s.Drop(1)` in test_list_seq — `Seq.Drop(n)` is correct functional method.

### 1.6 C++ Test Function Name Staleness (MEDIUM)

| File | Old Name | New Name |
|------|----------|----------|
| `src/tests/runtime/RTBagTests.cpp:44,328` | `test_bag_put_has` | `test_bag_add_has` |
| `src/tests/runtime/RTBagTests.cpp:77,329` | `test_bag_drop` | `test_bag_remove` |
| `src/tests/runtime/RTSortedSetTests.cpp:44,325` | `test_sortedset_put_has` | `test_sortedset_add_has` |

### 1.7 Missing `#pragma once` Guards — 36 Headers (HIGH)

36 headers use `#ifndef` instead of `#pragma once`. All other 162 headers use `#pragma once`. Standardize to `#pragma once` everywhere.

**Files (grouped by module):**

- **Core (5):** `rt_atomic_compat.h`, `rt_gc.h`, `rt_printf_compat.h`, `rt_stack_safety.h`, `rt_string_builder.h`
- **Collections (16):** `rt_buttongroup.h`, `rt_collision.h`, `rt_convert_coll.h`, `rt_debugoverlay.h`, `rt_grid2d.h`, `rt_objpool.h`, `rt_particle.h`, `rt_pathfollow.h`, `rt_quadtree.h`, `rt_screenfx.h`, `rt_smoothvalue.h`, `rt_sortedset.h`, `rt_spriteanim.h`, `rt_statemachine.h`, `rt_timer.h`, `rt_tween.h`
- **Graphics (2):** `rt_gui_internal.h`, `rt_inputmgr.h`
- **Audio (1):** `rt_playlist.h`
- **Network (4):** `rt_crypto.h`, `rt_ecdsa_p256.h`, `rt_restclient.h`, `rt_tls.h`, `rt_tls_internal.h`
- **Text (4):** `rt_aes.h`, `rt_compiled_pattern.h`, `rt_hash_util.h`, `rt_regex_internal.h`
- **OOP (1):** `rt_lazyseq.h`
- **Threads (2):** `rt_future.h`, `rt_parallel.h`

### 1.8 Magic Number → Enum Opportunities — 15+ Candidates (HIGH)

Places where `int64_t` or raw integers should be named enums for readability:

**Tier 1 — User-facing API parameters:**

| Location | Current | Proposed Enum | Values |
|----------|---------|---------------|--------|
| `src/runtime/audio/rt_playlist.h:144` | `int64_t mode` (0/1/2) | `rt_playlist_repeat_t` | `RT_REPEAT_NONE=0, RT_REPEAT_ALL=1, RT_REPEAT_ONE=2` |
| `src/runtime/audio/rt_synth.h:17-18` | `int64_t type` (0/1/2/3) | `rt_waveform_t` | `RT_WAVE_SINE=0, RT_WAVE_SQUARE=1, RT_WAVE_SAW=2, RT_WAVE_TRIANGLE=3` |
| `src/runtime/audio/rt_synth.h` | SFX presets (0-5) | `rt_sfx_preset_t` | `RT_SFX_JUMP=0, RT_SFX_COIN=1, RT_SFX_HIT=2, RT_SFX_EXPLOSION=3, RT_SFX_POWERUP=4, RT_SFX_LASER=5` |
| `src/runtime/graphics/rt_gui.h` | `int64_t mode` (0/1/2/3) | `rt_image_scale_t` | `RT_SCALE_NONE=0, RT_SCALE_FIT=1, RT_SCALE_FILL=2, RT_SCALE_STRETCH=3` |
| `src/runtime/graphics/rt_tilemap.h:177` | `int64_t coll_type` (0/1/2) | `rt_tilemap_collision_t` | `RT_COLLISION_NONE=0, RT_COLLISION_SOLID=1, RT_COLLISION_ONE_WAY_UP=2` |

**Tier 2 — GUI widget styling:**

| Location | Current | Proposed Enum | Values |
|----------|---------|---------------|--------|
| `rt_gui.h` button style | `int64_t style` (0-4) | `rt_button_style_t` | `RT_BTN_DEFAULT=0, RT_BTN_PRIMARY=1, RT_BTN_SECONDARY=2, RT_BTN_DANGER=3, RT_BTN_TEXT=4` |
| `rt_gui.h` icon position | `int64_t pos` (0/1) | `rt_icon_pos_t` | `RT_ICON_LEFT=0, RT_ICON_RIGHT=1` |

**Tier 3 — Internal but improve readability:**

| Location | Current | Proposed Enum |
|----------|---------|---------------|
| `rt_box.h:8` | Type tags 0/1/2/3 | `rt_box_type_t` (I64=0, F64=1, I1=2, STR=3) |
| `rt_pathfollow.h:37-39` | Play modes 0/1/2 | Already has enum — verify |
| `rt_tween.h:33` | Easing functions 0-11+ | Already has enum — verify |
| `rt_xml.h:37-40` | Node types 1/2/3/4 | Already has enum — verify |

### 1.9 Missing Collection Operations — Code Changes (MEDIUM)

| Collection | Missing Operation | All Peers Have It? |
|-----------|-------------------|-------------------|
| Bag | `Clone()` | Yes — List, Set, Map, Queue, Stack, Ring, Deque, Seq all have Clone |
| Trie | `Clone()` | Yes |
| UnionFind | `Clear()` alias | All other collections use `Clear()`, UnionFind only has `Reset()` |

### 1.10 Property Naming Normalization — runtime.def (MEDIUM)

| Class | Current Property | Should Also Have | Status |
|-------|-----------------|------------------|--------|
| BitSet | `Count` + `Length` | Both exist | ✅ OK (Count = popcount, Length = capacity — different semantics) |
| Grid2D | `Size` | `Length` alias | ⚠️ Add alias |
| BloomFilter | `Length` + `Count` alias | Both exist | ✅ OK |

**Note:** After investigation, BitSet actually has BOTH `Count` and `Length` already with different semantics (Count = number of set bits, Length = total capacity). This is correct — document the distinction clearly.

### 1.11 Existing Naming Strengths (No Changes Needed)

Verified as consistent — do NOT change:

- **Constructor**: All use `rt_<name>_new()` / `.New()` ✅
- **Query**: All use `.Has()` (never Contains/Exists) ✅
- **Conversions**: `.ToSeq()`, `.ToList()`, `.ToSet()` — PascalCase ✅
- **Method/Property case**: All PascalCase ✅
- **Clone naming**: All use `Clone()` ✅
- **Clear naming**: All use `Clear()` (except UnionFind `Reset()`) ✅
- **IsEmpty**: Universal ✅
- **Boxing**: `Box.I64()`, `Box.F64()`, `Box.Str()`, `Box.I1()` + `Unbox.*` ✅
- **Error handling**: NULL return on alloc failure, -1 for not-found, trap on OOB ✅
- **Existing enums**: All use `_t` suffix, UPPER_SNAKE_CASE ✅
- **Boolean properties**: `IsEmpty`, `IsFull`, `IsOpen` — "Is" prefix standard ✅

---

## Part 2: Implementation Plan

### Phase 1: Documentation — `Len`→`Length` Property Fix (40+ occurrences)
**Priority: CRITICAL | Treats doc mismatches as bugs**

Replace `| \`Len\`` → `| \`Length\`` in ALL property tables and update `.Len` references in code examples and notes.

**Files to modify (10 files):**

| File | Changes |
|------|---------|
| `docs/viperlib/collections/maps-sets.md` | 7 property table entries |
| `docs/viperlib/collections/multi-maps.md` | 8 property table entries + 2 text references |
| `docs/viperlib/collections/sequential.md` | 6 property table entries + 1 note (line 28) |
| `docs/viperlib/collections/specialized.md` | 4 property table entries |
| `docs/viperlib/collections/functional.md` | 1 property table entry |
| `docs/viperlib/text/patterns.md` | 1 property table entry |
| `docs/viperlib/io/streams.md` | 3 property table entries + 2 notes (lines 271, 712) |
| `docs/viperlib/threads.md` | 3 property table entries |
| `docs/release_notes/Viper_Release_Notes_0_2_3.md` | 5 text references (lines 156-159, 715, 811, 813-814) |
| `docs/viperlib/collections/sequential.md` | Fix note: "`Count` is available as an alias for `Length`" |

### Phase 2: Documentation — Method Verb Fixes (60+ occurrences)
**Priority: CRITICAL**

**2A: Bag `Put`→`Add`, `Drop`→`Remove` in docs**

| File | Changes |
|------|---------|
| `docs/viperlib/collections/specialized.md` | Method table lines 33-34: rename `Put`→`Add`, `Drop`→`Remove` |
| `docs/viperlib/collections/specialized.md` | Code examples ~20 references: `.Put(` → `.Add(`, `.Drop(` → `.Remove(` |
| `docs/bible/part2-building-blocks/13-stdlib.md` | Lines 727-729: `tags.Put(...)` → `tags.Add(...)` |
| `docs/bible/part2-building-blocks/13-stdlib.md` | Line 734: `tags.Drop(...)` → `tags.Remove(...)` |

**2B: SortedSet `Put`→`Add`, `Drop`→`Remove` in docs**

| File | Changes |
|------|---------|
| `docs/viperlib/collections/maps-sets.md` | Method table lines 444-445: rename |
| `docs/viperlib/collections/maps-sets.md` | Code examples lines 475-534: ~12 `.Put(` → `.Add(` |

**2C: Trie `Put`→`Set` in docs**

| File | Changes |
|------|---------|
| `docs/viperlib/collections/specialized.md` | Method table line 276: `Put(key, value)` → `Set(key, value)` |
| `docs/viperlib/collections/specialized.md` | Code examples lines 307-311, 346-350, 379: ~10 `t.Put(` → `t.Set(` |

**DO NOT change:** BiMap/MultiMap/LruCache/CountMap `.Put()`, `Seq.Drop(n)`, `Http.Put()`.

### Phase 3: Documentation — Missing Method Tables (40+ methods)
**Priority: HIGH**

Add method rows and brief descriptions for undocumented methods that exist in runtime.def:

| File | Collection | Methods to Add |
|------|-----------|----------------|
| `docs/viperlib/collections/sequential.md` | List | `Shuffle()`, `Clone()`, `ToSeq()`, `ToSet()`, `ToStack()`, `ToQueue()` |
| `docs/viperlib/collections/sequential.md` | Queue | `ToList()`, `ToSeq()` |
| `docs/viperlib/collections/sequential.md` | Stack | `ToList()`, `ToSeq()` |
| `docs/viperlib/collections/sequential.md` | Deque | `ToSeq()`, `ToList()` |
| `docs/viperlib/collections/sequential.md` | Ring | `ToSeq()` |
| `docs/viperlib/collections/maps-sets.md` | Set | `ToSeq()`, `ToList()` |
| `docs/viperlib/collections/specialized.md` | Bag | `ToSeq()`, `ToSet()` |
| `docs/viperlib/collections/functional.md` | Seq | `GetStr()`, `ToList()`, `ToSet()`, `ToStack()`, `ToQueue()`, `ToDeque()`, `ToBag()` |

### Phase 4: Test Canonicalization (18 references)
**Priority: HIGH**

**4A: BASIC/Zia test method name updates**

| File | Changes |
|------|---------|
| `tests/runtime_sweep/basic/collections.bas` | 6 refs: `bag.Put`→`bag.Add`, `bag.Drop`→`bag.Remove` |
| `tests/runtime_sweep/basic/edge_collections.bas` | 2 refs: `bag.Drop`→`bag.Remove`, `bag.Put`→`bag.Add` |
| `tests/rt_api/test_map_bag.bas` | 5 refs: `b.Put`→`b.Add`, `b.Drop`→`b.Remove` |
| `tests/rt_api/test_map_bag.zia` | 5 refs: `b.Put`→`b.Add`, `b.Drop`→`b.Remove` |
| `tests/rt_api/test_treemap_trie.bas` | 1 ref: `tm.Drop`→`tm.Remove` |
| `tests/rt_api/test_treemap_trie.zia` | 1 ref: `tm.Drop`→`tm.Remove` |

**DO NOT change:** `bm.Put(...)` (BiMap — canonical), `lru.Put(...)` (LruCache — canonical), `mm.Put(...)` (MultiMap — canonical), `s.Drop(1)` (Seq.Drop — correct functional method).

**4B: C++ test function names**

| File | Changes |
|------|---------|
| `src/tests/runtime/RTBagTests.cpp` | `test_bag_put_has`→`test_bag_add_has`, `test_bag_drop`→`test_bag_remove` |
| `src/tests/runtime/RTSortedSetTests.cpp` | `test_sortedset_put_has`→`test_sortedset_add_has` |

**4C: Verify golden files** — These test output should not change since aliases map to the same function. Verify with `ctest` after changes.

### Phase 5: `#pragma once` Standardization (36 headers)
**Priority: HIGH**

For each of the 36 headers using `#ifndef` guards:
1. Replace the `#ifndef VIPER_*_H` / `#define VIPER_*_H` / `#endif` pattern with `#pragma once`
2. Keep all other content unchanged

**Files:** See Section 1.7 for complete list.

### Phase 6: New Enum Types (5 enums, Tier 1)
**Priority: HIGH**

**6A: Playlist repeat mode — `src/runtime/audio/rt_playlist.h`**
```c
typedef enum { RT_REPEAT_NONE = 0, RT_REPEAT_ALL = 1, RT_REPEAT_ONE = 2 } rt_playlist_repeat_t;
```
Update `rt_playlist.c` to use enum names in comparisons. Function signatures stay `int64_t` for ABI stability.

**6B: Waveform type — `src/runtime/audio/rt_synth.h`**
```c
typedef enum { RT_WAVE_SINE = 0, RT_WAVE_SQUARE = 1, RT_WAVE_SAW = 2, RT_WAVE_TRIANGLE = 3 } rt_waveform_t;
```

**6C: SFX preset — `src/runtime/audio/rt_synth.h`**
```c
typedef enum { RT_SFX_JUMP = 0, RT_SFX_COIN = 1, RT_SFX_HIT = 2, RT_SFX_EXPLOSION = 3, RT_SFX_POWERUP = 4, RT_SFX_LASER = 5 } rt_sfx_preset_t;
```

**6D: Image scale mode — `src/runtime/graphics/rt_gui.h`**
```c
typedef enum { RT_SCALE_NONE = 0, RT_SCALE_FIT = 1, RT_SCALE_FILL = 2, RT_SCALE_STRETCH = 3 } rt_image_scale_t;
```

**6E: Tilemap collision — `src/runtime/graphics/rt_tilemap.h`**
```c
typedef enum { RT_COLLISION_NONE = 0, RT_COLLISION_SOLID = 1, RT_COLLISION_ONE_WAY_UP = 2 } rt_tilemap_collision_t;
```

### Phase 7: Tier 2 Enum Types (2 enums)
**Priority: MEDIUM**

**7A: Button style — `src/runtime/graphics/rt_gui.h`**
```c
typedef enum { RT_BTN_DEFAULT = 0, RT_BTN_PRIMARY = 1, RT_BTN_SECONDARY = 2, RT_BTN_DANGER = 3, RT_BTN_TEXT = 4 } rt_button_style_t;
```

**7B: Icon position — `src/runtime/graphics/rt_gui.h`**
```c
typedef enum { RT_ICON_LEFT = 0, RT_ICON_RIGHT = 1 } rt_icon_pos_t;
```

### Phase 8: Missing Collection Operations — Code Changes
**Priority: MEDIUM | ~120 LOC across 8 files**

**8A: Bag Clone**

| File | Change |
|------|--------|
| `src/runtime/collections/rt_bag.h` | Declare `void *rt_bag_clone(void *bag);` |
| `src/runtime/collections/rt_bag.c` | Implement (iterate entries, insert into new bag) |
| `src/il/runtime/runtime.def` | Add `RT_FUNC(BagClone, rt_bag_clone, "Viper.Collections.Bag.Clone", "obj(obj)")` |
| `src/il/runtime/RuntimeSignatures.cpp` or signatures file | Add signature |

**8B: Trie Clone**

| File | Change |
|------|--------|
| `src/runtime/collections/rt_trie.h` | Declare `void *rt_trie_clone(void *trie);` |
| `src/runtime/collections/rt_trie.c` | Implement (recursive node copy or iterate keys and re-insert) |
| `src/il/runtime/runtime.def` | Add `RT_FUNC(TrieClone, rt_trie_clone, "Viper.Collections.Trie.Clone", "obj(obj)")` |
| `src/il/runtime/RuntimeSignatures.cpp` or signatures file | Add signature |

**8C: UnionFind Clear alias**

| File | Change |
|------|--------|
| `src/il/runtime/runtime.def` | Add `RT_ALIAS("Viper.Collections.UnionFind.Clear", UnionFindReset)` |

### Phase 9: Property Naming Normalization — runtime.def
**Priority: MEDIUM**

**9A: Grid2D — Add `Length` alias**

| File | Change |
|------|--------|
| `src/il/runtime/runtime.def` | Add `RT_ALIAS("Viper.Collections.Grid2D.get_Length", Grid2DSize)` |

### Phase 10: Cross-Reference & Release Notes
**Priority: LOW**

| File | Change |
|------|--------|
| `docs/runtime-review.md` | Update stale function name references |
| `docs/codemap/runtime-library-c.md` | Verify function names match current state |
| `bugs/runtime_audit_20260212.md` | Update stale references |

---

## Part 3: Execution Order

| Phase | Description | Files | Est. Changes | Risk |
|-------|-------------|-------|-------------|------|
| **1** | Doc `Len`→`Length` property fix | 10 doc files | ~45 replacements | Very low — docs only |
| **2** | Doc method verb fixes (Bag/SortedSet/Trie) | 4 doc files | ~60 replacements | Very low — docs only |
| **3** | Doc missing method tables | 4 doc files | ~40 rows added | Low — docs only |
| **4** | Test canonicalization | 8 test files | ~20 replacements | Medium — tests must pass |
| **5** | `#pragma once` standardization | 36 header files | 36 files | Low — mechanical |
| **6** | Tier 1 enum types | 3 headers + impls | ~40 LOC | Low — additive |
| **7** | Tier 2 enum types | 1 header | ~15 LOC | Low — additive |
| **8** | Missing collection ops | 6 files | ~120 LOC | Medium — new C code |
| **9** | Property naming normalization | 1 file | ~2 LOC | Very low |
| **10** | Cross-references & release notes | 3 files | ~20 LOC | Low |

**Total: ~65 files, ~400+ individual changes**

---

## Part 4: Verification

After each phase:

1. `./scripts/build_viper.sh` — full build with zero warnings
2. `ctest --test-dir build --output-on-failure` — all tests pass

Phase-specific checks:

- **Phase 1**: `grep -rn '`Len`' docs/viperlib/ docs/release_notes/` — should return 0 hits (except `Length` and legitimate uses like `Len` inside code identifiers)
- **Phase 2**: `grep -rn '\.Put(' docs/viperlib/collections/specialized.md docs/viperlib/collections/maps-sets.md docs/bible/` — should only return BiMap/MultiMap/LruCache/Http references
- **Phase 3**: Manual review — each collection's doc method table matches runtime.def entries
- **Phase 4**: Run specific test binaries — verify all renamed test functions execute
- **Phase 5**: `grep -rn '#ifndef.*_H' src/runtime/**/*.h` — should return 0 hits
- **Phase 6-7**: Grep headers for new enum types — confirm typedef and constants present
- **Phase 8**: `./scripts/check_runtime_completeness.sh` — all RT_FUNC have implementations
- **Phase 9**: Grep runtime.def for `Grid2D.*Length` — confirm alias present
- **Phase 10**: Manual review of cross-references

---

## Part 5: What NOT to Change

Explicitly verified as correct — do NOT modify:

1. **BiMap/MultiMap/LruCache/CountMap `.Put()`** — correct verb for key-value insertion
2. **`Seq.Drop(n)`** — correct functional method (drop first N elements)
3. **`Http.Put()`** — correct HTTP verb
4. **C function compounding** (`rt_bitset_*` not `rt_bit_set_*`) — consistent
5. **`Cap` vs `Capacity` split** — intentional by usage context
6. **`rt_string_builder.h` include guard style** — might keep `#ifndef` since it's a stack-allocatable struct
7. **`Bytes.Copy()` method** — different operation from `Clone()`
8. **`MultiMap.KeyCount`** — semantically distinct from `.Length`
9. **Existing RT_ALIAS backward-compat entries** — keep for migration period
10. **`ButtonGroup.Count`** — already has `Length` alias
11. **BitSet `Count` vs `Length`** — different semantics (popcount vs capacity)
12. **`bm.Put()`/`lru.Put()`/`mm.Put()`** in test files — canonical names
