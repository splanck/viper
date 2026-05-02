---
status: active
audience: public
last-verified: 2026-05-01
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

File-based `viper codegen` loads and verifies the input IL once before backend lowering. Project builds through `viper build` skip the textual IL round trip and transfer the verified in-memory module to the backend. Native assembler debug line emission is disabled by default for faster object generation; pass `--debug-lines` when you need DWARF `.debug_line` content. `--fast-link` skips string deduplication and identical-code folding in the native linker; on arm64 it also emits one generated text section instead of per-function sections for faster debug links.

### viper install-package

Package a staged Viper toolchain install tree.

```bash
viper install-package --build-dir build --target tarball
viper install-package --build-dir build --target windows
viper install-package --build-dir build --target macos
viper install-package --build-dir build --stage-only
viper install-package --verify-only build/installers/viper-0.2.4-macos-arm64.tar.gz
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
| `-o <path>` | Output path, or output directory when building multiple targets |
| `--keep-stage-dir` | Preserve the auto-generated staging directory |
| `--no-verify` | Skip post-build structural verification |
| `--verbose` | Print staged version, arch, and file counts |

Developer wrappers:

- `scripts/build_installer.sh`
- `scripts/build_installer.cmd`

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
