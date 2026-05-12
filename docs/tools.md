---
status: active
audience: public
last-verified: 2026-05-04
---

# CLI Tools Reference

Reference documentation for the Viper command-line tools.

## User-Facing Tools

### zia

Run or compile Zia programs.

```bash
# Run a Zia program
zia program.zia

# Emit IL
zia program.zia --emit-il

# Save IL to file
zia program.zia -o program.il
```

### vbasic

Run or compile BASIC programs.

```bash
# Run a BASIC program
vbasic program.bas

# Emit IL
vbasic program.bas --emit-il

# Save IL to file
vbasic program.bas -o program.il
```

### zia-server

Language server for Zia — serves MCP (for AI assistants) and LSP (for editors).

```bash
# Start in MCP mode (for Claude Code, Copilot, etc.)
zia-server --mcp

# Start in LSP mode (for VS Code)
zia-server --lsp

# Auto-detect protocol from first message
zia-server
```

See [Language Server Reference](zia-server.md) for configuration and tool documentation.

### ilrun

Execute IL programs.

```bash
# Run an IL program
ilrun program.il

# With tracing
ilrun program.il --trace

# With breakpoints
ilrun program.il --break main:10
```

### il-verify

Verify IL correctness.

```bash
il-verify program.il
```

### il-dis

Disassemble IL modules.

```bash
il-dis program.il
```

---

## Advanced Tool: viper

The unified compiler driver provides advanced functionality.

### Overview

The CLI is organized around primary entry points:

- `viper init <name> [--lang zia|basic]` — Scaffold a new project
- `viper run <file|dir>` — Build and run a source file or project
- `viper build <file|dir> [-o out]` — Emit IL or build a native binary
- `viper -run <file.il>` — Execute an IL module
- `viper front zia -emit-il <file.zia>` — Legacy low-level Zia frontend entry point
- `viper front zia -run <file.zia>` — Legacy low-level Zia frontend execution path
- `viper front basic -emit-il <file.bas>` — Legacy low-level BASIC frontend entry point
- `viper front basic -run <file.bas>` — Legacy low-level BASIC frontend execution path
- `viper il-opt <in.il> -o <out.il>` — Run optimization passes
- `viper codegen x64 <in.il> -o <out>` — Compile to x86-64 native code
- `viper codegen arm64 <in.il> -S <out.s>` — Generate ARM64 assembly
- `viper package <dir>` — Package a project for distribution (.app, .deb, .exe, .tar.gz)
- `viper install-package` — Package the staged Viper toolchain itself (.exe, .pkg, .deb, .rpm, .tar.gz)
- `viper repl` — Launch the interactive REPL

### viper init

Scaffold a new Viper project.

```bash
viper init <project-name> [--lang zia|basic]
```

| Option          | Description                        | Default |
|-----------------|------------------------------------|---------|
| `--lang zia`    | Create a Zia project               | `zia`   |
| `--lang basic`  | Create a BASIC project             | —       |

Creates a project directory containing:
- `viper.project` — Project manifest (name, version, language, entry point, `profile balanced`, `optimize O1`)
- `main.zia` or `main.bas` — Entry-point source file with a hello-world template

```bash
viper init my-app
viper run my-app
```

### viper run / build

Build and run a source file or project, or build an IL/native artifact.

```bash
viper run program.zia
viper run program.zia --no-strict-diagnostics
viper run program.zia --no-bounds-checks
viper build program.zia -o program.il
viper build program.zia -o program
```

| Flag | Description |
|------|-------------|
| `--strict-diagnostics` | For Zia, promote safety-critical warnings to errors before execution (default) |
| `--no-strict-diagnostics` | Keep safety-critical Zia diagnostics as warnings |
| `--quiet-warnings`, `--no-warnings` | Do not print warnings when compilation succeeds |
| `--diagnostic-format text|json` | Select text or machine-readable JSON diagnostics |
| `--no-runtime-namespaces` | Disable automatic runtime namespace imports |
| `--bounds-checks` | Enable generated bounds checks where supported (default for source lowering) |
| `--no-bounds-checks` | Disable generated bounds checks for source lowering |
| `--verify-each` | Verify IL after every optimizer pass for debugging pass failures |
| `--paranoid-verify` | Run every frontend verifier checkpoint, including intermediate optimized-build checks |
| `--time-compile` | Print project resolution, source read, frontend phase, final verifier, asset, backend pass, and native-codegen/link timings |
| `--pass-stats` | Print detailed IL optimizer pass statistics; kept separate from `--time-compile` because it scans the module around each pass |
| `--fast-link` | Skip non-essential native-link size reductions and coalesce generated arm64 function sections; enabled automatically for debug/O0 native builds |
| `--build-profile debug|balanced|release` | Override the manifest build profile (`debug`=`O0`, `balanced`=`O1`, `release`=`O2`) |
| `-O0`, `-O1`, `-O2` | Override the final optimization level; this takes precedence over the build profile |

