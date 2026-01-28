# ViperIDE Bugs & Root Cause Analysis

Discovered during Phase 1 development of ViperIDE (`demos/zia/viperide/`).
First compilation attempt: `zia main.zia` from the viperide directory.

---

## IDE-BUG-001: `obj` is not a valid Zia type [USER CODE BUG]

**Severity:** Blocker
**Status:** Root cause found — fix in ViperIDE code

**Symptom:**
```
app_shell.zia:12:12: error[V3000]: Unknown type: obj
```

**Root cause:** `Sema.cpp:817-859` — `resolveNamedType()` checks types in this order:
1. Built-ins: `Integer`, `Number`, `Boolean`, `String`, `Byte`, `Unit`, `Void`, `Error`, `Ptr`
2. User-defined entities/values in `typeRegistry_`
3. Cross-module references (strips module prefix, retries)

`obj` matches none of these. It was never a valid Zia type.

At startup, `Sema::initRuntimeFunctions()` (`Sema_Runtime.cpp:131-207`) registers all runtime classes from RuntimeRegistry into `typeRegistry_`. This includes entries like `"Viper.Graphics.Canvas"`, `"Viper.GUI.App"`, etc. These resolve to `types::runtimeClass(qname)` which are Ptr types with a name tag.

**Fix:** Replace all `obj` with the actual fully-qualified Viper.GUI type from `runtime.def`:
```zia
// Before (WRONG):
expose obj app;

// After (CORRECT):
expose Viper.GUI.App? app;
```

Available types: `Viper.GUI.App`, `Viper.GUI.VBox`, `Viper.GUI.HBox`, `Viper.GUI.Button`, `Viper.GUI.Label`, `Viper.GUI.TabBar`, `Viper.GUI.Tab`, `Viper.GUI.CodeEditor`, etc.

---

## IDE-BUG-002: Wrong List API — PascalCase vs built-in syntax [USER CODE BUG]

**Severity:** Blocker
**Status:** Root cause found — fix in ViperIDE code

**Symptom:**
```
core/document_manager.zia:42:34: error[V3000]: Expression is not indexable
```

**Root cause:** `List[Type]` and `Viper.Collections.List` are the **same underlying type**. The Zia compiler's dispatch table (`Lowerer_Expr_Call.cpp:78-105`) maps built-in list methods to runtime functions case-insensitively:

| Zia method (lowercase) | Runtime function (PascalCase) |
|---|---|
| `.get(i)` | `Viper.Collections.List.get_Item(obj, i64)` |
| `.count()` | `Viper.Collections.List.get_Count(obj)` |
| `.add(item)` | `Viper.Collections.List.Add(obj, obj)` |
| `.removeAt(i)` | `Viper.Collections.List.RemoveAt(obj, i64)` |
| `.set(i, val)` | `Viper.Collections.List.set_Item(obj, i64, obj)` |
| `.find(item)` | `Viper.Collections.List.Find(obj, obj)` |

The empty literal `[]` compiles to a `Viper.Collections.List.New()` call (`Lowerer_Expr_Collections.cpp:25-39`). Element types are compile-time only — at runtime all elements are boxed pointers.

We incorrectly used `Viper.Collections.List.New()` explicitly with `.Item[i]` and `.Count` property syntax, which the compiler does not support for direct use. `.Item[i]` is a property getter that the compiler only invokes through the `.get()` dispatch.

**Fix:** Use built-in syntax:
```zia
// Before (WRONG):
documents = Viper.Collections.List.New();
var doc = documents.Item[activeIndex];
var n = documents.Count;

// After (CORRECT):
documents = [];
var doc = documents.get(activeIndex);
var n = documents.count();
```

**Note:** `List.remove()` is a known runtime bug (BUG-002 in sqldb). Use `.removeAt(index)` instead.

---

## IDE-BUG-003: Nullable runtime types [NOT A BUG — works correctly]

**Severity:** N/A
**Status:** Confirmed working

