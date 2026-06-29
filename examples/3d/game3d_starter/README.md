# Game3D Starter

This is the copyable starting point for a code-first 3D game in Viper. It keeps
common setup in Game3D helpers: world creation, lighting, quality, environment,
package-aware model loading, a static floor, a first-person character, and a
final-overlay HUD.

Run it from this directory:

```sh
../../../build/src/tools/viper/viper run main.zia
```

Run the deterministic test:

```sh
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run test.zia
```

Validate the package manifest and asset layout:

```sh
../../../build/src/tools/viper/viper package . --target tarball --dry-run
```

`viper.project` packages `assets/` back to `assets/`, so
`Assets3D.LoadEntityAsset("assets/models/triangle.gltf")` works in the source
tree and in packaged builds.
