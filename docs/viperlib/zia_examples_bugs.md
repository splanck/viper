# Zia Examples Audit — Bug Report

> Generated during systematic audit of viper-lib documentation.
> Each entry records bugs found while testing Zia examples against both VM and ARM64 native codegen.

---

## Summary

- **Total demos tested:** 53
- **Fully passing (VM + native match):** 49
- **Bugs found:** 12 (3 ARM64-specific, 9 compiler/runtime)

### Test Categories

| Category | Demos | Pass | Fail | Notes |
|----------|-------|------|------|-------|
| Core + Utilities | 7 | 7 | 0 | Box, String, StringExt, Convert, Fmt, Log, Parse |
| Math | 5 | 5 | 0 | Bits, Math, Random, Vec2, Vec3 |
| Collections | 8 | 8 | 0 | Bag, Bytes, Heap, Queue, Ring, Seq, Stack, TreeMap |
| Text + Crypto | 7 | 6 | 1 | GuidDemo fails in native |
| Time + Diagnostics | 3 | 3 | 0 | Clock, DateTime, Diag |
| IO | 9 | 8 | 1 | FileDemo path functions corrupt in native |
| Threads + Sync | 4 | 2 | 2 | GateDemo/RwLockDemo non-zero exit codes in native |
| Text (new) | 5 | 4 | 1 | CsvDemo runtime bugs; Html, JsonPath, Markdown, Toml pass |
| Crypto (new) | 3 | 3 | 0 | CryptoRand, Cipher, KeyDerive all pass |
| System (new) | 2 | 2 | 0 | Exec, Machine pass |

### Root Cause Classification

| Root Cause | Bugs |
|------------|------|
| ARM64 codegen `i1` handling | #1 |
| ARM64 register allocator `Blr` omission | #2 |
| ARM64 codegen void return in `main` | #3 |
| Zia lowerer: opaque `Ptr` field/method fallthrough | #4, #5, #8, #10 |
| `runtime.def` / `ZiaRuntimeExterns.inc` wrong types | #6, #7 |
| Runtime API contract mismatch (box vs raw) | #9, #12 |
| Runtime missing input validation | #11 |

---

## ARM64 Codegen Bugs

### Bug #1: `Guid.IsValid()` returns `false` in ARM64 native

**Severity:** High
**Category:** ARM64 codegen / runtime ABI
**Demo:** `GuidDemo.zia`
**Affected class:** `Viper.Text.Guid`

#### Symptom

`Guid.IsValid(id)` returns `true` in the VM but always returns `false` in ARM64 native, even for freshly generated GUIDs.

#### Reproduction

```zia
module GuidDemo;
bind Viper.Text.Guid;
bind Viper.Terminal;

func start() {
    var id = Guid.New();
    Terminal.Say("ID: " + id);
    Terminal.Say("Valid: " + Guid.IsValid(id).ToString());
    Terminal.Say("Invalid: " + Guid.IsValid("not-a-guid").ToString());
}
```

**VM output:**
```
ID: <valid-guid>
Valid: true
Invalid: false
```

**ARM64 native output:**
```
ID: <valid-guid>
Valid: false
Invalid: false
```

#### Root Cause

**File:** `src/codegen/aarch64/OpcodeDispatch.cpp`, lines 721-737

The `case Opcode::Call` handler captures the return value from `x0` after a `bl` but does not distinguish `i1` from other integer types:

```cpp
// After bl, for non-F64 return types:
bbOut().instrs.push_back(MInstr{
    MOpcode::MovRR,
    {MOperand::vregOp(RegClass::GPR, dst), MOperand::regOp(PhysReg::X0)}});
```

Per AAPCS64, a C function returning `bool` only guarantees the low 8 bits of `w0` are meaningful. The codegen copies all 64 bits of `x0` without masking. When the runtime function returns a boolean via `w0`, the upper bits may contain garbage, causing subsequent comparisons to fail.

