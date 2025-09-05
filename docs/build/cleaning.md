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

### Full purge (POSIX)

Use the helper script to remove build directories. It prompts before deleting
unless `YES=1` is set:

```sh
# auto-detect build* dirs at repo root
scripts/clean.sh

# explicit directories
scripts/clean.sh build build-rel

# non-interactive
YES=1 scripts/clean.sh
```


### Full purge (Windows)

Use the PowerShell helper script. It prompts before deleting unless `-Yes` is set:

```powershell
# non-interactive
powershell -ExecutionPolicy Bypass -File scripts/clean.ps1 -Yes
```
