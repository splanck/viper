---
status: active
audience: public
last-verified: 2026-04-23
---

# Locale, LocaleInfo, LocaleManager

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

---

## Viper.Localization.Locale

Immutable handle representing a BCP-47 language tag. Parsed from strings like `"en-US"`, `"zh-Hans-CN"`, `"fr-FR"`.

**Type:** Reference class (immutable, GC-managed).

### Methods

| Method | Signature | Description |
|---|---|---|
| `Parse(tag)` | `Locale(String)` | Canonicalize + validate; **traps** on invalid input. |
| `TryParse(tag)` | `Locale(String)` | Returns `null` on failure instead of trapping. |
| `FromParts(lang, script, region)` | `Locale(String, String, String)` | Build from pre-split subtags. Empty script/region strings mean "absent". |
| `Invariant()` | `Locale()` | Returns the `root` locale (universal fallback). |
| `Equals(other)` | `Bool(Locale)` | Canonical-tag equality. |
| `Fallbacks()` | `List[Locale]()` | Returns the walk-order fallback chain. |
| `ToString()` | `String()` | Canonical tag string. |

### Properties

| Property | Type | Description |
|---|---|---|
| `Language` | `String` | Lowercased primary subtag (e.g. `"en"`). |
| `Script` | `String` | Title-case script subtag (e.g. `"Latn"`) or `""`. |
| `Region` | `String` | Uppercase region subtag (e.g. `"US"`) or `""`. |
| `Tag` | `String` | Canonical BCP-47 tag (e.g. `"en-Latn-US"`). |

### Notes

- Input canonicalization: language lowercased, script Title-case, region uppercased, underscores normalized to dashes, extension/private-use subtags lowercased, encoding suffixes (`.UTF-8`) stripped.
- Leading, trailing, and duplicate separators are rejected (`"-en"`, `"en-"`, `"en--US"`).
- Variants, Unicode extensions, and private-use tails are preserved in `Tag`. Fallbacks walk from the full tag to its base language/script/region chain, then `root`.
- Parsed-but-unloaded locales are still usable for `Tag` / `Fallbacks()` — info queries fall through to the invariant until the locale is registered.
- `Parse` traps with message `"Viper.Localization.Locale: invalid BCP-47 tag ..."` on empty input, single-char input, subtags longer than 8 chars, or non-alphanumeric content.

### Zia Example

```rust
bind Locale : Viper.Localization.Locale

var us = Locale.Parse("en-US")
Say(us.Language)   # "en"
Say(us.Region)     # "US"
Say(us.Tag)        # "en-US"

for step in us.Fallbacks():
    Say(step.Tag)  # en-US, en, root
```

### BASIC Example

```basic
DIM loc AS Locale
loc = Locale.Parse("en-US")
PRINT loc.Tag        ' en-US
PRINT loc.Language   ' en
```

### See Also

- [LocaleInfo](#viperslocalizationlocaleinfo) — queries about a Locale.
- [LocaleManager](#viperslocalizationlocalemanager) — registry of loaded locales.

---

## Viper.Localization.LocaleInfo

Static utility: queries about a Locale's display properties.

**Type:** Static utility.

### Methods

| Method | Signature | Description |
|---|---|---|
| `DisplayName(loc, inLoc)` | `String(Locale, Locale)` | Combined display name (e.g. `"English (United States)"`). |
| `LanguageName(loc, inLoc)` | `String(Locale, Locale)` | Native language name. |
| `RegionName(loc, inLoc)` | `String(Locale, Locale)` | Native region name. |
| `TextDirection(loc)` | `String(Locale)` | `"ltr"` or `"rtl"`. |
| `FirstDayOfWeek(loc)` | `Int(Locale)` | 0 = Sunday .. 6 = Saturday. |
| `IsRightToLeft(loc)` | `Bool(Locale)` | Convenience for `TextDirection == "rtl"`. |
| `MeasurementSystem(loc)` | `String(Locale)` | `"metric"` / `"us"` / `"uk"`. |
| `Currency(loc)` | `String(Locale)` | Default ISO-4217 code (e.g. `"USD"`). |

### Notes

- `inLocale` is accepted but ignored in v1 (only en-US is baked); display names are always emitted in the target locale's native language.
- NULL locale falls through to the invariant's values (equal to en-US in v1).

---

## Viper.Localization.LocaleManager

Static process-global registry of loaded locales + current/system-locale state.

**Type:** Static utility.

### Methods

| Method | Signature | Description |
|---|---|---|
| `Current()` | `Locale()` | Currently active locale. |
| `SetCurrent(loc)` | `Void(Locale)` | Set current; **traps** if locale isn't loaded. |
| `System()` | `Locale()` | Detected system locale (may be unloaded). |
| `Available()` | `List[String]()` | Registered locale tags. |
| `IsLoaded(loc)` | `Bool(Locale)` | Whether the locale is in the registry. |
| `LoadFromJson(path)` | `Void(String)` | Load from filesystem; traps on error. |
| `TryLoadFromJson(path)` | `Bool(String)` | Returns `false` on failure instead of trapping. |
| `LoadFromAsset(name)` | `Void(String)` | Load from VPA asset. |
| `TryLoadFromAsset(name)` | `Bool(String)` | Returns `false` on failure. |
| `LoadBuiltin(tag)` | `Void(String)` | Load one of the C-baked locales (`"en-US"` only in v1). |
| `Load(tag)` | `Locale(String)` | High-level: canonicalizes the tag, then tries registered locales and filesystem search paths. |
| `SearchPath()` | `String()` | Current search path list. |
| `AddSearchPath(path)` | `Void(String)` | Append a filesystem search dir. |
| `Unload(loc)` | `Bool(Locale)` | Remove a locale; refuses when in use. |
| `Reset()` | `Void()` | Clear search paths and remove unused loaded locales; baked en-US remains. |

### Notes

- First access triggers lazy init: registers baked en-US, detects system, sets current.
- Thread-safe via a process-global rwlock; hot-path formatters capture the locale's data pointer at construction and never re-lock.
- `LoadFromJson(path)` and `LoadFromAsset(name)` parse locale JSON and register the canonical tag. `Try*` variants return `false` on missing/malformed input instead of trapping.
- `Load(tag)` looks for `<canonical-tag>.json` in directories added with `AddSearchPath`.
- `LoadFromJson` / `LoadFromAsset` refuse to replace a loaded locale while any live locale or formatter object still retains that locale data. `Try*` returns `false`; non-try loaders trap after releasing the registry lock.
- `Unload` returns `false` (does not trap) when the locale is the current or system locale, or when other live handles/formatters still hold its data pointer. Passing the only remaining `Locale` handle for that loaded record unbinds that handle and unloads the record.
- `Reset` leaves in-use loaded records registered so existing locale/formatter objects cannot dangle; unload them after those objects are released.

### See Also

- [Formatting](formatting.md), [Messages](messages.md), [Collation](collation.md) — consumers of registered locales.
- [Data files](data-files.md) — JSON schema for loaded locales.
