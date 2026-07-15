---
status: active
audience: public
last-verified: 2026-07-14
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
| `TryParseOption(tag)` | `Option[Locale](String)` | Returns `Some(Locale)` on success or `None` on failure. |
| `FromParts(lang, script, region)` | `Locale(String, String, String)` | Build from pre-split subtags. Empty script/region strings mean "absent". |
| `Invariant()` | `Locale()` | Returns the `root` locale (universal fallback). |
| `Equals(other)` | `Bool(Locale)` | Canonical-tag equality. |
| `Fallbacks()` | `Object()` (runtime `List` of `Locale`) | Returns the walk-order fallback chain. |
| `ToString()` | `String()` | Canonical tag string. |

Prefer `TryParseOption` for new code. `TryParse` remains as a compatibility helper for code that already checks null.

### Properties

| Property | Type | Description |
|---|---|---|
| `Language` | `String` | Lowercased primary subtag (e.g. `"en"`). |
| `Script` | `String` | Title-case script subtag (e.g. `"Latn"`) or `""`. |
| `Region` | `String` | Uppercase region subtag (e.g. `"US"`) or `""`. |
| `Tag` | `String` | Canonical BCP-47 tag (e.g. `"en-Latn-US"`). |

### Notes

- Input canonicalization lowercases the language, Title-cases the script, uppercases an alphabetic region, normalizes underscores to dashes, and lowercases extension/private-use subtags. `Locale.Parse` does **not** accept POSIX encoding or modifier suffixes such as `.UTF-8` or `@latin`; only the system-locale platform adapters strip those suffixes before parsing.
- Leading, trailing, and duplicate separators are rejected (`"-en"`, `"en-"`, `"en--US"`).
- Variants, Unicode extensions, and private-use tails are preserved in `Tag`. Fallbacks walk from the full tag to its base language/script/region chain, then `root`.
- Parsed-but-unloaded locales are still usable for `Tag` / `Fallbacks()`, but their info queries use the baked en-US invariant data. Loading the tag does not retroactively bind an already-created handle; parse it again, use the handle returned by `LocaleManager.Load`, or pass it to `SetCurrent` after loading.
- `Parse` traps on empty input, a primary language shorter than two characters, subtags longer than eight characters, malformed extension/private-use structure, or non-ASCII-alphanumeric subtag content.
- The runtime registry currently exposes `Fallbacks()` as opaque `Object`, so Zia cannot infer that it is iterable. Traverse it through `Viper.Collections.List` as shown below.

### Zia Example

```rust
module LocaleDemo;

bind Viper.Terminal;
bind Viper.Collections.List as RuntimeList;
bind Viper.Localization.Locale as Locale;

func start() {
    var us = Locale.Parse("en-US");
    Say(us.Language); // "en"
    Say(us.Region);   // "US"
    Say(us.Tag);      // "en-US"

    var chain = us.Fallbacks();
    var i = 0;
    while i < RuntimeList.get_Count(chain) {
        Say(Locale.ToString(RuntimeList.Get(chain, i))); // en-US, en, root
        i += 1;
    }
}
```

### BASIC Example

```basic
DIM localeObj AS Viper.Localization.Locale
localeObj = Viper.Localization.Locale.Parse("en-US")
PRINT localeObj.ToString()
PRINT Viper.Localization.Locale.get_Language(localeObj)
```

### See Also

- [LocaleInfo](#viperlocalizationlocaleinfo) — queries about a Locale.
- [LocaleManager](#viperlocalizationlocalemanager) — registry of loaded locales.

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
| `System()` | `Locale()` | Detected system locale (may be unloaded); falls back to en-US when detection fails. |
| `Available()` | `List[String]()` | Registered locale tags in registration order. |
| `IsLoaded(loc)` | `Bool(Locale)` | Whether the locale is in the registry. |
| `LoadFromJson(path)` | `Void(String)` | Load from filesystem; traps on error. |
| `TryLoadFromJson(path)` | `Bool(String)` | Returns `false` on failure instead of trapping. |
| `LoadFromAsset(name)` | `Void(String)` | Load through the asset system (embedded or mounted VPA asset). |
| `TryLoadFromAsset(name)` | `Bool(String)` | Returns `false` on failure. |
| `LoadBuiltin(tag)` | `Void(String)` | Load one of the C-baked locales (`"en-US"` only in v1). |
| `Load(tag)` | `Locale(String)` | Canonicalize the tag, then try the registry and filesystem search paths; returns null if invalid or unavailable. |
| `SearchPath()` | `String()` | Current search paths joined with the platform path-list separator. |
| `AddSearchPath(path)` | `Void(String)` | Append a filesystem search dir. |
| `Unload(loc)` | `Bool(Locale)` | Remove a locale; refuses when in use. |
| `Reset()` | `Void()` | Clear search paths and remove unused loaded locales; baked en-US remains. |

### Notes

- First access triggers lazy initialization: it registers baked en-US and detects the system locale. Current uses that locale only when its exact tag is already loaded; otherwise current starts as en-US.
- Thread-safe via a process-global rwlock; hot-path formatters capture the locale's data pointer at construction and never re-lock.
- `LoadFromJson(path)` and `LoadFromAsset(name)` parse locale JSON and register the canonical tag. `Try*` variants return `false` on missing/malformed input instead of trapping.
- `Load(tag)` looks for `<canonical-tag>.json` in directories added with `AddSearchPath`.
- `LoadFromJson` / `LoadFromAsset` refuse to replace a loaded locale while any live locale or formatter object still retains that locale data. `Try*` returns `false`; non-try loaders trap after releasing the registry lock.
- `Unload` returns `false` (does not trap) when the locale is the current or system locale, or when other live handles/formatters still hold its data pointer. Passing the only remaining `Locale` handle for that loaded record unbinds that handle and unloads the record.
- `Reset` leaves in-use loaded records registered so existing locale/formatter objects cannot dangle; unload them after those objects are released.

### See Also

- [Formatting](formatting.md), [Messages](messages.md), [Collation](collation.md) — consumers of registered locales.
- [Data files](data-files.md) — JSON schema for loaded locales.