**Fix:** When `ins.type.kind == il::core::Type::Kind::I1`, emit `and dst, x0, #1` after the `mov` to mask the boolean result to a single bit.

---

### Bug #2: Path functions return corrupt values when passed `Path.Join()` result (ARM64 native)

**Severity:** High
**Category:** ARM64 codegen / register allocator
**Demo:** `FileDemo.zia`
**Affected classes:** `Viper.IO.Path` (Ext, Name, Dir, Stem)

#### Symptom

When `Path.Join()` returns a string that is stored in a local variable and then passed to `Path.Ext()`, `Path.Name()`, `Path.Dir()`, and `Path.Stem()`, the native binary returns wrong/corrupt values. The same Path functions work correctly with hardcoded string literals (as shown in `PathDemo.zia`).

#### Reproduction

```zia
module FileDemo;
bind Viper.IO.File;
bind Viper.IO.Path;
bind Viper.Terminal;

func start() {
    var path = Path.Join("/tmp", "test.txt");
    File.WriteAllText(path, "hello");
    Terminal.Say("Ext: " + Path.Ext(path));
    Terminal.Say("Name: " + Path.Name(path));
    Terminal.Say("Dir: " + Path.Dir(path));
    Terminal.Say("Stem: " + Path.Stem(path));
    File.Delete(path);
}
```

**VM output:**
```
Ext: .txt
Name: test.txt
Dir: /tmp
Stem: test
```

**ARM64 native output:**
```
Ext:
Name: Ext:
Dir: .
Stem: Ext:
```

#### Root Cause

**File:** `src/codegen/aarch64/RegAllocLinear.cpp`, lines 237-240

The `isCall()` function only recognizes direct calls:

```cpp
static bool isCall(MOpcode opc)
{
    return opc == MOpcode::Bl;  // Missing: || opc == MOpcode::Blr
}
```

`Blr` (indirect call via register, used for `CallIndirect` at OpcodeDispatch.cpp:842) is not recognized as a call. This means the register allocator does not spill caller-saved registers before indirect calls, nor does it account for them in liveness analysis (line 589 `callPositions_`).

When a runtime-returned string is stored in a caller-saved register and a subsequent `Blr` occurs, the callee clobbers the register. The string handle in the alloca's address register gets overwritten, causing subsequent loads to read from the wrong memory location (string constants from the IL pool leak through).

**Fix:** Change `isCall()` to `return opc == MOpcode::Bl || opc == MOpcode::Blr;`

---

### Bug #3: ARM64 codegen missing `mov x0, #0` before `ret` in `@main() -> void`

**Severity:** Medium
**Category:** ARM64 codegen
**Demos:** `GateDemo.zia` (exit code 32), `RwLockDemo.zia` (exit code 48)
**Affected:** All native binaries with `@main() -> void`

#### Symptom

Native binaries compiled from `@main() -> void` functions exit with non-zero exit codes. The exit code matches whatever value was left in `x0` by the last runtime function call before `ret`.

#### Reproduction

```zia
module GateDemo;
bind Viper.Threads.Gate;
bind Viper.Terminal;

func start() {
    var g = Gate.New(false);
    Terminal.Say("Before: " + Gate.IsOpen(g).ToString());
    Gate.Enter(g);
    Terminal.Say("After enter: " + Gate.IsOpen(g).ToString());
    Gate.Leave(g);    // <-- returns non-zero in x0
}
```

**VM:** output correct, exit code 0
**ARM64 native:** output correct, exit code 32

#### Root Cause

**File:** `src/codegen/aarch64/OpcodeDispatch.cpp`, lines 865-928

The `case Opcode::Ret` handler checks `ins.operands.empty()` for void returns and skips materializing a return value:

```cpp
case Opcode::Ret:
{
    if (!ins.operands.empty())
    {
        // ... materialize return value into x0/v0 ...
    }
    // For void returns: falls through directly to Ret MIR
    bbOut().instrs.push_back(MInstr{MOpcode::Ret, {}});
    return true;
}
```

