---
status: active
audience: public
last-verified: 2026-07-14
---

# Collation & Text Direction

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

Text-direction utilities and implementation notes about collation.

---

## Internal collation implementation

The runtime contains internal `rt_collator_*` functions and tests, but
`Viper.Localization.Collator` is intentionally excluded from the frontend runtime registry
for the v1 surface. Zia and BASIC programs therefore cannot bind or construct a Collator.
The source-generated [localization API reference](../../generated/runtime/localization.md)
is the authoritative inventory of public classes.

---

## Viper.Localization.TextDirection

Static utilities for classifying text as LTR / RTL / mixed.

**Type:** Static utility.

### Methods

| Method | Signature | Description |
|---|---|---|
| `OfLocale(loc)` | `String(Locale)` | Returns `"ltr"` or `"rtl"` from locale data. |
| `Detect(s)` | `String(String)` | `"ltr"`, `"rtl"`, or `"mixed"`; null/empty input returns `""`, while all-neutral nonempty text defaults to `"ltr"`. |
| `IsRTL(s)` | `Bool(String)` | True when RTL strong-codepoint count is greater than LTR count. |
| `IsLTR(s)` | `Bool(String)` | True when LTR count is greater than or equal to RTL count; null/empty text is LTR. |
| `FirstStrong(s)` | `String(String)` | `"ltr"` / `"rtl"` / `"neutral"` based on first strong codepoint. |
| `Bidi(s)` | `String(String)` | Wraps RTL runs with U+2067/U+2069 isolates for mixed-content strings. |

### RTL scripts recognized

The implementation uses a fixed codepoint-range table covering Hebrew, Arabic, Syriac, Thaana, N'Ko, Samaritan, Mandaic, Arabic Extended-A/B/C, several historical RTL ranges, and Arabic/Hebrew presentation forms. It is not a complete Unicode bidi-property database; codepoints outside those ranges classify as LTR unless they fall in the implementation's neutral punctuation, digit, control, or spacing ranges.

### Notes

- `Bidi` does **not** implement the full Unicode BiDi algorithm — only run-level isolate wrapping. Pure-LTR and pure-RTL inputs pass through unchanged.
- Malformed UTF-8 is decoded as U+FFFD replacement characters. In this limited classifier U+FFFD falls through to LTR.
- Digits, punctuation, and whitespace are classified as neutral; `FirstStrong` skips them to find the first directional codepoint.

### Zia Example

```rust
module TextDirectionDemo;

bind Viper.Terminal;
bind Viper.Localization.TextDirection as TextDirection;

func start() {
    Say(TextDirection.Detect("Hello"));      // "ltr"
    Say(TextDirection.Detect("שלום"));       // "rtl"
    Say(TextDirection.Detect("Hello שלום")); // "mixed"
}
```

### See Also

- [Locale › LocaleInfo](locale.md#viperlocalizationlocaleinfo) — `TextDirection` and `IsRightToLeft` queries.
