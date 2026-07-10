# XENOSCAPE first-party assets

All files in this directory are project-bound release assets for XENOSCAPE and
are distributed under the Viper project's GNU GPL v3 license unless a nested
notice states otherwise. No runtime download or external product dependency is
required; `viper.project` bundles this directory into packaged builds.

## Art provenance

- `art/title-key-art.png` — commissioned specifically for XENOSCAPE on
  2026-07-09 using OpenAI's image-generation service from an original project
  brief. It has no embedded text, third-party logo, or watermark. The code-drawn
  title and menu remain authoritative, and the deterministic procedural title
  backdrop remains the missing-asset fallback.
- `art/aria-portrait.png` — commissioned specifically for XENOSCAPE on
  2026-07-09 using OpenAI's image-generation service from an original project
  brief. It depicts the original ARIA ship-AI avatar and contains no text,
  third-party logo, or watermark. Radio subtitles retain a code-drawn hologram
  fallback when the portrait is absent or unreadable.

## Deterministic production atlases

The following PNGs are baked by `tools/build_release_assets.zia` from the
in-tree first-party SpriteFactory recipes and fixed XENOSCAPE palette. They have
no external source material and are reproducible without a network connection:

- `sprites/player-release-sheet.png` — all 41 stable right-facing release poses
  in an 8x6 grid; the game mirrors left-facing frames after decode.
- `tiles/biome-tile-atlas.png` — stable tile ids 0-56 in an 8x8 grid.
- `icons/hud-icon-atlas.png` — pickup, map, objective, radio, and Simulator UI
  pictograms in eight 64px cells.
- `ui/title-menu-panel.png` — translucent circuit-frame texture behind the live
  title menu.

The runtime keeps the code-authored SpriteFactory and Canvas panel drawing as
permanent fallbacks. Rebuild these atlases from the game directory with:

```sh
mkdir -p assets/sprites assets/tiles assets/icons assets/ui
../../../build/src/tools/zia/zia tools/build_release_assets.zia
```
