<!--
File: docs/dev/debug-recursion.md
Purpose: Guide for debugging BASIC recursion failures using source tracing and breakpoints.
-->

# Debugging BASIC Recursion Failures

Use the factorial example to inspect recursive calls.

```sh
ilc -run tests/e2e/factorial.bas --trace=src \
    --break-src tests/e2e/factorial.bas:5 \
    --debug-cmds examples/il/debug_script.txt
```

The `--trace=src` flag prints each executed instruction with the originating
file and line. The `--break-src` flag pauses before the recursive call. The
debug script steps twice and then continues so you can watch the call enter
and return.

Check the trace for a `RETURN` at the end of `FACT`; missing returns suggest
the recursion never reaches the base case. At startup the trace should show
`fn=@main` followed by a call to `@fact`. If `@main` is absent or never calls
the function, the program may have been lowered incorrectly.