Both Zia and BASIC source paths print successful warnings by default. Zia O0/debug builds verify after lowering; optimized Zia builds normally verify the final optimized IL and skip the intermediate lower-stage verifier to keep large builds fast. `--paranoid-verify` restores every frontend verifier checkpoint, and `--verify-each` verifies after every optimizer pass when debugging an optimizer regression. If any verifier run fails, the command stops with diagnostics instead of running or building the result.

`viper build` defaults to the `balanced` profile and `O1` optimization when no explicit directive is present. `viper run` defaults to `debug`/`O0` for convention projects and manifests that do not set `profile`, `build-profile`, or `optimize`, keeping edit-run cycles fast. An explicit manifest profile, explicit manifest optimization level, `--build-profile`, or `-O*` flag is respected by both commands.

Native builds hand the already-verified, already-optimized frontend IL module directly to the backend and pass the selected optimization level to MIR/codegen passes such as pre-regalloc cleanup, block layout, scheduling, and peephole optimization. Safe per-function IL optimizer passes, x86-64 peephole work, and arm64 binary function emission may run in parallel unless a diagnostic dump or `--verify-each` requires strict serial instrumentation.

### viper -run

Execute IL modules with debugging controls.

```bash
viper -run <file.il> [flags]
```

| Flag                         | Description                                  |
|------------------------------|----------------------------------------------|
| `--trace=il`                 | Emit line-per-instruction trace              |
| `--trace=src`                | Show source file, line, column for each step |
| `--stdin-from <file>`        | Feed program stdin from file                 |
| `--max-steps <N>`            | Limit execution to N VM steps                |
| `--bounds-checks`            | Enable runtime bounds checking               |
| `--break <Label\|file:line>` | Halt before executing instruction            |
| `--break-src <file:line>`    | Explicit source-line breakpoint              |
| `--debug-cmds <file>`        | Read debugger actions from file              |
| `--step`                     | Enter debug mode, break at entry             |
| `--continue`                 | Ignore breakpoints and run to completion     |
| `--watch <name>`             | Print when scalar changes                    |
| `--count`                    | Print executed instruction count at exit     |
| `--time`                     | Print wall-clock execution time              |
| `--bytecode`                 | Run through the bytecode VM with checked bytecode compilation |
| `--diagnostic-format text|json` | Select text or JSON diagnostics for load/compile failures |

Debugger command files accept `continue`, `step`, `step N`, `step-over`, and `step-out`. Step-over and step-out are
frame-depth based and are intended for VM debugging workflows where source-level line stepping is not required.

### viper front

Low-level frontend entry points retained for direct compiler testing and
compatibility. Prefer `zia`, `vbasic`, or `viper run` / `viper build` for normal
workflows.

```bash
# Zia
viper front zia -emit-il <file.zia> [--bounds-checks|--no-bounds-checks] [--strict-diagnostics|--no-strict-diagnostics] [--diagnostic-format text|json]
viper front zia -run <file.zia> [--strict-diagnostics|--no-strict-diagnostics] [--trace=il|src] [--stdin-from <file>] [--max-steps N]

# BASIC
viper front basic -emit-il <file.bas> [--bounds-checks|--no-bounds-checks] [--diagnostic-format text|json]
viper front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] [--max-steps N] [--quiet-warnings]
```

### viper il-opt

Run optimization passes on IL modules.

```bash
viper il-opt <in.il> -o <out.il> [flags]
```

| Flag              | Description                        |
|-------------------|------------------------------------|
| `--passes a,b,c`  | Override the pass list             |
| `--no-mem2reg`    | Drop mem2reg from the selected pipeline when present |
| `--mem2reg-stats` | Print counts of promoted variables |

Default pipeline: O1 (`simplify-cfg, mem2reg, simplify-cfg, sccp, constfold, peephole, dce, simplify-cfg, sccp, inline, peephole, dce, simplify-cfg`)

