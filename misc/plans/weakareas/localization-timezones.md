# Localization: IANA Time Zone Support

**Status:** Reframed — localization is broad (130 runtime functions incl. script
detection); the **only** verified gap is time zones.
**Area:** `src/runtime/localization/`, `Viper.Time.*`
**Effort:** M (M–L if a full embedded tzdata is chosen)
**Roadmap fit:** v0.3.x P3 (missing features)

## Problem

`Viper.Time.DateTime` handles creation, arithmetic, ISO parse/format, `Now`, and
`ToLocal`, but there is **no IANA/Olson time-zone support**: a `runtime.def` grep for
`timezone`/`iana`/`olson`/`tz` returns nothing relevant. `ToLocal` uses the host's
current local offset only — you cannot convert/format an instant in an *arbitrary*
named zone (e.g. `Europe/Paris`) with correct DST. For a localization stack that
already does locale-aware number/date formatting, plural rules, and collation, this is
the conspicuous missing piece.

This is the corrected, narrow version of the original (overstated) "localization is
really thin" finding.

## Current state (verified)

- `Viper.Time.DateTime.{Now,NowMs,Create,AddDays,AddSeconds,Diff,Format,ToISO,ToLocal,
  ParseISO,ParseDate,Year..Second,DayOfWeek}`.
- `Viper.Localization` — 130 functions incl. `Locale.get_Script`, number/currency/date
  formatting, collation, plural rules, message bundles.
- No zone abstraction, no DST rule engine, no zone database.

## Goal & scope

- **In:** A `TimeZone` abstraction able to (a) resolve a zone by IANA name, (b) compute
  the UTC offset for a given instant including DST, (c) convert an instant to wall-clock
  in that zone, and (d) format in that zone. `DateTime.ToZone(name)` / `FormatInZone`.
- **Out:** Historical pre-1970 transitions beyond what the chosen data source provides;
  leap seconds; calendar systems other than proleptic Gregorian (already handled).

## Design — the data-source decision is the crux

The zero-dependency rule means we cannot link `libicu`/`libtz`. Three viable sources,
pick one explicitly:

1. **Embedded compiled tzdata subset (recommended).** Generate a compact binary table
   from an IANA `tzdata` release at build time (mirror the existing asset-embedding /
   `embed_font.py`-style codegen) covering common zones + their DST rules. Deterministic,
   offline, cross-platform-identical. Cost: a generator script + a periodic data refresh.
2. **Read the OS zoneinfo at runtime.** POSIX has `/usr/share/zoneinfo` (parse the TZif
   binary format). **Windows has no zoneinfo** — it needs a Windows-zone↔IANA mapping
   table and registry reads. Cross-platform parity is harder and output can differ by
   host OS version (breaks determinism).
3. **POSIX `TZ`-string rules only.** Support fixed offset + the `std/dst,M m.w.d` POSIX
   rule grammar. Small and dependency-free, but no named-zone history.

**Recommendation:** option 1 (embedded subset) — it preserves determinism and the
zero-dependency, identical-across-platforms guarantees the project values. Start with a
curated zone set; expand the embedded table over time.

## Implementation steps

1. `scripts/` generator: parse an IANA `tzdata` release → emit a compact C table
   (`src/runtime/localization/rt_tzdata_generated.inc`) of zones, transition times, and
   offsets/abbreviations. Document the source release + refresh procedure in
   `docs/generated-files.md`.
2. `src/runtime/localization/rt_timezone.h/.c`: `rt_tz_find(name)`,
   `rt_tz_offset_at(tz, utc_instant)`, `rt_tz_to_wall(tz, instant)`,
   `rt_tz_format(tz, instant, pattern)`.
3. Extend `Viper.Time.DateTime` with `ToZone`/`FormatInZone`; register `TimeZone` class.
4. `check_runtime_completeness.sh`; docs under `docs/viperlib/` (time/localization).

## API surface (`runtime.def`)

```
RT_FUNC(TimeZoneFind,    rt_tz_find,        "Viper.Time.TimeZone.Find",       "obj(str)")    // null/trap on unknown
RT_FUNC(TimeZoneName,    rt_tz_name,        "Viper.Time.TimeZone.get_Name",   "str(obj)")
RT_FUNC(TimeZoneOffsetAt,rt_tz_offset_at,   "Viper.Time.TimeZone.OffsetAt",   "i64(obj,i64)")// seconds, at utc instant
RT_FUNC(TimeZoneIsDstAt, rt_tz_is_dst_at,   "Viper.Time.TimeZone.IsDstAt",    "i1(obj,i64)")
RT_FUNC(DateTimeToZone,  rt_datetime_to_zone,"Viper.Time.DateTime.ToZone",    "str(i64,obj)")
RT_FUNC(DateTimeFormatInZone, rt_datetime_format_in_zone,"Viper.Time.DateTime.FormatInZone","str(i64,obj,str)")
```

## Tests (`src/tests/runtime/`)

- **DST transition:** `America/New_York`, instant just before/after 2025 spring-forward
  → offset flips `-05:00`→`-04:00`; the skipped local hour handled per policy.
- **Southern-hemisphere DST** (e.g. `Australia/Sydney`) to catch sign assumptions.
- **Non-DST zone** (e.g. `Asia/Tokyo`) constant offset.
- **Fixed-offset / UTC** identity.
- **Unknown zone name** → defined error (trap or null — pick and test).
- **Determinism:** identical output across mac/win/linux for the same instant+zone
  (this is the payoff of the embedded-data approach; add to the equivalence suite).

## Cross-platform

With embedded data there is **no platform branch** — same table everywhere, preserving
determinism. (Options 2/3 would introduce per-OS divergence; that is the main reason to
prefer option 1.)

## Documentation

- Document `Viper.Time.TimeZone` + `DateTime.ToZone`/`FormatInZone` in the
  `docs/viperlib/` time/localization section, with DST-transition examples.
- Record the embedded `tzdata` source release and refresh procedure in
  `docs/generated-files.md` (it is a generated artifact).
- Note the ambiguous/skipped-local-time resolution policy in the reference.
- One concise release-notes line.

## Risks / open questions

- **Data size & refresh cadence:** embedding tzdata adds binary size and a maintenance
  task when IANA publishes updates. Scope the initial zone set; document the refresh.
- **Ambiguous/!skipped local times** around transitions: define the resolution policy
  (earliest/latest/reject) and test it.
