# Zia Language — Full IntelliSense Code Completion Plan

## Context

ViperIDE is a functional IDE for the Zia language but has no code completion. The user wants a
full IntelliSense-style implementation: member access completions (`expr.`), scope completions
(Ctrl+Space), type completions, keyword/snippet suggestions, and a navigable popup that appears
at the cursor — matching Visual Studio IntelliSense depth.

**Output:** Save final plan to `bugs/autocomplete_ide_plan.md` as first implementation action.

---

## Architecture Overview

Five layers, built bottom-up:

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 5 — Zia UI  (demos/zia/viperide/editor/completion.zia)│
│ CompletionController entity: popup, keyboard nav, insertion  │
├─────────────────────────────────────────────────────────────┤
│ Layer 4 — Runtime Def  (src/il/runtime/runtime.def)         │
│ RT_CLASS Viper.Zia.Completion + new CodeEditor RT_METHODs   │
├─────────────────────────────────────────────────────────────┤
│ Layer 3 — C Runtime Bridge  (src/runtime/graphics/)         │
│ rt_zia_completion.cpp + new rt_gui_codeeditor functions      │
├─────────────────────────────────────────────────────────────┤
│ Layer 2 — C++ Completion Engine  (src/frontends/zia/)       │
│ ZiaCompletion.cpp: context extraction, sema query, ranking  │
├─────────────────────────────────────────────────────────────┤
│ Layer 1 — Sema/Compiler Extensions  (src/frontends/zia/)    │
│ parseAndAnalyze(), Sema symbol enumeration APIs              │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1 — Sema & Compiler Extensions

### P1-A: `parseAndAnalyze()` in Compiler.hpp

Existing `compile()` runs all 5 stages through IL lowering. Completion only needs stages 1–4.

**File:** `src/frontends/zia/Compiler.hpp`

Add:
```cpp
struct AnalysisResult {
    std::unique_ptr<ModuleDecl> ast;       // Parsed + resolved AST
    std::unique_ptr<Sema>       sema;      // Type-checked, symbol table populated
    DiagnosticList              diagnostics;
    bool hasErrors() const;
    // Partial result returned even with errors (parser has resyncAfterError())
};

// Run Lexer → Parser → ImportResolver → Sema; stop before Lowerer.
// Tolerant: returns partial result even if source has syntax/type errors.
AnalysisResult parseAndAnalyze(
    const CompilerInput&   input,
    const CompilerOptions& opts,
    SourceManager&         sm
);
```

