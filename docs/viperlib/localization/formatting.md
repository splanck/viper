---
status: active
audience: public
last-verified: 2026-04-23
---

# Formatting

**Part of [Viper Runtime Library](../README.md) › [Localization](README.md)**

Number, date, relative-time, and list formatters that consume a `Locale`'s data to produce user-facing strings.

---

## Viper.Localization.NumberFormat

Locale-aware number formatting **and parsing** with configurable fraction digits, grouping, strict-mode ambiguity handling, and six rounding modes.

### Methods (format)

| Method | Signature | Description |
|---|---|---|
| `ForLocale(loc)` | `NumberFormat(Locale)` | Construct bound to the given locale. |
| `Decimal(n)` | `String(Float)` | Locale-formatted decimal. |
| `DecimalN(n, digits)` | `String(Float, Int)` | Force exactly N fraction digits. |
| `Integer(n)` | `String(Int)` | Integer with locale grouping. |
| `Percent(n)` | `String(Float)` | Multiplies by 100; appends percent symbol. |
| `Currency(n)` | `String(Float)` | Uses locale's default currency. |
| `CurrencyOf(n, code)` | `String(Float, String)` | Override ISO-4217 code. |
| `Scientific(n, digits)` | `String(Float, Int)` | Scientific notation. |
| `Ordinal(n)` | `String(Int)` | `"1st" / "2nd"` (English rules; other locales pending). |

### Methods (parse)

| Method | Signature | Description |
|---|---|---|
| `ParseDecimal(s)` | `Float(String)` | Strict-or-lenient decimal parse; traps on failure. |
| `TryParseDecimal(s)` | `Optional[Float](String)` | Returns None on failure, no trap. |
| `ParseInteger(s)` | `Int(String)` | Rejects fractional input. |
| `TryParseInteger(s)` | `Optional[Int](String)` | |
| `ParseCurrency(s)` | `Float(String)` | Strips optional leading/trailing symbol. |
| `TryParseCurrency(s)` | `Optional[Float](String)` | |

### Properties

| Property | Type | Description |
|---|---|---|
| `Locale` | `Locale` | Read-only. |
| `MinFractionDigits` | `Int` | Min digits after decimal (0-20). |
| `MaxFractionDigits` | `Int` | Max digits after decimal (0-20). |
| `UseGrouping` | `Bool` | Enable/disable the locale's thousands separator. |
| `Strict` | `Bool` | Strict parse mode; rejects ambiguous groupings. |
| `RoundingMode` | `String` | `"halfEven"` (default) / `"halfUp"` / `"halfDown"` / `"up"` / `"down"` / `"ceiling"` / `"floor"`. |

### Notes

