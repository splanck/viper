---
status: active
audience: public
last-verified: 2026-06-20
---

# Localization

> Locale-aware formatting, translation, collation, and text-direction utilities.

**Part of [Viper Runtime Library](../README.md)**

---

## Contents

| File | Classes |
|------|---------|
| [Locale & registry](locale.md) | `Locale`, `LocaleInfo`, `LocaleManager` |
| [Formatting](formatting.md) | `NumberFormat`, `DateFormat`, `RelativeTimeFormat`, `ListFormat` |
| [Messages](messages.md) | `MessageBundle`, `PluralRules` |
| [Collation & direction](collation.md) | `Collator`, `TextDirection` |
| [Data files](data-files.md) | JSON locale-data schema + VPA authoring guide |

---

## Concept overview

`Viper.Localization.*` exposes eleven classes built on top of a shared **locale record** (internally `rt_locale_data_t`) that carries the number separators, currency conventions, month / day names, date patterns, plural rules, relative-time templates, list-formatting templates, and collation tailorings for a single language/region pair. Named IANA time zones live under `Viper.Time.TimeZone`; they are separate from locale data because time-zone transitions are instant/region rules, not language formatting rules.

**What ships baked in:** only **en-US**. Every other locale is loaded at runtime from JSON — either from the filesystem via `LocaleManager.LoadFromJson(path)` or from a VPA-embedded asset via `LocaleManager.LoadFromAsset(name)`.

## Quick start

```rust
bind LocaleManager : Viper.Localization.LocaleManager
bind Locale        : Viper.Localization.Locale
bind NumberFormat  : Viper.Localization.NumberFormat

# The manager auto-detects the system locale on first access. Set it
# explicitly for reproducible output.
LocaleManager.SetCurrent(Locale.Parse("en-US"))

var fmt = NumberFormat.ForLocale(LocaleManager.Current())
Say(fmt.Currency(1234.56))  # "$1,234.56"
```

## Fallback chain model

Every `Locale` has a walkable fallback chain. `en-Latn-US` falls back through `en-US → en → root`; `MessageBundle` can use the same chain for locale-qualified keys such as `en-US:greet`, `en:greet`, and `root:greet` before walking explicit fallback bundles.

```rust
var loc = Locale.Parse("en-Latn-US")
for step in loc.Fallbacks():
    Say(step.Tag)
# en-Latn-US
# en-US
# en
# root
```

## Search Path

`LocaleManager.LoadFromJson(path)` loads exactly the file you pass. `LocaleManager.Load(tag)` canonicalizes the tag, then looks for `<tag>.json` in the directories added with `LocaleManager.AddSearchPath(path)`.

VPA assets are loaded explicitly with `LocaleManager.LoadFromAsset(name)`.

## See Also

- [Time](../time.md) — `DateTime`, `TimeZone`, `Duration`, and `DateOnly`; `DateTime` values are the inputs consumed by `DateFormat` and `RelativeTimeFormat`.
- [Text](../text/formats.md) — `Viper.Text.InvariantNumberFormat` is the C-locale sibling of `Viper.Localization.NumberFormat`.
- [Collections](../collections/README.md) — `Map` and `List` are the value-carrier types for `MessageBundle.Format` / `ListFormat`.