No `mov x0, #0` is emitted for void functions. Since `main` is the C entry point, x0 at `ret` becomes the process exit code. The `AsmEmitter.cpp` (lines 819-826, 849-860) has special main handling for init calls but not for zeroing x0.

**Fix:** In the `Ret` handler, when `ins.operands.empty()` and `ctx.mf.name == "main"`, emit `MovRI x0, #0` before the `Ret`.

#### Affected demos

| Demo | Last runtime call | Exit code |
|------|------------------|-----------|
| GateDemo | `_rt_gate_leave` | 32 |
| RwLockDemo | `_rt_rwlock_write_exit` | 48 |
| SafeI64Demo | `_rt_term_say` | 0 (lucky) |
| BarrierDemo | `_rt_term_say` | 0 (lucky) |

---

## Zia Compiler / Lowerer Bugs

### Bug #4: `Seq.Get()` + `Box.ToStr()` codegen failure (call.indirect)

**Severity:** High
**Category:** Zia lowerer / IL codegen
**Affected classes:** Any use of `Seq.Get(index)` followed by `Box.ToStr()`

#### Symptom

When calling `seq.Get(index)` via `call.indirect` and then passing the result to `Box.ToStr()`, the call.indirect return value is not captured. The literal index value (e.g., `0`) leaks through as the argument to `Box.ToStr()` instead of the actual boxed value.

#### Reproduction

```zia
module CsvDemo;
bind Viper.Terminal;
bind Viper.Text.Csv as Csv;
bind Viper.Collections;

func start() {
    var fields = Csv.ParseLine("Alice,30,NY");
    var first = fields.Get(0);      // call.indirect - return not captured
    var name = Box.ToStr(first);    // receives literal 0 instead of ptr
    Say("Name: " + name);
}
```

**Result:** Compiles but produces wrong output. The generated IL shows `call.indirect` for `Get` but the return value register is not used.

#### Root Cause

The bug traces through a chain of failures when the receiver has opaque `Ptr` type (no class name):

1. **Sema fails name check** — `src/frontends/zia/Sema_Expr.cpp`, line 1015: `baseType->name.find("Viper.") == 0` fails because the `Ptr` type has an empty name (returned from a function declared as `types::ptr()`).

2. **Lowerer falls through to indirect call** — `src/frontends/zia/Lowerer_Expr_Call.cpp`, lines 1092-1131: Since `sema_.runtimeCallee(expr)` returns empty, the callee expression is lowered as a field access.

3. **Field access returns constInt(0)** — `src/frontends/zia/Lowerer_Expr.cpp`, line 512: Unresolved field access on an opaque `Ptr` returns `{Value::constInt(0), Type(Type::Kind::I64)}`.

4. **Return type resolves to Void** — `src/frontends/zia/Lowerer_Expr_Call.cpp`, lines 1140-1141: `calleeType->returnType()` returns nullptr (from `types::unknown()`), so `ilReturnType` becomes `Void`.

5. **Void call.indirect emitted** — Lines 1237-1240: `emitCallIndirect(funcPtr, args)` with void return; no result captured. Returns `{constInt(0), Void}`.

**Fix:** The root fix is Bug #7 (correct the return types). Additionally, the lowerer at line 512 should emit a diagnostic rather than silently returning `constInt(0)`.

---

### Bug #5: `Bytes.ToHex()` / `Bytes.ToStr()` inline method call causes lowerer type corruption

**Severity:** High
**Category:** Zia lowerer / semantic analysis
**Affected classes:** Any `ptr`-typed value where `.ToHex()` or `.ToStr()` is called as an inline method

#### Symptom

When calling `bytes.ToHex()` on a value whose Zia semantic type is `ptr` (not recognized as `Bytes`), the lowerer loses the return type and tracks it as `i64` instead of `str`. This causes downstream type mismatches (e.g., `str` + `i64` in concatenation).

#### Reproduction

