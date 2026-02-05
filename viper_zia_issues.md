# Zia Runtime Binding Issues

Issues discovered while adding Zia examples to Viper Library documentation.
Root cause analysis performed by investigating runtime.def, ZiaRuntimeExterns.inc,
RuntimeClasses.inc, RuntimeComponents.hpp, and the Zia frontend source.

---

## Root Cause A: Stale Source .inc Files — FIXED

**The single largest root cause.** The source `.inc` files committed to the repo were
significantly out of date compared to the generated versions in `build/generated/`.

**Fix applied:** Copied generated files over source files:
- `src/il/runtime/ZiaRuntimeExterns.inc` (2298 → 2802 lines)
- `src/il/runtime/RuntimeClasses.inc` (2659 → 3409 lines)

### Issues fixed:

- **7. `Viper.Collections.Deque`** — FIXED (also needed new RT_FUNC entries, see below).
- **8. `Viper.Collections.SortedSet`** — FIXED (also needed new RT_FUNC entries, see below).
- **9. `Viper.Collections.WeakMap`** — FIXED.
- **17. `Viper.Text.JsonPath`** — FIXED.
- **27. `Viper.Threads.CancellationToken`** — FIXED.
- **28. `Viper.Threads.Debouncer`** — FIXED.
- **29. `Viper.Threads.Throttler`** — FIXED.
- **30. `Viper.Threads.Scheduler`** — FIXED.
- **31. `Viper.Threads.Promise`** — FIXED.
- **34. `Viper.Network.RestClient`** — FIXED.
- **35. `Viper.Network.RetryPolicy`** — FIXED.
- **36. `Viper.Network.RateLimiter`** — FIXED.
- **37. `Viper.Graphics.Scene`** — FIXED.
- **38. `Viper.Graphics.SceneNode`** — FIXED.
- **39. `Viper.Graphics.SpriteBatch`** — FIXED.

---

## Root Cause B: Instance Method Dispatch Bug in Zia Frontend — FIXED

The Zia frontend failed to resolve instance method calls on certain runtime class objects
when type information was lost. Instead of emitting direct extern calls, it emitted
`call.indirect 0` which crashed with "Null indirect callee" at runtime.

**Root cause:** When `Set.New()` returns `runtimeClass("Viper.Collections.Set")`, the sema
at lines 880-883 of Sema_Expr.cpp converts this to `types::set(types::unknown())` (changing
TypeKindSem from Ptr to Set). This loses the "Viper.Collections.Set" name, so the runtime
class method handler at line 985 (`baseType->name.find("Viper.") == 0`) doesn't match.

**Fix applied (multi-part):**
1. Added runtime class method fallback in Sema_Expr.cpp: after Set/List/Map built-in method
   tables fail to match, maps the semantic type kind back to the runtime class name and
   looks up the method via `lookupSymbol()`. This handles runtime-style methods (get_Len,
   Put, Clear, etc.).
2. Added `lowerSetMethodCall()` in Lowerer_Expr_Call.cpp: mirrors the existing
   `lowerListMethodCall`/`lowerMapMethodCall` pattern, handling Zia-friendly Set method
   aliases (count, has, add, remove, clear).
3. Fixed self-parameter push in Lowerer_Expr_Call.cpp: the runtimeCallee handler now
   recognizes Set/List/Map type kinds (not just Ptr types with "Viper." names).
4. Added Set runtime name aliases to RuntimeNames.hpp (kSetCount, kSetHas, kSetPut,
   kSetDrop, kSetClear).
5. Fixed Set.Common/Diff/Merge return types from `types::ptr()` to
   `types::runtimeClass("Viper.Collections.Set")`, and Set.Items to
   `types::runtimeClass("Viper.Collections.Seq")` in ZiaRuntimeExterns.inc.

### Issues fixed (return type corrections):

- **1. `Viper.IO.Stream`** — FIXED. Changed `OpenFile`/`OpenMemory`/`OpenBytes` return
  types from `types::ptr()` to `types::runtimeClass("Viper.IO.Stream")`. Also fixed
  `AsBinFile` → `runtimeClass("Viper.IO.BinFile")`, `AsMemStream` →
  `runtimeClass("Viper.IO.MemStream")`, `ToBytes` → `runtimeClass("Viper.Collections.Bytes")`.
  Stream class also added via .inc file update (Root Cause A).

- **3. `Viper.Collections.Set`** — FIXED. Instance method dispatch now works for all
  Set methods. `s.get_Len()`, `s.Put()`, `s.Clear()`, `s.count`, etc. all work correctly.

