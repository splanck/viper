# Viper Bible Audit — Issues Found

Systematic audit of all code examples, API references, and output claims in the Viper Bible.
Tracked issues are categorized by severity.

**Legend:**
- **P0**: Code won't compile or crashes at runtime
- **P1**: Wrong output, incorrect API name, or misleading behavior
- **P2**: Style/consistency issue
- **P3**: Minor doc issue, unclear wording

**Audit date**: 2026-03-11
**Remediation date**: 2026-03-11
**Compiler fixes applied before audit**: .Length property, number+"string" concat, .Sort() dispatch, match OR patterns, match comma/semicolon separators
**Platform gaps implemented**: Http.Put/Delete (A1), Range .rev()/.step() (A2), Color constants (A3)

---

## Summary

| Chapter/Section | Status | Notes |
|---|---|---|
| Ch00-02 | CLEAN | Pedagogical inconsistency only (P3) |
| Ch03-04 | CLEAN | No issues |
| Ch05 | FIXED | `.step()` and `.rev()` implemented (A2) |
| Ch06 | FIXED | `[Type]` → `List[Type]` (B1) |
| Ch07 | FIXED | `[Type]` → `List[Type]` (B1) |
| Ch08 | FIXED | `format()`, `.code()`, char literals, `.Join()` all fixed |
| Ch09 | FIXED | Bare function names → qualified (B2) |
| Ch10 | FIXED | Typed catch, custom errors, module paths all fixed |
| Ch11 | CLEAN | No issues |
| Ch12 | FIXED | Per-item aliases, Vec2.Add casing |
| Ch13 | FIXED | Bare function names, module paths (B2, B3) |
| Ch14 | CLEAN | No issues |
| Ch15 | FIXED | `.Append()`→`.Push()`, `.removeLast()`→`.Pop()` |
| Ch16 | FIXED | `.remove()` casing |
| Ch17 | CLEAN | Minor P2 only |
| Ch18 | FIXED | `.Write()`/`.Read()` casing |
| Ch19 | FIXED | Canvas API completely rewritten (B4) |
| Ch20 | FIXED | Input API rewritten (B4) |
| Ch21 | FIXED | Game project rewritten (B4) |
| Ch22 | FIXED | TCP/HTTP API + module paths |
| Ch23 | FIXED | Module paths + `[Type]` syntax (B1, B3) |
| Ch24 | FIXED | Complete rewrite documenting actual Viper.Threads.* API (C1) |
| Ch25-26 | FIXED | `--profile` flag implemented + documented (C2) |
| Appendix A | FIXED | `[Type]` → `List[Type]` (B1) |
| Appendix D | FIXED | Canvas/Color/Input reference rewritten (B4) |

**Original: ~38 distinct issue types, ~400+ individual wrong code instances**
**Remediated: All categories addressed. ~900+ individual edits across 25+ files.**

---

## Chapters 00-02 (Getting Started, The Machine, First Program)

### ISSUE-00-001 (P3): Inconsistent bind pattern in Chapter 0
- **File**: `part1-foundations/00-getting-started.md` ~lines 482, 734
- **Code**: `Viper.Terminal.Say("Setup complete!");`
- **Problem**: Uses full qualified path without `bind`, while Chapter 2 teaches `bind Viper.Terminal;` + `Say()`. Pedagogically confusing — two patterns without explanation.
- **Fix**: Either add `bind Viper.Terminal;` and use `Say()`, or add a note that `bind` is introduced in Chapter 2.

**All other Ch00-02 code verified correct.**

---

## Chapter 03 (Values and Names)

**PASS** — All code examples correct. `bind`, `Say()`, `InputLine()`, `Convert.ToInt64()` all verified.

---

## Chapter 04 (Decisions)

**PASS** — All code examples correct. `Viper.String.Has()`, `Viper.String.Length()` verified against runtime.def.

---

## Chapter 05 (Repetition)