```zia
module Demo;
bind Viper.Terminal;
bind Viper.Crypto.Rand as CRand;
bind Viper.Collections;

func start() {
    var bytes = CRand.Bytes(16);
    // FAILS: bytes.ToHex() - lowerer corrupts return type
    // var hex = bytes.ToHex();

    // WORKS: static call pattern bypasses the bug
    var hex = Bytes.ToHex(bytes);
    Say("Hex: " + hex);
}
```

#### Workaround

Use the static call pattern `Bytes.ToHex(value)` instead of the instance method `value.ToHex()`. The static dispatch doesn't depend on the sema recognizing the receiver type.

#### Root Cause

**Same mechanism as Bug #4.** The chain is:

1. `CRand.Bytes(16)` returns `types::ptr()` (no class name) — see Bug #7.
2. Sema at `src/frontends/zia/Sema_Expr.cpp:1015` fails the `"Viper."` name check on the empty-named `Ptr`.
3. Lowerer at `src/frontends/zia/Lowerer_Expr.cpp:512` returns `{constInt(0), I64}` for the unresolved `.ToHex()` field.
4. The indirect call emits void return type, and the expression evaluates to `{constInt(0), Void}`.
5. Subsequent string concatenation receives `i64` where `str` is expected.

The static call `Bytes.ToHex(bytes)` works because the sema resolves `Bytes.ToHex` directly to the extern function `"Viper.Collections.Bytes.ToHex"` without needing to know the receiver's type.