**File:** `src/frontends/zia/Compiler.cpp` — implement by calling internal pipeline stages
directly (already reachable via compile()'s implementation). Add `CompilerOptions::stopAfterSema`
bool flag (default false) so existing callers are unaffected.

### P1-B: Sema Public APIs for Symbol Enumeration

**File:** `src/frontends/zia/Sema.hpp` — add public const methods (callable after `analyze()`):

```cpp
// All symbols in scope at a given source location (1-based line, 0-based col).
// Includes locals, parameters, top-level decls, bound runtime names.
std::vector<Symbol> getSymbolsAtLocation(int line, int col) const;

// All exposed fields and methods of an entity, value, or interface type.
std::vector<Symbol> getMembersOf(const TypeRef& type) const;

// All registered methods/props of a runtime class (e.g. "Viper.GUI.App").
std::vector<Symbol> getRuntimeMembers(const std::string& className) const;

// All type names visible in module scope (entity, value, interface names).
std::vector<std::string> getTypeNames() const;

// Bound module names (from `bind X;` declarations).
std::vector<std::string> getBoundModuleNames() const;

// Exported symbols of a bound module.
std::vector<Symbol> getModuleExports(const std::string& moduleName) const;
```

Implementations query existing private maps (`fieldTypes_`, `methodTypes_`, `entityDecls_`,
`scopes_` stack). No new data stored — public accessors only.

---

## Phase 2 — C++ Completion Engine

**New files:**
- `src/frontends/zia/ZiaCompletion.hpp`
- `src/frontends/zia/ZiaCompletion.cpp`

Add `ZiaCompletion.cpp` to `ZIA_SOURCES` in `src/frontends/zia/CMakeLists.txt`.

### Data Types

```cpp
enum class CompletionKind : uint8_t {
    Keyword, Snippet,
    Variable, Parameter,
    Field, Method, Function,
    Entity, Value, Interface,
    Module, RuntimeClass, Property
};

struct CompletionItem {
    std::string    label;        // Displayed in popup
    std::string    insertText;   // Inserted into buffer
    CompletionKind kind;
    std::string    detail;       // Type/signature string shown right-aligned
    std::string    documentation;
    int            sortPriority; // Lower = higher
};

struct CompletionContext {
    enum class TriggerKind {
        None, MemberAccess, AfterNew, AfterColon, AfterReturn, CtrlSpace
    };
    TriggerKind trigger;
    std::string triggerExpr;  // LHS of '.', e.g. "shell.app"
    std::string prefix;       // Chars typed since trigger
    int         line;         // 1-based
    int         col;          // 0-based
    int         replaceStart; // Column where prefix begins
};
```

### CompletionEngine class

```cpp
class CompletionEngine {
public:
    std::vector<CompletionItem> complete(
        std::string_view source, int line, int col,
        std::string_view filePath = "<editor>",
        int maxResults = 50
    );
    void clearCache();

private:
    CompletionContext extractContext(std::string_view src, int line, int col) const;

    // Providers
    std::vector<CompletionItem> provideKeywords(const std::string& prefix) const;
    std::vector<CompletionItem> provideSnippets(const std::string& prefix) const;
    std::vector<CompletionItem> provideScopeSymbols(const Sema&, const CompletionContext&) const;
    std::vector<CompletionItem> provideMemberCompletions(const Sema&, const CompletionContext&) const;
    std::vector<CompletionItem> provideTypeNames(const Sema&, const std::string& prefix) const;
    std::vector<CompletionItem> provideModuleMembers(const Sema&, const std::string& mod,
                                                     const std::string& prefix) const;
    std::vector<CompletionItem> provideRuntimeMembers(const Sema&, const std::string& cls,
                                                      const std::string& prefix) const;

    void filterByPrefix(std::vector<CompletionItem>&, const std::string& prefix) const;
    void rank(std::vector<CompletionItem>&, const std::string& prefix) const;

    // One-entry LRU sema cache
    struct Cache {
        std::string           sourceHash;
        std::unique_ptr<Sema> sema;
    };
    Cache                          cache_;
    std::unique_ptr<SourceManager> sm_;
};
```

### Context Extraction Algorithm

1. Extract line text up to `col`
2. Scan backward for trigger:
   - `.` → `MemberAccess`; collect balanced expression before `.` as `triggerExpr`
   - `new ` → `AfterNew`
   - `: ` → `AfterColon` (type context)
   - `return ` → `AfterReturn`
   - default → `CtrlSpace`
3. Collect `prefix` = identifier chars after trigger up to cursor
4. `replaceStart` = col - prefix.length()

### Completion Dispatch per TriggerKind

| TriggerKind | Providers called |
|------------|-----------------|
| `MemberAccess` | `provideMemberCompletions` (queries Sema for type of `triggerExpr`, then getMembersOf / getRuntimeMembers) |
| `AfterNew` | `provideTypeNames` (entity + value names) |
| `AfterColon` | `provideTypeNames` + built-in type keywords |
| `AfterReturn` | `provideScopeSymbols` + `provideKeywords` |
| `CtrlSpace` | ALL providers |

### Keyword List (static)

**Statement keywords:** `var`, `func`, `if`, `else`, `while`, `for`, `in`, `return`,
`break`, `continue`, `and`, `or`, `not`, `is`, `as`, `new`, `true`, `false`, `null`

**Declaration keywords:** `entity`, `interface`, `value`, `expose`, `module`, `bind`

**Built-in types:** `Integer`, `Number`, `Boolean`, `String`, `Byte`, `Bytes`,
`List`, `Map`, `Set`, `Object`

### Snippet List (static)

| Label | insertText (literal, no cursor stops) |
|-------|--------------------------------------|
| `if` | `if  {\n    \n}` |
| `if-else` | `if  {\n    \n} else {\n    \n}` |
| `while` | `while  {\n    \n}` |
| `for` | `for i in 0..n {\n    \n}` |
| `for-in` | `for item in  {\n    \n}` |
| `func` | `func name() {\n    \n}` |
| `entity` | `entity Name {\n    expose func init() {\n    }\n}` |

### Completion Result Serialization (C++ → Zia)

Tab-delimited lines:
```
label\tinsertText\tkind\tdetail\n
```
`kind` integer: Keyword=0 Snippet=1 Variable=2 Parameter=3 Field=4 Method=5
Function=6 Entity=7 Value=8 Interface=9 Module=10 RuntimeClass=11 Property=12

### Sema Cache Strategy

- Hash source: FNV-1a over source bytes (fast, ~1µs for 10KB)
- Cache hit: reuse existing Sema (skip re-parse)
- Cache miss: call `parseAndAnalyze()` with error-tolerant mode
- Cache invalidated when hash differs
- Files > 200KB: always re-parse (no cache — too large to hash cheaply)

---

## Phase 3 — C Runtime Bridge

**New files:**
- `src/runtime/graphics/rt_zia_completion.h`
- `src/runtime/graphics/rt_zia_completion.cpp` (C++ with `extern "C"` wrappers)

Add to runtime CMakeLists. Link `fe_zia` into the runtime graphics target.

```c
// extern "C" API
rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col);
void      rt_zia_completion_clear_cache(void);
```

Singleton `CompletionEngine` — `static CompletionEngine s_engine;` — per process.

### New CodeEditor C functions

**File:** `src/runtime/graphics/rt_gui_codeeditor.c` + `src/runtime/graphics/rt_gui.h`

```c
// Screen-absolute pixel position of the primary cursor caret (top-left of caret rect)
int64_t rt_codeeditor_get_cursor_pixel_x(void *editor);
int64_t rt_codeeditor_get_cursor_pixel_y(void *editor);

// Text manipulation at primary cursor
void      rt_codeeditor_insert_at_cursor(void *editor, rt_string text);
rt_string rt_codeeditor_get_word_at_cursor(void *editor);
void      rt_codeeditor_replace_word_at_cursor(void *editor, rt_string new_text);

// Get a single line's text (0-based line index)
rt_string rt_codeeditor_get_line(void *editor, int64_t line_index);
```

**Pixel coord implementation:**
- Access `editor->cursor_line`, `editor->cursor_col`, `editor->scroll_top_line`,
  `editor->gutter_width`, `editor->line_height`, `editor->char_width` from `vg_codeeditor_t`
- Get absolute screen position via `vg_widget_get_screen_bounds(&editor->base, &ax, &ay, NULL, NULL)`
- `pixel_x = ax + gutter_width + (cursor_col * char_width)`
- `pixel_y = ay + header_height + (cursor_line - scroll_top_line) * line_height`

**Insert at cursor implementation:**
- Locate cursor offset in internal line buffer
- Splice text bytes into buffer
- Advance cursor by inserted length
- Set `editor->modified = true`, `editor->base.needs_paint = true`

**Word at cursor / replace word:**
- Scan left from `cursor_col` while char is `[A-Za-z0-9_]`; scan right similarly
- `get_word_at_cursor`: return substring
- `replace_word_at_cursor`: splice new text over that range; reposition cursor

---

## Phase 4 — Runtime Registration

**File:** `src/il/runtime/runtime.def`

Add to `RT_CLASS_BEGIN("Viper.GUI.CodeEditor", ...)`:

```
RT_METHOD("GetCursorPixelX",      "i64()",       CodeEditorGetCursorPixelX)
RT_METHOD("GetCursorPixelY",      "i64()",       CodeEditorGetCursorPixelY)
RT_METHOD("InsertAtCursor",       "void(str)",   CodeEditorInsertAtCursor)
RT_METHOD("GetWordAtCursor",      "str()",       CodeEditorGetWordAtCursor)
RT_METHOD("ReplaceWordAtCursor",  "void(str)",   CodeEditorReplaceWordAtCursor)
RT_METHOD("GetLine",              "str(i64)",    CodeEditorGetLine)
```

Matching RT_FUNC entries (sigs prefixed with `obj`).

New static class:
```
RT_CLASS_BEGIN("Viper.Zia.Completion", "none", none)
    RT_METHOD("Complete",    "str(str,i64,i64)",  ZiaComplete)
    RT_METHOD("ClearCache",  "void()",            ZiaCompletionClearCache)
RT_CLASS_END()
```

**File:** `src/il/runtime/classes/RuntimeClasses.hpp` — add `RTCLS_ZiaCompletion`

Run `./scripts/check_runtime_completeness.sh` after changes.

---

## Phase 5 — Floating Panel Widget

A true IntelliSense popup must float at an absolute screen position regardless of layout.

**New files:**
- `src/lib/gui/src/widgets/vg_floating_panel.c`
- Registers as `"Viper.GUI.FloatingPanel"` in `runtime.def`
- Methods: `New(app)`, `SetPosition(x,y)`, `SetSize(w,h)`, `SetVisible(0/1)`, `AddChild(widget)`
- During paint: draw above all other content, positioned at its absolute `x,y`
- Parented to app root widget; drawn in a separate post-widget-tree pass (like dialogs)

`RT_CLASS_BEGIN("Viper.GUI.FloatingPanel", ...)` + `RTCLS_FloatingPanel` in RuntimeClasses.hpp

---

## Phase 6 — Zia UI Layer

**New file:** `demos/zia/viperide/editor/completion.zia`

### CompletionController entity (summary)

Fields: `editor` (GUI.CodeEditor), `popup` (GUI.FloatingPanel), `isVisible` (Boolean),
`selectedIndex` (Integer), `itemCount` (Integer), `lastCursorLine` (Integer),
`lastCursorCol` (Integer), `insertTexts` (List[String])

Key methods:
- `Setup(ed, app)` — create FloatingPanel, embed ListBox inside it
- `TriggerCompletion()` — call `Viper.Zia.Completion.Complete(text, line, col)`,
  parse result, populate ListBox, position panel at `(GetCursorPixelX, GetCursorPixelY+20)`,
  call `Show()`
- `Update() -> Boolean` — per-frame: check keyboard shortcuts, auto-dismiss on line change,
  return true if event consumed
- `AcceptSelected()` — call `editor.ReplaceWordAtCursor(insertTexts.get(selectedIndex))`, Hide()
- `NavigateUp/Down()` — adjust selectedIndex, call `popup.SelectIndex()`
- `Show(x,y)` / `Hide()` — toggle visibility

### Integration in `main.zia`

Add after engine.Setup():
```rust
bind "editor/completion";
var completer = new completion.CompletionController();
completer.Setup(engine.editor, shell.app);

Shortcuts.Register("completion_down",    "Down",      "Next Completion Item");
Shortcuts.Register("completion_up",      "Up",        "Prev Completion Item");
Shortcuts.Register("completion_accept",  "Tab",       "Accept Completion");
Shortcuts.Register("completion_dismiss", "Escape",    "Dismiss Completion");
Shortcuts.Register("trigger_completion", "Ctrl+Space","Trigger Completion");
```

In main loop after `shell.Poll()`:
```rust
var completionConsumed = completer.Update();
if Shortcuts.WasTriggered("trigger_completion") != 0 {
    completer.TriggerCompletion();
}
// Detect '.' typed (cursor col advanced + last char is '.')
var curLine = engine.GetCursorLine();
var curCol  = engine.GetCursorCol();
if curCol != prevCursorCol or curLine != prevCursorLine {
    if curLine == prevCursorLine and curCol == prevCursorCol + 1 {
        var lineText = engine.editor.GetLine(curLine - 1);
        if Str.CharAt(lineText, curCol - 2) == "." {
            completer.TriggerCompletion();
        }
    }
    prevCursorLine = curLine;
    prevCursorCol  = curCol;
}
```

---

## Files Created / Modified

### New files:
| File | Purpose |
|------|---------|
| `src/frontends/zia/ZiaCompletion.hpp` | Completion engine header |
| `src/frontends/zia/ZiaCompletion.cpp` | Completion engine (context extraction, providers, ranking) |
| `src/runtime/graphics/rt_zia_completion.h` | C bridge header |
| `src/runtime/graphics/rt_zia_completion.cpp` | C bridge (extern "C", singleton engine) |
| `src/lib/gui/src/widgets/vg_floating_panel.c` | Floating overlay widget |
| `demos/zia/viperide/editor/completion.zia` | CompletionController entity |

### Modified files:
| File | Change |
|------|--------|
| `src/frontends/zia/Compiler.hpp` + `Compiler.cpp` | Add `parseAndAnalyze()`, `stopAfterSema` option |
| `src/frontends/zia/Sema.hpp` | Add `getSymbolsAtLocation`, `getMembersOf`, `getRuntimeMembers`, `getTypeNames`, `getBoundModuleNames`, `getModuleExports` |
| `src/frontends/zia/CMakeLists.txt` | Add ZiaCompletion.cpp to ZIA_SOURCES |
| `src/runtime/graphics/rt_gui_codeeditor.c` | Add GetCursorPixelX/Y, InsertAtCursor, GetWordAtCursor, ReplaceWordAtCursor, GetLine |
| `src/runtime/graphics/rt_gui.h` | Declare new codeeditor functions |
| `src/il/runtime/runtime.def` | New CodeEditor RT_METHODs; new Viper.Zia.Completion class; FloatingPanel class |
| `src/il/runtime/classes/RuntimeClasses.hpp` | Add RTCLS_ZiaCompletion, RTCLS_FloatingPanel |
| `src/runtime/CMakeLists.txt` | Add rt_zia_completion.cpp; link fe_zia |
| `demos/zia/viperide/main.zia` | Completion controller integration, shortcuts, dot-trigger logic |

---

## Implementation Order

1. **P1** — Compiler.hpp `parseAndAnalyze()` + Sema enumeration APIs (no user-visible change)
2. **P2** — ZiaCompletion.cpp + unit test `test_zia_completion.cpp`
3. **P3** — rt_zia_completion.cpp + CodeEditor new functions (pixel coords, insert, word ops)
4. **P4** — runtime.def registrations + RuntimeClasses.hpp
5. **P5** — FloatingPanel widget
6. **P6** — completion.zia + main.zia integration
7. **Polish** — ranking tuning, real-time narrowing as user types, snippet cursors

---

## Verification

```sh
cmake --build build -j
# Zero errors, zero new warnings

ctest --test-dir build -R test_zia_completion --output-on-failure
# New unit tests pass

./scripts/check_runtime_completeness.sh
# Zero warnings

viper run demos/zia/viperide/main.zia
```

**Manual acceptance criteria:**
1. Ctrl+Space → popup with keywords, type names, all in-scope variables
2. Type `shell.` → popup with every AppShell field/method + type signature detail
3. Type `shell.app.` → popup with GUI.App methods
4. Arrow keys navigate; Tab inserts selected item at cursor (replaces partial word)
5. Escape dismisses popup; popup auto-dismisses on new-line cursor move
6. Typing more characters after trigger narrows the list in real-time
7. Snippet items (`if`, `while`, `entity`) insert full template text
8. All existing ViperIDE functionality (build, open, save, find) unaffected
