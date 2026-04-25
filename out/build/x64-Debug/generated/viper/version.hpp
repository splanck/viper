#pragma once

// Toolchain (Viper) version as provided by CMake/configure_file.
#define VIPER_VERSION_STR    "0.2.5-snapshot"

// Numeric parts (derived from VIPER_VERSION_STR before any pre-release suffix)
#define VIPER_VERSION_MAJOR  0
#define VIPER_VERSION_MINOR  2
#define VIPER_VERSION_PATCH  5

// Optional snapshot/build metadata derived from `git describe`.
// Empty if not in a git checkout or no matching tags.
#define VIPER_SNAPSHOT_STR   "0.2.4-dev-76-g24d8750f1-dirty"

// IL/spec version (single source of truth from IL_VERSION file)
#define VIPER_IL_VERSION_STR "0.2.0"

// Runtime namespace transition flag (1 = dual-publish legacy rt_* aliases)
#define VIPER_RUNTIME_NS_DUAL 1