**Fix:** Correct crypto function return types (Bug #7). The `RuntimeAdapter.cpp` `toZiaType` function at lines 82-86 maps all `Object` returns to `types::ptr()`, losing class identity — this is the systemic root cause.

---

### Bug #6: `Viper.String.Equals` declared with wrong return type

**Severity:** Medium
**Category:** Zia runtime extern declarations
**Affected class:** `Viper.String.Equals`

#### Symptom

`Viper.String.Equals(a, b)` returns `i64` instead of `i1` (boolean). This can cause type mismatches when the result is used in boolean contexts.

#### Workaround

Use the `==` operator instead of `String.Equals()`:
```zia
// WORKS
if (a == b) { ... }

// PROBLEMATIC - wrong return type
var eq = String.Equals(a, b);
```

#### Root Cause

**File:** `src/il/runtime/runtime.def`, line 2141

```
RT_FUNC(StrEquals, rt_str_eq, "Viper.String.Equals", "i64(str,str)")
```

The signature specifies `i64` return instead of `bool`. The C function at `src/runtime/rt_string_ops.c:937` returns `int64_t`:

```c
int64_t rt_str_eq(rt_string a, rt_string b)
```

The `toZiaType` adapter at `src/frontends/zia/RuntimeAdapter.cpp:59` faithfully maps `ILScalarType::I64` to `types::integer()`.

The internal lowerer works around this — `src/frontends/zia/Lowerer_Expr_Binary.cpp:332-337` emits `ICmpNe` after the call to convert i64 to i1. But direct user calls through the runtime callee path expose the raw i64 type.

**Fix:** Change runtime.def signature to `"bool(str,str)"` and update `rt_str_eq` to return `bool`/`int8_t`, or add a post-call `ICmpNe` conversion in the runtime callee dispatch path.

---

### Bug #7: Crypto function return types declared as `ptr` instead of `Bytes`

**Severity:** High (causes Bugs #4, #5, #8, #10 in crypto/text contexts)
**Category:** Zia runtime extern declarations / type adapter
**Affected classes:** `Viper.Crypto.Rand.Bytes()`, `Viper.Crypto.Cipher.Encrypt()`, `Viper.Crypto.Cipher.Decrypt()`, `Viper.Crypto.Cipher.GenerateKey()`, `Viper.Crypto.KeyDerive.Pbkdf2SHA256()`

#### Symptom

`Bytes.Len` returns `0` on objects returned from crypto functions. Instance method calls (`.ToHex()`, `.ToStr()`, `.Len`) do not work correctly because the sema/lowerer doesn't recognize the return value as a `Bytes` object.

#### Reproduction

```zia
module Demo;
bind Viper.Terminal;
bind Viper.Crypto.Cipher as Cipher;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var key = Cipher.GenerateKey();
    Say("Key len: " + Fmt.Int(key.Len));  // Outputs: Key len: 0
}
```

#### Root Cause

**Systemic issue in `src/frontends/zia/RuntimeAdapter.cpp`, lines 82-86:**

```cpp
case il::runtime::ILScalarType::Object:
    return types::ptr();
```

The `toZiaType` function maps ALL `Object` (obj) return types to `types::ptr()`, discarding any class identity. The runtime.def signatures use `"obj(i64)"` etc., which doesn't encode WHICH class the object is.

**Affected declarations in `src/il/runtime/ZiaRuntimeExterns.inc`:**

| Line | Declaration | Should Return |
|------|-------------|---------------|
| 300 | `Viper.Collections.Bytes.Slice` → `types::ptr()` | `types::runtimeClass("Viper.Collections.Bytes")` |
| 637 | `Viper.Crypto.KeyDerive.Pbkdf2SHA256` → `types::ptr()` | `types::runtimeClass("Viper.Collections.Bytes")` |
| 639 | `Viper.Crypto.Rand.Bytes` → `types::ptr()` | `types::runtimeClass("Viper.Collections.Bytes")` |
| 641-649 | `Viper.Crypto.Cipher.*` → `types::ptr()` | `types::runtimeClass("Viper.Collections.Bytes")` |
| 647-649 | `Viper.Crypto.Aes.*` → `types::ptr()` | `types::runtimeClass("Viper.Collections.Bytes")` |

The `RuntimeClasses.inc` at line 549 shows `RUNTIME_METHOD("Bytes", "obj(i64)", "Viper.Crypto.Rand.Bytes")` — the `"obj"` return type is the source of the problem.

**Fix:** Either (a) extend the runtime.def signature format to encode return class names (e.g., `"Bytes(i64)"` instead of `"obj(i64)"`), or (b) add post-processing in the ZiaRuntimeExterns.inc generator to replace `types::ptr()` with `types::runtimeClass(...)` for known classes, or (c) teach `RuntimeAdapter::toZiaType` to accept a class context parameter.

---

## Runtime Bugs

### Bug #8: `Csv.ParseLine()` returns empty sequence

**Severity:** Medium
**Category:** Zia frontend type inference (NOT a runtime bug)
**Affected class:** `Viper.Text.Csv`

#### Symptom

`Csv.ParseLine("Alice,30,NY")` returns a sequence with count `0` instead of the expected 3 fields.

#### Reproduction

```zia
module CsvDemo;
bind Viper.Terminal;
bind Viper.Text.Csv as Csv;
bind Viper.Fmt as Fmt;

func start() {
    var fields = Csv.ParseLine("Alice,30,NY");
    Say("Count: " + Fmt.Int(fields.Count));  // Outputs: Count: 0
}
```

**Expected:** `Count: 3`
**Actual:** `Count: 0`

#### Root Cause

**The C runtime is correct.** `rt_csv_parse_line` at `src/runtime/rt_csv.c:400` correctly parses CSV and populates a sequence with 3 elements.

**The bug is in the Zia frontend.** The signature in `src/il/runtime/runtime.def:2244` is:

```
RT_FUNC(CsvParseLine, rt_csv_parse_line, "Viper.Text.Csv.ParseLine", "obj(str)")
```

Return type `obj` maps to `Kind::Ptr` (opaque). When the Zia code does `fields.Count`, the lowerer at `src/frontends/zia/Lowerer_Expr.cpp` checks:
- Line 453: `baseType->kind == TypeKindSem::List` — NO
- Line 465: `baseType->kind == TypeKindSem::Map` — NO
- Line 476: `baseType->kind == TypeKindSem::Set` — NO
- Line 488: `baseType->kind == TypeKindSem::Ptr && !baseType->name.empty()` — NO (name is empty)

Falls through to line 512: `return {Value::constInt(0), Type(Type::Kind::I64)};`

The `.Count` property access on an opaque `Ptr` is silently resolved to literal `0`.

**Fix:** Same systemic fix as Bug #7 — runtime functions returning sequences should use a typed return, or the lowerer should emit `rt_seq_len` for `.Count` on generic `Ptr` values.

---

### Bug #9: `Csv.FormatLine()` produces garbage output

**Severity:** Medium
**Category:** Runtime API contract mismatch
**Affected class:** `Viper.Text.Csv`

#### Symptom

`Csv.FormatLine(seq)` returns incorrect/garbage string output instead of properly comma-separated values.

#### Reproduction

```zia
module CsvDemo;
bind Viper.Terminal;
bind Viper.Text.Csv as Csv;
bind Viper.Collections;

func start() {
    var fields = Seq.New();
    fields.Push(Box.FromStr("Alice"));
    fields.Push(Box.FromStr("30"));
    var line = Csv.FormatLine(fields);
    Say("Line: " + line);  // Outputs garbage
}
```

**Expected:** `Line: Alice,30`
**Actual:** Garbage/incorrect output

#### Root Cause

**File:** `src/runtime/rt_csv.c`, line 645

```c
rt_string field = (rt_string)rt_seq_get(fields, i);
const char *str = rt_string_cstr(field);
```

The function casts `void*` from the sequence directly to `rt_string`. But the sequence contains `rt_box_t*` pointers (from `Box.FromStr`), not raw `rt_string` values.

A `rt_box_t` (`src/runtime/rt_box.c:71-85`) has `int64_t tag` as its first field, followed by a union. When cast to `rt_string` (which is `rt_string_impl*`), `rt_string_cstr()` reads the box's tag and data fields as if they were a string structure, producing garbage.

**Fix:** Either (a) `rt_csv_format_line_with` should detect and unbox boxed strings, or (b) the documentation should specify that `FormatLine` expects raw strings (not boxed), or (c) provide a separate `FormatLineBoxed` variant.

---

### Bug #10: `Markdown.ExtractHeadings()` returns empty sequence

**Severity:** Medium
**Category:** Zia frontend type inference (NOT a runtime bug)
**Affected class:** `Viper.Text.Markdown`

#### Symptom

`Markdown.ExtractHeadings(text)` returns a sequence with count `0` regardless of input content.

#### Reproduction

```zia
module Demo;
bind Viper.Terminal;
bind Viper.Text.Markdown as Md;
bind Viper.Fmt as Fmt;

func start() {
    var text = "# Hello\n## World\nParagraph.";
    var headings = Md.ExtractHeadings(text);
    Say("Count: " + Fmt.Int(headings.Count));  // Outputs: Count: 0
}
```

**Expected:** `Count: 2` (or more, depending on implementation)
**Actual:** `Count: 0`

#### Root Cause

**The C runtime is correct.** `rt_markdown_extract_headings` at `src/runtime/rt_markdown.c:378-413` correctly parses headings and populates a sequence.

**Same root cause as Bug #8.** The signature in `src/il/runtime/runtime.def:2363` is:

```
RT_FUNC(MarkdownExtractHeadings, rt_markdown_extract_headings, "Viper.Text.Markdown.ExtractHeadings", "obj(str)")
```

Return type `obj` maps to opaque `Ptr`. `.Count` on opaque `Ptr` falls through to `constInt(0)` at `src/frontends/zia/Lowerer_Expr.cpp:512`.

**Fix:** Same as Bug #8 — correct the return type or teach the lowerer to handle `.Count` on generic `Ptr`.

---

### Bug #11: `JsonPath.GetInt()` segfaults after `GetStr()` call

**Severity:** High
**Category:** Runtime missing input validation
**Affected class:** `Viper.Text.JsonPath`

#### Symptom

Calling `JsonPath.GetInt()` after a `JsonPath.GetStr()` call on the same JSON string causes a segfault (exit code 139).

#### Reproduction

```zia
module Demo;
bind Viper.Terminal;
bind Viper.Text.JsonPath as JP;
bind Viper.Fmt as Fmt;

func start() {
    var json = "{\"name\":\"Alice\",\"age\":30}";
    var name = JP.GetStr(json, "$.name");
    Say("Name: " + name);         // Works
    var age = JP.GetInt(json, "$.age");  // SEGFAULT
    Say("Age: " + Fmt.Int(age));
}
```

**VM result:** Segfault (exit code 139)
**Note:** Happens in both VM and native.

#### Root Cause

**File:** `src/runtime/rt_jsonpath.c`, lines 21-52 (`navigate_segment`) and 56-116 (`resolve_path`)

The `rt_jsonpath_get_*` functions expect a **pre-parsed JSON object** (map/seq structure from `Json.Parse()`), not a raw JSON string. The signatures in `src/il/runtime/runtime.def`:

```
RT_FUNC(JsonPathGetStr, ..., "str(obj,str)")  // line 2373: first param is obj
RT_FUNC(JsonPathGetInt, ..., "i64(obj,str)")  // line 2374: first param is obj
```

When a raw JSON string is passed as the first `obj` argument:

1. `resolve_path` at line 112 calls `navigate_segment(current, "name", 4)`
2. `navigate_segment` at line 21 tries `rt_seq_len(current)` on the raw string pointer — reads garbage from `((rt_seq_impl*)current)->len`
3. Falls to map lookup at line 48: `rt_map_get(current, key)` — interprets the string structure as a map, causing UB
4. First call (GetStr) may succeed by luck (returns NULL → empty string fallback at line 243)
5. Second call (GetInt) segfaults because `rt_string_cstr((rt_string)val)` at line 251 is called on an invalid pointer

**Correct usage should be:**
```zia
var parsed = Json.Parse(json);       // Returns a map/seq structure
var name = JP.GetStr(parsed, "$.name");
var age = JP.GetInt(parsed, "$.age");
```

**Fix:** Either (a) add input validation in `rt_jsonpath_get` to detect non-map/non-seq inputs and return NULL safely, or (b) have `rt_jsonpath_get` auto-detect and parse raw JSON strings, or (c) improve documentation to clearly state the first argument must be a parsed JSON object.

---

### Bug #12: Toml.Get() + Box.ToStr() unbox failure

**Severity:** Medium
**Category:** Runtime API contract mismatch
**Affected class:** `Viper.Text.Toml`

#### Symptom

`Toml.Get()` returns a boxed value, but `Box.ToStr()` / `rt_unbox_str` fails with a type mismatch when trying to unbox it.

#### Reproduction

```zia
module Demo;
bind Viper.Terminal;
bind Viper.Text.Toml as Toml;
bind Viper.Collections;

func start() {
    var toml = "name = \"Alice\"\nage = 30";
    var val = Toml.Get(toml, "name");
    var name = Box.ToStr(val);  // rt_unbox_str type mismatch
    Say("Name: " + name);
}
```

**Expected:** `Name: Alice`
**Actual:** Runtime error or incorrect output

#### Root Cause

**Files:** `src/runtime/rt_toml.c` (lines 236-239) and `src/runtime/rt_box.c` (lines 135-155)

The TOML parser stores values as **raw `rt_string`** values, not boxed values:

```c
// rt_toml.c, lines 237-239 (in rt_toml_parse):
rt_string val = parse_value(&p);
if (val)
    rt_map_set(current_section, key, val);
```

`rt_toml_get` at lines 315-346 returns the raw `void*` found in the map. When `Box.ToStr()` receives this raw `rt_string` pointer:

```c
// rt_box.c, lines 135-155:
rt_string rt_unbox_str(void *box)
{
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_STR)  // <-- FAILS: raw rt_string != rt_box_t
    {
        rt_trap("rt_unbox_str: type mismatch (expected str)");
    }
    // ...
}
```

The raw `rt_string_impl*` pointer is cast to `rt_box_t*`. The `tag` field reads the first 8 bytes of the string impl structure, which is not `RT_BOX_STR` (3). The tag check fails.

**Fix:** Either (a) `rt_toml_parse` should store boxed strings via `rt_box_str(val)`, or (b) `rt_toml_get` should box the returned value before returning, or (c) document that `Toml.Get()` returns raw strings castable directly to `str` without using `Box.ToStr()`.

---

## Passing Demos (Full List)

All of the following demos produce identical output between VM and ARM64 native execution:

### Core + Utilities (7/7)
- BoxDemo, StringDemo, StringExtDemo, ConvertDemo, FmtDemo, LogDemo, ParseDemo

### Math (5/5)
- BitsDemo, MathDemo, RandomDemo (same seed = same values), Vec2Demo, Vec3Demo

### Collections (8/8)
- BagDemo, BytesDemo, HeapDemo, QueueDemo, RingDemo, SeqDemo, StackDemo, TreeMapDemo

### Text + Crypto — original (6/7)
- CodecDemo, JsonDemo, PatternDemo, TemplateDemo, StringBuilderDemo, HashDemo

### Time + Diagnostics (3/3)
- ClockDemo (structural match — timestamps differ expectedly), DateTimeDemo, DiagDemo

### IO (8/9)
- DirDemo, PathDemo, CompressDemo, ArchiveDemo, BinFileDemo, LineReaderDemo, LineWriterDemo, MemStreamDemo

### Threads + Sync (2/4 fully, 4/4 output)
- SafeI64Demo, BarrierDemo (output + exit code match)
- GateDemo, RwLockDemo (output matches, exit code differs — Bug #3)

### Text — new (4/5)
- HtmlDemo, JsonPathDemo, MarkdownDemo, TomlDemo (all IDENTICAL VM vs native)
- CsvDemo: runtime bugs prevent meaningful output

### Crypto — new (3/3)
- CryptoRandDemo, CipherDemo, KeyDeriveDemo (all produce valid output; crypto randomness means VM/native differ as expected)

### System — new (2/2)
- ExecDemo, MachineDemo (IDENTICAL VM vs native)

---

## Classes Without Zia Examples (Infeasible)

The following classes were evaluated but Zia examples were not added due to language limitations or infrastructure requirements:

### Requires function pointers (not supported in Zia)
- `Viper.Threads.Thread` — needs function reference for thread entry point
- `Viper.Threads.Monitor` — needs function reference for `Sync()` callback
- `Viper.Threads.Parallel` — needs function references for `ForEach()` / `Map()`

### Requires network infrastructure
- `Viper.Network.Http` — needs HTTP server/endpoint
- `Viper.Network.Tcp` / `TcpServer` — needs TCP listener setup
- `Viper.Network.Udp` — needs UDP socket pair
- `Viper.Network.WebSocket` — needs WebSocket server
- `Viper.Crypto.Tls` — needs TLS-capable endpoint

### Abstract / interface only
- `Viper.IO.Stream` — abstract base class, not directly instantiable

### Not yet implemented
- Deque, LazySeq, Set, SortedSet, WeakMap, CompiledPattern
- Countdown, Stopwatch, Promise, Future, CancellationToken
- Debouncer, Throttler, Scheduler, RetryPolicy, RateLimiter

---

## Notes

- `Fmt.Num(30.0)` outputs `"30"` not `"30.0"` — consistent between VM and native, likely by design
- Clock/DateTime tests use structural comparison since timestamps are inherently time-dependent
- RandomDemo uses a fixed seed so outputs are deterministic and comparable
- Crypto demos (CryptoRand, Cipher, KeyDerive) produce different outputs between VM and native due to random salt/IV generation — this is expected and correct behavior
- Static call workaround (`Bytes.ToHex(x)` instead of `x.ToHex()`) was used in documentation examples to avoid Bug #5
