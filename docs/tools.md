---
status: active
audience: public
last-verified: 2026-06-03
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
The LSP side includes diagnostics, completion, hover, document symbols, definition,
references, rename, signature help, workspace symbols, and full semantic tokens.

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

`viper --help` is intentionally concise and limited to common command shapes. Use `viper <subcommand> --help` or `viper help <subcommand>` for operational flags, and this reference for release-only packaging, signing, and manifest details that are too noisy for the default help screen.

### Overview

The CLI is organized around primary entry points:

- `viper init <name> [--lang zia|basic]` — Scaffold a new project
- `viper run <file|dir>` — Build and run a source file or project
- `viper build <file|dir> [-o out]` — Emit IL or build a native binary
- `viper check <file|dir>` — Type-check and verify without running or emitting
- `viper eval [code]` — Evaluate a one-line snippet and print the result
- `viper explain <code>` — Describe a diagnostic code from the central catalog
- `viper -run <file.il>` — Execute an IL module
- `viper front zia -emit-il <file.zia>` — Legacy low-level Zia frontend entry point
- `viper front zia -run <file.zia>` — Legacy low-level Zia frontend execution path
- `viper front basic -emit-il <file.bas>` — Legacy low-level BASIC frontend entry point
- `viper front basic -run <file.bas>` — Legacy low-level BASIC frontend execution path
- `viper il-opt <in.il> -o <out.il>` — Run optimization passes
- `viper codegen x64 <in.il> -o <out>` — Compile to x86-64 native code
- `viper codegen arm64 <in.il> -S <out.s>` — Generate ARM64 assembly
- `viper package <dir>` — Package a project for distribution (.app, .dmg, .deb, .rpm, .AppImage, .exe, .tar.gz)
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

### viper check

Type-check and verify a source file or project without executing or emitting
anything. This is the fast verification gate for editors, scripts, and AI
coding agents: it runs the same frontend + IL verifier pipeline as `viper run`
(at `O0` by default for speed) and stops.

```bash
viper check program.zia
viper check my-project/ --diagnostic-format=json
```

| Flag | Description |
|------|-------------|
| `--diagnostic-format text\|json` | Select text or machine-readable JSON diagnostics (stderr) |
| `--strict-diagnostics` / `--no-strict-diagnostics` | Control safety-warning promotion (strict by default) |
| `--quiet-warnings`, `--no-warnings` | Suppress warning output |
| `--build-profile`, `-O0/-O1/-O2`, `--bounds-checks`, `--no-bounds-checks` | Same meaning as `viper run` |

Exit codes are differentiated so callers can branch without parsing output:

| Code | Meaning |
|------|---------|
| `0` | No errors (warnings allowed) |
| `1` | Usage error, or the target could not be resolved |
| `2` | Compile or verification errors (diagnostics printed) |

JSON diagnostics include the stable `code`, pipeline `stage`, the underlined
`range`, related `notes`, and machine-applicable `fixits` (for example,
did-you-mean replacements for undefined identifiers).

### viper eval

Evaluate a single Zia or BASIC snippet through a fresh REPL session and print
the result. Reads the snippet from stdin when no code argument is given.

```bash
viper eval '2 + 3 * 4'                       # prints 14
viper eval --json --type 'Viper.Math.Sqrt(2.0)'
echo 'Say("hi")' | viper eval
viper eval --lang basic 'PRINT 2+2'
```

| Flag | Description |
|------|-------------|
| `--lang zia\|basic` | Snippet language (default `zia`) |
| `--json` | Emit one JSON object on stdout: `{success, trapped, resultType, output, error, type?, il?}` |
| `--type` | Include the inferred expression type (Zia only) |
| `--il` | Include the generated IL (Zia only) |

Exit codes: `0` success, `1` usage error, `2` compile/eval error, `3` runtime
trap. The snippet is evaluated as a single REPL input with expression
auto-printing, so `viper eval` behaves exactly like typing the snippet into
`viper repl`.

### viper explain

Describe a diagnostic code from the central catalog.

```bash
viper explain V-ZIA-UNDEFINED
viper explain B2001 --json
viper explain --list --json      # full catalog as a JSON array
viper --print-error-codes --json # same catalog via a driver flag
```

Cataloged codes print their subsystem and a one-sentence summary. Codes that
are not yet cataloged but match a known prefix family (e.g., `B21xx`,
`V-BC-*`) still resolve to their subsystem description; completely unknown
codes exit `1`.

