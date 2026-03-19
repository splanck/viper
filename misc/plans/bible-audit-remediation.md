# Viper Bible Audit — Complete Remediation Plan

## Context

The Viper Bible audit (2026-03-11) found **~400+ incorrect code instances** across 38 distinct issue types in the documentation. Additionally, the audit revealed **platform gaps** where the Bible describes features that don't exist in the compiler/runtime. This plan addresses all three categories:

1. **Category A: Platform Gaps** — Implement missing features in the compiler/runtime (HTTP verbs, range methods, Color constants)
2. **Category B: Documentation Fixes** — Fix wrong API names, module paths, and syntax across all Bible chapters
3. **Category C: Aspirational Features** — Mark unimplemented features (threading, profiler, typed catch) as planned

**Reference**: Full issue catalogue at `docs/bible/AUDIT_ISSUES.md`

---

## Phase 1: Platform Gap Implementations (Category A)

### A1: Http.Put() and Http.Delete() — ISSUE-22-001

**Why**: REST API incomplete without PUT/DELETE. Bible Ch22 uses them.

**Files**:
- `src/il/runtime/runtime.def` — Add 4 RT_FUNC entries after line 4408
- `src/runtime/network/rt_network_http.c` — Add 4 C functions (~180 LOC)
- `src/runtime/network/rt_network.h` — Add function declarations

**Implementation**: Copy existing patterns:
- `rt_http_put(url, body)` — clone `rt_http_post`, change method to `"PUT"`
- `rt_http_put_bytes(url, bytes)` — clone `rt_http_post_bytes`, change method to `"PUT"`
- `rt_http_delete(url)` — clone `rt_http_get`, change method to `"DELETE"`
- `rt_http_delete_bytes(url)` — clone `rt_http_get_bytes`, change method to `"DELETE"`

**runtime.def entries**:
```
RT_FUNC(HttpPut, rt_http_put, "Viper.Network.Http.Put", "str(str,str)", always)
RT_FUNC(HttpPutBytes, rt_http_put_bytes, "Viper.Network.Http.PutBytes", "obj(str,obj)", always)
RT_FUNC(HttpDelete, rt_http_delete, "Viper.Network.Http.Delete", "str(str)", always)
RT_FUNC(HttpDeleteBytes, rt_http_delete_bytes, "Viper.Network.Http.DeleteBytes", "obj(str)", always)
```

**Effort**: ~1 hour. **Test**: build + `./scripts/check_runtime_completeness.sh`

---

### A2: Range `.rev()` and `.step(n)` — ISSUE-05-001, ISSUE-05-002

**Why**: No way to count backward or skip in for-in loops. Bible Ch05 uses both.

**Approach**: AST sugar — add fields to RangeExpr, no method resolution needed.

**Files**:
- `src/frontends/zia/AST_Expr.hpp` — Add `step` (ExprPtr) and `reversed` (bool) fields to RangeExpr
- `src/frontends/zia/Parser_Expr.cpp` — In `parseRange()`, after creating RangeExpr, check for `.rev()` and `.step(n)` suffixes on the result expression
- `src/frontends/zia/Sema_Expr_Advanced.cpp` — In `analyzeRange()`, validate step is positive integer if present
- `src/frontends/zia/Lowerer_Stmt.cpp` — In `lowerForInStmt()`:
  - Use step value instead of hardcoded `Value::constInt(1)` (line ~440)
  - For `.rev()`: swap start/end, use decrement, flip comparison (SCmpGE instead of SCmpLE)
  - For `.rev()` + `.step()`: combine both
- `src/frontends/zia/ZiaAstPrinter.cpp` — Print step/reversed fields

**Lowerer changes detail** (currently at Lowerer_Stmt.cpp:378-454):
```
Current:  init=start, cond=(i < end), update=(i = i + 1)
.step(2): init=start, cond=(i < end), update=(i = i + step)
.rev():   init=end,   cond=(i >= start), update=(i = i - 1)
Combined: init=end,   cond=(i >= start), update=(i = i - step)
```

