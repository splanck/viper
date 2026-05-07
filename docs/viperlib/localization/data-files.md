---
status: active
audience: public
last-verified: 2026-04-23
---

# Locale Data Files

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

JSON schema for locale data files loaded via `LocaleManager.LoadFromJson(path)` and `LocaleManager.LoadFromAsset(name)`.

---

## Overview

Viper ships with **en-US** baked into the runtime. Every other locale is loaded at runtime from a JSON file matching the schema below. Files are typically named `<tag>.json` (e.g. `fr-FR.json`) and placed either in the filesystem (see [search path](README.md#search-path)) or embedded as VPA assets at build time.

## Schema

```json
{
  "tag": "fr-FR",
  "names": {
    "language": "français",
    "region":   "France",
    "display":  "français (France)"
  },
  "text_direction": "ltr",
  "first_day_of_week": 1,
  "measurement": "metric",

  "numbers": {
    "decimal_sep": ",",
    "group_sep":   "\u00A0",
    "group_size":  3,
    "secondary_group_size": 3,
    "minus":       "-",
    "plus":        "+",
    "percent":     "%",
    "infinity":    "∞",
    "nan":         "NaN",
    "exponent":    "e",
    "digits":      "0123456789"
  },

  "currency": {
    "default_code":     "EUR",
    "symbol":           "€",
    "pattern_positive": "{n} {s}",
    "pattern_negative": "-{n} {s}",
    "fraction_digits":  2
  },

  "dates": {
    "months_wide":   ["janvier","février","mars","avril","mai","juin",
                      "juillet","août","septembre","octobre","novembre","décembre"],
    "months_abbr":   ["janv.","févr.","mars","avr.","mai","juin",
                      "juil.","août","sept.","oct.","nov.","déc."],
    "days_wide":     ["dimanche","lundi","mardi","mercredi","jeudi","vendredi","samedi"],
    "days_abbr":     ["dim.","lun.","mar.","mer.","jeu.","ven.","sam."],
    "am": "AM",
    "pm": "PM",
    "patterns": {
      "short":       "dd/MM/yyyy",
      "medium":      "d MMM yyyy",
      "long":        "d MMMM yyyy",
      "full":        "EEEE d MMMM yyyy",
      "time_short":  "HH:mm",
      "time_medium": "HH:mm:ss",
      "datetime_short":  "dd/MM/yyyy HH:mm",
      "datetime_medium": "d MMM yyyy HH:mm:ss"
    }
  },

  "relative_time": {
    "now":    "maintenant",
    "past":   "il y a {n} {unit}",
    "future": "dans {n} {unit}",
    "units": {
      "second": { "one": "seconde", "other": "secondes" },
      "minute": { "one": "minute",  "other": "minutes"  },
      "hour":   { "one": "heure",   "other": "heures"   },
      "day":    { "one": "jour",    "other": "jours"    },
      "week":   { "one": "semaine", "other": "semaines" },
      "month":  { "one": "mois",    "other": "mois"     },
      "year":   { "one": "an",      "other": "ans"      }
    },
    "short_past": "{n} {unit}",
    "short_future": "+{n} {unit}",
    "short_units": {
      "second": { "one": "s", "other": "s" },
      "minute": { "one": "min", "other": "min" },
      "hour":   { "one": "h", "other": "h" },
      "day":    { "one": "j", "other": "j" },
      "week":   { "one": "sem.", "other": "sem." },
      "month":  { "one": "m.", "other": "m." },
      "year":   { "one": "a", "other": "a" }
    }
  },

  "plural_cardinal": [
    { "category": "one",   "rule": "i = 0 or i = 1" },
    { "category": "other", "rule": "true" }
  ],
  "plural_ordinal": [
    { "category": "one",   "rule": "n = 1" },
    { "category": "other", "rule": "true" }
  ],

  "list_format": {
    "and":  { "pair": "{0} et {1}", "start": "{0}, {1}",
              "middle": "{0}, {1}",  "end": "{0} et {1}" },
    "or":   { "pair": "{0} ou {1}", "start": "{0}, {1}",
              "middle": "{0}, {1}",  "end": "{0} ou {1}" },
    "unit": { "pair": "{0} {1}",    "start": "{0} {1}",
              "middle": "{0} {1}",   "end": "{0} {1}" }
  },

  "collation": { "strength": 3, "reorder": [], "overrides": [] }
}
```

## Field rules

- **`tag`** — must parse as valid BCP-47. Canonicalized on load.
- **Missing optional fields** — inherit the baked en-US default for that field.
- **Months/Days arrays** — exact lengths required: 12 months, 7 days (index 0 = Sunday).
- **Numbers** — `group_size` is the rightmost group size; `secondary_group_size` is the repeated group size to the left (`0` or missing means same as `group_size`). Both must be in `1..9` when present, except `secondary_group_size=0`.
- **Digits** — `numbers.digits` must contain exactly 10 UTF-8 codepoints.
- **Currency** — `default_code` must be a 3-letter uppercase ISO-style code; currency patterns must contain only literal text plus `{n}` and `{s}` and include both placeholders.
- **Relative time** — `past` and `future` must contain `{n}` and `{unit}`. `now`, `short_past`, `short_future`, and `short_units` are optional and inherit en-US defaults when absent.
- **String length** — individual string fields capped at 256 bytes.
- **File size** — total capped at 256 KB.
- **Unknown keys** — ignored.
- **Loaded data lifetime** — `Unload` and `Reset` free JSON/VPA data only when no live formatter/collator/message object is retaining it.

## Plural rule mini-language

The `plural_cardinal` / `plural_ordinal` entries use a CLDR-subset DSL:

```
Rule        = OrExpr | "true"
OrExpr      = AndExpr ("or" AndExpr)*
AndExpr     = Comparison ("and" Comparison)*
Comparison  = Expr (("=" | "!=") (Expr | RangeList))
            | Expr ("in" | "not in" | "within" | "not within") RangeList
Expr        = Var ("mod" Integer)?
Var         = "n" | "i" | "v" | "f" | "t"
RangeList   = Range ("," Range)*
Range       = Integer | Integer ".." Integer
Integer     = [0-9]+
```

Variables (per CLDR):
- `n` — absolute value of input
- `i` — integer part
- `v` — visible fraction digit count (with trailing zeros)
- `f` — visible fraction digits as integer (with trailing zeros)
- `t` — visible fraction digits without trailing zeros

Rule length is capped at 256 chars.

`in` requires the evaluated expression to be integral; `within` accepts fractional values. `not in` and `not within` are logical inverses.

## Currency patterns

`pattern_positive` and `pattern_negative` use two placeholders:
- `{n}` — the formatted number
- `{s}` — the currency symbol

Examples:
- en-US: `"{s}{n}"` → `"$1,234.56"`
- fr-FR: `"{n} {s}"` → `"1 234,56 €"`
- Accounting style: `"({s}{n})"` → `"($1,234.56)"` for negative values.

## List format templates

Each style (`and`, `or`, `unit`) has four templates using `{0}` / `{1}` placeholders:
- `pair` — exactly 2 items: `"{0} and {1}"`
- `start` — first item + rest: `"{0}, {1}"`
- `middle` — middle combinations for 3+: `"{0}, {1}"`
- `end` — last item: `"{0}, and {1}"`

Join algorithm for N ≥ 3: apply `end` to `items[N-2], items[N-1]`, then `middle` from right-to-left, then `start` with `items[0]`.

## VPA asset packaging

Locale data files can be embedded in the compiled binary via VPA (see [Viper.IO.Assets](../io/assets.md)):

```
# viper.project
pack-compressed locales/fr-FR.json
pack-compressed locales/de-DE.json
```

Load at runtime:
```rust
LocaleManager.LoadFromAsset("locales/fr-FR.json")
```

VPA-embedded locale files are resolved independently of the filesystem search path.

## See Also

- [Locale & registry](locale.md) — `LocaleManager.LoadFromJson` / `LoadFromAsset`.
- [Viper.IO.Assets](../io/assets.md) — VPA embedding infrastructure.
