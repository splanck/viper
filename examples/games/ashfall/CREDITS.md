# ASHFALL — Credits & Licensing

## Code

All game code is original Zia, part of the Viper project, under the GNU GPL v3.
See the repository `LICENSE`. No external code dependencies — every system
(movement, ballistics, AI, level tech, economy, UI, FX, audio mix) is written
from scratch on the Viper runtime.

## Art & audio

This build ships with **procedural graybox** art only: all meshes are engine
primitives (boxes, spheres, cylinders) and all materials are built in
`assets/fallbacks.zia`. There are no bundled third-party assets, so there is
nothing to attribute yet.

The engine honors an **asset-optional contract**: the game is fully playable with
the procedural fallbacks. When CC0 model/texture/audio packs are added, their
sources and licenses will be listed here per pack, and the asset registry will
swap them in over the same material/mesh factory used by the fallbacks.

## Engine

Built on the Viper toolchain (IL-based compiler → VM / native codegen) and its
3D runtime (Graphics3D / Game3D / Physics3D / Sound). See the top-level project
documentation.
