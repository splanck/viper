# Runtime Bugs

This log captures runtime defects found during the Zia runtime sweep.

## Format

- **ID**: RT-###
- **Component**: Viper.* class or subsystem
- **Summary**: краткое описание
- **Repro**: Zia program path + steps
- **Expected**: what should happen
- **Actual**: what happened
- **Root Cause**: file:line + explanation
- **Notes**: platform, determinism, frequency, workaround

---

## Open Bugs

- **ID**: RT-008
  - **Component**: Viper.String.IndexOf
  - **Summary**: IndexOf returns 1 for empty needle instead of 0.
  - **Repro**: `tests/runtime_sweep/basic/edge_strings.bas`
  - **Expected**: `IndexOf("hello", "")` returns 0 (empty string found at start).
  - **Actual**: Returns 1.
  - **Root Cause**: `src/runtime/rt_string_ops.c:577-578`
    ```c
    if (needle_len == 0)
        return 1;  // Intentional: BASIC uses 1-based indexing, 0 means "not found"
    ```
    This is actually **correct behavior** for 1-based indexing. The function comment at line 567 states "which returns 1 per the language rules".
  - **Notes**: **RESOLUTION: Not a bug.** API uses 1-based indexing where 0 means "not found", so returning 1 means "empty string found at position 1". Needs documentation clarification only.

- **ID**: RT-012
  - **Component**: Viper.DateTime (Year/Month/Day vs ToISO)
  - **Summary**: DateTime component functions return local time but ToISO returns UTC.
  - **Repro**: `tests/runtime_sweep/basic/edge_datetime.bas`
  - **Expected**: Consistent timezone handling. `Year(0)` should return 1970 if ToISO(0) returns "1970-01-01".
  - **Actual**: `Year(0)` returns 1969, `Month(0)` returns 12, `Day(0)` returns 31 (local time), but `ToISO(0)` returns "1970-01-01T00:00:00Z" (UTC).
  - **Root Cause**:
    - `src/runtime/rt_datetime.c:187` uses `localtime(&t)` for Year/Month/Day
    - `src/runtime/rt_datetime.c:516` uses `gmtime(&t)` for ToISO
    The documentation at lines 54-57 acknowledges this: "Component extraction uses local time zone / ISO format output uses UTC". This is **documented but confusing**.
  - **Notes**: **Design issue, not implementation bug.** The behavior is intentional and documented in the file header, but the API is inconsistent and error-prone. Consider adding `YearUTC()`, `MonthUTC()`, etc. or `ToISOLocal()`.

- **ID**: RT-013
  - **Component**: Viper.DateTime.Create
  - **Summary**: Invalid dates silently normalize instead of erroring.
  - **Repro**: `tests/runtime_sweep/basic/edge_datetime.bas`
  - **Expected**: `Create(2024, 13, 1, ...)` with month=13 should error or be clearly documented as lenient.
  - **Actual**: Silently normalizes to 2025-01-01.
  - **Root Cause**: `src/runtime/rt_datetime.c:585-599`
    ```c
    int64_t rt_datetime_create(...)
    {
        struct tm tm = {0};
        tm.tm_year = (int)(year - 1900);
        tm.tm_mon = (int)(month - 1);  // No validation
        // ... no range checks ...
        time_t t = mktime(&tm);  // mktime normalizes overflow
        return (int64_t)t;
    }
    ```
    The function relies on C's `mktime()` which normalizes out-of-range values. Documentation at lines 564-567 explicitly describes this: "Month 13 → January of next year".
  - **Notes**: **Documented behavior.** The function comment explicitly describes overflow handling. However, users may expect validation. Consider adding a strict `CreateStrict()` variant.