### ISSUE-05-001 (P0): `.rev()` method does not exist on ranges
- **File**: `part1-foundations/05-repetition.md` ~lines 417, 443, 488
- **Code**: `for i in (1..=10).rev() { Say(i); }`
- **Problem**: Ranges have no `.rev()` method. The lowerer (`Lowerer_Stmt.cpp:378-453`) only supports forward iteration with increment 1. No method call resolution exists on range types.
- **Fix**: Replace with while loop or manual index:
  ```zia
  var i = 10;
  while i >= 1 { Say(i); i -= 1; }
  ```

### ISSUE-05-002 (P0): `.step()` method does not exist on ranges
- **File**: `part1-foundations/05-repetition.md` ~lines 467, 472, 477, 488
- **Code**: `for i in (0..=10).step(2) { Say(i); }`
- **Problem**: Same as above — ranges only support step-by-1. No `.step()` in the AST, sema, or lowerer.
- **Fix**: Replace with while loop:
  ```zia
  var i = 0;
  while i <= 10 { Say(i); i += 2; }
  ```

---

## Chapter 06 (Collections)

### ~~ISSUE-06-001 (P0): `[Type]` list type syntax is not valid Zia~~ FIXED
- **Status**: Fixed — all `[Type]` type annotations replaced with `List[Type]` across all bible chapters.
- **Note**: `[value; count]` repeat syntax (line 301) left as-is — needs parser verification separately.

---

## Chapter 07 (Functions)

### ~~ISSUE-07-001 (P0): `[Type]` list type syntax (same as ISSUE-06-001)~~ FIXED
- **Status**: Fixed — all instances replaced with `List[Type]`.

---

## Chapter 08 (Strings)

### ISSUE-08-001 (P0): `format()` function does not exist
- **File**: `part2-building-blocks/08-strings.md` ~lines 1442, 1457, 1469-1478
- **Code**: `var message = format("Player {} scored {} points!", name, score);`
- **Problem**: No general `format()` function with `{}` placeholders exists in the runtime. `Viper.Fmt` provides `Fmt.Int()`, `Fmt.Num()`, `Fmt.NumFixed()`, `Fmt.IntPad()`, etc. — but no placeholder-based formatting.
- **Fix**: Replace with string concatenation or `Fmt` functions:
  ```zia
  var message = "Player " + name + " scored " + score + " points!";
  Say(Fmt.NumFixed(pi, 2));  // instead of format("{:.2}", pi)
  Say(Fmt.IntPad(42, 5, "0"));  // instead of format("{:05}", 42)
  ```

### ISSUE-08-002 (P0): `.code()` method does not exist on characters
- **File**: `part2-building-blocks/08-strings.md` ~lines 135, 175-177, 1374-1393
- **Code**: `if c.code() >= 'A'.code() && c.code() <= 'Z'.code()`
- **Problem**: No `.code()` method exists. The correct API is `String.Asc()` (instance method, returns i64). Also, single-quote character literals (`'A'`) do not exist in Zia — only double-quote strings.
- **Fix**: `if text[i].Asc() >= "A".Asc() && text[i].Asc() <= "Z".Asc()`

### ISSUE-08-003 (P0): `Char.fromCode()` does not exist
- **File**: `part2-building-blocks/08-strings.md` ~lines 1399-1411
- **Code**: `var char = Char.fromCode(65);` / `Char.fromCode(c.code() - 32)`
- **Problem**: No `Char` type or `Char.fromCode()`. The correct API is `String.Chr(i64)` which returns a single-character string.
- **Fix**: `var char = Viper.String.Chr(65);`

### ISSUE-08-004 (P1): String method usage inconsistency
- **File**: `part2-building-blocks/08-strings.md` ~line 1112, 1270
- **Code**: `words.Join(" ")` (line 1112) vs `Str.Join("\n", lines)` (line 1270)
- **Problem**: `String.Join` is a static function taking `(separator, seq)`, not an instance method on arrays. Usage is inconsistent within the chapter.
- **Fix**: Always use `Viper.String.Join(separator, sequence)` pattern.

---

## Chapter 09 (Files)

