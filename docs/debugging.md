# Debugging

## Breaking on source lines

Use `--break <file:line>` to stop before executing the instruction that maps to a
specific source location. `--break` accepts either a block label or a
`file:line` pair; the `--break-src` flag is an explicit alias for source-line
breakpoints.

Paths are normalized before matching: platform separators are converted to `/`,
and `.` and `..` segments are resolved. If the normalized path still does not
match the path recorded in the IL, `ilc` falls back to comparing only the
basename. When multiple instructions map to the same source line, `ilc` reports a
break only once per line until execution reaches a different line.

Example:

```
$ ilc -run examples/il/break_src.il --break examples/il/break_src.il:3
  [BREAK] src=examples/il/break_src.il:3 fn=@main blk=entry ip=#0

$ ilc -run examples/il/break_src.il --break-src examples/il/break_src.il:3
  [BREAK] src=examples/il/break_src.il:3 fn=@main blk=entry ip=#0
```