For `.rev()` on inclusive ranges: swap and adjust bounds so `(1..=10).rev()` iterates 10,9,...,1.

**Effort**: ~3-4 hours, ~200 LOC across 5 files. **Test**: build + manual `.zia` test + add unit test.

---

### A3: Color Constants — ISSUE-19-003

**Why**: Bible uses `Color.RED`, `Color.WHITE` etc. throughout Ch19/20/21. Adding constants is simpler than rewriting 50+ doc references.

**Files**:
- `src/il/runtime/runtime.def` — Add RT_PROP entries to Color class for common colors
- `src/runtime/graphics/rt_drawing.c` (or `rt_canvas.c`) — Add getter functions

**Constants to add** (as read-only properties on Color):
```
Color.RED     → Color.RGB(255, 0, 0)       → 0xFF0000FF
Color.GREEN   → Color.RGB(0, 255, 0)       → 0x00FF00FF
Color.BLUE    → Color.RGB(0, 0, 255)       → 0x0000FFFF
Color.WHITE   → Color.RGB(255, 255, 255)   → 0xFFFFFFFF
Color.BLACK   → Color.RGB(0, 0, 0)         → 0x000000FF
Color.YELLOW  → Color.RGB(255, 255, 0)     → 0xFFFF00FF
Color.CYAN    → Color.RGB(0, 255, 255)     → 0x00FFFFFF
Color.MAGENTA → Color.RGB(255, 0, 255)     → 0xFF00FFFF
Color.GRAY    → Color.RGB(128, 128, 128)   → 0x808080FF
Color.ORANGE  → Color.RGB(255, 165, 0)     → 0xFFA500FF
```

Implementation: RT_PROP getter that returns a constant i64 (packed RGBA). Each getter is 1-2 lines of C.

**Effort**: ~1 hour. **Test**: build + test program using `Color.RED`.

---

## Phase 2: Cross-Cutting Documentation Fixes (Category B)

These affect multiple chapters. Fix the patterns first, then sweep per-chapter.

### B1: `[Type]` → `List[Type]` — XCUT-001

**Affected files**: Ch06, Ch07, Ch22, Ch23, Ch24, Appendix A (~60+ instances)

**Strategy**: Targeted find-replace in type annotation positions. Must be careful NOT to change:
- Array literal syntax `[1, 2, 3]` (this is valid)
- Index access `arr[0]` (this is valid)
- Only change `: [Type]` annotations and `[[Type]]` nested annotations

**Patterns to replace**:
- `: [Integer]` → `: List[Integer]`
- `: [String]` → `: List[String]`
- `: [Boolean]` → `: List[Boolean]`
- `: [Number]` → `: List[Number]`
- `: [[Integer]]` → `: List[List[Integer]]`
- `func f(x: [String])` → `func f(x: List[String])`
- Also `[value; count]` repeat syntax — verify if supported or needs rewrite

**Effort**: ~2 hours (careful regex + manual verification per file)

---

### B2: Bare Function Names → Qualified — XCUT-002

**Affected files**: Ch09, Ch10, Ch13 (~40+ instances)

**Mapping** (add `bind File = Viper.IO.File;` to each example, then prefix):
| Wrong | Correct |
|-------|---------|
| `readText(path)` | `File.ReadAllText(path)` |
| `writeText(path, data)` | `File.WriteAllText(path, data)` |
| `appendText(path, data)` | `File.Append(path, data)` |
| `readLines(path)` | `File.ReadAllLines(path)` |
| `readBytes(path)` | `File.ReadAllBytes(path)` |
| `writeBytes(path, data)` | `File.WriteAllBytes(path, data)` |
| `exists(path)` | `File.Exists(path)` |
| `delete(path)` | `File.Delete(path)` |
| `GetVariable(x)` | `Env.GetVariable(x)` |
| `HasVariable(x)` | `Env.HasVariable(x)` |

**Effort**: ~3 hours (each code block needs bind statement + function rename)

---

### B3: Wrong Module Paths — XCUT-004