### Machine-readable registry dumps

Two driver flags emit JSON inventories generated from the live in-process
registries, so they can never drift from the binary:

```bash
viper --dump-runtime-api   # {version, functions:[{name,signature}], classes:[{name,constructor,properties,methods}]}
viper --dump-opcodes       # {ilVersion, opcodes:[{mnemonic,resultArity,resultType,operandsMin,operandsMax,operandTypes,sideEffects,successors,terminator}]}
```

These complement the human-oriented `--dump-runtime-descriptors` and
`--dump-runtime-classes` text dumps.

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
viper package . --target appimage -o myapp.AppImage
viper package . --target rpm --linux-sign-key "Maintainer Key"
viper package . --dry-run --verbose
```

| Option | Description |
|--------|-------------|
| `--target macos|linux|windows|appimage|rpm|dmg|tarball` | Select output format; default is the host platform |
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
| `--windows-install-dir <name>` | Override the Windows install-root directory name |
| `--windows-sign` | Authenticode-sign the generated Windows installer with `signtool` |
| `--windows-sign-pfx <path>` | PFX certificate for Windows signing; the password comes from `VIPER_WINDOWS_SIGN_PASSWORD` |
| `--windows-sign-thumbprint <sha1>` | Sign with a certificate-store SHA-1 thumbprint; spaces are accepted and normalized |
| `--windows-timestamp-url <url>` | RFC3161 timestamp URL for Windows signing |
| `--windows-signtool <path>` | `signtool.exe` path override |
| `--windows-sign-no-verify` | Skip `signtool verify` after signing |
| `--linux-sign-key <id>` | GPG-sign the generated `.deb`/`.rpm` with `dpkg-sig`/`rpmsign` |
| `--dry-run` | Validate metadata and print resolved package contents without building |
| `--keep-failed-artifact` | Preserve generated artifacts after a failed package step for inspection |
| `--verbose` | Print binary, output, asset, and verification details |

`.app`/`.dmg` build on a macOS host (`.dmg` shells to `hdiutil`), `.deb`/`.AppImage`/`.tar.gz` are emitted directly on any host, and `.rpm` requires `rpmbuild`. `--linux-sign-key` applies only to `--target linux`/`rpm` and requires `dpkg-sig`/`rpmsign`; each path fails with a clear diagnostic when its tool is unavailable.

Packaging manifest paths are project-relative. The `--executable` CLI option is a normal command-line path: relative values resolve from the current working directory. Scalar package fields such as `package-name`, `package-author`, `package-homepage`, `package-license`, platform minimum versions, and `package-category` must be one manifest token; quote values that contain spaces. `package-icon`, `asset`, `post-install`, and `pre-uninstall` paths may also be quoted when they contain spaces. Sources are resolved inside the canonical project root and reject absolute paths, `..` traversal, unreadable directory entries, and symlinks that resolve outside the project. Missing icons/assets, non-file icons, and assets that are neither files nor directories are fatal. Archive entry paths are normalized to forward slashes, must remain relative, and must be unique after normalization.

Package names, executable names, Windows shortcut names, macOS bundle identifiers, macOS signing modes, Windows ProgID bases, dotted file-association extensions, MIME types, Debian dependency expressions, freedesktop desktop categories, Debian Policy-style versions, platform dotted versions, URLs with non-empty hosts, and single-line metadata fields are validated before writing artifacts. Desktop categories are normalized to semicolon-terminated freedesktop category lists. Duplicate scalar package directives and duplicate file-association extensions are rejected. Invalid metadata fails the package command instead of producing malformed `.desktop`, plist, control, spec, shortcut, tar, ZIP, or installer data. Portable tarballs do not validate Debian-only fields such as `.deb` dependencies or desktop categories because they are not emitted into the tarball.

Prebuilt and compiled payload executables are inspected before packaging. macOS targets require Mach-O, Linux targets require ELF, Windows targets require PE32+, and the detected payload architecture must match `--arch` unless a macOS universal binary contains the requested supported slice; both fat32 and fat64 Mach-O universal headers are accepted. Portable tarballs accept Mach-O, ELF, or PE payloads, but still reject unknown executable formats and architecture mismatches. When a project omits `version`, package metadata and the default output filename both use `0.0.0`. Default output filenames sanitize project, version, and architecture components so manifest metadata cannot create paths outside the working directory.

Linux `.deb` packages embed validated `post-install` and `pre-uninstall` script file contents as maintainer scripts. Linux desktop shortcuts install a normal application entry and, when `shortcut-desktop on` is set, copy it to existing user Desktop directories during `postinst` and remove it during `prerm`. File associations always add MIME XML and a desktop handler with `%f`; if menu and desktop shortcuts are disabled, that handler is installed with `NoDisplay=true` so file opens still work without adding a menu item. MIME and desktop caches are refreshed after package removal in `postrm`.

Windows installers default to `windows-install-scope machine`, which installs under `%ProgramFiles%`, writes uninstall metadata under HKLM, registers ProgIDs under `HKLM\Software\Classes`, and requests elevation. `windows-install-scope user` installs under `%LocalAppData%`, writes uninstall metadata and file associations under HKCU, creates current-user shortcuts, and embeds an `asInvoker` manifest. `windows-install-dir <name>` overrides the directory below `%ProgramFiles%` or `%LocalAppData%`; otherwise the package display name is used. App installers do not blanket-delete the install root before extraction; upgrades overwrite packaged files and uninstall cleanup removes the files recorded in the generated package layout, leaving unrelated user content in place. File associations advertise ProgIDs through `OpenWithProgids` without overwriting the existing default handler for an extension. A fourth `file-assoc` token is Windows-only open-command arguments; the generated command is `"<installed exe>" <windows-open-args> "%1"`. Existing extension `Content Type` values are preserved; new MIME `Content Type` values are tagged with a VAPS owner marker and only that owned value is removed during uninstall. `OpenWithProgids` entries are written as zero-length `REG_NONE` values, and uninstall cleanup removes VAPS-owned OpenWith, ProgID, `DefaultIcon`, MIME `Content Type`, uninstall metadata, shortcuts, and installed files. ProgID cleanup checks the stored VAPS owner marker value before deleting the tree with `RegDeleteTreeW`. Desktop and Start Menu shortcuts are created from package overlay `.lnk` files that use `%ProgramFiles%` or `%LocalAppData%` expansion instead of fixed absolute paths; when `package-icon` is set, a generated `.ico` is installed next to the app and used by shortcuts. Windows installers write Add/Remove Programs fields including `QuietUninstallString`, `DisplayIcon`, `NoModify`, `NoRepair`, `EstimatedSize`, runtime `InstallDate`, and homepage URLs when available, and generated setup/uninstall PEs include VERSIONINFO resources.

Windows installers accept `/quiet` or `/silent` to suppress message boxes and `/norestart` to skip reboot-time self-delete scheduling when explicitly supplied as standalone command-line tokens. Generated Add/Remove Programs `QuietUninstallString` values use `/quiet` without `/norestart`, so standard silent uninstalls still schedule the running uninstaller for cleanup on the next reboot. Generated installer stubs reserve long-path buffers, resolve roots with `SHGetKnownFolderPath`, bounds-check composed registry commands, validate and terminate registry string reads, stream stored bootstrap overlay entries in fixed-size chunks using 64-bit `SetFilePointerEx`, expand the main DEFLATE-compressed inner payload ZIP with the Windows PowerShell archive provider from `System32`, check file and directory removal failures, remove only the PATH token they added, and do not schedule the installation directory itself for deletion on reboot. Interactive uninstallers ask for confirmation before deleting files. Application packaging inspects PE imports recursively and bundles adjacent non-system DLLs while ignoring known Windows DLL names, API-set DLLs, and exact/versioned VC runtime DLL names; similarly prefixed custom DLLs such as `msvcp_plugin.dll` are treated as application dependencies. The built-in system DLL policy includes common game/runtime imports such as `xinput1_4.dll`, `xinput9_1_0.dll`, `iphlpapi.dll`, and `d3dcompiler_47.dll`. If a non-system imported DLL is not beside the executable, packaging fails before writing the installer. Each Windows installer overlay includes `meta/manifest.sha256` for the stored bootstrap files, and verification rejects duplicate, missing, mismatched, or uncovered manifest entries.

Windows `--arch arm64` validates that the payload executable is ARM64 PE32+, while the generated installer bootstrap remains an x64 PE so it can run through Windows-on-ARM emulation and install the native ARM64 app payload.

Release signing can be driven directly by `viper package --target windows --windows-sign`, `viper install-package --target windows --windows-sign`, by `scripts/sign-windows-installer.ps1`, or by the `Windows Release Installer` GitHub Actions workflow. PFX signing uses `VIPER_WINDOWS_SIGN_PFX` and `VIPER_WINDOWS_SIGN_PASSWORD`; certificate-store signing uses `--windows-sign-thumbprint`, `windows-sign-thumbprint`, or `VIPER_WINDOWS_SIGN_THUMBPRINT`. The release workflow expects `VIPER_WINDOWS_SIGN_PFX_BASE64` and `VIPER_WINDOWS_SIGN_PASSWORD` secrets and an optional `VIPER_WINDOWS_TIMESTAMP_URL` repository variable. Both paths use `signtool`, SHA-256 file digesting, RFC3161 timestamping, a validated `VIPER_WINDOWS_TIMESTAMP_URL` or HTTPS default, and optional `VIPER_WINDOWS_SIGNTOOL`. Signed `.exe` verification ignores the Authenticode certificate table when validating the embedded ZIP overlay, so `viper install-package --verify-only <installer.exe>` works for signed installers. Public demo packages such as XenoScape should be produced with a release certificate supplied on the command line or through signing environment variables rather than committing private certificate material to `viper.project`.

The XenoScape demo manifest is configured as a user-scope Windows game package: it installs under `%LocalAppData%\Xenoscape`, creates Start Menu and desktop shortcuts, declares a Windows 10 minimum, and includes both the `sounds/` WAV payload and `xenoscape.runtime.json`. Its runtime first probes the installed working directory for those assets, then the executable directory, then source-tree locations used by local development runs. The Windows installer smoke test installs the package, launches `xenoscape.exe --viper-package-smoke` from a non-install working directory to verify executable-directory asset lookup, verifies shortcuts and HKCU uninstall metadata, and runs the generated uninstaller.

macOS app packages are staged as a real `.app` bundle before ZIP emission. On macOS the default signing mode is `adhoc`, which runs `codesign --force --sign -` over the bundle so `Info.plist` and bundled resources are sealed in `Contents/_CodeSignature/CodeResources`; on non-macOS hosts the default is `preserve` because local signing tools are unavailable. `adhoc` signing does not require an Apple Developer account and is suitable for local testing or internal handoff where users can explicitly approve an unidentified developer app. For public distribution to quarantined Macs, use `macos-sign-mode developer-id`, `macos-sign-identity "Developer ID Application: ..."` and `macos-notary-profile <profile>`; notarization requires Apple credentials configured in `notarytool` and is accepted only with Developer ID signing. `macos-staple on` requires `macos-sign-mode developer-id` and `macos-notary-profile <profile>`, then staples the ticket before the final ZIP. `preserve` leaves an already-signed payload untouched, and `none` emits an unsigned bundle.

`asset <source> <target>` targets are relative to the platform's app resource root: `Contents/Resources/<target>` on macOS, `/usr/share/<package>/<target>` for Linux `.deb`, `<target>` inside the Windows install-root payload, and `<top-dir>/<target>` in portable tarballs. For example, `asset assets assets` packages `assets/fonts/font.bdf` as `Contents/Resources/assets/fonts/font.bdf` in a macOS app. Asset directory symlinks are followed when their resolved targets remain inside the project root, packaged paths preserve the symlink path rather than leaking the canonical target path, and packagers read from the validated resolved path. Linux `.deb` and portable tarball outputs preserve executable bits on asset files. Portable tarball top directories use a filesystem-safe version component, so Debian epochs such as `2:1.0` become `2_1.0` in the directory name while the package version remains unchanged.

Built artifacts are structurally and payload-verified by default: macOS ZIPs must contain the `.app` Info.plist and executable, `.deb` packages must contain the expected `usr/bin` payload, Windows installers verify the PE structure plus required ZIP overlay entries including `meta/manifest.sha256`, and tarballs verify gzip framing, USTAR headers, duplicate-free paths, and the expected executable. ZIP verification normalizes paths before duplicate checks and rejects central-directory/local-header disagreements. Failed verification removes the generated artifact. On macOS, signing failures are fatal before ZIP output, and the staged app bundle is checked with `codesign --verify --deep --strict`.

### viper install-package

Package a staged Viper toolchain install tree.

```bash
viper install-package --build-dir build --target tarball
viper install-package --build-dir build --target linux-deb
viper install-package --build-dir build --target linux-rpm
viper install-package --build-dir build --target windows
viper install-package --build-dir build --target macos
viper install-package --build-dir build --stage-only
viper install-package --verify-only build/installers/viper-0.2.6-dev-macos-arm64.tar.gz
```

Typical workflow:

- run `cmake --install <build-dir> --prefix <stage-dir>` yourself, or let `viper install-package --build-dir <build-dir>` create a temporary staged install
- validate the staged install tree before packaging
- emit one or more native toolchain artifacts from the same staged manifest

| Option | Description |
|--------|-------------|
| `--target windows|macos|linux-deb|linux-rpm|appimage|tarball|all` | Select artifact format(s) |
| `--build-dir <dir>` | Stage from an existing build tree via `cmake --install` |
| `--stage-dir <dir>` | Package an already-staged install tree |
| `--stage-only` | Validate and print staged metadata without producing artifacts |
| `--verify-only <path>` | Structurally verify an existing installer artifact |
| `--arch x64|arm64` | Override manifest architecture for output naming and format metadata |
| `--macos-pkg-version <version>` | Dotted numeric package version override when the Viper version contains Debian/SemVer suffixes |
| `--macos-sign-identity <identity>` | Developer ID Installer identity for signing generated macOS `.pkg` artifacts |
| `--macos-notary-profile <profile>` | `notarytool` keychain profile used to notarize signed macOS `.pkg` artifacts |
| `--macos-staple` | Staple the notarization ticket after successful macOS package submission |
| `--macos-notary-timeout <seconds>` | Bound the `notarytool --wait` timeout for macOS package submission |
| `--macos-dmg` | Also wrap a generated macOS `.pkg` in a styled `.dmg` |
| `--macos-dmg-background <path>` | PNG background image for the generated toolchain `.dmg` window |
| `--macos-dmg-icon <path>` | `.icns` volume icon for the generated toolchain `.dmg` |
| `--macos-pkg-license <path>` | License text shown by the macOS `.pkg` installer |
| `--macos-pkg-background <path>` | Background image for the macOS `.pkg` installer pane |
| `--license <spdx>` | Toolchain package license metadata override |
| `--maintainer <name>` | Toolchain maintainer/packager metadata override |
| `--maintainer-email <email>` | Debian maintainer email metadata override |
| `--homepage <url>` | Toolchain package homepage metadata override |
| `--windows-sign` | Authenticode-sign generated Windows toolchain installers |
| `--windows-sign-pfx <path>` | PFX certificate for Windows signing; the password comes from `VIPER_WINDOWS_SIGN_PASSWORD` |
| `--windows-sign-thumbprint <sha1>` | Sign with a certificate-store SHA-1 thumbprint |
| `--windows-timestamp-url <url>` | RFC3161 timestamp URL for Windows signing |
| `--windows-signtool <path>` | `signtool.exe` path override |
| `--windows-sign-no-verify` | Skip `signtool verify` after signing |
| `--linux-sign-key <id>` | GPG-sign generated `.deb`/`.rpm` toolchain packages with `dpkg-sig`/`rpmsign` |
| `--windows-install-scope user|machine` | Select the toolchain installer scope (default `user`) |
| `--windows-install-dir <name>` | Override the Windows toolchain install-root directory name |
| `--windows-no-path` | Do not add the installed `bin/` directory to `PATH` |
| `--windows-file-associations on|off` | Register `.zia`/`.bas`/`.il` file associations (default `off`) |
| `--windows-shortcuts on|off` | Create Start Menu developer shortcuts (default `on`) |
| `--allow-debug-toolchain` | Permit Windows packages that reference MSVC debug CRTs |
| `--skip-build` | With `--build-dir`, run `cmake --install` without rebuilding first |
| `-o <path>` | Output path, or output directory when building multiple targets |
| `--keep-stage-dir` | Preserve the auto-generated staging directory |
| `--no-verify` | Skip post-build structural verification |
| `--verbose` | Print staged version, arch, and file counts |

Developer wrappers:

- `scripts/build_installer.sh`
- `scripts/build_installer.cmd`

Staged toolchain packaging accepts `x64` and `arm64` architecture names, and also accepts `universal` for detected macOS fat32 and fat64 Mach-O toolchains. It requires a package version from `lib/cmake/Viper/ViperConfigVersion.cmake` or `include/viper/version.hpp`; CMake package path validation is case-insensitive for staged filesystems that vary directory casing. Linux `.deb` output maps architectures to `amd64` and `arm64`; RPM output maps them to `x86_64` and `aarch64`. RPM generation requires `rpmbuild`; Linux `--target all` includes `.deb`, `.rpm`, `.AppImage`, and portable tarball output and fails with an actionable diagnostic if `rpmbuild` is missing, so release builders do not silently skip an expected artifact. For Windows and macOS stages, `--target all` emits the staged platform's native package type plus a portable tarball. When the staged `viper` binary has a recognizable Mach-O, ELF, or PE header, `install-package` derives both manifest platform and architecture from that binary and rejects a conflicting `--arch` override. An explicitly incompatible target such as `--target windows` for an ELF stage fails before writing an artifact. Portable toolchain tarball top directories use the same filesystem-safe version component as app tarballs. Linux-platform tarballs include `install.sh`, `uninstall.sh`, `README.install`, and `share/viper/install_manifest.txt`; by default the scripts install under `/usr/local`, honor `PREFIX` and `DESTDIR`, preserve staged modes/symlinks through `tar`, remove stale files from the prior tarball manifest before copying the new payload, refresh man/MIME/desktop caches when installing directly on a host, and uninstall only the files listed in the manifest. `-o <path>` is treated as an output directory when it already exists as a directory or when multiple targets are selected; otherwise the parent directory of the requested output file is created before writing.

Staged toolchain packaging rejects symlinks whose resolved targets leave the staged prefix, including when `--stage-dir` itself is a symlink and CMake's install manifest records paths through that alias. Relative internal symlink targets are preserved as written in tar-based artifacts and in macOS package roots; absolute internal symlinks are converted to archive-relative targets. Linux `.deb`, `.rpm`, and Linux-platform tarball outputs preserve staged Unix permission bits, map root-level docs such as `LICENSE` to `/usr/share/doc/viper/`, include hidden desktop/MIME metadata for the default Viper file associations, and refresh MIME, desktop, and manpage caches from post-removal maintainer hooks. Installed Linux packages open source associations through `/usr/bin/viper run %f` and IL associations through `/usr/bin/viper -run %f`; portable Linux tarballs use `viper run %f` and `viper -run %f` so the entries do not hard-code `/usr/bin`. Debian toolchain packages use a valid maintainer field and declare hard dependencies for libc/C++/libgcc, CMake, `make`, and a C++ compiler, plus X11 or ALSA when the staged support libraries require them. RPM specs declare matching hard `Requires`, including `cmake`, `gcc-c++`, and `make`, so package-manager installs set up the developer workflow instead of leaving compiler/CMake setup as a weak recommendation. RPM `%install` copies the full staged source tree, including top-level dotfiles, and `%files` entries are quoted and escaped for paths that contain spaces, quotes, backslashes, or percent signs. RPM output discovery accepts distribution release suffixes added by `%{?dist}`.

Windows toolchain installers dereference only symlinks to regular files and reject directory symlinks with a diagnostic because Windows installer payloads do not carry POSIX symlink metadata. The toolchain installer installs a compressed payload archive, expands it into the selected install root, writes `.viper-install-manifest.txt`, compares that manifest against the previous install manifest, and removes only Viper-owned stale files after the new payload expands successfully. It adds the installed `bin` directory to the selected user or machine `Path`, records the previous value in uninstall metadata only when it actually appends the entry, skips re-adding an existing Viper-owned PATH entry on reinstall, removes that PATH token during uninstall while preserving unrelated user edits, broadcasts an environment change, and registers default Viper file associations only when requested. `.zia` and `.bas` files open through `"bin\viper.exe" run "%1"`; `.il` files open through `"bin\viper.exe" -run "%1"`. The uninstaller schedules only the running `uninstall.exe` for reboot-time deletion when necessary and leaves any non-empty install root in place after removing owned files. Generated toolchain installers include `share/viper/README.windows-prerequisites.txt`; Release builds that depend on the MSVC dynamic runtime require the Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 unless the staged `bin` directory includes the required runtime DLLs or the toolchain was built with the static MSVC runtime. Optional staged support libraries such as `vipergfx`, `vipergui`, and `viperaud` are required only when the staged CMake metadata references them.

The Windows Release installer lane configures a Release build with `VIPER_BUILD_VSCODE_VSIX=ON` and `VIPER_ENABLE_WINDOWS_INSTALLER_E2E=ON`, builds the packaged Zia `.vsix`, runs packaging unit tests, installs the generated per-user toolchain installer, verifies `viper --version`, verifies `viper run`, compiles and runs a native codegen smoke outside the build tree, projects the registry PATH into a fresh process, builds an external `find_package(Viper)` consumer, and uninstalls. For manual clean-VM release validation, copy the generated installer to the VM and run `scripts/validate-windows-toolchain-installer.ps1 -Installer <installer.exe>`; it performs the same installed-toolchain checks without requiring a source checkout build tree.

macOS toolchain packages are generated without `pkgbuild` or `productbuild`: Viper writes the component `Payload` as gzip-compressed portable ASCII CPIO, assembles component/product XAR archives natively, and uses `mkbom` only for the component bill of materials on macOS hosts. The package installs the staged tree under `/usr/local/viper`, includes receipt-owned `/usr/local/bin` command symlinks and `/usr/local/share/man` manpage symlinks, and installs `/usr/local/lib/cmake/Viper` wrapper configs so `find_package(Viper CONFIG REQUIRED)` works from CMake's default `/usr/local` prefix. It also installs `/Applications/Viper Toolchain.app`, a LaunchServices document handler for `.zia`, `.bas`, and `.il` files that opens the installed CLI in Terminal, and registers that handler from `postinstall`. Preinstall/postinstall scripts remove stale files from the prior Viper manifest, refresh command/manpage symlinks, register the file-handler app, and update manpage caches when available. `/usr/local/viper/share/viper/uninstall.sh` unregisters the handler app, removes receipt-created symlinks and CMake wrappers, removes files listed in the Viper install manifest, and forgets the package receipt for local cleanup. Use `--macos-pkg-version` when the Viper version contains suffixes such as `+build` or `~rc1`; the generator rejects lossy automatic truncation. `--macos-sign-identity` signs the finished product package with `productsign`; `--macos-notary-profile` submits it with `notarytool --wait`, and `--macos-staple` staples the resulting ticket. `VIPER_MACOS_SIGN_IDENTITY` and `VIPER_MACOS_NOTARY_PROFILE` can provide the signing identity and notary profile when the corresponding workflow is requested.

The macOS GUI's Destination Select step chooses the destination volume; the install prefix is fixed at `/usr/local/viper` so the package can set up `/usr/local/bin` commands and `/usr/local/lib/cmake/Viper` discovery consistently. The macOS installer smoke test is intentionally opt-in because it installs into `/usr/local`. Linux `.deb`/`.rpm` installer smoke tests are also opt-in for the privileged install/remove step; their non-privileged artifact and payload checks run when the host package tools are available. Run the platform smoke tests as root with `VIPER_RUN_MACOS_INSTALLER_SMOKE=1` or `VIPER_RUN_LINUX_INSTALLER_SMOKE=1` when validating a developer machine image or release candidate.

`install-package --verify-only` infers formats only from supported extensions: `.exe`, `.pkg`, `.deb`, `.rpm`, `.AppImage`, `.tar.gz`, and `.tgz`. Unknown extensions fail unless a supported `--target` is provided. Post-build verification for generated Windows, Debian, RPM, AppImage, macOS, and tarball toolchain artifacts checks every staged manifest path in the emitted payload where the format exposes a payload listing, including generated Linux desktop/MIME metadata and macOS command, manpage, manifest, file-handler app, uninstall helper, and CMake-wrapper paths. `.pkg` verification is native: it validates product and component XAR headers/TOCs/checksums, inflates gzip payloads, validates CPIO structure, rejects AppleDouble sidecars, requires `Payload`, `PackageInfo`, `Bom`, `Scripts`, `Distribution`, and script entries, then checks the payload paths directly. `.rpm` verification checks the RPM lead, signature header, main header bounds, that a non-empty payload follows the headers, reads RPM file-list tags from the main header, and compares those paths natively without shelling out to `rpm -qpl`.

---

## Exit Codes

| Code | Meaning                                   |
|------|-------------------------------------------|
| `0`  | Program completed successfully            |
| `10` | Halted at breakpoint with no debug script |
| `>0` | Trap or error                             |

`viper check` and `viper eval` define differentiated exit codes for
programmatic callers: see their sections above (`0` ok, `1` usage, `2`
compile error, and `3` runtime trap for `eval`).

---

## CMake Integration

Projects embedding Viper tooling can consume the exported CMake package:

```cmake
find_package(Viper CONFIG REQUIRED)
target_link_libraries(mytool PRIVATE viper::il_core viper::il_io viper::il_vm)
```