- **4. `Viper.Collections.Bag.Merge/Common/Diff`** — FIXED. Changed return types from
  `types::ptr()` to `types::runtimeClass("Viper.Collections.Bag")`. Also fixed `Items`
  to return `types::runtimeClass("Viper.Collections.Seq")`.

- **5. `Viper.Text.Csv`** — FIXED. Changed `ParseLine`/`ParseLineWith`/`Parse`/`ParseWith`
  return types from `types::runtimeClass("Viper.Text.Csv")` to
  `types::runtimeClass("Viper.Collections.Seq")` (CSV parse functions return Seq objects).

---

## Root Cause C: Constructor Naming — FIXED

**6. `Viper.IO.Watcher`** — FIXED.
- Changed `"Viper.IO.Watcher.new"` to `"Viper.IO.Watcher.New"` in runtime.def.
- Changed return type from `types::ptr()` to `types::runtimeClass("Viper.IO.Watcher")`
  in ZiaRuntimeExterns.inc.

---

## Root Cause D: Type Signature Errors in runtime.def — FIXED

Changed return types from `i64` to `i1` (bool) in runtime.def and from
`types::integer()` to `types::boolean()` in ZiaRuntimeExterns.inc.

- **15. `Viper.Box.EqI64/EqF64/EqStr`** — FIXED (runtime.def + ZiaRuntimeExterns.inc).
- **40. `Viper.GUI.Checkbox.IsChecked()`** — FIXED (runtime.def RT_FUNC + RT_METHOD + ZiaRuntimeExterns.inc).
- **41. `Viper.GUI.CodeEditor.IsModified()`** — FIXED (runtime.def RT_FUNC + RT_METHOD + ZiaRuntimeExterns.inc).
- **Bonus: `Viper.GUI.MenuItem.IsChecked()`** — FIXED (same pattern, found during fix).

---

## Root Cause E: Missing typeRegistry_ Entries — FIXED

- **32. `Viper.Threads.Channel`** — FIXED. Added `typeRegistry_["Viper.Threads.Channel"]`
  to ZiaRuntimeExterns.inc.
- **33. `Viper.Threads.Pool`** — FIXED. Added `typeRegistry_["Viper.Threads.Pool"]`
  to ZiaRuntimeExterns.inc.

---

## Root Cause F: Intentional Design (Not Bugs)

**10. `Viper.Diagnostics`** — NOT A BUG. By design.
- Diagnostics is a static utility namespace, not a class.
- **Workaround:** Use `bind Viper.Diagnostics;` (no alias) and call with full path.

**11. `Viper.Box`** — NOT A BUG. By design.
- Box is a static utility for boxing/unboxing primitives. No instances.
- **Workaround:** Use `bind Viper.Box;` and call as `Viper.Box.I64(42)`.

**13. `Viper.Math.Vec2 / Vec3`** — NOT A BUG. Documentation/usage issue.
- Properties require explicit getter syntax: `obj.get_X()`.
- **Resolution:** Document the `get_X()` / `set_X()` pattern clearly.

**14. `Viper.Text.StringBuilder`** — NOT A BUG. Documentation issue.
- The property is named `get_Length` not `get_Len`.
- **Resolution:** Use `sb.get_Length()` — document correctly.

---

## Root Cause G: Missing LineWriter.Append in Class Definition — FIXED

**2. `Viper.IO.LineWriter.Append()`** — FIXED.
- Added `RT_METHOD("Append", "obj(str)", LineWriterAppend)` to the LineWriter class body
  in runtime.def.
- Changed return type from `types::ptr()` to `types::runtimeClass("Viper.IO.LineWriter")`
  in ZiaRuntimeExterns.inc.

---

## Root Cause H: Missing Map Generic Type Support — FIXED

**12. `Viper.Collections.Map`** — FIXED.
- Added `TypeKindSem::Map` property resolution in Sema_Expr.cpp (count/size/length/Len).
- Added `TypeKindSem::Map` property lowering in Lowerer_Expr.cpp (emits `kMapCount`).
- Added `TypeKindSem::Map` optional chaining support in both Sema and Lowerer.
- Also added `TypeKindSem::Set` to the same property resolution and optional chaining paths.

---

## Root Cause I: Unimplemented Feature

**16. `Viper.Crypto.Hash.SHA512`** — NOT A BUG. Unimplemented.
- No `RT_FUNC` entry in runtime.def. No C implementation exists.
- Only MD5, SHA1, SHA256 are implemented.