### viper codegen

Compile IL to native code.

```bash
# x86-64
viper codegen x64 <in.il> -o <executable>
viper codegen x64 <in.il> -S <out.s>  # Assembly only
viper codegen x64 <in.il> -o <executable> --asset-blob assets.vpa --extra-obj assets.o
viper codegen x64 <in.il> --native-asm -o <out.o>
viper codegen x64 <in.il> --native-asm --debug-lines -o <out.o>
viper codegen x64 <in.il> --native-asm --target-linux -o <out.o>
viper codegen x64 <in.il> --native-asm --target-windows -o <out.obj>

# ARM64 (Apple Silicon validated)
viper codegen arm64 <in.il> -S <out.s>
viper codegen arm64 <in.il> -o <executable> --asset-blob assets.vpa --extra-obj assets.o
viper codegen arm64 <in.il> --native-asm -o <out.o>
viper codegen arm64 <in.il> --native-asm --debug-lines -o <out.o>
viper codegen arm64 <in.il> --native-asm --target-linux -o <out.o>
viper codegen arm64 <in.il> --native-asm --target-windows -o <out.obj>
```

On x86-64, `--asset-blob` embeds the VPA payload directly when using `--native-asm`. If you force `--system-asm`, pair it with `--extra-obj <asset.o>` so the asset symbols are still linked into the final binary.

On x86-64, `--target-darwin`, `--target-linux`, and `--target-windows` select the assembly dialect, native object
format, and native-link platform together. `--target-win64` still switches the calling convention to Win64 and also
selects the Windows platform policy. When you use `--native-asm` with `-o <file.o>` or `-o <file.obj>`, the compiler
writes a relocatable object instead of linking an executable.

On arm64, target selection is explicit: `--target-darwin`, `--target-linux`, and `--target-windows` select the assembly dialect, native object format, and native-link platform together. When you use `--native-asm` with `-o <file.o>` or `-o <file.obj>`, the compiler writes a relocatable object instead of linking an executable.

File-based `viper codegen` loads and verifies the input IL once before backend lowering. Project builds through `viper build` skip the textual IL round trip and transfer the verified in-memory module to the backend. Native assembler debug line emission is disabled by default for faster object generation and smaller native-link executables; pass `--debug-lines` when you need DWARF `.debug_line` content in native objects and linked outputs. `--fast-link` skips string deduplication and identical-code folding in the native linker; on arm64 it also emits one generated text section instead of per-function sections for faster debug links.

### viper package

Build a native payload and package a project for distribution.

```bash
viper package .
viper package . --target linux
viper package . --target windows --executable build/myapp.exe
viper package . --target tarball -o myapp.tar.gz
viper package . --dry-run --verbose
```

| Option | Description |
|--------|-------------|
| `--target macos|linux|windows|tarball` | Select output format; default is the host platform |
| `--arch x64|arm64` | Select payload architecture |
| `--executable <path>` | Package a prebuilt native executable; required for non-host installer targets |
| `-o <path>` | Output artifact path |
| `--macos-sign-mode none\|preserve\|adhoc\|developer-id` | Override macOS signing mode |
| `--macos-sign-identity <identity>` | Developer ID Application identity for macOS signing |
| `--macos-entitlements <path>` | Entitlements plist used during macOS signing |
| `--macos-hardened-runtime` | Enable hardened runtime for macOS signing |
| `--macos-notary-profile <profile>` | `notarytool` keychain profile used to notarize a Developer ID signed app |
| `--macos-staple` | Staple the notarization ticket before final ZIP output |
| `--windows-install-scope machine\|user` | Override Windows install scope; `machine` uses Program Files/HKLM, `user` uses LocalAppData/HKCU |
| `--windows-sign` | Authenticode-sign the generated Windows installer with `signtool` |
| `--windows-sign-pfx <path>` | PFX certificate for Windows signing; the password comes from `VIPER_WINDOWS_SIGN_PASSWORD` |
| `--windows-timestamp-url <url>` | RFC3161 timestamp URL for Windows signing |
| `--windows-signtool <path>` | `signtool.exe` path override |
| `--windows-sign-no-verify` | Skip `signtool verify` after signing |
| `--dry-run` | Validate metadata and print resolved package contents without building |
| `--verbose` | Print binary, output, asset, and verification details |

