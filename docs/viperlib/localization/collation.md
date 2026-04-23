---
status: active
audience: public
last-verified: 2026-04-22
---

# Collation & Text Direction

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

Locale-aware string comparison and script-direction utilities.

---

## Viper.Localization.Collator

Locale-aware string comparison + sort-key generation with configurable strength and case/accent sensitivity.

### Methods

| Method | Signature | Description |
|---|---|---|
| `New()` | `Collator()` | Construct for the current locale. |
| `ForLocale(loc)` | `Collator(Locale)` | Construct for a specific locale. |
| `Compare(a, b)` | `Int(String, String)` | Returns -1 / 0 / 1. |
| `Equals(a, b)` | `Bool(String, String)` | |
| `SortKey(s)` | `String(String)` | Deterministic, byte-comparable sort key (hex-encoded). |
| `Sort(items)` | `List[String](List[String])` | New sorted list. |

### Properties

| Property | Type | Description |
|---|---|---|
| `Locale` | `Locale` | |
| `Strength` | `Int` | 1 (primary) / 2 (secondary) / 3 (tertiary, default). 4 clamps with a warning. |
| `IgnoreCase` | `Bool` | Skip tertiary weights. |
| `IgnoreAccents` | `Bool` | Skip secondary weights. |

### Strength levels

- **1 (primary)** — base letters only. `"apple"` and `"APPLE"` are equal.
- **2 (secondary)** — accents differentiate. `"naive"` ≠ `"naïve"`, but still case-insensitive.
- **3 (tertiary, default)** — case differentiates.
- **4 (quaternary)** — not implemented in v1; `SetStrength(4)` issues a warning via `rt_diag` and clamps to 3.

### Locale tailorings (v1)

- **en-US** — default DUCET-lite ordering (Latin + Latin-1 diacritics + Latin Extended-A folded to base letters).
- **sv-SE / sv-FI** — å, ä, ö sort **after z** (Swedish convention).
- **de-* (phonebook style)** — ä/ö/ü/ß fold to base letters at primary level (default behavior).
- **Unsupported scripts** — codepoint-order fallback (documented, not silent).

### Notes

- Inputs exceeding 1 MiB trap with `"input exceeds 1 MiB cap"`.
- `SortKey` output is hex-encoded bytes; byte-wise string comparison of two keys matches the Collator's `Compare` result.
- `Sort` uses insertion sort (fine up to ~5000 items); larger inputs deferred to a future merge-sort implementation.

### Zia Example

```zia
bind Collator : Viper.Localization.Collator
bind Locale   : Viper.Localization.Locale

var c = Collator.ForLocale(Locale.Parse("en-US"))
Say(c.Compare("apple", "banana"))    # -1
Say(c.Compare("apple", "Apple"))     # -1 (lowercase first)

c.IgnoreCase = true
Say(c.Compare("apple", "Apple"))     # 0

# Swedish: å sorts after z
var sv = Collator.ForLocale(Locale.Parse("sv-SE"))
Say(sv.Compare("z", "å"))            # -1
```

---

## Viper.Localization.TextDirection

Static utilities for classifying text as LTR / RTL / mixed.

**Type:** Static utility.

### Methods

| Method | Signature | Description |
|---|---|---|
| `OfLocale(loc)` | `String(Locale)` | Returns `"ltr"` or `"rtl"` from locale data. |
| `Detect(s)` | `String(String)` | `"ltr"` / `"rtl"` / `"mixed"` / `""`. |
| `IsRTL(s)` | `Bool(String)` | Majority-RTL strong codepoints. |
| `IsLTR(s)` | `Bool(String)` | |
| `FirstStrong(s)` | `String(String)` | `"ltr"` / `"rtl"` / `"neutral"` based on first strong codepoint. |
| `Bidi(s)` | `String(String)` | Wraps RTL runs with U+202E/U+202C marks for mixed-content strings. |

### RTL scripts recognized

Hebrew (U+0590–U+05FF), Arabic (U+0600–U+06FF, U+0750–U+077F), Syriac (U+0700–U+074F), Thaana (U+0780–U+07BF), N'Ko (U+07C0–U+07FF), plus Arabic/Hebrew presentation forms. Everything else classifies as LTR.

### Notes

- `Bidi` does **not** implement the full Unicode BiDi algorithm — only run-level marker wrapping. Pure-LTR and pure-RTL inputs pass through unchanged.
- Digits, punctuation, and whitespace are classified as neutral; `FirstStrong` skips them to find the first directional codepoint.

### Zia Example

```zia
bind TextDirection : Viper.Localization.TextDirection

Say(TextDirection.Detect("Hello"))          # "ltr"
Say(TextDirection.Detect("שלום"))           # "rtl"
Say(TextDirection.Detect("Hello שלום"))     # "mixed"
```

### See Also

- [Locale › LocaleInfo](locale.md#viperslocalizationlocaleinfo) — `TextDirection` and `IsRightToLeft` queries.
