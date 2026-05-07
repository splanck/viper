---
status: active
audience: public
last-verified: 2026-04-23
---

# Messages & Plural Rules

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

Translation catalogs with fallback chains and CLDR-style plural category selection.

---

## Viper.Localization.MessageBundle

Translation catalog keyed by message ID. Supports placeholder interpolation, fallback stacks, and plural-aware lookup.

### Methods

| Method | Signature | Description |
|---|---|---|
| `New()` | `MessageBundle()` | Empty bundle bound to the current locale. |
| `LoadFromJson(loc, path)` | `MessageBundle(Locale, String)` | Load from filesystem JSON. |
| `LoadFromAsset(loc, name)` | `MessageBundle(Locale, String)` | Load from VPA asset. |
| `FromMap(loc, map)` | `MessageBundle(Locale, Map[String, String])` | Use an existing map. |
| `Get(key)` | `String(String)` | Traps when no bundle in the chain has `key`. |
| `TryGet(key)` | `String(String)` | Returns `""` when missing. |
| `Has(key)` | `Bool(String)` | |
| `Format(key, vars)` | `String(String, Map[String, String])` | `{name}`-style placeholders. |
| `FormatWith(key, values)` | `String(String, List[String])` | Positional `{0}`, `{1}`, … |
| `Plural(key, n, vars)` | `String(String, Int, Map[String, String])` | Plural-aware lookup (see Notes). |
| `Fallback(other)` | `MessageBundle(MessageBundle)` | Attach a fallback; traps on cycle. |
| `Keys()` | `List[String]()` | Keys defined by this bundle (excludes fallback). |

### Properties

| Property | Type | Description |
|---|---|---|
| `Locale` | `Locale` | |
| `Count` | `Int` | Entries in this bundle (excludes fallback). |

### Notes

- **Fallback chain.** `Get` first checks the raw key, then locale-qualified keys inside the same bundle using `<locale-tag>:<key>` across the locale fallback chain, then walks explicit fallback bundles depth-first up to 16 bundles. `Fallback` detects self-cycles and traps.
- **Plural.** `Plural(key, n, vars)` evaluates the bound locale's cardinal plural category for `n`, then looks up `<key>.<category>` (falling through to `<key>.other` if missing). `{n}` is added to a temporary copy of `vars`; the caller's map is not mutated.
- **Value types.** `FromMap`, JSON loaders, `Format(vars)`, and `Plural(vars)` require string values. `FormatWith(values)` requires a list of strings; oversized positional indices are preserved literally.
- **Missing placeholders.** Placeholders referencing absent keys in `vars` are preserved literally, e.g. `{name}` stays `{name}`.
- **Escaping.** Use `{{` and `}}` for literal braces.
- JSON loaders expect a flat `{"key": "value", ...}` object and reject non-string values.

### Zia Example

```rust
bind MessageBundle : Viper.Localization.MessageBundle
bind Locale        : Viper.Localization.Locale

var msgs = MessageBundle.FromMap(
    Locale.Parse("en-US"),
    {
        "greet": "Hello, {name}!",
        "items.one": "1 item",
        "items.other": "{n} items"
    }
)

Say(msgs.Format("greet", {"name": "Alice"}))   # "Hello, Alice!"
Say(msgs.Plural("items", 1, {}))               # "1 item"
Say(msgs.Plural("items", 5, {}))               # "5 items"
```

Locale-qualified fallback example:

```rust
var msgs = MessageBundle.FromMap(Locale.Parse("fr-CA"), {
    "fr:greet": "Bonjour",
    "root:greet": "Hello"
})

Say(msgs.Get("greet"))  # "Bonjour"
```

### See Also

- [PluralRules](#viperslocalizationpluralrules) — underlying category evaluator.
- [Data files](data-files.md) — JSON schema for loaded messages.

---

## Viper.Localization.PluralRules

CLDR plural category selection for cardinal and ordinal numeric forms.

### Methods

| Method | Signature | Description |
|---|---|---|
| `ForLocale(loc)` | `PluralRules(Locale)` | |
| `Cardinal(n)` | `String(Float)` | Returns `"zero"/"one"/"two"/"few"/"many"/"other"`. |
| `CardinalInt(n)` | `String(Int)` | Integer fast path. |
| `Ordinal(n)` | `String(Int)` | English: `"one"/"two"/"few"/"other"` (1st/2nd/3rd/4th). |
| `Categories()` | `List[String]()` | Distinct categories used by this locale. |

### Notes

- **Operands.** CLDR plural operands (`n`, `i`, `v`, `f`, `t`) are computed from the input:
  - `n` = absolute value
  - `i` = integer part
  - `v` = visible fraction digit count (incl. trailing zeros)
  - `f` = visible fraction digits as integer (with trailing zeros)
  - `t` = visible fraction digits without trailing zeros
- Integer-valued doubles skip the decimal-string pass: `v=f=t=0`.
- Loaded locale plural rules support CLDR-style range/list predicates: `in`, `not in`, `within`, `not within`, comma-separated lists, and `a..b` ranges. `in` matches integral values only; `within` also matches fractional values.
- Nonfinite `Cardinal` inputs return `"other"`.
- **en-US** cardinal: `one` for `i=1 && v=0`; else `other`.
- **en-US** ordinal: `one` for `n mod 10 = 1 && n mod 100 != 11`; `two` for `n mod 10 = 2 && n mod 100 != 12`; `few` for `n mod 10 = 3 && n mod 100 != 13`; else `other`.

### Zia Example

```rust
bind PluralRules : Viper.Localization.PluralRules
bind Locale      : Viper.Localization.Locale

var pr = PluralRules.ForLocale(Locale.Parse("en-US"))
Say(pr.CardinalInt(1))    # "one"
Say(pr.CardinalInt(5))    # "other"
Say(pr.Ordinal(11))       # "other"  (11th, not 11st)
Say(pr.Ordinal(21))       # "one"    (21st)
```

### See Also

- [Formatting › RelativeTimeFormat](formatting.md#viperslocalizationrelativetimeformat) — the primary consumer of `PluralRules` inside the runtime.
