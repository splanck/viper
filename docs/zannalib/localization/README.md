---
status: active
audience: public
last-verified: 2026-07-14
---

# Localization

> Locale-aware formatting, translation, and text-direction utilities.

**Part of [Zanna Runtime Library](../README.md)**

---

## Contents

| File | Classes |
|------|---------|
| [Locale & registry](locale.md) | `Locale`, `LocaleInfo`, `LocaleManager` |
| [Formatting](formatting.md) | `NumberFormat`, `DateFormat`, `RelativeTimeFormat`, `ListFormat` |
| [Messages](messages.md) | `MessageBundle`, `PluralRules` |
| [Collation & direction](collation.md) | `TextDirection` plus internal collation notes |
| [Data files](data-files.md) | JSON locale-data schema + ZPAK authoring guide |

---

## Concept overview

`Zanna.Localization.*` exposes public classes built on top of a shared **locale record**
(internally `rt_locale_data_t`) that carries number separators, currency conventions,
month/day names, date patterns, plural rules, relative-time templates, list-formatting
templates, and internal collation settings for one canonical locale tag. The exact public
class inventory is source-generated in the
[localization API reference](../../generated/runtime/localization.md). Named IANA time
zones live under `Zanna.Time.TimeZone`; they are separate from locale data because
time-zone transitions are instant/region rules, not language-formatting rules.

**What ships baked in:** only **en-US**. Every other locale is loaded at runtime from JSON—either
from the filesystem via `LocaleManager.LoadFromJson(path)` or through the asset system via
`LocaleManager.LoadFromAsset(name)`. The runtime accepts a constrained BCP-47-shaped tag grammar;
see the [Locale parser limitations](locale.md#notes) before using arbitrary external language
tags as registry identifiers.

## Quick start

```zia
module LocalizationQuickStart;

bind Zanna.Terminal;
bind Zanna.Localization.Locale as Locale;
bind Zanna.Localization.LocaleManager as LocaleManager;
bind Zanna.Localization.NumberFormat as NumberFormat;

func start() {
    // The manager detects the system locale on first access. Set it
    // explicitly for reproducible output.
    LocaleManager.SetCurrent(Locale.Parse("en-US"));

    var fmt = NumberFormat.ForLocale(LocaleManager.Current());
    Say(fmt.Currency(1234.56)); // "$1,234.56"
}
```

## Fallback chain model

Every `Locale` has a walkable fallback chain. `en-Latn-US` falls back through `en-US → en → root`; `MessageBundle` can use the same chain for locale-qualified keys such as `en-US:greet`, `en:greet`, and `root:greet` before walking explicit fallback bundles.

```zia
module LocaleFallbacks;

bind Zanna.Terminal;
bind Zanna.Collections.List as RuntimeList;
bind Zanna.Localization.Locale as Locale;

func start() {
    var loc = Locale.Parse("en-Latn-US");
    var chain = loc.Fallbacks();
    var i = 0;
    while i < RuntimeList.get_Count(chain) {
        Say(Locale.ToString(RuntimeList.Get(chain, i)));
        i += 1;
    }
    // en-Latn-US
    // en-US
    // en
    // root
}
```

## Search Path

`LocaleManager.LoadFromJson(path)` loads exactly the file you pass. `LocaleManager.Load(tag)` canonicalizes the tag, then looks for `<tag>.json` in the directories added with `LocaleManager.AddSearchPath(path)`.

Packaged or mounted assets are loaded explicitly with `LocaleManager.LoadFromAsset(name)`.

## See Also

- [Time](../time.md) — `DateTime`, `TimeZone`, `Duration`, and `DateOnly`; `DateTime` values are the inputs consumed by `DateFormat` and `RelativeTimeFormat`.
- [Text](../text/formats.md) — `Zanna.Text.InvariantNumberFormat` is the C-locale sibling of `Zanna.Localization.NumberFormat`.
- [Collections](../collections/README.md) — runtime `Map` and `List` objects are the value-carrier types for `MessageBundle.Format` / `ListFormat`; see those APIs' notes about raw string elements.
