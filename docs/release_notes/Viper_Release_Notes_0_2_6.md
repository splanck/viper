# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. -->

### What this release is about

A single marquee theme: **the new `Viper.Localization.*` namespace** — eleven classes giving Viper programs locale-aware number/date/relative-time/list formatting, translation bundles with fallback chains, CLDR plural-category selection, and locale-aware collation. Zero external dependencies (no ICU, no libintl); en-US is baked into the runtime, every other locale loads from JSON via filesystem or VPA-embedded assets.

- **Full 11-class surface ships in v1:** Locale, LocaleInfo, LocaleManager, NumberFormat (format + parse), DateFormat (CLDR pattern letters), RelativeTimeFormat, MessageBundle ({name}/{0} placeholder interpolation + Plural + fallback), PluralRules (CLDR AST evaluator), Collator (DUCET-lite weights + sv-SE tailoring), ListFormat, TextDirection.
- **Hybrid data model:** en-US as a static `rt_locale_data_t` record (zero runtime cost); everything else via `LocaleManager.LoadFromJson(path)` or `LocaleManager.LoadFromAsset(name)`. JSON schema documented at `docs/viperlib/localization/data-files.md`.
- **Thread-safe registry** guarded by `rt_rwlock`; formatters capture the locale's data pointer at construction so the hot path never locks.
- **Shared-code design:** the new `numfmt_group_digits()` helper in `rt_numfmt_internal.h` is called from both the existing `Viper.Text.NumberFormat.Thousands` and every `Viper.Localization.NumberFormat.*` path — zero duplication, no public-surface breakage.

### Localization runtime

- **Locale + LocaleManager + LocaleInfo** — BCP-47 parser with case canonicalization, fallback chain, registry bootstrap with system-locale detection (Windows `GetUserDefaultLocaleName`, macOS/Linux `$LANG` cascade with `.UTF-8` suffix stripping).
- **NumberFormat** — locale-aware Decimal / DecimalN / Integer / Percent / Currency / CurrencyOf / Scientific / Ordinal format methods, matching ParseDecimal / ParseInteger / ParseCurrency with `Optional<T>` Try* variants. Six rounding modes (halfEven default, halfUp, halfDown, up, down, ceiling, floor). Strict mode rejects ambiguous group placements; lenient mode accepts.
- **PluralRules** — CLDR cardinal + ordinal chains evaluated on a compact AST (7 node kinds, 5 operand variables `n / i / v / f / t`). en-US rules cover one/two/few/other for both cardinal and ordinal. Engine stays stateless and thread-safe.
- **DateFormat** — CLDR pattern-letter emitter supporting `y / M / d / E / H / h / m / s / a` plus quoted literals (`'text'`, `''` for literal apostrophe). Short / Medium / Long / Full / TimeShort / TimeMedium / DateTimeShort / DateTimeMedium / Custom entry points plus MonthName / DayName / AmPm queries.
- **RelativeTimeFormat** — "3 days ago" / "in 2 hours" with automatic unit selection across 7 thresholds (year / month / week / day / hour / minute / second) and plural form picked via PluralRules. FormatFrom(then, now) + explicit Numeric(value, unit) entry points.
- **MessageBundle** — translation catalog with `{name}` named placeholders, `{0}` positional placeholders, Plural key resolution (`<key>.<category>` with `.other` fallback), fallback bundle chain (depth-16 cap, self-cycle detection), LoadFromJson + LoadFromAsset.
- **ListFormat** — 0/1/2/3+ item joining via the locale's And / Or / Unit style templates with start/middle/end recursive combine.
- **TextDirection** — strong-RTL codepoint classifier covering Hebrew / Arabic / Syriac / Thaana / N'Ko + presentation forms. Detect / IsRTL / IsLTR / FirstStrong / Bidi (RLO/PDF wrapping for mixed runs). Does NOT implement full Unicode BiDi — run marking only.
- **Collator** — DUCET-lite weight classifier covering basic Latin + Latin-1 Supplement + Latin Extended-A folded to base letters, with sv-SE tailoring (å/ä/ö sort after z). Strengths 1-3 (quaternary clamps with warning). IgnoreCase / IgnoreAccents toggles. SortKey (hex-encoded, byte-comparable) and List-Sort driver.

### Shared plumbing & codegen

- **`rt_numfmt_group_digits()`** — extracted from the monolithic `rt_numfmt_thousands` into a shared helper at `rt_numfmt_internal.h`; consumed by both existing Viper.Text.NumberFormat.Thousands and new Viper.Localization.NumberFormat.{Decimal,Integer,Currency,Percent}.
- **`RtComponent::Localization`** — new codegen component with `rt_locale_*` symbol prefix classification and `Viper.Localization.*` namespace routing for selective linking.
- **`viper_rt_localization` archive** — participates in the runtime-component manifest, install toolchain, and linker runtime-import audit. Added libc `strnlen` to the dynamic-import allowlist.

### Tests

~250 assertions across 11 new test files under `src/tests/runtime/`: `RTLocaleTests.cpp`, `RTLocaleManagerTests.cpp`, `RTLocaleInfoTests.cpp`, `RTLocaleNumberFormatTests.cpp`, `RTPluralRulesTests.cpp`, `RTDateFormatTests.cpp`, `RTRelativeTimeFormatTests.cpp`, `RTMessageBundleTests.cpp`, `RTListFormatTests.cpp`, `RTTextDirectionTests.cpp`, `RTCollatorTests.cpp`. Plus 3 libFuzzer harnesses (plural-rule parser, date-pattern parser, locale-JSON loader) under `src/tests/fuzz/` gated on `VIPER_ENABLE_FUZZ=ON`.

### Docs & examples

- 6 markdown files under `docs/viperlib/localization/`: README, locale, formatting, messages, collation, data-files.
- 3 Zia example programs under `examples/localization/`: `hello-localized.zia` (minimal), `intl-numbers.zia` (format/parse round-trip across locales), `translated-app.zia` (MessageBundle-driven UI).
- Sample JSON locale files under `examples/localization/locales/` (en-US + fr-FR + messages-en-US).
- `docs/viperlib/README.md` Quick Navigation gains a Localization row.

### Carryover hardening

- `rt_fmt.c` `vsnprintf` with non-literal format now suppresses `-Wformat-nonliteral` via a localized pragma; pre-existing uncommitted work reached build-correctness.
- BASIC frontend `IoStatementLowerer.cpp` — four references to `kConvertToDouble` / `kConvertToInt` / `kParseDouble` / `kParseInt64` namespace-qualified to resolve a generator-emitted name collision.

### Commits

See `git log` for the full history. Commits group as: runtime.def registrations, per-class C implementations, test file additions, doc set, fuzz harnesses, examples.