---

## Root Cause J: Documentation Errors

**18. `Viper.Text.Pattern`** — NOT A CODE BUG. Documentation issue.
- Argument order is (pattern, text), not (text, pattern).

**19. `Viper.IO.File`** — NOT A CODE BUG. Documentation issue.
- Actual method names are `ReadAllText`/`WriteAllText`, not `ReadStr`/`WriteStr`.

---

## Root Cause K: ARM64/x86_64 Native Codegen Missing Symbol Mappings — FIXED

Comprehensive update to `src/codegen/common/RuntimeComponents.hpp`:

- Added ~50 missing symbol prefix mappings covering all runtime source files.
- Added `RtComponent::Audio` and `RtComponent::Network` enum values.
- **Refactored:** Consolidated duplicated component resolution logic from
  `cmd_codegen_arm64.cpp` and `CodegenPipeline.cpp` (x86_64) into shared utilities:
  - `resolveRequiredComponents()` — classifies symbols and applies dependency rules.
  - `archiveNameForComponent()` — maps component to library archive name.
- Both backends now use the common functions — single source of truth.

Issues **20-26** and all additional unmapped prefixes — FIXED.

---

## Deque and SortedSet Missing RT_FUNC Entries — FIXED

**7. `Viper.Collections.Deque`** and **8. `Viper.Collections.SortedSet`** had
`RT_CLASS_BEGIN` entries referencing constructor identifiers (`DequeNew`, `SortedSetNew`)
but no corresponding `RT_FUNC` entries existed in runtime.def — missing from both
source AND generated files.

**Fix applied:**
- Added 16 RT_FUNC entries for Deque (New, get_Len, get_Cap, get_IsEmpty, PushFront,
  PopFront, PeekFront, PushBack, PopBack, PeekBack, Get, Set, Clear, Has, Reverse, Clone).
- Added 23 RT_FUNC entries for SortedSet (New, get_Len, get_IsEmpty, Clear, Put, Drop,
  Has, First, Last, Floor, Ceil, Lower, Higher, At, IndexOf, Items, Range, Take, Skip,
  Merge, Common, Diff, IsSubset).
- Added corresponding `defineExternFunction` entries in ZiaRuntimeExterns.inc for both.

---

## Summary

| # | Issue | Status | Root Cause |
|---|-------|--------|------------|
| 1 | Stream null callee | FIXED | A + B (stale .inc + return types) |
| 2 | LineWriter.Append | FIXED | G (missing from class body) |
| 3 | Set null callee | FIXED | B (instance dispatch + lowerer) |
| 4 | Bag Merge/Common/Diff | FIXED | B (return type = ptr) |
| 5 | Csv returns 0 | FIXED | B (return type = ptr) |
| 6 | Watcher lowercase new | FIXED | C (naming) |
| 7 | Deque not constructible | FIXED | A + missing RT_FUNC |
| 8 | SortedSet not constructible | FIXED | A + missing RT_FUNC |
| 9 | WeakMap no New | FIXED | A (stale .inc) |
| 10 | Diagnostics can't alias | n/a | F (by design) |
| 11 | Box can't alias | n/a | F (by design) |
| 12 | Map typed properties | FIXED | H (added Map/Set handling) |
| 13 | Vec2/Vec3 properties | n/a | F (use get_X() syntax) |
| 14 | StringBuilder Len | n/a | F (property is Length) |
| 15 | Box.EqI64 type | FIXED | D (wrong signature) |
| 16 | Hash.SHA512 missing | n/a | I (unimplemented) |
| 17 | JsonPath not in externs | FIXED | A (stale .inc) |
| 18 | Pattern arg order | n/a | J (doc error) |
| 19 | File ReadStr names | n/a | J (doc error) |
| 20-26 | ARM64 missing symbols | FIXED | K (missing mappings) |
| 27-31 | Thread classes New | FIXED | A (stale .inc) |
| 32 | Channel no typeRegistry | FIXED | E (missing entry) |
| 33 | Pool no typeRegistry | FIXED | E (missing entry) |
| 34-36 | Network classes New | FIXED | A (stale .inc) |
| 37-39 | Graphics classes New | FIXED | A (stale .inc) |
| 40 | Checkbox.IsChecked type | FIXED | D (wrong signature) |
| 41 | CodeEditor.IsModified type | FIXED | D (wrong signature) |

**Fixed: 30** | **Open: 0** | **By design / doc: 11**

All code bugs have been resolved.