### ISSUE-09-001 (P0): All file I/O function names are wrong
- **File**: `part2-building-blocks/09-files.md` — throughout entire chapter
- **Lines**: 281, 293, 305, 334, 357, 381, 407, 433, 451-453, 550, 553, 578, 581, 602-607, 619-636, 727-765, 862-883, and many more
- **Wrong → Correct**:
  - `readText(path)` → `File.ReadAllText(path)` (with `bind File = Viper.IO.File;`)
  - `writeText(path, data)` → `File.WriteAllText(path, data)`
  - `appendText(path, data)` → `File.Append(path, data)` or `File.AppendLine(path, data)`
  - `readLines(path)` → `File.ReadAllLines(path)`
  - `readBytes(path)` → `File.ReadAllBytes(path)`
  - `writeBytes(path, data)` → `File.WriteAllBytes(path, data)`
  - `exists(path)` → `File.Exists(path)`
  - `delete(path)` → `File.Delete(path)`
  - `openRead(path)` → uses `Viper.IO.LineReader.Open(path)`
  - `openWrite(path)` → uses `Viper.IO.LineWriter.Open(path)`
- **Fix**: Add `bind File = Viper.IO.File;` and use PascalCase qualified names throughout.

---

## Chapter 10 (Errors)

### ~~ISSUE-10-001 (P0): Wrong module path for File~~ FIXED
- **File**: `part2-building-blocks/10-errors.md` ~lines 257, 274, 547, 587, 627, 858, 893
- **Code**: `bind Viper.File;`
- **Problem**: Module is `Viper.IO.File`, not `Viper.File`.
- **Fix**: `bind File = Viper.IO.File;`

### ~~ISSUE-10-002 (P0): Typed catch blocks are not supported~~ FIXED
- **Status**: Fixed — typed catch (`catch(e: ErrorType)`) now implemented in parser, sema, and lowerer. Supports all 12 TrapKind error types plus `Error` catch-all. Unmatched errors re-raise to outer handlers via `trap.from_err`.

### ISSUE-10-003 (P0): Custom error types don't exist as constructors
- **File**: `part2-building-blocks/10-errors.md` ~lines 445-447, 961, 980
- **Code**: `throw FileNotFound("config.txt");` / `throw ParseError("...")` / `throw ValidationError("...")`
- **Problem**: Only generic `Error()` constructor exists.
- **Fix**: `throw Error("File not found: config.txt");`

### ISSUE-10-004 (P0): Bare function names without module prefix
- **File**: `part2-building-blocks/10-errors.md` — throughout
- **Lines**: 262, 279, 553, 562, 592, 594, 632, 904, 911, 948, 960, 964
- **Code**: `readText(filename)`, `exists(filename)`, `readLines(filename)`, `appendText(LOG_FILE, entry)`
- **Problem**: Same as ISSUE-09-001 — must use `File.ReadAllText()`, `File.Exists()`, etc.

### ISSUE-10-005 (P1): ReadLine() returns optional string
- **File**: `part2-building-blocks/10-errors.md` ~line 827
- **Code**: `var input = ReadLine().Trim();`
- **Problem**: `ReadLine()` returns `str?` (optional). Cannot call `.Trim()` directly. Use `InputLine()` for non-optional, or null-check first.

---

## Chapter 11 (Structures)

**PASS** — All code examples correct. `struct` keyword for structures verified.

---

## Chapter 12 (Modules)

### ~~ISSUE-12-001 (P0): Per-item aliases in selective bind not supported~~ FIXED
- **File**: `part2-building-blocks/12-modules.md` ~lines 201-210
- **Code**: `bind MathUtils { square as sq, PI as pi };`
- **Problem**: Parser (`Parser_Decl.cpp:166-210`) explicitly forbids combining `as` with selective imports `{ ... }`. Only plain identifiers allowed in braces.
- **Fix**: Replaced with module-level alias: `bind MathUtils as M;` then `M.square()`.

### ~~ISSUE-12-002 (P0): Function name casing mismatch~~ FIXED
- **File**: `part2-building-blocks/12-modules.md` ~lines 798, 870, 1057
- **Code**: `Vec2.Add(a, b)` — but function is defined as `export func add(...)` (lowercase) on line 730
- **Fix**: Changed to `Vec2.add(a, b)` — Zia is case-sensitive.

