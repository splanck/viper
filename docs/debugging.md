# Debugging

## Breaking on source lines

`ilc` can halt execution before running a specific source line.

### Flags

- `--break <file:line>`: Generic breakpoint flag. If the argument contains a path separator or dot, it is interpreted as a source-line breakpoint.
- `--break-src <file:line>`: Explicit source-line breakpoint.

Paths are normalized before comparison, including platform separators and `.`/`..` segments. When the normalized path does not match the location recorded in the IL, `ilc` falls back to comparing only the basename.

Specifying the same breakpoint more than once coalesces into a single breakpoint. When multiple instructions map to the same source line, `ilc` reports the breakpoint once per line until control transfers to a different basic block.

### Examples

```sh
# Path normalization: break at the first PRINT in math_basics.bas
ilc front basic -run ./examples/basic/../basic/math_basics.bas \
  --break ./examples/basic/../basic/math_basics.bas:4 --trace=src

# Basename fallback with the explicit flag
ilc front basic -run examples/basic/sine_cosine.bas \
  --break-src sine_cosine.bas:5 --trace=src
```
