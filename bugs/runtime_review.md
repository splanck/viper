# Viper Runtime API Review

**Date:** 2026-01-18
**Reviewer:** Claude (Opus 4.5)
**Scope:** `Viper.*` namespace API - classes, methods, and functionality

---

## Executive Summary

The Viper runtime provides a comprehensive C-based API for programs compiled to IL. It includes collections, I/O, threading, graphics, networking, cryptography, and input handling. The codebase demonstrates solid engineering with proper reference counting, bounds checking, and deterministic behavior.

This review focuses on the **public API** exposed to Viper programs - identifying missing functionality, API design issues, and areas for improvement compared to modern runtime libraries like .NET, Java, and Python.

---

## Table of Contents

1. [Current API Overview](#1-current-api-overview)
2. [Missing Functionality](#2-missing-functionality)
3. [API Design Issues](#3-api-design-issues)
4. [Incomplete Implementations](#4-incomplete-implementations)
5. [Priority Matrix](#5-priority-matrix)
6. [Recommendations](#6-recommendations)

---

## 1. Current API Overview

### What Works Well

| Namespace | Classes/Functions | Status |
|-----------|-------------------|--------|
| **Viper.String** | String operations (concat, slice, trim, case, split, join) | Complete |
| **Viper.Collections** | List, Seq, Map, TreeMap, Stack, Queue, Bag, Ring, Heap, Bytes | Complete |
| **Viper.Math** | Trigonometry, logarithms, rounding, clamp, lerp | Complete |
| **Viper.Bits** | AND, OR, XOR, shifts, rotates, popcount, leading/trailing zeros | Complete |
| **Viper.Vec2/Vec3** | Vector arithmetic, dot, cross, normalize, lerp | Complete |
| **Viper.DateTime** | Unix timestamps, formatting, decomposition | Complete |
| **Viper.Random** | Deterministic PRNG (LCG-based) | Complete |
| **Viper.Parse** | Safe parsing with Try/Or patterns | Complete |
| **Viper.Hash** | MD5, SHA1, SHA256, CRC32, HMAC variants | Complete |
| **Viper.Crypto** | AES-128/256-CBC, PBKDF2 | Complete |
| **Viper.Pattern** | Regex match, find, replace, split | Complete |
| **Viper.IO** | File, Dir, Path, BinFile, LineReader/Writer, MemStream | Complete |
| **Viper.Compress** | DEFLATE/GZIP | Complete |
| **Viper.Archive** | ZIP support | Complete |
| **Viper.Text** | Codec, CSV, JSON, Template, StringBuilder | Complete |
| **Viper.Graphics** | Canvas, Color, Pixels, Sprite, Tilemap, Camera | Complete |
| **Viper.Input** | Keyboard, Mouse, Gamepad (4 controllers) | Complete |
| **Viper.Threads** | Thread, Monitor, Gate, Barrier, RwLock, SafeI64 | Complete |
| **Viper.Network** | TCP, UDP, DNS, HTTP client | Complete |
| **Viper.Terminal** | ANSI terminal I/O | Complete |
| **Viper.Log** | Leveled logging | Complete |
| **Viper.Exec** | Process execution | Complete |

---

## 2. Missing Functionality

### Critical Priority

| Missing Feature | Description | Use Case | Status |
|-----------------|-------------|----------|--------|
| **JSON Support** | No JSON parsing or serialization | Config files, REST APIs, data exchange | **IMPLEMENTED** |
| **Cycle Detection** | Reference counting cannot detect cycles | Memory safety for complex object graphs | **DOCUMENTED** |
| **Collection Sorting** | No Sort() method on Seq, List | Data ordering, binary search prep | **IMPLEMENTED** |
| **HTTPS/TLS** | HTTP client lacks SSL/TLS support | Secure web communication | Pending |

### High Priority

| Missing Feature | Description | Use Case |
|-----------------|-------------|----------|
| **Collection Filtering** | No Where/Filter/Any/All predicates | Data querying, conditional processing |
| **Collection Mapping** | No Map/Select transformation | Functional data transformation |
| **Iterator Pattern** | find_all() returns Seq, no lazy iteration | Memory-efficient processing of large data |
| **Async/Await** | No coroutines or async primitives | Non-blocking I/O, responsive apps |
| **Thread Pools** | Only raw Thread creation | Efficient task scheduling |
| **Channels** | No message-passing between threads | Safe concurrent communication |

### Medium Priority

| Missing Feature | Description | Use Case |
|-----------------|-------------|----------|
| **Set Collection** | Bag exists but no HashSet with proper set ops | Deduplication, membership testing |
| **Priority Queue API** | Heap exists internally but not exposed cleanly | Task scheduling, Dijkstra |
| **Decimal Type** | Only f64 for floating-point | Financial calculations |
| **BigInteger** | No arbitrary precision integers | Cryptography, large numbers |
| **Complex Numbers** | No complex arithmetic | Scientific computing, signal processing |
| **Random Distributions** | Only uniform random | Simulations, statistical modeling |
| **Memory-Mapped Files** | Standard I/O only | Large file processing |
| **File Watching** | Dir watcher exists but incomplete | Live reload, file sync |
| **Unicode Normalization** | No NFC/NFD normalization | Correct string comparison |
| **Locale Support** | No locale-aware operations | Internationalization |

### Low Priority

| Missing Feature | Description | Use Case |
|-----------------|-------------|----------|
| **XML Support** | No XML parser/serializer | Legacy data formats |
| **Database Bindings** | No SQLite or other DB access | Data persistence |
| **WebSocket Client** | HTTP exists but no WebSocket | Real-time communication |
| **Audio Playback** | Graphics/Input exist but limited audio | Games, multimedia |
| **Public-Key Crypto** | Only symmetric (AES); no RSA/ECDSA | Digital signatures, key exchange |
| **3D Graphics** | 2D only; no OpenGL/Vulkan | 3D games, visualization |
| **Image Processing** | Pixel buffer but no filters/transforms | Image manipulation |
| **Reflection** | No runtime type inspection | Debugging, serialization |
| **Structured Exceptions** | Only trap mechanism | Recoverable error handling |

---

## 3. API Design Issues

### Naming Inconsistencies

| Issue | Example | Recommendation |
|-------|---------|----------------|
| **Array prefix varies** | `rt_arr_i32_*` vs `rt_array_i32_*` | Standardize to one prefix |
| **File API mixing** | `rt_file_*` vs `rt_*_ch_*` (channel-based) | Separate clean APIs |
| **Hash naming** | `rt_hash_*` but `rt_hash_hmac_*` | Consider `rt_hmac_*` |
| **Empty checks vary** | `rt_seq_is_empty()` vs `rt_str_is_empty()` | Use consistent `is_` prefix |
| **Pattern vs Regex** | `rt_pattern_*` naming differs from industry | Match common conventions |

### API Ergonomics

| Issue | Description | Impact |
|-------|-------------|--------|
| **No fluent builders** | Map/TreeMap require repeated insert calls | Verbose initialization code |
| **Missing overloads** | `rt_file_write(file, data, len, ...)` has no string variant | Manual length calculation |
| **Indexing inconsistency** | `rt_instr3()` is 1-based, most APIs are 0-based | Confusion for developers |
| **No string length function** | Must use `rt_len()` not `rt_string_len()` | Inconsistent with `rt_arr_*_len()` |
| **TreeMap iteration** | keys()/values() copy to Seq, no in-place iteration | Memory overhead |

### Ownership & Lifetime

| Issue | Description | Risk |
|-------|-------------|------|
| **Retain/release patterns** | Manual reference counting is error-prone | Memory leaks, use-after-free |
| **Consume vs borrow unclear** | `rt_concat()` consumes both; `rt_str_eq()` doesn't | Ownership confusion |
| **No weak reference docs** | `rt_weak_store/load` exist but usage unclear | Feature effectively unusable |
| **Different return semantics** | `rt_list_get_item()` returns retained copy; `rt_map_get()` doesn't | Inconsistent ownership |

### Error Handling

| Issue | Description | Recommendation |
|-------|-------------|----------------|
| **Mixed patterns** | File I/O uses out-params; arrays trap on bounds error | Standardize approach |
| **No Try variants** | Regex traps on invalid pattern | Add `rt_pattern_try_*()` |
| **Silent clamping** | `rt_pad_vibrate()` clamps 0-1 silently | Return clamped value or bool |
| **No stack traces** | Trap mechanism provides no backtrace | Add debug info to traps |

### Missing Generics

| Issue | Description | Impact |
|-------|-------------|--------|
| **Specialized arrays** | Separate i32/i64/f64/str/obj array types | Code duplication |
| **String-only Map keys** | Map/TreeMap only accept string keys | Can't use objects as keys |
| **void* collections** | List, Seq store raw pointers | No compile-time type safety |
| **No container nesting** | List<List<int>> requires manual boxing | Awkward nested data structures |

---

## 4. Incomplete Implementations

### Graphics Features

| Feature | Location | Status |
|---------|----------|--------|
| Sprite Rotation | `rt_sprite.c:356` | `// TODO: Implement rotation` |
| Canvas Clipping | `rt_graphics.c:1886-1896` | Stub functions |
| Canvas Title Setting | `rt_graphics.c:1902` | Stub function |
| Fullscreen Toggle | `rt_graphics.c:1925-1931` | Stub functions |

### Memory Management

| Feature | Location | Issue |
|---------|----------|-------|
| Refcount Overflow | `rt_heap.c:171-177` | Only checked in debug builds |
| Pool Allocator | `rt_heap.c:120-130` | Only used for strings, not objects |
| Immortal Strings | `rt_string_encode.c:112` | Workaround hack, not proper fix |

### Threading

| Feature | Description | Status |
|---------|-------------|--------|
| Thread-Local Random | Random state is global | Not per-thread |
| Concurrent Collections | No thread-safe variants | Requires external Monitor |

---

## 5. Priority Matrix

### Critical (Fix This Quarter)

| Item | Category | Effort | Impact | Status |
|------|----------|--------|--------|--------|
| JSON Parser/Serializer | Missing Feature | Medium | Very High | **DONE** |
| Collection Sorting | Missing Feature | Low | High | **DONE** |
| Refcount Overflow Check | Bug | Low | Critical | **DONE** |
| Cycle Detection/Documentation | Design | Medium | High | **DONE** |

### High (Fix Next Quarter)

| Item | Category | Effort | Impact |
|------|----------|--------|--------|
| HTTPS/TLS Support | Missing Feature | High | High |
| Collection Filter/Map | Missing Feature | Medium | High |
| Sprite Rotation | Incomplete | Low | Medium |
| Canvas Clipping | Incomplete | Low | Medium |
| Iterator Pattern | Missing Feature | Medium | Medium |
| Async/Await Primitives | Missing Feature | High | Medium |

### Medium (Plan for This Year)

| Item | Category | Effort | Impact |
|------|----------|--------|--------|
| Thread Pool | Missing Feature | Medium | Medium |
| Channels | Missing Feature | Medium | Medium |
| Set Collection | Missing Feature | Low | Medium |
| Decimal Type | Missing Feature | Medium | Medium |
| Random Distributions | Missing Feature | Low | Low |
| API Naming Cleanup | Design | High | Medium |
| Error Handling Standardization | Design | High | Medium |

### Low (Nice to Have)

| Item | Category | Effort | Impact |
|------|----------|--------|--------|
| XML Support | Missing Feature | Medium | Low |
| Database Bindings | Missing Feature | High | Low |
| WebSocket Client | Missing Feature | Medium | Low |
| Audio Improvements | Missing Feature | Medium | Low |
| Public-Key Crypto | Missing Feature | High | Low |
| 3D Graphics | Missing Feature | Very High | Low |
| Reflection | Missing Feature | High | Low |
| Unicode Normalization | Missing Feature | Medium | Low |
| Locale/i18n | Missing Feature | High | Low |

---

## 6. Recommendations

### Immediate Actions (COMPLETED 2026-01-18)

1. ~~**Add JSON Support**~~ - Implemented `Viper.Text.Json` with Parse, Format, FormatPretty, IsValid, TypeOf
2. ~~**Add Collection Sorting**~~ - Implemented `Seq.Sort()` and `Seq.SortDesc()` with stable merge sort
3. ~~**Fix Refcount Overflow**~~ - Enabled overflow check in all builds (rt_heap.c)
4. ~~**Document Cycle Limitations**~~ - Added comprehensive section to docs/devdocs/lifetime.md

### Short-Term Improvements

1. **Complete Graphics Stubs** - Implement sprite rotation and canvas clipping
2. **Add Filter/Map to Collections** - Enable functional programming patterns
3. **Standardize Error Handling** - Use consistent out-param or Try pattern
4. **Add HTTPS** - Integrate TLS library for secure HTTP

### Long-Term Architectural Changes

1. **Consider GC** - For cycle detection, evaluate mark-sweep as supplement to refcount
2. **Add Async Support** - Design coroutine or async/await primitives
3. **Improve Type Safety** - Consider adding generic collections or type tags
4. **API Naming Audit** - Standardize prefixes and naming conventions

---

## Appendix A: API Summary by Namespace

### Viper.String
- Creation: `from_bytes`, `from_lit`, `empty`
- Manipulation: `concat`, `substr`, `mid`, `left`, `right`, `trim`, `ucase`, `lcase`
- Search: `instr`, `index_of`, `has`, `starts_with`, `ends_with`, `count`
- Comparison: `eq`, `lt`, `cmp`, `cmp_nocase`
- Conversion: `to_int`, `to_double`, `chr`, `asc`
- Formatting: `pad_left`, `pad_right`, `repeat`, `replace`, `split`, `join`

### Viper.Collections.Seq
- Lifecycle: `new`, `with_capacity`, `clone`
- Access: `get`, `set`, `first`, `last`, `peek`
- Mutation: `push`, `pop`, `insert`, `remove`, `clear`
- Search: `find`, `has`, `reverse`, `shuffle`
- Sorting: `Sort` (ascending), `SortDesc` (descending) - stable merge sort
- Bulk: `slice`, `push_all`

### Viper.Collections.Map
- Lifecycle: `new`
- Access: `get`, `set`, `get_or`, `set_if_missing`
- Query: `has`, `len`, `is_empty`
- Mutation: `remove`, `clear`
- Bulk: `keys`, `values`

### Viper.Collections.TreeMap
- Same as Map plus: `first`, `last`, `floor`, `ceil`

### Viper.Math
- Basic: `abs`, `sgn`, `min`, `max`, `clamp`
- Trigonometry: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`
- Hyperbolic: `sinh`, `cosh`, `tanh`
- Exponential: `exp`, `log`, `log10`, `log2`, `sqrt`, `hypot`
- Rounding: `floor`, `ceil`, `round`, `trunc`, `fmod`
- Utilities: `lerp`, `wrap`, `deg`, `rad`
- Constants: `pi`, `e`, `tau`

### Viper.Vec2/Vec3
- Creation: `new`, `zero`, `one`
- Components: `x`, `y`, `z`
- Arithmetic: `add`, `sub`, `mul`, `div`
- Products: `dot`, `cross`
- Length: `len`, `len_sq`, `norm`, `dist`
- Interpolation: `lerp`
- Rotation: `angle`, `rotate` (Vec2 only)

### Viper.Hash
- Algorithms: `md5`, `sha1`, `sha256`, `crc32`
- HMAC: `hmac_md5`, `hmac_sha1`, `hmac_sha256`
- Input: String and Bytes variants for each

### Viper.Text.Json
- Parsing: `Parse` (any JSON value), `ParseObject` (object only), `ParseArray` (array only)
- Formatting: `Format` (compact), `FormatPretty` (indented)
- Validation: `IsValid` (check syntax without allocating)
- Inspection: `TypeOf` (returns "null", "bool", "number", "string", "array", "object")
- Type mapping: objects→Map, arrays→Seq, strings→String, numbers→Box.F64, bools→Box.I64

### Viper.Pattern (Regex)
- Matching: `is_match`, `find`, `find_from`, `find_pos`, `find_all`
- Replacement: `replace`, `replace_first`
- Splitting: `split`
- Utility: `escape`

### Viper.DateTime
- Current: `now`, `now_ms`
- Components: `year`, `month`, `day`, `hour`, `minute`, `second`, `day_of_week`
- Formatting: `format`, `to_iso`
- Construction: `create`, `add_seconds`, `add_days`, `diff`

### Viper.Input.Keyboard
- State: `is_down`, `is_up`, `was_pressed`, `was_released`
- Queries: `get_down`, `any_down`, `get_pressed`, `get_released`, `get_text`
- Modifiers: `shift`, `ctrl`, `alt`, `caps_lock`
- 40+ key-specific getters

### Viper.Input.Mouse
- Position: `x`, `y`, `delta_x`, `delta_y`
- Buttons: `is_down`, `is_up`, `was_pressed`, `was_released`
- Clicks: `was_clicked`, `was_double_clicked`
- Scroll: `wheel_x`, `wheel_y`
- Cursor: `show`, `hide`, `capture`, `release`, `set_pos`

### Viper.Input.Gamepad
- Enumeration: `count`, `is_connected`, `name`
- Buttons: `is_down`, `is_up`, `was_pressed`, `was_released`
- Axes: `left_x`, `left_y`, `right_x`, `right_y`, `left_trigger`, `right_trigger`
- Deadzone: `set_deadzone`, `get_deadzone`
- Rumble: `vibrate`, `stop_vibration`

---

*End of Review*