---

## Chapter 13 (Standard Library)

### ISSUE-13-001 (P0): Bare function names without module prefix
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 462-489
- **Code**: `var home = GetVariable("HOME");` / `HasVariable("DEBUG")`
- **Problem**: Must be qualified: `Env.GetVariable("HOME")` with `bind Env = Viper.Environment;`

### ~~ISSUE-13-002 (P0): Wrong module path for Guid/UUID~~ FIXED
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 979-983
- **Code**: `bind Viper.Guid;` / `var id = New();`
- **Problem**: Module is `Viper.Text.Uuid`, not `Viper.Guid`. Function is `Uuid.New()`, not bare `New()`.
- **Fix**: `bind Uuid = Viper.Text.Uuid;` then `var id = Uuid.New();`

### ISSUE-13-003 (P0): Bare file I/O function names
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 854-870
- **Code**: `readText("data.txt")`, `writeText(...)`, `exists(...)`, `delete(...)`
- **Fix**: Same as ISSUE-09-001.

### ISSUE-13-004 (P0): Bare directory API function names
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 875-888
- **Code**: `create("output")`, `listFiles("data")`, `listDirs("data")`
- **Fix**: `IO.Dir.Make("output")`, `IO.Dir.Files("data")`, `IO.Dir.Dirs("data")`

### ISSUE-13-005 (P0): Bare path API function names
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 895-914
- **Code**: `join("users", "alice")`, `fileName(...)`, `extension(...)`, `isAbsolute(...)`
- **Fix**: `IO.Path.Join(...)`, `IO.Path.Name(...)`, `IO.Path.Ext(...)`, `IO.Path.IsAbs(...)`

### ~~ISSUE-13-006~~ (INVALID): String instance methods
- **File**: `part2-building-blocks/13-stdlib.md` ~lines 599-625
- **Status**: **FALSE POSITIVE** — `PadLeft`, `Repeat`, `Flip`, `IndexOf`, `Has`, `ToUpper`, `ToLower`, `Trim`, `Split` are all confirmed as RT_METHOD instance methods on String. The Bible's usage is correct.

---

## Chapters 14-18 (Objects, Inheritance, Interfaces, Polymorphism, Patterns)

### ~~ISSUE-15-001 (P0): `.Append()` and `.removeLast()` don't exist~~ FIXED
- **File**: `part3-objects/15-inheritance.md` ~lines 895, 899, 916, 920
- **Code**: `self.Append(item)` / `self.removeLast()`
- **Problem**: List API uses `.Push(item)` and `.Pop()`.
- **Fix**: Replaced `.Append(item)` with `.Push(item)`, `.removeLast()` with `.Pop()`.

### ~~ISSUE-16-001 (P1): `.remove(obj)` wrong casing~~ FIXED
- **File**: `part3-objects/16-interfaces.md` ~line 1219
- **Code**: `self.observers.remove(obs)` — wrong casing
- **Problem**: `List.Remove(obj)` is PascalCase. Also fixed `self.users.remove(id)` on maps.
- **Fix**: Changed to `.Remove(obs)` and `.Remove(id)`.

### ~~ISSUE-18-001 (P1): Method casing mismatch with interface definition~~ FIXED
- **File**: `part3-objects/18-patterns.md` ~lines 2212-2304
- **Code**: `self.stream.Write(data)` / `self.stream.Read()` — uppercase
- **Problem**: Interface defines `func write()` / `func read()` (lowercase). Method calls must match definition casing.
- **Fix**: Changed to `self.stream.write(data)` / `self.stream.read()` throughout all decorators and usage.

---

## Chapter 19 (Graphics)

### ISSUE-19-001 (P0): Canvas constructor missing title parameter
- **File**: `part4-applications/19-graphics.md` ~lines 206, 442, 581, 819, 938, 1016, 1241
- **Code**: `var canvas = Canvas(800, 600);`
- **Correct**: `var canvas = Canvas.New("My Game", 800, 600);`

