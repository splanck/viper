---
status: active
audience: public
last-verified: 2026-07-14
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

  "collation": { "strength": 3 }
}
```

## Field rules

- **`tag`** — required; must pass Viper's constrained non-`root` locale-tag parser and is
  canonicalized on load. This is not full RFC 5646 validation; see
  [Locale parser limitations](locale.md#notes).
- **Missing optional fields** — inherit the baked en-US default for that field.
- **Top-level values** — `text_direction` is `"ltr"` or `"rtl"`; `first_day_of_week` is `0..6` (`0` = Sunday); `measurement` is `"metric"`, `"us"`, or `"uk"`.
- **Months/Days arrays** — exact lengths required: 12 months, 7 days (index 0 = Sunday).
- **Numbers** — `group_size` is the rightmost group size; `secondary_group_size` is the repeated group size to the left (`0` or missing means same as `group_size`). Both must be in `1..9` when present, except `secondary_group_size=0`. Separator and sign strings must have the right JSON type, but the loader does not require them to be distinct or otherwise unambiguous.
- **Digits** — `numbers.digits` is intended to contain exactly 10 UTF-8 codepoints. The current
  validator counts leading-byte spans rather than fully validating UTF-8 (VDOC-069), so locale
  authors should validate this field independently.
- **Currency** — `default_code` must be a 3-letter uppercase ISO-style code; `fraction_digits` must be `0..9`; currency patterns may contain only literal text plus `{n}` and `{s}` and must include both placeholders. The loader currently permits either placeholder more than once, even though duplicate `{n}` forms need not parse back (VDOC-070).
- **Relative time** — `past` and `future` must contain `{n}` and `{unit}`. `now`, `short_past`, `short_future`, and `short_units` are optional and inherit en-US defaults when absent. Short templates are not checked for placeholders.
- **Plural rules** — each cardinal/ordinal chain must contain `1..32` entries. A single predicate range list is capped at 64 ranges.
- **Collation** — the loader accepts internal `strength` values `1..4`, but the internal collator
  currently clamps 4 to 3 because quaternary strength is not implemented (VDOC-082). Other
  collation keys, including `reorder` and `overrides`, are ignored because `Collator` is not on
  the public frontend surface.
- **Formatter-template validation** — date patterns and list templates are not parsed during
  loading. An unsupported date pattern can therefore load successfully and trap only when used;
  list templates missing `{0}` or `{1}` can silently discard items (VDOC-070).
- **String length** — recognized individual string fields and array elements are capped at 256 bytes. Escaped U+0000 is not rejected; downstream C-string use truncates at that byte and can bypass validation (VDOC-068).
- **File size** — total capped at 256 KB.
- **Encoding envelope** — after ASCII space, tab, CR, or LF, the first byte must be `{`; a UTF-8
  BOM is not skipped by the loader's object pre-check.
- **Unknown keys** — ignored.
- **Loaded data lifetime** — `Unload` and `Reset` free JSON/asset data only when no live `Locale` handle or localization object retains it.

## Plural rule mini-language

The `plural_cardinal` / `plural_ordinal` entries use a CLDR-subset DSL:

```
Rule        = OrExpr | "true"
OrExpr      = AndExpr ("or" AndExpr)*
AndExpr     = Comparison ("and" Comparison)*
Comparison  = Expr (("=" | "!=") (Expr | RangeList))
            | Expr ("in" | "not in" | "within" | "not within") RangeList
Expr        = Integer | Var ("mod" Integer)?
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

Rule length is capped at 256 bytes.

`in` requires the evaluated expression to be integral; `within` accepts fractional values. `not in` and `not within` are logical inverses.

Loading validates this grammar but does not require a final catch-all rule. When no entry matches,
selection falls back to `other`. Float operands involving `v`, `f`, or `t` also have a known
exponent-notation defect, and integer rule evaluation loses precision above 2^53; see
[PluralRules](messages.md#viperlocalizationpluralrules).

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

## Asset packaging

Locale data files can be embedded in the executable's asset blob (see [Viper.IO.Assets](../io/assets.md)):

```
# viper.project
embed locales/fr-FR.json
embed locales/de-DE.json
```

Load at runtime:
```rust
LocaleManager.LoadFromAsset("locales/fr-FR.json")
```

For sidecar VPA files, the directive requires both a pack name and a source path:

```
# Both entries go into <project-name>-locales.vpa
pack-compressed locales locales/fr-FR.json
pack-compressed locales locales/de-DE.json
```

Mount the generated VPA with `Viper.IO.Assets.Mount` before calling
`LoadFromAsset`. Embedded and mounted assets are resolved independently of the
locale filesystem search path.

## See Also

- [Locale & registry](locale.md) — `LocaleManager.LoadFromJson` / `LoadFromAsset`.
- [Viper.IO.Assets](../io/assets.md) — VPA embedding infrastructure.