Packaging manifest paths are project-relative. Scalar package fields such as `package-name`, `package-author`, `package-homepage`, `package-license`, platform minimum versions, and `package-category` must be one manifest token; quote values that contain spaces. `package-icon`, `asset`, `post-install`, and `pre-uninstall` paths may also be quoted when they contain spaces. Sources are resolved inside the canonical project root, reject absolute paths and `..` traversal, and skip symlinks that resolve outside the project. Missing icons/assets, non-file icons, and assets that are neither files nor directories are fatal. Archive entry paths are normalized to forward slashes, must remain relative, and must be unique after normalization.

Package names, executable names, Windows shortcut names, macOS bundle identifiers, macOS signing modes, Windows ProgID bases, dotted file-association extensions, MIME types, Debian dependency expressions, freedesktop desktop categories, Debian Policy-style versions, platform dotted versions, URLs with non-empty hosts, and single-line metadata fields are validated before writing artifacts. Desktop categories are normalized to semicolon-terminated freedesktop category lists. Duplicate scalar package directives and duplicate file-association extensions are rejected. Invalid metadata fails the package command instead of producing malformed `.desktop`, plist, control, spec, shortcut, tar, ZIP, or installer data. Portable tarballs do not validate Debian-only fields such as `.deb` dependencies or desktop categories because they are not emitted into the tarball.

Prebuilt and compiled payload executables are inspected before packaging. macOS targets require Mach-O, Linux targets require ELF, Windows targets require PE32+, and the detected payload architecture must match `--arch` unless a macOS universal binary contains both supported slices. Portable tarballs accept Mach-O, ELF, or PE payloads, but still reject unknown executable formats and architecture mismatches. When a project omits `version`, package metadata and the default output filename both use `0.0.0`.

Linux `.deb` packages embed validated `post-install` and `pre-uninstall` script file contents as maintainer scripts. Linux desktop shortcuts install a normal application entry and, when `shortcut-desktop on` is set, copy it to existing user Desktop directories during `postinst` and remove it during `prerm`. File associations always add MIME XML and a desktop handler with `%f`; if menu and desktop shortcuts are disabled, that handler is installed with `NoDisplay=true` so file opens still work without adding a menu item. MIME and desktop caches are refreshed after package removal in `postrm`.

Windows installers default to `windows-install-scope machine`, which installs under `%ProgramFiles%`, writes uninstall metadata under HKLM, registers ProgIDs under `HKLM\Software\Classes`, and requests elevation. `windows-install-scope user` installs under `%LocalAppData%`, writes uninstall metadata and file associations under HKCU, creates current-user shortcuts, and embeds an `asInvoker` manifest. File associations advertise ProgIDs through `OpenWithProgids` without overwriting the existing default handler for an extension. Existing extension `Content Type` values are preserved; new MIME `Content Type` values are tagged with a VAPS owner marker and only that owned value is removed during uninstall. `OpenWithProgids` entries are written as zero-length `REG_NONE` values, and uninstall cleanup removes VAPS-owned OpenWith, ProgID, `DefaultIcon`, MIME `Content Type`, uninstall metadata, shortcuts, and installed files. ProgID cleanup checks the stored VAPS owner marker value before deleting the tree. Desktop and Start Menu shortcuts are created from package overlay `.lnk` files that use `%ProgramFiles%` or `%LocalAppData%` expansion instead of fixed absolute paths; when `package-icon` is set, a generated `.ico` is installed next to the app and used by shortcuts. Windows installers write Add/Remove Programs fields including `QuietUninstallString`, `DisplayIcon`, `NoModify`, `NoRepair`, `EstimatedSize`, `InstallDate`, and homepage URLs when available.

Windows installers accept `/quiet` or `/silent` to suppress message boxes and `/norestart` to skip reboot-time self-delete scheduling. Generated installer stubs reserve long-path buffers, bounds-check composed registry commands, stream overlay extraction in fixed-size chunks using 64-bit `SetFilePointerEx`, check file and directory removal failures, remove only the PATH token they added, and do not schedule the installation directory itself for deletion on reboot. Application packaging inspects PE imports and bundles adjacent non-system DLLs; if a non-system imported DLL is not beside the executable and is not a known Windows or VC runtime component, packaging fails before writing the installer. Each Windows installer overlay includes `meta/manifest.sha256` for the payload files.