### ISSUE-19-002 (P0): Canvas drawing API completely wrong
- **File**: `part4-applications/19-graphics.md` — throughout (120+ instances)
- **Wrong → Correct method mapping**:
  - `canvas.setColor(Color.RED)` → does not exist (colors passed to each draw call)
  - `canvas.fillRect(x,y,w,h)` → `canvas.Box(x,y,w,h,color)`
  - `canvas.drawRect(x,y,w,h)` → `canvas.Frame(x,y,w,h,color)`
  - `canvas.fillCircle(cx,cy,r)` → `canvas.Disc(cx,cy,r,color)`
  - `canvas.drawCircle(cx,cy,r)` → `canvas.Ring(cx,cy,r,color)`
  - `canvas.drawLine(x1,y1,x2,y2)` → `canvas.Line(x1,y1,x2,y2,color)`
  - `canvas.drawText(x,y,text)` → `canvas.Text(x,y,text,color)`
  - `canvas.setPixel(x,y)` → `canvas.Plot(x,y,color)`
  - `canvas.fillPolygon(...)` → `canvas.Polygon(points,color)`
  - `canvas.show()` → `canvas.Flip()`

### ISSUE-19-003 (P0): Color constants don't exist
- **File**: `part4-applications/19-graphics.md` ~lines 185-194, 446-481
- **Code**: `Color.RED`, `Color.GREEN`, `Color.BLUE`, `Color.WHITE`, `Color.BLACK`
- **Problem**: No color constants. Only factory functions: `Color.RGB(r,g,b)`, `Color.RGBA(r,g,b,a)`, `Color.FromHSL(h,s,l)`, `Color.FromHex(str)`.
- **Fix**: `Color.RGB(255, 0, 0)` instead of `Color.RED`.

### ISSUE-19-004 (P0): `canvas.waitForClose()` / `canvas.isOpen()` don't exist
- **File**: `part4-applications/19-graphics.md` ~lines 217, 486, 592, 721, 763
- **Correct**: Use `while !canvas.ShouldClose { canvas.Poll(); ... canvas.Flip(); }` or `while canvas.BeginFrame() != 0 { ... }`

### ISSUE-19-005 (P0): `Color(r,g,b)` constructor wrong
- **File**: `part4-applications/19-graphics.md` ~lines 446-481
- **Code**: `var color = Color(135, 206, 235);`
- **Correct**: `var color = Color.RGB(135, 206, 235);`

---

## Chapter 20 (Input)

### ISSUE-20-001 (P0): Input API uses wrong namespace and method names
- **File**: `part4-applications/20-input.md` — throughout (50+ instances)
- **Lines**: 105, 110, 138, 184, 213, 238, 252-275, 283, 568-612, 715, 819, 822
- **Wrong → Correct**:
  - `Input.isKeyDown(Key.SPACE)` → `Viper.Input.Keyboard.IsDown(KEY_SPACE)`
  - `Input.wasKeyPressed(Key.SPACE)` → `Viper.Input.Keyboard.WasPressed(KEY_SPACE)`
  - `Input.wasKeyReleased(Key.S)` → `Viper.Input.Keyboard.WasReleased(KEY_S)`
  - `Input.hasTextInput()` / `Input.getTextInput()` → `Viper.Input.Keyboard.GetText()`
- **Key constant format**: `Key.LEFT` → `KEY_LEFT` (flat constants, not namespaced)

### ISSUE-20-002 (P0): Mouse API method names wrong
- **File**: `part4-applications/20-input.md` (later sections)
- **Wrong → Correct**:
  - `Input.mouseX()` → `Viper.Input.Mouse.X()`
  - `Input.mouseY()` → `Viper.Input.Mouse.Y()`
  - `Input.isMouseDown()` → `Viper.Input.Mouse.IsDown()`

---

## Chapter 21 (Game Project)