**Affected files**: Ch10, Ch13, Ch22, Ch23, Appendix D

| Wrong | Correct |
|-------|---------|
| `bind Viper.File;` | `bind Viper.IO.File;` |
| `bind Viper.JSON;` | `bind Viper.Text.Json;` |
| `bind Viper.CSV;` | `bind Viper.Text.Csv;` |
| `bind Viper.Convert;` | `bind Viper.Core.Convert;` |
| `bind Viper.Guid;` | `bind Viper.Text.Uuid;` |
| `bind Viper.Threading;` | `bind Viper.Threads;` |
| `bind Viper.Codec;` | verify actual path |

**Effort**: ~1 hour (find-replace per file)

---

### B4: Canvas/Graphics API Rewrite — XCUT-003

**Affected files**: Ch19, Ch20, Ch21, Appendix D (~215+ instances)

This is the largest single task. Every graphics code block must be rewritten.

**Reference code**: `examples/games/sidescroller/`, `examples/games/lib/gamebase.zia`, `tests/runtime/test_graphics.zia`

**Method mapping**:
| Old (stateful) | New (stateless) |
|---|---|
| `Canvas(w, h)` | `Canvas.New("title", w, h)` |
| `canvas.setColor(c); canvas.fillRect(x,y,w,h)` | `canvas.Box(x,y,w,h,color)` |
| `canvas.drawRect(x,y,w,h)` | `canvas.Frame(x,y,w,h,color)` |
| `canvas.fillCircle(cx,cy,r)` | `canvas.Disc(cx,cy,r,color)` |
| `canvas.drawCircle(cx,cy,r)` | `canvas.Ring(cx,cy,r,color)` |
| `canvas.drawLine(x1,y1,x2,y2)` | `canvas.Line(x1,y1,x2,y2,color)` |
| `canvas.drawText(x,y,text)` | `canvas.Text(x,y,text,color)` |
| `canvas.setPixel(x,y)` | `canvas.Plot(x,y,color)` |
| `canvas.show()` | `canvas.Flip()` |
| `canvas.isOpen()` | `!canvas.ShouldClose` |
| `canvas.waitForClose()` | `while !canvas.ShouldClose { canvas.Poll(); canvas.Flip(); }` |
| `Color(r,g,b)` | `Color.RGB(r,g,b)` (or `Color.RED` after A3) |
| `Input.isKeyDown(Key.X)` | `Keyboard.IsDown(KEY_X)` (with `bind Keyboard = Viper.Input.Keyboard;`) |
| `Input.mouseX()` | `Mouse.X()` (with `bind Mouse = Viper.Input.Mouse;`) |

**Strategy**: Process one chapter at a time. For each code block:
1. Add correct `bind` statements
2. Replace constructor
3. Remove `setColor()` calls, merge color into draw calls
4. Replace method names
5. Replace game loop patterns
6. Verify output comments still make sense

**Effort**: ~8-12 hours across Ch19/20/21 + Appendix D

---

## Phase 3: Per-Chapter Documentation Fixes (Category B continued)

### Ch05: Replace `.rev()` / `.step()` examples
- After A2 is implemented, these become correct — no doc fix needed
- If A2 is deferred, replace with while-loop alternatives

### Ch08: Fix string API issues
- Replace `format(...)` → string concatenation + `Fmt.*` functions (ISSUE-08-001)
- Replace `c.code()` → `text[i].Asc()` (ISSUE-08-002)
- Replace `Char.fromCode(n)` → `Viper.String.Chr(n)` (ISSUE-08-003)
- Replace `'A'` character literals → `"A"` string literals
- Fix `words.Join(" ")` → `Viper.String.Join(" ", words)` (ISSUE-08-004)
- **Effort**: ~2 hours

### Ch10: Fix error handling examples
- Replace `bind Viper.File;` → `bind File = Viper.IO.File;` (ISSUE-10-001)
- Replace typed catch `catch e: FileNotFound` → generic `catch e` (ISSUE-10-002)
- Replace custom errors `throw FileNotFound(...)` → `throw Error(...)` (ISSUE-10-003)
- Fix bare function names (ISSUE-10-004)
- Fix `ReadLine().Trim()` → `InputLine().Trim()` (ISSUE-10-005)
- **Effort**: ~3 hours

