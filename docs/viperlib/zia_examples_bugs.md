# Zia Examples Audit — Bug Report

> Generated during systematic audit of viper-lib documentation.
> Each entry records bugs found while testing Zia examples against both VM and ARM64 native codegen.
> Last updated: 2026-02-06

---

## Summary

- **Total demos tested:** 56
- **VM pass:** 56/56 (all doc examples pass in VM)
- **ARM64 native compile:** 56/56 (bugs #13-17 fixed)
- **ARM64 native output match:** 52/56 (2 real bugs, 2 expected diffs)
- **Bugs found:** 17 total (5 linker bugs fixed, 12 carried forward)

### Test Results by Category (Post-Fix)

| Category | Demos | VM Pass | ARM64 Compile | ARM64 Match | Issues |
|----------|-------|---------|---------------|-------------|--------|
| Core (Box, String, StringExt) | 3 | 3/3 | 3/3 | 3/3 | |
| Math (Bits, Math, Random, Vec2, Vec3) | 5 | 5/5 | 5/5 | 5/5 | |
| Utilities (Fmt, Convert, Parse, Log, Diag) | 5 | 5/5 | 5/5 | 5/5 | Log has expected timestamp diff |
| System (Terminal, Env, Machine, Exec) | 4 | 4/4 | 4/4 | 3/4 | Env.IsNative expected diff |
| Collections (Seq-TreeMap) | 10 | 10/10 | 10/10 | 10/10 | #13 fixed |
| Text (Codec-Markdown) | 11 | 11/11 | 11/11 | 10/11 | Uuid bool bug (#1), #14 fixed |
| Time (Clock, DateTime) | 2 | 2/2 | 2/2 | 1/2 | Clock has expected timestamp diff |
| IO (Dir-Archive) | 9 | 9/9 | 9/9 | 8/9 | File Path.Join corruption (#2) |
| Crypto (Hash, CryptoRand) | 2 | 2/2 | 2/2 | 2/2 | #15 fixed |
| Threads (SafeI64-RwLock) | 4 | 4/4 | 4/4 | 4/4 | #16 fixed |
| Game (Grid2D-Quadtree) | 13 | 13/13 | 13/13 | 13/13 | |
| Network (Dns, Url) | 2 | 2/2 | 2/2 | 2/2 | #17 fixed |

### Toml.IsValid Note

`Toml.IsValid("= bad")` returns `true` instead of expected `false`. This may be a parsing bug or intentional lenient behavior. VM and native match.

---

## ARM64 Codegen Bugs (Carried Forward)

### Bug #1: `Uuid.IsValid()` returns `false` in ARM64 native (i1 handling)

**Severity:** High
**Demo:** `UuidDemo.zia`
**Status:** Still reproduces

**VM output:**
```
UUID: <valid-uuid>
Valid: true
Invalid: false
```

**ARM64 native output:**
```
UUID: <valid-uuid>
Valid: false
Invalid: false
```

**Root Cause:** ARM64 codegen copies all 64 bits of `x0` after `bl` for boolean returns without masking. Per AAPCS64, only the low 8 bits of `w0` are meaningful for `bool` returns.

**Fix:** When `ins.type.kind == il::core::Type::Kind::I1`, emit `and dst, x0, #1` after the `mov`.
**File:** `src/codegen/aarch64/OpcodeDispatch.cpp`, lines 721-737

---

### Bug #2: Path functions return corrupt values when passed `Path.Join()` result (ARM64 native)

**Severity:** High
**Demo:** `FileDemo.zia`
**Status:** Still reproduces

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

**Root Cause:** `isCall()` in register allocator only recognizes `MOpcode::Bl` (direct calls), missing `MOpcode::Blr` (indirect calls). This causes caller-saved registers not to be spilled before indirect calls.

**Fix:** Change `isCall()` to `return opc == MOpcode::Bl || opc == MOpcode::Blr;`
**File:** `src/codegen/aarch64/RegAllocLinear.cpp`, lines 237-240

---

## ARM64 Linker Bugs (Fixed)

### Bug #13: `Bytes.ToHex()` / `Bytes.ToBase64()` fail to link in ARM64 native

**Severity:** Medium
**Status:** FIXED
**Demo:** `BytesDemo.zia`

**Root Cause:** `rt_bytes.c` internally calls `rt_codec_hex_enc_bytes` (in `libviper_rt_text.a`), but only `libviper_rt_collections.a` was linked.

**Fix Applied:** Added `Collections → Text` dependency in `resolveRequiredComponents()` in `RuntimeComponents.hpp`.

---

### Bug #14: `Pattern` module functions fail to link in ARM64 native

**Severity:** Medium
**Status:** FIXED
**Demo:** `PatternDemo.zia`

**Root Cause:** `rt_pattern_*` symbols not mapped to any component.

**Fix Applied:** Added `starts("rt_pattern_")` to Text component in `componentForRuntimeSymbol()`.

---

### Bug #15: `Crypto.Rand` functions fail to link in ARM64 native

**Severity:** Medium
**Status:** FIXED
**Demo:** `CryptoRandDemo.zia`

**Root Cause:** `rt_crypto_rand_*` symbols not mapped to any component. These live in `libviper_rt_text.a`.

**Fix Applied:** Added `starts("rt_crypto_rand_")` to Text component in `componentForRuntimeSymbol()`.

---

### Bug #16: Gate, Barrier, RwLock fail to link in ARM64 native

**Severity:** Medium
**Status:** FIXED
**Demos:** `GateDemo.zia`, `BarrierDemo.zia`, `RwLockDemo.zia`

**Root Cause:** Two issues: (1) `rt_gate_*`, `rt_barrier_*`, `rt_rwlock_*` not mapped to Threads component; (2) `libviper_rt_threads.a` is C++ and needs `-lc++` linked.

**Fix Applied:** Added symbol prefixes to Threads component in `componentForRuntimeSymbol()`. Added `-lc++` to ARM64 link command when Threads component is present (`cmd_codegen_arm64.cpp`).

---

### Bug #17: Network module functions fail to link in ARM64 native

**Severity:** Medium
**Status:** FIXED
**Demos:** `DnsDemo.zia`, `UrlDemo.zia`

**Root Cause:** `rt_dns_*`, `rt_url_*` symbols not mapped to Network component.

**Fix Applied:** Added `starts("rt_dns_")` and `starts("rt_url_")` to Network component in `componentForRuntimeSymbol()`.

---

## Carried Forward Bugs (From Prior Audit)

### Bug #3: ARM64 codegen missing `mov x0, #0` before `ret` in void main

**Severity:** Medium — Non-zero exit codes from void main functions.
**Fix:** Emit `MovRI x0, #0` before `Ret` when `ins.operands.empty()` and function is `main`.

### Bug #4: `Seq.Get()` + `Box.ToStr()` codegen failure with opaque Ptr

**Severity:** High — call.indirect return value not captured on opaque Ptr types.
**Root Cause:** Sema/lowerer chain loses type information for opaque `Ptr` returns.

### Bug #5: Instance method calls on opaque `Ptr` values cause type corruption

**Severity:** High — e.g., `bytes.ToHex()` fails, but `Bytes.ToHex(bytes)` works.
**Workaround:** Use static call pattern instead of instance method syntax.

### Bug #6: `Viper.String.Equals` declared with `i64` return instead of `bool`

**Severity:** Medium — Direct calls expose wrong return type.
**Workaround:** Use `==` operator instead.

### Bug #7: Runtime functions returning objects use opaque `ptr` type

**Severity:** High (systemic) — `RuntimeAdapter.cpp:toZiaType` maps all `Object` returns to `types::ptr()`, losing class identity. Root cause of bugs #4, #5, #8, #10.

### Bug #8: `Csv.ParseLine()` returns empty sequence (opaque Ptr)

**Severity:** Medium — `.Count` on opaque `Ptr` resolves to `constInt(0)`.
**Note:** Doc example avoids this by using `fields.Len` property directly.

### Bug #9: `Csv.FormatLine()` box/raw string mismatch

**Severity:** Medium — Runtime expects raw strings but receives boxed values.

### Bug #10: `Markdown.ExtractHeadings()` returns empty sequence

**Severity:** Medium — Same opaque `Ptr` issue as Bug #8.

### Bug #11: `JsonPath.GetInt()` segfaults after `GetStr()` with raw JSON string

**Severity:** High — Passing raw JSON string instead of parsed object causes UB.

### Bug #12: `Toml.Get()` + `Box.ToStr()` unbox failure

**Severity:** Medium — Toml stores raw strings, not boxed values.

---

## Root Cause Classification

| Root Cause | Bug IDs |
|------------|---------|
| ARM64 `i1` return value handling | #1 |
| ARM64 register allocator `Blr` omission | #2 |
| ARM64 void return in `main` | #3 |
| Zia lowerer: opaque `Ptr` field/method fallthrough | #4, #5, #8, #10 |
| `runtime.def` / `ZiaRuntimeExterns.inc` wrong types | #6, #7 |
| Runtime API contract mismatch (box vs raw) | #9, #12 |
| Runtime missing input validation | #11 |
| ARM64 linker missing component mappings | #13, #14, #15, #16, #17 |

---

## Complete Test Results

### VM Results (56/56 pass)

All doc examples run correctly in the VM.

### ARM64 Native Results

#### Full Match (52 demos)
BoxDemo, StringDemo, StringExtDemo, BitsDemo, MathDemo, RandomDemo, Vec2Demo, Vec3Demo, FmtDemo, ConvertDemo, ParseDemo, LogDemo (structural), DiagDemo, TerminalDemo, MachineDemo, ExecDemo, SeqDemo, MapDemo, ListDemo, StackDemo, QueueDemo, BagDemo, RingDemo, HeapDemo, TreeMapDemo, BytesDemo, CodecDemo, CsvDemo, JsonDemo, TemplateDemo, TomlDemo, StringBuilderDemo, PatternDemo, HtmlDemo, JsonPathDemo, MarkdownDemo, DateTimeDemo, DirDemo, PathDemo, LineReaderDemo, LineWriterDemo, MemStreamDemo, BinFileDemo, CompressDemo, ArchiveDemo, HashDemo, CryptoRandDemo, SafeI64Demo, GateDemo, BarrierDemo, RwLockDemo, DnsDemo, UrlDemo, Grid2DDemo, TimerDemo, StateMachineDemo, TweenDemo, ButtonGroupDemo, SmoothValueDemo, ParticleDemo, SpriteAnimDemo, CollisionDemo, ObjectPoolDemo, ScreenFXDemo, PathFollowerDemo, QuadtreeDemo

#### Expected Differences (2 demos)
- **EnvDemo:** `IsNative()` returns `false` in VM, `true` in native — **by design**
- **ClockDemo:** Tick values differ — **expected** (time-dependent)

#### ARM64 Output Mismatches (2 demos — real bugs)
- **UuidDemo:** `Valid: false` instead of `true` — **Bug #1** (i1 handling)
- **FileDemo:** Path functions corrupt when passed `Path.Join()` result — **Bug #2** (Blr omission)

#### ARM64 Link Failures — ALL FIXED
Bugs #13-17 resolved. All 56 demos now compile and link on ARM64.

---

## Classes Without Zia Examples (Infeasible)

### Requires function pointers (not supported in Zia)
- `Viper.Threads.Thread`, `Monitor`, `Parallel`

### Requires network infrastructure
- `Viper.Network.Http`, `Tcp`, `TcpServer`, `Udp`, `WebSocket`
- `Viper.Crypto.Tls`

### Abstract / interface only
- `Viper.IO.Stream`

### Not yet constructible from Zia
- Deque, LazySeq, Set, SortedSet, WeakMap, CompiledPattern
- Countdown, Stopwatch, Promise, Future, CancelToken
- Debouncer, Throttler, Scheduler, RetryPolicy, RateLimiter
- Object (base class)
- SceneNode, Scene, SpriteBatch (graphics)
- RestClient (network)
- GUI individual widgets (except via App demo)

---

## Notes

- `Fmt.Num(30.0)` outputs `"30"` not `"30.0"` — consistent between VM and native, likely by design
- `Toml.IsValid("= bad")` returns `true` — possible validation bug, VM and native agree
- Static call workaround (`Bytes.ToHex(x)` instead of `x.ToHex()`) used in doc examples to avoid Bug #5
- Doc examples use `new Seq()` / `seq.Len` syntax (property access) which works correctly, unlike the older `Seq.New()` / `seq.Len()` method-call syntax which triggered Bug #4
