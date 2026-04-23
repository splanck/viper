---
status: active
audience: public
last-verified: 2026-04-22
---

# Viper.Localization examples

Three Zia programs demonstrating the `Viper.Localization.*` namespace.

| Example | Shows |
|---|---|
| [`hello-localized.zia`](hello-localized.zia) | Minimal: load fr-FR, format a currency value in both en-US and fr-FR. |
| [`intl-numbers.zia`](intl-numbers.zia) | Number format + parse round-trip across en-US, fr-FR, de-DE, ja-JP. |
| [`translated-app.zia`](translated-app.zia) | MessageBundle-driven small program with JSON translation files under `locales/`. |

Run any of them with:
```sh
viper run hello-localized.zia
```

Locale data lives under `locales/`. For deployment, consider embedding the JSONs via VPA (see [Localization data files](../../docs/viperlib/localization/data-files.md)).