**Root cause:** `Parser_Type.cpp:22-34` parses the `?` suffix and wraps the base type in an `OptionalType`. `Types.cpp:94-108` `isAssignableFrom()` accepts either the inner type or `null` (Unit type). Since runtime classes resolve to Ptr types, `Optional[Ptr]` works the same as `Optional[Entity]`.

```zia
// This IS valid Zia:
expose Viper.GUI.App? app;

expose func init() {
    app = null;  // OK — null assigns to Optional[Ptr]
}
```

Evidence: `sqldb/parser.zia:748` uses `var joinCondition: Expr? = null;` for nullable entity types. The same mechanism supports runtime types.

---

## IDE-BUG-004: TabBar has no active tab detection [RUNTIME GAP]

**Severity:** Major
**Status:** Fix planned — add runtime wrappers

**Root cause:** The C layer fully supports active tab tracking:
- `vg_tabbar_t.active_tab` pointer (`vg_ide_widgets.h:1495`)
- `vg_tabbar_get_active()` getter (`vg_tabbar.c:502-505`)
- `on_select` callback on tab change (`vg_ide_widgets.h:1516`)
- Tab click sets active via `vg_tabbar_set_active()` (`vg_tabbar.c:353`)

But NO `rt_*` wrapper functions exist in `rt_gui.c`, and nothing is registered in `runtime.def`. The gap is between the C widget layer and the Zia-visible runtime API.

**Fix:** Add to `runtime.def` and `rt_gui.c`:
- `TabBar.GetActive() -> Tab` — wraps `vg_tabbar_get_active()`
- `TabBar.GetActiveIndex() -> Integer` — iterate linked list to find index
- `TabBar.WasChanged() -> Integer` — per-frame change detection
- `TabBar.TabCount` property — expose `tab_count` field

---

## IDE-BUG-005: CodeEditor has no cursor position getters [RUNTIME GAP]

**Severity:** Major
**Status:** Fix planned — register existing functions

**Root cause:** The C layer has complete cursor support:
- `vg_codeeditor_t.cursor_line` / `.cursor_col` fields (`vg_ide_widgets.h:1905-1906`, 0-based)
- `vg_codeeditor_get_cursor()` C function (`vg_codeeditor.c:1211-1219`)

The runtime layer ALREADY has wrapper functions:
- `rt_codeeditor_get_cursor_line_at(editor, index)` (`rt_gui.c:3368-3374`)
- `rt_codeeditor_get_cursor_col_at(editor, index)` (`rt_gui.c:3377-3383`)

These are declared in `rt_gui.h:1602,1608` but are **not registered in `runtime.def`**. They were written as part of the multi-cursor Phase 4 API but never exposed to the Zia class definition.

**Fix:** Add simple wrappers that call the existing `_at` functions with index=0, then register as `RT_PROP` in the CodeEditor class definition in `runtime.def`.

---

## IDE-BUG-006: Tab has no click/close events [RUNTIME GAP]

**Severity:** Major
**Status:** Fix planned — add per-frame tracking

**Root cause:** The C layer uses a callback mechanism (`vg_ide_widgets.h:1480-1486`):
```c
typedef void (*vg_tab_select_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
typedef bool (*vg_tab_close_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
```

The event handler (`vg_tabbar.c:317-356`) processes mouse clicks:
- Line 325-344: Detects close-button click area, calls `on_close` callback, auto-removes tab
- Line 352-353: Activates clicked tab via `vg_tabbar_set_active()`

Callbacks cannot be used from Zia (no function pointer support). The existing `WasClicked()` pattern for buttons uses a global variable (`g_last_clicked_widget`) set during event processing and read during the poll loop.

**Fix:** Apply the same per-frame global pattern:
- Add `prev_active_tab` field to `vg_tabbar_t` to detect selection changes
- Add `close_requested_tab` field to record close-button clicks without auto-removing
- Add `auto_close` flag (default `true` for backward compat)
- Expose via `TabBar.WasChanged()` and `TabBar.WasCloseClicked()`

---

## IDE-BUG-007: Cascading type errors from `obj` [DERIVATIVE]

