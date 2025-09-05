<!--
File: docs/build/cleaning.md
Purpose: Explain cleaning strategies for CMake builds.
-->

# Cleaning builds

These commands assume an out-of-source build in `build/`.

CMake generators fall into single-config and multi-config variants.

## Single-config generators

Ninja and Makefiles manage one configuration per build tree:

```sh
cmake --build build --target clean
```

## Multi-config generators

MSVC and Xcode host multiple configurations. Add `--config` to specify which one to clean:

```sh
cmake --build build --target clean --config Debug
cmake --build build --target clean --config Release
```

## Full purge

Remove the `build` directory for a full reset:

```sh
rm -rf build
```

Helper scripts (`scripts/clean.*`) and a `distclean` target will provide additional options in later prompts.

