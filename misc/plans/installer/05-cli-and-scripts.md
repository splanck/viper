# Phase 5: CLI Orchestration and Build Scripts

## Goal

Make toolchain packaging a normal, repeatable Viper workflow:
- stage the install tree
- build one or more installer artifacts
- verify them
- write outputs to a predictable directory

## Reuse First

This phase should reuse:
- the current `viper package` command structure in `src/tools/viper/cmd_package.cpp`
- the shared `viper_packaging` library
- the existing build-script conventions from:
  - `scripts/build_viper_unix.sh`
  - `scripts/build_viper_win.cmd`
  - `scripts/build_demos.sh`

Important correction to the draft:
- do not make the toolchain packager depend on a source-tree walk for normal operation
- prefer `--stage-dir` or `--build-dir`, because the staged install tree should already contain the ship set after Phase 0
- when `--build-dir` is used, treat the build tree only as a way to produce a staged install tree; every later step should read from the stage

## Recommended CLI

### New command

`viper install-package`

Suggested usage:

```text
Usage: viper install-package [options]

  Package the Viper toolchain from a staged install tree.

Options:
  --target <fmt>        windows | macos | linux-deb | linux-rpm | tarball | all
  --arch <arch>         x64 | arm64 (default: host)
  --stage-dir <dir>     Existing staged install tree
  --build-dir <dir>     Build tree; command will run cmake --install into a temp stage dir
  -o <path>             Output file or directory
  --version <ver>       Override version string
  --verify-only         Build nothing; verify an existing artifact
  --no-verify           Skip post-build verification
  --verbose, -v         Verbose output
  --help, -h            Show help
```

Behavior:
- require exactly one of `--stage-dir`, `--build-dir`, or `--verify-only`
- if `--build-dir` is provided:
  - create a temporary or explicit staging directory
  - run `cmake --install`
  - package from that staged tree
- by default, verify all built artifacts

Recommended additions:
- `--keep-stage-dir`
  - preserve the generated staging directory for debugging
- `--stage-only`
  - stop after `cmake --install` and manifest validation
- `--metadata-file <path>`
  - optional override for release-facing metadata such as package description, license blurb, or release notes if we do not want to derive them solely from the staged docs

## Why a new command is still the right shape

Do not overload `viper package`.

`viper package` is project packaging:
- input: `viper.project`
- output: packaged application

`viper install-package` is toolchain packaging:
- input: staged install tree
- output: packaged Viper toolchain

They should share lower-level helpers and CLI patterns, but they should remain separate commands.

## Common helper extraction

If duplication with `cmd_package.cpp` grows, extract shared helpers rather than copy logic.

Likely shared helpers:
- target/arch parsing
- output naming
- verification summary formatting
- common filesystem validation
- dry-run / verbose reporting style
- artifact summary printing

Possible internal helper:
- `src/tools/viper/cmd_package_common.hpp/cpp`

## Output naming

Recommended defaults:
- Windows: `viper-{version}-win-{arch}.exe`
- macOS: `viper-{version}-macos-{arch}.pkg`
- Linux `.deb`: `viper_{version}_{arch}.deb`
- Linux `.rpm`: `viper-{version}-1.{arch}.rpm`
- Portable archive: `viper-{version}-{platform}-{arch}.tar.gz`

Important detail:
- the CLI should own format-specific architecture spelling instead of exposing one raw arch token everywhere
- examples:
  - Windows/macOS CLI surface: `x64`, `arm64`
  - Debian output naming/metadata: `amd64`, `arm64`
  - RPM output naming/metadata: `x86_64`, `aarch64`

## Script wrappers

### Unix/macOS

Recommended:
- `scripts/build_installer.sh`

Behavior:
- accept `BUILD_DIR` and target format
- optionally run `cmake --install` automatically
- write artifacts under `${BUILD_DIR}/installers`
- follow the same environment-variable style as the existing build scripts where practical
- do not reimplement packaging logic in shell; the script should only stage, call `viper install-package`, and relay paths/status

### Windows

Recommended:
- `scripts/build_installer.cmd`

Behavior:
- stage via `cmake --install`
- invoke `viper.exe install-package`
- print built artifact paths

## CI integration

The scripts should be thin wrappers around the CLI, not alternate implementations.

Recommended CI pattern:
1. build Viper
2. run `viper install-package ...`
3. verify artifacts
4. optionally install/uninstall them in host-specific lanes

## Modified Existing Files

- `src/tools/viper/main.cpp`
- `src/CMakeLists.txt`

New:
- `src/tools/viper/cmd_install_package.cpp`
- `scripts/build_installer.sh`
- `scripts/build_installer.cmd`

Optional:
- `src/tools/viper/cmd_package_common.hpp/cpp`

## Test Plan

### CLI unit/integration tests
- option parsing
- mutual exclusion of `--stage-dir`, `--build-dir`, `--verify-only`
- auto-staging via `--build-dir`
- `--keep-stage-dir` / `--stage-only` behavior
- output naming
- error reporting for missing staged artifacts
- reuse of `cmd_package`-style verification summaries and dry-run output

### Script smoke tests
- Unix script stages and builds expected artifact
- Windows script stages and builds expected artifact
- `--target all` produces the expected set for the host

## Exit Criteria

This phase is done when:
- there is one supported CLI for toolchain packaging
- the wrapper scripts are thin and deterministic
- the normal developer and CI path is `cmake --install` -> `viper install-package`