**Severity:** N/A
**Status:** Auto-resolves with BUG-001 fix

**Root cause:** Once the compiler fails to resolve `obj`, the field type becomes `?` (unknown). All downstream operations on that field — method calls, property access, assignments, conditionals — produce cascading errors like `Type mismatch: expected ?, got Ptr`. The type checker propagates `?` through every expression involving the unresolved field.

Example cascade: `app_shell.zia:48` → `app = Viper.GUI.App.New(...)` → "expected ?, got Ptr" because `app`'s type is `?` (from failed `obj` resolution) and the RHS is `Ptr` (from the factory method).

---

## IDE-BUG-008: Boolean `true` passed to Integer parameter [NOT A BUG]

**Severity:** N/A
**Status:** Working as designed

**Root cause:** At the IL level, Boolean is stored as i64 (0 or 1):
- `Types.hpp:63` — "Boolean → i64 (0 or 1)"
- `Types.hpp:850` — "Boolean → i64 (stored as 0 or 1)"
- `Types.hpp:869` — "Boolean: 8 bytes (stored as i64)"

`true` is lowered as `I1` in IL but at the ABI level both `I1` and `I64` fit in a register. When passed to a runtime function expecting `int64_t`, `true` becomes `1`. The lowerer at `Lowerer_Expr.cpp:250-251` tags the value as `isBool` but the generated code is ABI-compatible.

VEdit's `tabBar.AddTab("Untitled 1", true)` works because `true` → `i64(1)` at machine level. Both `true` and `1` are valid for the `closable` parameter.

---

## IDE-BUG-009: `List[Viper.GUI.Tab]` generic validation [NOT A BUG]

**Severity:** N/A
**Status:** Confirmed working

**Root cause:** `Sema.cpp:894-898` resolves List element types:
```cpp
if (generic->name == "List") {
    return types::list(args.empty() ? types::unknown() : args[0]);
}
```

The type argument goes through `resolveTypeNode()` which calls `resolveNamedType()`. Runtime types like `Viper.GUI.Tab` are found in `typeRegistry_` (registered at startup from RuntimeRegistry). They resolve to `types::runtimeClass("Viper.GUI.Tab")`, which is a `Ptr` type.

At runtime, all list elements are boxed pointers regardless of declared element type. Element types are compile-time only. `List[Viper.GUI.Tab]` compiles and works identically to `List[Column]` or `List[Integer]`.

---

## IDE-BUG-010: Method dispatch fails on nullable types [COMPILER LIMITATION]

**Severity:** Blocker
**Status:** Workaround applied — use non-nullable types

**Root cause:** The Zia compiler cannot dispatch methods on `Optional[Ptr]` types (e.g., `Viper.GUI.TabBar?`). When a method is called on a nullable type, the lowerer resolves the callee to null, producing a `call.indirect 0, ...` instruction in IL. At runtime this traps with "Null indirect callee".

The type checker doesn't narrow nullable types after null guards. Even with `if tabBar == null { return; }`, subsequent `tabBar.AddTab(...)` still sees `tabBar` as `Optional[Ptr]`, not `Ptr`.

**Fix:** Use non-nullable types for all widget fields. Initialize via Build/Setup before any method calls, matching the pattern used by existing projects (paint, sqldb).

```zia
// Before (FAILS — can't call methods on nullable type):
expose Viper.GUI.TabBar? tabBar;
// ...
tabBar.AddTab(name, 1);  // ERROR: call.indirect 0

// After (WORKS — non-nullable, methods resolve correctly):
expose Viper.GUI.TabBar tabBar;
```

---

## IDE-BUG-011: Method dispatch fails on untyped `obj` returns [COMPILER LIMITATION]

**Severity:** Major
**Status:** Workaround applied — explicit type annotations

**Root cause:** When a runtime property returns `obj` (generic pointer), calling methods on the result fails. The compiler can't resolve methods because it doesn't know the concrete type. Example: `app.Root` returns `obj`, so `root.AddChild(...)` generates `call.indirect` with a null callee.

