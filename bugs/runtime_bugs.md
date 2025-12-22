# Runtime Bugs

This log captures runtime defects found during the ViperLang runtime sweep.

## Format

- **ID**: RT-###
- **Component**: Viper.* class or subsystem
- **Summary**: краткое описание
- **Repro**: ViperLang program path + steps
- **Expected**: what should happen
- **Actual**: what happened
- **Notes**: platform, determinism, frequency, workaround

---

## Open Bugs

(Empty)

---

## Closed/Fixed

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