Release signing can be driven directly by `viper package --target windows --windows-sign` or by `scripts/sign-windows-installer.ps1`. Both use `signtool`, SHA-256 file digesting, RFC3161 timestamping, `VIPER_WINDOWS_SIGN_PFX`, `VIPER_WINDOWS_SIGN_PASSWORD`, optional `VIPER_WINDOWS_TIMESTAMP_URL`, and optional `VIPER_WINDOWS_SIGNTOOL`. Signed `.exe` verification ignores the Authenticode certificate table when validating the embedded ZIP overlay, so `viper install-package --verify-only <installer.exe>` works for signed installers.

macOS app packages are staged as a real `.app` bundle before ZIP emission. On macOS the default signing mode is `adhoc`, which runs `codesign --force --sign -` over the bundle so `Info.plist` and bundled resources are sealed in `Contents/_CodeSignature/CodeResources`; on non-macOS hosts the default is `preserve` because local signing tools are unavailable. `adhoc` signing does not require an Apple Developer account and is suitable for local testing or internal handoff where users can explicitly approve an unidentified developer app. For public distribution to quarantined Macs, use `macos-sign-mode developer-id`, `macos-sign-identity "Developer ID Application: ..."` and `macos-notary-profile <profile>`; notarization requires Apple credentials configured in `notarytool` and is accepted only with Developer ID signing. `macos-staple on` requires `macos-sign-mode developer-id` and `macos-notary-profile <profile>`, then staples the ticket before the final ZIP. `preserve` leaves an already-signed payload untouched, and `none` emits an unsigned bundle.

`asset <source> <target>` targets are relative to the platform's app resource root: `Contents/Resources/<target>` on macOS, `/usr/share/<package>/<target>` for Linux `.deb`, `app/<target>` in the Windows installer overlay, and `<top-dir>/<target>` in portable tarballs. For example, `asset assets assets` packages `assets/fonts/font.bdf` as `Contents/Resources/assets/fonts/font.bdf` in a macOS app. Asset directory symlinks are followed when their resolved targets remain inside the project root, and their packaged paths preserve the symlink path rather than leaking the canonical target path.

Built artifacts are structurally and payload-verified by default: macOS ZIPs must contain the `.app` Info.plist and executable, `.deb` packages must contain the expected `usr/bin` payload, Windows installers verify the PE structure plus required ZIP overlay entries including `meta/manifest.sha256`, and tarballs verify gzip framing, USTAR headers, duplicate-free paths, and the expected executable. ZIP verification normalizes paths before duplicate checks and rejects central-directory/local-header disagreements. Failed verification removes the generated artifact. On macOS, signing failures are fatal before ZIP output, and the staged app bundle is checked with `codesign --verify --deep --strict`.

### viper install-package

Package a staged Viper toolchain install tree.

```bash
viper install-package --build-dir build --target tarball
viper install-package --build-dir build --target windows
viper install-package --build-dir build --target macos
viper install-package --build-dir build --stage-only
viper install-package --verify-only build/installers/viper-0.2.5-dev-macos-arm64.tar.gz
```

Typical workflow:

- run `cmake --install <build-dir> --prefix <stage-dir>` yourself, or let `viper install-package --build-dir <build-dir>` create a temporary staged install
- validate the staged install tree before packaging
- emit one or more native toolchain artifacts from the same staged manifest

| Option | Description |
|--------|-------------|
| `--target windows|macos|linux-deb|linux-rpm|tarball|all` | Select artifact format(s) |
| `--build-dir <dir>` | Stage from an existing build tree via `cmake --install` |
| `--stage-dir <dir>` | Package an already-staged install tree |
| `--stage-only` | Validate and print staged metadata without producing artifacts |
| `--verify-only <path>` | Structurally verify an existing installer artifact |
| `--arch x64|arm64` | Override manifest architecture for output naming and format metadata |
| `--macos-pkg-version <version>` | Dotted numeric version to pass to `pkgbuild` when the Viper version contains Debian/SemVer suffixes |
| `-o <path>` | Output path, or output directory when building multiple targets |
| `--keep-stage-dir` | Preserve the auto-generated staging directory |
| `--no-verify` | Skip post-build structural verification |
| `--verbose` | Print staged version, arch, and file counts |

Developer wrappers:

- `scripts/build_installer.sh`
- `scripts/build_installer.cmd`

