# Testing and release validation

Run all Xenoscape lanes after an incremental build:

```sh
ctest --test-dir build -R xenoscape --output-on-failure
```

The release suite covers:

| Test | Contract |
|---|---|
| `zia_smoke_xenoscape` | Canvas/system construction and one-frame render |
| `zia_xenoscape_level_validation` | Geometry, spawns, interactions, mechanics, all per-region content anchors |
| `zia_xenoscape_progression` | Schema-v2 slot round trip, checkpoint/tutorial/meta fields, corrupt backup |
| `zia_xenoscape_mechanics` | Ability gates, combat traits, surfaces, camera, collision grace, hit stop, animation minimums |
| `zia_xenoscape_world` | Exact graph, gates, routes, room ids, discovery, fast travel |
| `zia_xenoscape_meta` | Economy, difficulty, death recovery, tutorials, ranks, radio, postgame rules |
| `zia_xenoscape_settings` | Defaults, normalization, persistence, presentation/rumble mappings |
| `zia_xenoscape_campaign` | Clean-profile route to normal/true endings and NG+ |
| `zia_xenoscape_render` | Authored/fallback title screenshots and distinct frame signatures |
| `zia_xenoscape_package` | Mandatory assets, release toggles, procedural fallback inventory |
| `zia_xenoscape_performance` | Two deterministic Veteran NG+ campaign stress passes under fixed pool caps |
| `xenoscape_package_dry_run` | Standalone tarball packaging plan |
| `native_xenoscape_mechanics`, `native_xenoscape_campaign` | Non-Windows native parity for the pure mechanics and ending routes |

The render lanes use the `viper_display` resource lock. Pure probes must print
`RESULT: ok` and produce identical VM/native behavior. The project build smoke
also runs from a non-source working directory to catch missing packaged paths.
The performance lane uses a 20-second CTest timeout as a deliberately generous
cross-platform budget and avoids a brittle in-program wall-clock threshold.

For the repository-wide gate, use the platform build script documented in the
root `AGENTS.md`; do not substitute a raw full CMake build.