- Format output uses the locale's `numbers.decimal_sep`, `numbers.group_sep`, `numbers.percent`, `currency.symbol`, and `currency.pattern_*` templates.
- Grouping supports both `group_size` and `secondary_group_size`; locales such as Hindi can emit `1,23,45,678`.
- Integer formatting/parsing is exact across the full signed 64-bit range, including `-9223372036854775808`.
- `numbers.digits` is honored for both formatting and parsing, so non-Latin digit sets round-trip.
- Decimal and scientific formatting/parsing use C-locale numeric conversion internally, then apply locale separators, so host process locale does not change results.
- `CurrencyOf` requires a 3-letter uppercase ISO-style code. Non-default valid codes are rendered literally as the symbol placeholder unless the locale has a dedicated symbol table in a future release.
- Currency parsing accepts the locale's positive and negative patterns, including accounting parentheses such as `"($1,234.56)"`.
- **Strict mode parses**: strict rejects inputs where a group separator appears at a non-group-size position (e.g. `"1,00"` under en-US where `group_size=3`). Lenient accepts the same input by treating the separator as informational.
- Rounding: `halfEven` (banker's) is the default; matches IEEE 754 round-to-nearest-even semantics.
- Ordinal in v1 delegates to `Viper.Text.NumberFormat.Ordinal` (English suffixes); locale-specific ordinal suffix tables are a future phase.

### Zia Example

```zia
bind Locale       : Viper.Localization.Locale
bind NumberFormat : Viper.Localization.NumberFormat

var fmt = NumberFormat.ForLocale(Locale.Parse("en-US"))
Say(fmt.Decimal(1234.5))       # "1,234.5"
Say(fmt.Currency(1234.56))     # "$1,234.56"
Say(fmt.Percent(0.125))        # "12.5%"

var parsed = fmt.ParseDecimal("1,234.5")
Say(parsed)                    # 1234.5
```

### BASIC Example

```basic
DIM fmt AS NumberFormat
fmt = NumberFormat.ForLocale(Locale.Parse("en-US"))
PRINT fmt.Currency(99.99)   ' $99.99
```

---

## Viper.Localization.DateFormat

CLDR-pattern-letter date and time formatting.

### Methods

| Method | Signature | Description |
|---|---|---|
| `ForLocale(loc)` | `DateFormat(Locale)` | |
| `Short(ts)` | `String(Int)` | `"3/15/27"` |
| `Medium(ts)` | `String(Int)` | `"Mar 15, 2027"` |
| `Long(ts)` | `String(Int)` | `"March 15, 2027"` |
| `Full(ts)` | `String(Int)` | `"Monday, March 15, 2027"` |
| `Custom(ts, pattern)` | `String(Int, String)` | CLDR pattern letters. |
| `TimeShort(ts)` | `String(Int)` | `"2:30 PM"` |
| `TimeMedium(ts)` | `String(Int)` | `"2:30:05 PM"` |
| `DateTimeShort(ts)` | `String(Int)` | Date + time. |
| `DateTimeMedium(ts)` | `String(Int)` | |
| `DateOnly(d, style)` | `String(DateOnly, String)` | Format a DateOnly by named style (`"short"/"medium"/"long"/"full"`). |
| `MonthName(m, abbr)` | `String(Int, Bool)` | `1..12`. |
| `DayName(dow, abbr)` | `String(Int, Bool)` | `0..6`, 0 = Sunday. |
| `AmPm(isPm)` | `String(Bool)` | |

### Supported CLDR pattern letters

| Letter | Meaning | Rep counts |
|---|---|---|
| `y` | Year | 1 (auto), 2 (2-digit), 4 (full) |
| `M` | Month | 1 (num), 2 (0-padded), 3 (abbr), 4 (wide), 5 (narrow) |
| `d` | Day of month | 1 (num), 2 (0-padded) |
| `E` | Weekday | 1-3 (abbr), 4 (wide), 5 (narrow) |
| `H` | Hour 0-23 | 1, 2 |
| `h` | Hour 1-12 | 1, 2 |
| `m` | Minute | 1, 2 |
| `s` | Second | 1, 2 |
| `a` | AM/PM | 1 |
| `'...'` | Quoted literal | `''` inside = literal apostrophe |

### Notes

- Timestamp inputs are Unix seconds (matches `rt_datetime` convention).
- Numeric pattern output uses the locale's `numbers.digits`; date styles and custom patterns can emit non-Latin digits.
- `DateTimeShort` and `DateTimeMedium` use locale `datetime_short` / `datetime_medium` composition patterns when provided, falling back to date + space + time.
- Unsupported pattern letters (`G`, `Q`, `D`, `w`, `k`, `K`, `z`, `Z`, `v`, `V`) trap with `"unsupported pattern letter"`.
- Unterminated quoted literals trap instead of silently treating the rest of the pattern as literal text.
- Pattern length capped at 256 chars.

### Zia Example

```zia
bind DateTime    : Viper.Time.DateTime
bind DateFormat  : Viper.Localization.DateFormat
bind Locale      : Viper.Localization.Locale

var fmt = DateFormat.ForLocale(Locale.Parse("en-US"))
var ts  = DateTime.Create(2027, 3, 15, 14, 30, 5)

Say(fmt.Long(ts))                        # "March 15, 2027"
Say(fmt.Custom(ts, "yyyy-MM-dd HH:mm"))  # "2027-03-15 14:30"
Say(fmt.Custom(ts, "EEEE, MMM d"))       # "Monday, Mar 15"
```

---

## Viper.Localization.RelativeTimeFormat

Human-readable "N units ago" / "in N units" strings.

### Methods

| Method | Signature | Description |
|---|---|---|
| `ForLocale(loc)` | `RelativeTimeFormat(Locale)` | |
| `Format(duration)` | `String(Int)` | Duration in milliseconds. Positive = past. |
| `FormatFrom(then, now)` | `String(Int, Int)` | Format relative delta between two Unix-second timestamps. |
| `Short(duration)` | `String(Int)` | Short-style form. |
| `Long(duration)` | `String(Int)` | Long-style form (default). |
| `Numeric(value, unit)` | `String(Int, String)` | Explicit unit: `"second"/"minute"/"hour"/"day"/"week"/"month"/"year"`. |

### Properties

| Property | Type | Description |
|---|---|---|
| `Locale` | `Locale` | |
| `Style` | `String` | `"long"` / `"short"`. |

### Notes

- Unit thresholds: `>= 1y`, `>= 30d`, `>= 7d`, `>= 1d`, `>= 1h`, `>= 1m`, else second.
- Durations whose absolute value is less than one second format with the locale's `relative_time.now` string.
- Plural form selected via the bound locale's `PluralRules` cardinal table.
- `Style` accepts only `"long"` and `"short"`; unknown values trap. Short style uses `short_past` / `short_future` and `short_units` when available.
- Relative-time numbers are localized with the locale digit set.

---

## Viper.Localization.ListFormat

Locale-correct list joining ("A, B, and C").

### Methods

| Method | Signature | Description |
|---|---|---|
| `ForLocale(loc)` | `ListFormat(Locale)` | |
| `And(items)` | `String(List[String])` | Conjunction ("and") join. |
| `Or(items)` | `String(List[String])` | Disjunction ("or") join. |
| `Unit(items)` | `String(List[String])` | Plain unit join (no conjunction). |
| `Short(items)` | `String(List[String])` | Short form (uses `And` templates in v1). |

### Notes

- 0 items → `""`; 1 item → the item verbatim; 2 → pair template; 3+ → start/middle/end recursive combine per CLDR.

### Zia Example

```zia
bind ListFormat : Viper.Localization.ListFormat
bind Locale     : Viper.Localization.Locale

var fmt = ListFormat.ForLocale(Locale.Parse("en-US"))
Say(fmt.And(["apples", "bananas", "cherries"]))
# "apples, bananas, and cherries"

Say(fmt.Or(["red", "blue"]))
# "red or blue"
```

### See Also

- [Messages](messages.md) — `MessageBundle.Plural` and `PluralRules` for locale-aware plural selection.
- [Collation](collation.md) — `Collator` and `TextDirection`.
- [Data files](data-files.md) — JSON schema for loaded locales' format templates.