### Ch12: Fix module binding examples
- Replace per-item alias `bind Mod { x as y }` → `bind Mod as M;` (ISSUE-12-001)
- Fix `Vec2.Add` → `Vec2.add` casing (ISSUE-12-002)
- **Effort**: ~30 minutes

### Ch13: Fix stdlib examples
- Fix all bare function names + wrong module paths (ISSUE-13-001 through 13-005)
- **Effort**: ~2 hours

### Ch15: Fix list method names
- `.Append(item)` → `.Push(item)`, `.removeLast()` → `.Pop()` (ISSUE-15-001)
- **Effort**: ~15 minutes

### Ch16-18: Fix collection/interface issues
- `.remove(obj)` → iterate-to-find-index pattern (ISSUE-16-001)
- `.Write()`/`.Read()` casing → `.write()`/`.read()` (ISSUE-18-001)
- **Effort**: ~30 minutes

### Ch22: Fix networking examples
- Fix module paths (ISSUE-22-003)
- Fix method casing `Http.post` → `Http.Post` (ISSUE-22-004)
- Fix TCP patterns (ISSUE-22-002)
- After A1: Http.Put/Delete will work
- **Effort**: ~2 hours

### Ch23: Fix data format examples
- Fix `Viper.JSON` → `Viper.Text.Json` (ISSUE-23-001)
- Fix `Viper.CSV` → `Viper.Text.Csv` (ISSUE-23-002)
- Fix `[Type]` syntax + File module path (ISSUE-23-003)
- **Effort**: ~1 hour

### Appendix D: Fix runtime reference
- Fix Convert path (ISSUE-D-001)
- Add auto-dispatch note (ISSUE-D-002)
- Rewrite Canvas section (ISSUE-D-003 through D-007) — after B4
- **Effort**: ~2 hours

---

## Phase 4: Aspirational Features — Mark as Planned (Category C)

These features don't exist and are too large to implement. Add "Coming Soon" notices in the Bible.

### C1: Threading API (Ch24)
- `Thread.spawn()`, `Atomic[T]`, `ThreadPool`, `Future<T>` — none exist
- **Action**: Add disclaimer at chapter start: "This chapter describes Viper's planned concurrency model. Currently available: `Viper.Threads.ConcurrentQueue` and `Viper.Threads.ConcurrentMap`."
- Add working examples using ConcurrentQueue/ConcurrentMap
- **Effort**: ~1 hour for disclaimer + ~3 hours for alternative examples

### C2: Profiler CLI flag (Ch26)
- `--profile` flag doesn't exist
- **Action**: Remove profiler section or add "planned feature" notice. Replace with actual available diagnostics (`--dump-il`, `--dump-il-opt`, etc.)
- **Effort**: ~1 hour

### C3: Typed Catch / Custom Error Types (Ch10)
- `catch e: FileNotFound` and `throw FileNotFound(...)` not supported
- **Action**: Already handled in Phase 3 Ch10 fixes (replace with generic catch/Error())

### C4: `[Type]` Shorthand Syntax
- `var x: [Integer] = []` not supported by parser
- **Action**: Already handled in B1 (replace with `List[Type]`)
- **Future**: Could add parser support as a low-priority enhancement

---

## Execution Order