- **ID**: RT-014
  - **Component**: Viper.Collections.Ring
  - **Summary**: Ring.New(0) silently adjusts capacity to 1.
  - **Repro**: `tests/runtime_sweep/basic/edge_collections.bas`
  - **Expected**: `Ring.New(0)` either creates a ring with 0 capacity or errors.
  - **Actual**: Returns a Ring with Cap=1 instead of 0.
  - **Root Cause**: `src/runtime/rt_ring.c:112-113`
    ```c
    if (capacity <= 0)
        capacity = 1; // Minimum capacity of 1
    ```
    The comment documents this is intentional.
  - **Notes**: **Intentional behavior.** A zero-capacity ring is meaningless (can't store anything). The function enforces a minimum viable ring. Could trap instead if strict semantics are preferred.

- **ID**: RT-017
  - **Component**: Viper.String.Length
  - **Summary**: String.Length returns byte count instead of character count for Unicode strings.
  - **Repro**: `tests/runtime_sweep/basic/edge_strings_length.bas`
  - **Expected**: `Length("Привет")` returns 6 (number of characters).
  - **Actual**: Returns 12 (number of UTF-8 bytes).
  - **Root Cause**: `src/runtime/rt_string_ops.c:288-294`
    ```c
    int64_t rt_len(rt_string s)
    {
        size_t len = rt_string_len_bytes(s);  // Returns byte count
        ...
        return (int64_t)len;
    }
    ```
    The function explicitly returns byte length, as indicated by the helper name `rt_string_len_bytes()`.
  - **Notes**: **Intentional behavior** based on naming, but may not match user expectations. Consider:
    1. Add `String.ByteLen()` for explicit byte count
    2. Change `String.Length()` to return codepoint count
    3. Add `String.CharLen()` for codepoint count (keep Length as bytes)

- **ID**: RT-019
  - **Component**: Viper.Collections.Map
  - **Summary**: `Map.Keys()` traps with `InvalidOperation: null indirect callee`.
  - **Repro**: `/tmp/map_keys.zia`
  - **Expected**: Returns a sequence of keys.
  - **Actual**: Trap: `InvalidOperation (code=0): null indirect callee`.
  - **Root Cause**: Unknown. `Viper.Collections.Map.Keys` is present in `runtime.def`, but the method call resolves to a null callee at runtime.
  - **Notes**: Minimal repro:
    ```viper
    module Test;
    func start() {
        var m = {"a": 1, "b": 2};
        m.keys();
    }
    ```

---

## Closed/Fixed

- **ID**: RT-009
  - **Component**: Viper.Text.Template.Render
  - **Summary**: Template substitution returned empty strings even when map had correct keys.
  - **Root Cause**: `rt_template.c:236` expected boxed values but Map stores raw rt_string pointers.
  - **Notes**: Fixed by handling both boxed strings (`RT_BOX_STR`) and raw `rt_string` handles in `render_internal()`.

- **ID**: RT-010
  - **Component**: Viper.Text.Template.Keys
  - **Summary**: Template.Keys appeared to return Bag with 0 elements.
  - **Root Cause**: **Test error.** The test assigned a Bag to a Seq variable (`DIM keys AS Viper.Collections.Seq`).
  - **Notes**: Fixed test to use `DIM keys AS Viper.Collections.Bag`. The runtime function was correct.

- **ID**: RT-011
  - **Component**: Viper.Math.Clamp
  - **Summary**: Clamp with inverted min/max returned incorrect result.
  - **Root Cause**: `rt_math.c:344` and `rt_math.c:358` had no validation for `lo > hi`.
  - **Notes**: Fixed by swapping lo/hi when inverted in both `rt_clamp_f64` and `rt_clamp_i64`.

- **ID**: RT-015
  - **Component**: Viper.String.Flip
  - **Summary**: Flip reversed bytes instead of Unicode characters, corrupting multi-byte UTF-8.
  - **Root Cause**: `rt_string_ops.c:1226` did byte-by-byte reversal.
  - **Notes**: Rewritten to parse UTF-8 codepoints and reverse the sequence of characters.

- **ID**: RT-016
  - **Component**: Viper.String.ToUpper / ToLower
  - **Summary**: ToUpper/ToLower only handled ASCII, not Unicode letters.
  - **Root Cause**: `rt_string_ops.c:676` only checked ASCII range 'a'-'z'.
  - **Notes**: Extended to support Latin-1 Supplement (0xC0-0xFF). Full Unicode (Cyrillic, Greek, CJK) requires ICU integration and is documented as a limitation.

- **ID**: RT-018
  - **Component**: Viper.Network.Url.IsValid
  - **Summary**: Url.IsValid accepted clearly invalid URLs as valid.
  - **Root Cause**: `rt_network_http.c:2491` accepted any non-empty string.
  - **Notes**: Added validation: reject spaces, require valid scheme syntax, reject "://" without scheme.

- **ID**: RT-001
  - **Component**: Object retention / Collections (Heap, TreeMap)
  - **Summary**: String handles stored in object collections triggered heap header assertions.
  - **Repro**: `tests/runtime_sweep/basic/collections.bas` (Heap/TreeMap operations with string values)
  - **Expected**: Collections accept string values without crashing.
  - **Actual**: `Assertion failed: (hdr->magic == RT_MAGIC)` in `rt_heap_validate_header`.
  - **Notes**: Fixed by tagging runtime string handles, routing `rt_obj_*` through string retain/release, and switching TreeMap retention to `rt_obj_retain_maybe`.

- **ID**: RT-002
  - **Component**: Viper.Text.Template
  - **Summary**: Brace escaping mishandled `{{` / `}}` literals.
  - **Repro**: `tests/runtime_sweep/basic/text.bas` (`Template.Escape`, `Render`)
  - **Expected**: Escaped templates preserve literal braces.
  - **Actual**: Escaped templates produced malformed output / stripped braces.
  - **Notes**: Normalized escape handling and literal brace emission.

- **ID**: RT-003
  - **Component**: Viper.Fmt.Size
  - **Summary**: Size formatting dropped the required decimal precision for KB+ outputs.
  - **Repro**: `tests/runtime_sweep/basic/core_fmt_convert.bas` (`Fmt.Size`)
  - **Expected**: `Fmt.Size(1024)` => `1.0 KB`.
  - **Actual**: Returned `1 KB` without the decimal.
  - **Notes**: Keep 1024-based units and always emit one decimal digit for KB+.

- **ID**: RT-004
  - **Component**: Viper.IO.File.ReadLines
  - **Summary**: Trailing newline produced an extra empty line.
  - **Repro**: `tests/runtime_sweep/basic/io_files.bas` (`File.ReadLines`)
  - **Expected**: No extra empty entry for trailing newline.
  - **Actual**: Extra empty string appended.
  - **Notes**: Fixed trailing line handling.

- **ID**: RT-005
  - **Component**: Viper.String.Trim
  - **Summary**: Trim only removed ASCII space instead of all whitespace.
  - **Repro**: `tests/runtime_sweep/basic/core_strings.bas` (`String.Trim`)
  - **Expected**: Trim removes tabs/newlines/whitespace.
  - **Actual**: Only spaces were trimmed.
  - **Notes**: Switched to whitespace-aware trimming.

- **ID**: RT-006
  - **Component**: Viper.Network.Http / Viper.Network.Url
  - **Summary**: URL validation and HTTP response helpers returned invalid/boxed values.
  - **Repro**: `tests/runtime_sweep/basic/network_http_url.bas` (`Url.IsValid`, `Http.Head`, `Http.Download`, headers/query maps)
  - **Expected**: Invalid URLs rejected; header/query maps return strings; Head/Download return stable response objects.
  - **Actual**: `http://` accepted as valid; header/query maps boxed inconsistently; Head/Download could return freed objects.
  - **Notes**: Fixed URL parsing validation, header/query boxing, and response lifetime handling.

- **ID**: RT-007
  - **Component**: Viper.Text.StringBuilder
  - **Summary**: StringBuilder bridge test crashed after runtime string layout change.
  - **Repro**: `src/tests/unit/test_stringbuilder_bridge.c` (`test_stringbuilder_bridge`)
  - **Expected**: StringBuilder ToString returns a readable runtime string.
  - **Actual**: Segfault when the test accessed string data via a stale struct layout.
  - **Notes**: Updated test to use `rt_string_cstr` instead of direct field access.