**Fix:** Use explicit type annotations on variables holding untyped returns:
```zia
// Before (FAILS):
var root = app.Root;
root.AddChild(mainVBox);  // call.indirect 0 — method not resolved

// After (WORKS):
var root: Viper.GUI.Widget = app.Root;
root.AddChild(mainVBox);  // call @Viper.GUI.Widget.AddChild
```

---

## IDE-BUG-012: List cannot store C-allocated widget handles [RUNTIME LIMITATION]

**Severity:** Blocker
**Status:** Workaround applied — use TabBar.GetTabAt() instead

**Root cause:** `List.Add()` calls `rt_obj_retain_maybe()` → `rt_heap_retain()` on every item, which validates the object's `RT_MAGIC` header (`rt_heap.c:66`). C-allocated widget pointers (e.g., `vg_tab_t*` from `vg_tabbar_add_tab()`) don't have runtime heap headers, causing an assertion failure.

This affects any `List[Viper.GUI.*]` where the element type is a C-allocated widget rather than a Zia entity.

**Fix:** Don't store widget handles in Lists. Use the widget's own index-based API instead:
```zia
// Before (CRASHES — Tab handles are C-allocated, no RT_MAGIC header):
expose List[Viper.GUI.Tab] tabHandles;
tabHandles.add(tab);

// After (WORKS — use TabBar's index-based lookup):
var tab: Viper.GUI.Tab = tabBar.GetTabAt(index);
```

Added `TabBar.GetTabAt(index)` to the runtime to support this pattern.

---

## Summary

| Bug | Category | Root Cause | Status |
|-----|----------|-----------|--------|
| BUG-001 | USER CODE | `obj` not in type registry | FIXED — replaced with `Viper.GUI.*` types |
| BUG-002 | USER CODE | Wrong list API surface | FIXED — use `[]`, `.get()`, `.count()` |
| BUG-003 | NOT A BUG | `Type?` works for runtime types | N/A |
| BUG-004 | RUNTIME GAP | C getter exists, no `rt_` wrapper | FIXED — added TabBar APIs to runtime |
| BUG-005 | RUNTIME GAP | `rt_` functions exist, not in runtime.def | FIXED — registered CursorLine/CursorCol |
| BUG-006 | RUNTIME GAP | C callbacks exist, no polling API | FIXED — added WasChanged/WasCloseClicked |
| BUG-007 | DERIVATIVE | Cascades from BUG-001 | FIXED — auto-resolved |
| BUG-008 | NOT A BUG | Boolean is i64 at ABI level | N/A |
| BUG-009 | NOT A BUG | Generic List accepts any resolved type | N/A |
| BUG-010 | COMPILER | No method dispatch on nullable types | WORKAROUND — non-nullable types |
| BUG-011 | COMPILER | No method dispatch on untyped `obj` | WORKAROUND — explicit type annotations |
| BUG-012 | RUNTIME | List retains C-allocated pointers | WORKAROUND — index-based lookup |
| BUG-013 | C WIDGET | CodeEditor had no scrollbar visible | FIXED — added scrollbar drawing |
| BUG-014 | C WIDGET | CodeEditor cursor went off-screen | FIXED — added ensure_cursor_visible() |
| BUG-015 | COMPILER | No short-circuit evaluation for `and`/`or` | FIXED — added lazy evaluation to Lowerer |

---

## IDE-BUG-013: CodeEditor scrollbar not visible [C WIDGET BUG]

**Severity:** Major
**Status:** FIXED

**Symptom:** Users could not scroll through large files in the CodeEditor. No scrollbar was visible on the right side of the editor.

**Root cause:** The `codeeditor_paint()` function in `vg_codeeditor.c` did not draw a scrollbar. The widget only supported mouse wheel scrolling (which also crashed - see BUG-015).

**Fix:** Added scrollbar drawing to `codeeditor_paint()`:
- Draw scrollbar track on right side (12px wide)
- Calculate thumb size based on visible vs total content height
- Draw thumb at position based on scroll_y
- Added click-to-jump handling in mouse event handler

