# Debugging

## Breaking on source lines

Use `--break <file:line>` to stop execution before the instruction mapped to a
source line. `--break-src <file:line>` is an explicit alias.

Paths are normalized (platform separators and `.`/`..` segments). If the
normalized path still does not match the path recorded in the IL, `ilc` falls
back to comparing only the basename.

When multiple instructions map to the same source line, `ilc` reports a
breakpoint once per line until control transfers to a different line.

Example using `--break`:

```bash
ilc -run examples/il/break_src.il --break examples/il/break_src.il:1
```

Example using `--break-src`:

```bash
ilc -run examples/il/break_src.il --break-src examples/il/break_src.il:1
```

