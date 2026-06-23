# Appendix B: Viper BASIC Reference

Viper BASIC is the BASIC-family frontend supported by the current Viper toolchain. The maintained language reference is:

- `docs/basic-reference.md`

Use that document, plus `viper check`, as the source of truth for syntax and supported features. This appendix intentionally avoids repeating a long handwritten reference because the compiler and runtime surface evolve over time.

---

## Checked BASIC Example

```basic
REM A small complete Viper BASIC program
DIM count AS INTEGER = 3
DIM total AS INTEGER = 0

FOR i = 1 TO count
    total = total + i
NEXT i

PRINT "total = "; total
```

Run or check BASIC programs with:

```bash
build/src/tools/viper/viper check program.bas --diagnostic-format=json
build/src/tools/viper/viper run program.bas --diagnostic-format=json
```

---

## Practical Notes

- Prefer complete `.bas` programs when sharing examples. Many BASIC snippets rely on declarations from surrounding context and should be labeled as pseudocode if copied into prose.
- Use the repository build scripts before running broad test suites.
- For runtime APIs, use `build/src/tools/viper/viper --dump-runtime-api` and the `docs/viperlib/` pages.
- For diagnostics, use `build/src/tools/viper/viper explain <CODE> --json`.