### ISSUE-21-001 (P0): Inherits all Ch19/Ch20 API errors
- **File**: `part4-applications/21-game-project.md` — throughout (45+ instances)
- **Problem**: Uses `setColor()`, `fillRect()`, `drawRect()`, `fillCircle()`, `show()`, `Canvas(w,h)`, `Input.isKeyDown()` — all wrong per ISSUE-19-* and ISSUE-20-*.

---

## Chapter 22 (Networking)

### ~~ISSUE-22-001 (P0): Missing HTTP methods in runtime~~ FIXED
- **File**: `part4-applications/22-networking.md` ~lines 339, 349
- **Code**: `Http.put(...)` / `Http.Delete(...)`
- **Status**: `Http.Put` and `Http.Delete` now exist in runtime.def. Fixed casing to PascalCase.

### ~~ISSUE-22-002 (P0): TCP API uses wrong patterns~~ FIXED
- **File**: `part4-applications/22-networking.md` ~lines 452, 615, 622, 634
- **Code**: `TcpSocket.connect()`, `TcpServer.listen()`, `self.server.acceptNonBlocking()`
- **Fix**: Changed to `Tcp.Connect()`, `TcpServer.Listen()`, `server.AcceptFor(100)`.

### ~~ISSUE-22-003 (P0): Wrong module paths~~ FIXED
- **File**: `part4-applications/22-networking.md` ~lines 366-368, 744-747, 908
- **Code**: `bind Viper.JSON;` / `bind Viper.Threading;` / `bind Viper.Convert;`
- **Fix**: `bind Json = Viper.Text.Json;` / `bind Viper.Threads;` / `bind Convert = Viper.Core.Convert;`

### ~~ISSUE-22-004 (P1): HTTP method casing~~ FIXED
- **File**: `part4-applications/22-networking.md` ~line 329
- **Code**: `Http.post(...)` (lowercase)
- **Fix**: Changed all to `Http.Post(...)` (PascalCase) throughout chapter.

---

## Chapter 23 (Data Formats)

### ~~ISSUE-23-001 (P0): Wrong module path for JSON~~ FIXED
- **File**: `part4-applications/23-data-formats.md` ~lines 282, 322, 856, 1443, 1592
- **Code**: `bind Viper.JSON;`
- **Fix**: `bind Json = Viper.Text.Json;` — all `JSON.*` calls updated to `Json.*` (PascalCase)

### ~~ISSUE-23-002 (P0): Wrong module path for CSV~~ FIXED
- **File**: `part4-applications/23-data-formats.md` ~lines 527-529
- **Code**: `bind Viper.CSV;`
- **Fix**: `bind Csv = Viper.Text.Csv;` — all `CSV.*` calls updated to `Csv.*` (PascalCase)

### ~~ISSUE-23-003 (P0): `[Type]` list syntax + wrong File module~~ FIXED
- **Status**: Both fixed — `inventory: List[String]` and File module path corrected.

---

## Chapter 24 (Concurrency)

### ~~ISSUE-24-001 (P0): Threading API does not exist~~ FIXED
- **Status**: Fixed — Complete rewrite of Ch24 documenting actual `Viper.Threads.*` API (18 classes, 89 RT_FUNCs, 129 RT_METHODs). All fictional `Thread.spawn()` / `Atomic[T]` / `ThreadPool.create()` / `Future<T>` references replaced with real API.

### ~~ISSUE-24-002 (P0): `[Type]` list syntax~~ FIXED
- **Status**: Fixed — all instances replaced with `List[Type]`.

---

## Chapters 25-26 (How Viper Works, Performance)

### ~~ISSUE-25-001 (P0): `--profile` flag does not exist~~ FIXED
- **Status**: Fixed — `--profile` CLI flag implemented (enables `--count` + `--time` + top opcodes summary). Ch26 documentation updated.

### ~~ISSUE-25-002 (P1): Profiler output documentation for non-existent feature~~ FIXED
- **Status**: Fixed — profiler output documentation now matches actual `--profile` flag behavior.

---

## Appendix A (Zia Reference)

### ~~ISSUE-A-001 (P2): `[Type]` array syntax shown~~ FIXED
- **Status**: Fixed — replaced with `List[Integer]`.