Staged toolchain packaging accepts `x64` and `arm64` architecture names, and also accepts `universal` for detected macOS fat Mach-O toolchains. It requires a package version from `lib/cmake/Viper/ViperConfigVersion.cmake` or `include/viper/version.hpp`. Linux `.deb` output maps architectures to `amd64` and `arm64`; RPM output maps them to `x86_64` and `aarch64`. RPM generation requires `rpmbuild`; `--target all` includes RPM output only when that tool is available, while explicit `--target linux-rpm` fails with an actionable diagnostic if it is missing. When the staged `viper` binary has a recognizable Mach-O, ELF, or PE header, `install-package` derives both manifest platform and architecture from that binary and rejects a conflicting `--arch` override. `--target all` emits only the staged platform's native package type plus a portable tarball, and an explicitly incompatible target such as `--target windows` for an ELF stage fails before writing an artifact. `-o <path>` is treated as an output directory when it already exists as a directory or when multiple targets are selected; otherwise the parent directory of the requested output file is created before writing.

Staged toolchain packaging rejects symlinks whose resolved targets leave the staged prefix, including when `--stage-dir` itself is a symlink and CMake's install manifest records paths through that alias. Relative internal symlink targets are preserved as written in tar-based artifacts and in macOS package roots; absolute internal symlinks are converted to archive-relative targets. Linux `.deb`, `.rpm`, and Linux-platform tarball outputs preserve staged Unix permission bits, include hidden desktop/MIME metadata for the default Viper file associations, and refresh MIME, desktop, and manpage caches from post-removal maintainer hooks. Installed Linux packages open source associations through `/usr/bin/viper run %f` and IL associations through `/usr/bin/viper -run %f`; portable Linux tarballs use `viper run %f` and `viper -run %f` so the entries do not hard-code `/usr/bin`. Debian toolchain packages use a valid maintainer field and declare conservative runtime dependencies. RPM `%install` copies the full staged source tree, including top-level dotfiles, and `%files` entries are quoted and escaped for paths that contain spaces, quotes, backslashes, or percent signs. RPM output discovery accepts distribution release suffixes added by `%{?dist}`.

Windows toolchain installers dereference only symlinks to regular files and reject directory symlinks with a diagnostic because Windows installer payloads do not carry POSIX symlink metadata. The toolchain installer cleans the owned install root before extraction so upgrades do not leave removed files behind, adds the installed `bin` directory to the system `Path`, records the previous value in uninstall metadata only when it actually appends the entry, skips re-adding an existing Viper-owned PATH entry on reinstall, removes that PATH token during uninstall while preserving unrelated user edits, broadcasts an environment change, and registers default Viper file associations. `.zia` and `.bas` files open through `"bin\viper.exe" run "%1"`; `.il` files open through `"bin\viper.exe" -run "%1"`. The uninstaller schedules only the running `uninstall.exe` for reboot-time deletion when necessary and leaves any non-empty install root in place after removing owned files. Optional staged support libraries such as `vipergfx`, `vipergui`, and `viperaud` are required only when the staged CMake metadata references them. macOS toolchain packages pass a validated dotted numeric package version to `pkgbuild`, and `/usr/local/bin` command symlinks are part of the package payload so package receipts own them. Use `--macos-pkg-version` when the Viper version contains suffixes such as `+build` or `~rc1`; the generator rejects lossy automatic truncation.

`install-package --verify-only` infers formats only from supported extensions: `.exe`, `.pkg`, `.deb`, `.rpm`, `.tar.gz`, and `.tgz`. Unknown extensions fail unless a supported `--target` is provided. Post-build verification for generated Windows, Debian, RPM, macOS, and tarball toolchain artifacts checks every staged manifest path in the emitted payload, including generated Linux desktop/MIME metadata and macOS command symlinks. `.pkg` verification checks the XAR header, inflates and validates the TOC, requires `Payload`, `PackageInfo`, and `Bom` entries, and uses `pkgutil --payload-files` for generated manifest-backed packages. `.rpm` verification checks the RPM lead, signature header, main header bounds, that a non-empty payload follows the headers, and uses `rpm -qpl` for generated manifest-backed packages.

---

## Exit Codes

| Code | Meaning                                   |
|------|-------------------------------------------|
| `0`  | Program completed successfully            |
| `10` | Halted at breakpoint with no debug script |
| `>0` | Trap or error                             |

---

## CMake Integration

Projects embedding Viper tooling can consume the exported CMake package:

```cmake
find_package(Viper CONFIG REQUIRED)
target_link_libraries(mytool PRIVATE viper::il_core viper::il_io viper::il_vm)
```
