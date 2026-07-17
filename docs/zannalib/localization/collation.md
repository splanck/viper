---
status: active
audience: public
last-verified: 2026-07-14
---

# Collation & Text Direction

**Part of [Zanna Runtime Library](../README.md) › [Localization](README.md)**

Text-direction utilities and implementation notes about collation.

---

## Internal collation implementation

The runtime contains internal `rt_collator_*` functions and tests, but
`Zanna.Localization.Collator` is intentionally excluded from the frontend runtime registry
for the v1 surface. Zia and BASIC programs therefore cannot bind or construct a Collator.
The source-generated [localization API reference](../../generated/runtime/localization.md)
is the authoritative inventory of public classes.

The internal implementation uses a loaded locale's `collation.strength` plus a small hardcoded
tag-patch table (for example Swedish ordering). Locale JSON accepts strength `1..4`, although the
collator clamps 4 to 3 because quaternary strength is not implemented; `reorder` and `overrides`
fields are ignored (VDOC-082).

---

## Zanna.Localization.TextDirection

Static utilities for classifying text as LTR / RTL / mixed.

**Type:** Static utility.

### Methods

| Method | Signature | Description |
|---|---|---|
| `OfLocale(loc)` | `String(Locale)` | Returns `"ltr"` or `"rtl"` from locale data. |
| `Detect(s)` | `String(String)` | `"ltr"`, `"rtl"`, or `"mixed"`; null/empty input returns `""`, while all-neutral nonempty text defaults to `"ltr"`. |
| `IsRtl(s)` | `Bool(String)` | True when the fixed classifier's RTL count is greater than its LTR count. |
| `IsLtr(s)` | `Bool(String)` | True when that LTR count is greater than or equal to RTL; null/empty text is LTR. |
| `FirstStrong(s)` | `String(String)` | `"ltr"` / `"rtl"` / `"neutral"` based on first strong codepoint. |
| `Bidi(s)` | `String(String)` | Wraps RTL runs with U+2067/U+2069 isolates for mixed-content strings. |

### RTL scripts recognized

The implementation uses a fixed codepoint-range table covering Hebrew, Arabic, Syriac, Thaana,
N'Ko, Samaritan, Mandaic, Arabic Extended-A/B/C, several historical RTL ranges, and
Arabic/Hebrew presentation forms. It is not a Unicode bidi-property database: the entire listed
blocks are treated as strong RTL, while nearly every codepoint outside them and a few fixed
neutral ranges is treated as strong LTR.

### Notes

- `Bidi` does **not** implement the full Unicode BiDi algorithm — only run-level isolate wrapping. Pure-LTR and pure-RTL inputs pass through unchanged.
- Malformed UTF-8 is classified as U+FFFD (advancing one byte for a bad lead/continuation, or to
  end for a truncated sequence); U+FFFD falls through to LTR. `Bidi` copies the original malformed
  bytes rather than replacing them in its returned string.
- The neutral table covers ASCII digits, controls, much ASCII/Latin-1 punctuation, NBSP,
  U+2000–U+206F, Arabic-Indic digits (bidi class AN), and the common combining-mark blocks
  (NSM) — so weak characters never decide `FirstStrong`. It remains block-based rather than
  full UCD bidi tables; Devanagari digits classify LTR, matching their UCD class `L`.

### Zia Example

```rust
module TextDirectionDemo;

bind Zanna.Terminal;
bind Zanna.Localization.TextDirection as TextDirection;

func start() {
    Say(TextDirection.Detect("Hello"));      // "ltr"
    Say(TextDirection.Detect("שלום"));       // "rtl"
    Say(TextDirection.Detect("Hello שלום")); // "mixed"
}
```

### See Also

- [Locale › LocaleInfo](locale.md#zannalocalizationlocaleinfo) — `TextDirection` and `IsRightToLeft` queries.