**File:** `src/lib/gui/src/widgets/vg_codeeditor.c`

---

## IDE-BUG-014: CodeEditor cursor goes off-screen when navigating [C WIDGET BUG]

**Severity:** Major
**Status:** FIXED

**Symptom:** When using arrow keys to navigate through a file, the cursor would move beyond the visible area without the view scrolling to follow it.

**Root cause:** The keyboard navigation code in `codeeditor_handle_event()` updated `cursor_line` and `cursor_col` but did not adjust `scroll_y` to keep the cursor visible.

**Fix:** Added `ensure_cursor_visible()` helper function that:
- Checks if cursor_y is above visible area → scroll up
- Checks if cursor_y is below visible area → scroll down
- Clamps scroll_y to valid range

Called after all cursor movement operations (arrow keys, page up/down, character input).

**File:** `src/lib/gui/src/widgets/vg_codeeditor.c:136-164`

---

## IDE-BUG-015: Missing short-circuit evaluation for `and`/`or` [COMPILER BUG]

**Severity:** Critical
**Status:** FIXED

**Symptom:** Null pointer dereference crashes when using `and` expressions like `shell != null and shell.ShouldClose() == false`. The method call was executed even when the left side was false.

**Crash pattern:**
```
*** NULL POINTER DEREFERENCE ***
Attempting to access field at offset 16 on null object
PC: 11, Function: AppShell.ShouldClose
```

**Root cause:** The Zia compiler's `lowerBinary()` function in `Lowerer_Expr_Binary.cpp` evaluated both operands of `and`/`or` expressions **before** checking the operator type:

```cpp
// BEFORE (BUGGY): Both operands evaluated eagerly
auto left = lowerExpr(expr->left.get());
auto right = lowerExpr(expr->right.get());  // Executes even if left is false!
// ... later switch on expr->op ...
```

This generated IL that called methods on both sides before AND-ing the results:
```
%t136 = icmp_ne %t135, 0        // null check (left side)
%t137 = load ptr, %t5
call @AppShell.ShouldClose(%t137)  // Called BEFORE checking null!
%t145 = and %t143, %t144        // AND results together AFTER both computed
```

When `shell` was null, `ShouldClose()` was still called, causing the crash.

**Fix:** Implemented proper short-circuit (lazy) evaluation for `BinaryOp::And` and `BinaryOp::Or`:

1. Check for `And`/`Or` **before** lowering the right operand
2. Added `lowerShortCircuit()` function that uses control flow:
   - For `A and B`: If A is false, skip B entirely
   - For `A or B`: If A is true, skip B entirely
3. Uses basic blocks and conditional branches to implement lazy evaluation:
   ```
   entry:
     left = evaluate A
     store result_slot, left
     cbr left, eval_right_block, merge_block  // Skip if short-circuit
   eval_right_block:
     right = evaluate B
     store result_slot, right
     br merge_block
   merge_block:
     result = load result_slot
   ```

**Files modified:**
- `src/frontends/zia/Lowerer_Expr_Binary.cpp:236-242` - Early return to `lowerShortCircuit()` for And/Or
- `src/frontends/zia/Lowerer_Expr_Binary.cpp:531-613` - New `lowerShortCircuit()` implementation
- `src/frontends/zia/Lowerer.hpp:700-702` - Declaration
- `src/tests/zia/test_zia_expressions.cpp` - Updated test to check for CBr instead of And opcode

**Generated IL (after fix):**
```
while_cond_8:
  %t133 = alloca 8
  %t134 = load ptr, %t5           // Load shell
  %t139 = icmp_ne %t138, 0
  store i1, %t133, %t139
  cbr %t139, and_rhs_11, and_merge_12  // Short-circuit!
and_rhs_11:
  %t140 = load ptr, %t5
  call @AppShell.ShouldClose(%t140)  // Only called if shell != null
  store i1, %t133, %t147
  br and_merge_12
and_merge_12:
  %t148 = load i1, %t133
  cbr %t148, while_body_9, while_end_10
```

Now `ShouldClose()` is only called when `shell` is not null.
