<!--
File: docs/build/cleaning.md
Purpose: Guide on cleaning CMake build artifacts.
-->

# Cleaning CMake build trees

CMake generates build artifacts in a separate directory. Cleaning removes these
artifacts without reconfiguring the project.

## Generator types

Single-config generators (Ninja, Make) produce one configuration per build
tree. Multi-config generators (MSVC, Xcode) hold multiple configurations side by
side and require a specific build type when invoking targets.

### Single-config (Ninja/Make)

```sh
cmake --build build --target clean
```

### Multi-config (MSVC/Xcode)

```sh
cmake --build build --target clean --config Debug
cmake --build build --target clean --config Release
```

## Purging

Removing the build directory nukes all generated files and caches:

```sh
rm -rf build
```

Upcoming `scripts/clean.*` helpers and a `distclean` target will offer a
portable way to purge build trees.
