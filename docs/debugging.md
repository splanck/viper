# Debugging

## Breaking on source lines

`ilc` supports breakpoints on block labels and source lines. Use `--break` with a
label or `file:line` pair to halt before executing the matching instruction.
`--break-src <file:line>` provides an explicit alias for source-line
breakpoints.

Paths are normalized before matching: platform separators are canonicalized and
`.`/`..` segments are removed. If the normalized path still fails to match the
location embedded in the IL, `ilc` falls back to comparing only the basename.

When multiple IL instructions map to the same source line, `ilc` reports a
breakpoint only once per line until control transfers to a different line.

### Examples

Break on a block label:

```bash
ilc -run examples/il/break_label.il --break L3
```

Break on a source line:

```bash
ilc -run examples/il/break_src.il --break examples/il/break_src.il:1
```

Using the explicit alias:

```bash
ilc -run examples/il/break_src.il --break-src examples/il/break_src.il:1
```
