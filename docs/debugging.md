# Debugging

## Breaking on source lines

`ilc` can halt execution when it reaches a specific file and line. There are
 two options:

- `--break <file:line>`: break on the given IL file.
- `--break-src <file:line>`: break on the original source file when debug info is present.

Before matching, the debugger normalizes paths (resolving relative segments and
symlinks). If no file matches the normalized path, it falls back to comparing
just the basename. This allows commands like `--break break_label.il:5` even
when the compiled program was referenced via a different directory.

Breakpoints are coalesced: if multiple instructions map to the same line, `ilc`
will stop only once per line until the line number changes.

### Examples

```sh
# Break on line 5 of the IL file
ilc -run examples/il/break_label.il --break examples/il/break_label.il:5

# Break using only the basename; path resolution falls back to the filename
ilc -run examples/il/break_label.il --break break_label.il:5

# Break on the original source line when debug info is available
ilc -run examples/il/break_label.il --break-src break_label.il:5
```
