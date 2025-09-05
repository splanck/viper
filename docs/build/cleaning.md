<!--
File: docs/build/cleaning.md
Purpose: Explain how to clean CMake build artifacts.
-->

# Cleaning CMake builds

CMake build trees accumulate intermediate files. This guide covers removing
those artifacts.

## Single vs multi-config generators

Single-config generators (Ninja, Makefiles) produce one build tree per build
type. Multi-config generators (MSVC, Xcode) keep multiple configurations in one
tree and require selecting a configuration at build time.

## Buildsystem clean

Single-config:

```sh
cmake --build build --target clean
```

Multi-config:

```sh
cmake --build build --target clean --config Debug
```

## Full purge

Remove the entire build directory to start fresh:

```sh
rm -rf build
```

Future prompts will add `scripts/clean.*` helpers and a `distclean` target.