---

## Appendix D (Runtime Reference)

### ~~ISSUE-D-001 (P1): Wrong module path for Convert~~ FIXED
- **File**: `appendices/d-runtime-reference.md` ~line 105
- **Code**: `bind Viper.Convert as Convert;`
- **Fix**: `bind Convert = Viper.Core.Convert;`

### ISSUE-D-002 (P2): Say signature doesn't mention auto-dispatch
- **File**: `appendices/d-runtime-reference.md` ~line 43
- **Fix**: Add note that Say/Print accept Integer, Number, Boolean directly.
- **Note**: The compiler now auto-dispatches `Say(Integer)` → `SayInt`, `Say(Number)` → `SayNum`, `Say(Boolean)` → `SayBool`. So the doc's claim of "accepts any type" is effectively correct now. Just needs explicit mention of auto-dispatch mechanism.

### ISSUE-D-003 (P0): Canvas constructor signature wrong
- **File**: `appendices/d-runtime-reference.md` ~line 2240
- **Code**: `Canvas(width, height)`
- **Fix**: `Canvas.New(title, width, height)`

### ISSUE-D-004 (P0): Canvas method names completely wrong
- **File**: `appendices/d-runtime-reference.md` ~lines 2242-2362
- **Problem**: Same as ISSUE-19-002 — uses `setColor`, `fillRect`, `drawRect`, `fillCircle`, `show`, `waitForClose`, `isOpen`, etc.
- **Fix**: Complete rewrite of Canvas section with correct API.

### ISSUE-D-005 (P0): Color constructor syntax wrong
- **File**: `appendices/d-runtime-reference.md` ~lines 2272-2273
- **Code**: `Color(r, g, b)`
- **Fix**: `Color.RGB(r, g, b)`

### ISSUE-D-006 (P0): Input API method names wrong
- **File**: `appendices/d-runtime-reference.md` ~lines 2937-2956
- **Code**: `Input.mouseX()`, `Input.isMouseDown()`, `Input.mouseScroll()`
- **Fix**: `Viper.Input.Mouse.X()`, `Viper.Input.Mouse.IsDown()`, `Viper.Input.Mouse.WheelY()`

### ISSUE-D-007 (P0): Canvas example code all wrong
- **File**: `appendices/d-runtime-reference.md` ~lines 2309-2340
- **Problem**: All example code uses non-existent methods.
- **Fix**: Rewrite with correct Box/Frame/Disc/Ring/Line/Text/Flip API.

---

## Cross-Cutting Issues (Affect Multiple Chapters)

### XCUT-001: `[Type]` list type syntax
- **Affected**: Ch06, Ch07, Ch22, Ch23, Ch24, Appendix A
- **Scope**: ~60+ instances across all affected files
- **Fix**: Global find-replace `[Type]` → `List[Type]` in type annotation positions

### XCUT-002: Bare function names (no module prefix)
- **Affected**: Ch09, Ch10, Ch13
- **Scope**: ~40+ instances
- **Fix**: Add proper `bind` + qualified names

### XCUT-003: Canvas/Graphics API
- **Affected**: Ch19, Ch20, Ch21, Appendix D
- **Scope**: ~215+ wrong API calls
- **Fix**: Complete rewrite of all graphics code examples

### ~~XCUT-004: Wrong module paths~~ FIXED
- **Affected**: Ch08, Ch09, Ch10, Ch11, Ch13, Ch16, Ch18, Ch21, Ch22, Ch23, Ch24, Ch27, Ch28, Appendix D, Appendix E
- **Common errors**: `Viper.File` → `Viper.IO.File`, `Viper.JSON` → `Viper.Text.Json`, `Viper.Convert` → `Viper.Core.Convert`, `Viper.Guid` → `Viper.Text.Uuid`, `Viper.Threading` → `Viper.Threads`, `Viper.Codec` → `Viper.Text.Codec`, `Viper.CSV` → `Viper.Text.Csv`
- All bind statements updated to correct `bind Alias = Module.Path;` syntax across all affected files