| Step | Phase | Task | Est. Hours | Depends On |
|------|-------|------|-----------|------------|
| 1 | A1 | Http.Put/Delete | 1h | — |
| 2 | A2 | Range .rev()/.step() | 3-4h | — |
| 3 | A3 | Color constants | 1h | — |
| 4 | — | Build + test all platform changes | 0.5h | 1-3 |
| 5 | B1 | `[Type]` → `List[Type]` all files | 2h | — |
| 6 | B3 | Wrong module paths all files | 1h | — |
| 7 | B2 | Bare function names (Ch09/10/13) | 3h | — |
| 8 | B4 | Canvas/Graphics rewrite (Ch19/20/21/AppD) | 10h | 3 (Color constants) |
| 9 | — | Ch05 (.rev/.step examples) | 0.5h | 2 |
| 10 | — | Ch08 (strings) | 2h | — |
| 11 | — | Ch10 (errors) | 3h | 7 |
| 12 | — | Ch12 (modules) | 0.5h | — |
| 13 | — | Ch13 (stdlib) | 2h | 6 |
| 14 | — | Ch15-18 (OOP) | 0.5h | — |
| 15 | — | Ch22 (networking) | 2h | 1, 6 |
| 16 | — | Ch23 (data formats) | 1h | 5, 6 |
| 17 | C1 | Ch24 (concurrency) disclaimer + examples | 4h | — |
| 18 | C2 | Ch26 (performance) profiler notice | 1h | — |
| 19 | — | Final build + full test suite | 0.5h | all |

**Total estimated effort**: ~38-42 hours

---

## Verification

After each phase:

1. **Platform changes (A1-A3)**: `./scripts/build_viper.sh` + `ctest --test-dir build --output-on-failure` + `./scripts/check_runtime_completeness.sh`
2. **Doc fixes**: Manual spot-check of 5-10 code blocks per chapter against runtime.def
3. **Final validation**: Write a comprehensive test program exercising all newly-implemented features:
```zia
module AuditTest;
bind Viper.Terminal;
bind Viper.Collections;
bind Http = Viper.Network.Http;
bind Viper.Graphics.Color;

func start() {
    // A1: Http verbs
    // var r = Http.Put("http://httpbin.org/put", "data");
    // var d = Http.Delete("http://httpbin.org/delete");

    // A2: Range methods
    for i in (1..=5).rev() { Print(i + " "); }  // 5 4 3 2 1
    Say("");
    for i in (0..=10).step(2) { Print(i + " "); }  // 0 2 4 6 8 10
    Say("");

    // A3: Color constants
    Say(Color.RED);    // Should print packed i64
    Say(Color.BLUE);
}
```

4. **All 1284+ tests must pass** with zero regressions.

---

## Files Modified Summary

### Platform (Category A) — ~15 files
- `src/il/runtime/runtime.def`
- `src/runtime/network/rt_network_http.c`, `rt_network.h`
- `src/frontends/zia/AST_Expr.hpp`
- `src/frontends/zia/Parser_Expr.cpp`
- `src/frontends/zia/Sema_Expr_Advanced.cpp`
- `src/frontends/zia/Lowerer_Stmt.cpp`
- `src/frontends/zia/ZiaAstPrinter.cpp`
- `src/runtime/graphics/rt_drawing.c` (or similar for Color constants)

### Documentation (Category B+C) — ~20 Bible files
- `docs/bible/part1-foundations/05-repetition.md`
- `docs/bible/part1-foundations/06-collections.md`
- `docs/bible/part1-foundations/07-functions.md`
- `docs/bible/part2-building-blocks/08-strings.md`
- `docs/bible/part2-building-blocks/09-files.md`
- `docs/bible/part2-building-blocks/10-errors.md`
- `docs/bible/part2-building-blocks/12-modules.md`
- `docs/bible/part2-building-blocks/13-stdlib.md`
- `docs/bible/part3-objects/15-inheritance.md`
- `docs/bible/part3-objects/16-interfaces.md`
- `docs/bible/part3-objects/18-patterns.md`
- `docs/bible/part4-applications/19-graphics.md`
- `docs/bible/part4-applications/20-input.md`
- `docs/bible/part4-applications/21-game-project.md`
- `docs/bible/part4-applications/22-networking.md`
- `docs/bible/part4-applications/23-data-formats.md`
- `docs/bible/part4-applications/24-concurrency.md`
- `docs/bible/part5-mastery/26-performance.md`
- `docs/bible/appendices/a-zia-reference.md`
- `docs/bible/appendices/d-runtime-reference.md`
