# CODEMAP: Tools

- **src/tools/basic-ast-dump/main.cpp**

  Implements the standalone `basic-ast-dump` utility that reads a BASIC source file and prints its abstract syntax tree. The `main` routine validates that exactly one path argument is supplied, loads the file into memory, and registers it with the shared `SourceManager` so locations resolve correctly. It then builds a `Parser`, constructs the AST, and renders it to standard output via `AstPrinter`, making the tool useful for debugging front-end behaviour and producing golden data. Dependencies include `frontends/basic/AstPrinter.hpp`, `frontends/basic/Parser.hpp`, `support/source_manager.hpp`, and standard I/O headers `<fstream>`, `<sstream>`, and `<iostream>`.

- **src/tools/basic-lex-dump/main.cpp**

  Provides the `basic-lex-dump` command-line tool for inspecting how the lexer tokenizes BASIC input. After checking the argument count and loading the requested file, it registers the buffer with `SourceManager`, instantiates `Lexer`, and repeatedly calls `next` until an EOF token is produced. Each token is printed with its line and column plus the lexeme for identifiers, strings, and numbers, allowing developers to build golden token streams when evolving the lexer. Dependencies cover `frontends/basic/Lexer.hpp`, `frontends/basic/Token.hpp`, `support/source_manager.hpp`, and the standard `<fstream>`, `<sstream>`, and `<iostream>` facilities.

- **src/tools/il-dis/main.cpp**

  Acts as a tiny IL disassembler demo that constructs a module in memory and emits it as text. The program uses `il::build::IRBuilder` to declare the runtime `rt_print_str` extern, create a global string, and populate `main` with basic blocks and instructions that print and return zero. Once the synthetic module is built it serializes the result to standard output via the IL serializer, making the example handy for tutorials and smoke tests. Dependencies include `viper/il/IRBuilder.hpp`, `viper/il/IO.hpp`, and `<iostream>`.

- **src/tools/ilc/break_spec.cpp**

  Implements helpers for parsing the `--break` specifications accepted by the `ilc` driver. `isSrcBreakSpec` splits strings on the final colon, ensures the right-hand side contains only digits, and checks that the left-hand side resembles a path so breakpoints map cleanly to files. By rejecting malformed input early it prevents the debugger from enqueuing meaningless breakpoints that would confuse later resolution stages. Dependencies are limited to the local `break_spec.hpp` plus `<cctype>` and `<string>` from the standard library.
- **src/tools/ilc/main.cpp**

  Hosts the entry point for the `ilc` multipurpose driver. `usage` prints the supported subcommands and BASIC guidance, and `main` validates arguments before dispatching to `cmdRunIL`, `cmdILOpt`, or `cmdFrontBasic`. It also lists BASIC intrinsics so users know which builtin names are available when invoking the front-end mode. Dependencies include the local `cli.hpp`, `frontends/basic/Intrinsics.hpp`, and standard `<iostream>`/`<string>` facilities.

- **src/tools/ilc/cmd_run_il.cpp**

  Executes serialized IL modules through the VM while honoring debugging and tracing flags from the CLI. The option parser accepts breakpoints, scripted debug command files, stdin redirection, instruction counting, and timing toggles before loading the module via the expected-based API. After verifying the module it configures `TraceConfig` and `DebugCtrl`, constructs the VM, and prints optional summaries when counters are requested. Dependencies span `vm/Debug.hpp`, `vm/DebugScript.hpp`, `vm/Trace.hpp`, `vm/VM.hpp`, shared CLI helpers, IL API headers, and standard `<chrono>`, `<fstream>`, `<memory>`, `<string>`, `<algorithm>`, `<cstdint>`, and `<cstdio>` utilities.

- **src/tools/ilc/cmd_il_opt.cpp**

  Implements the `ilc il-opt` subcommand that runs transformation passes over IL files. It parses output destinations, comma-separated pass lists, and mem2reg toggles before loading the input through `il::api::v2::parse_text_expected`. Pass registrations wire default and user-selected pipelines into the `transform::PassManager`, and the optimized module is serialized in canonical form. Dependencies cover `il/transform` headers (`PassManager`, `Mem2Reg`, `ConstFold`, `Peephole`, `DCE`), the CLI facade, the IL API and serializer, plus `<algorithm>`, `<fstream>`, `<iostream>`, `<string>`, and `<vector>` utilities.

- **src/tools/ilc/cli.cpp**

  Provides helpers for parsing CLI flags shared across all `ilc` subcommands. `parseSharedOption` recognizes trace mode settings, stdin redirection, maximum step limits, and bounds-check toggles while updating a shared options struct. Its return value lets callers know whether a flag was handled or if they should treat it as an error. Dependencies include `cli.hpp`, which defines `SharedCliOptions` and ties into `il::vm::TraceConfig`, along with the standard string utilities included there.

- **src/tools/ilc/cmd_front_basic.cpp**

  Drives the BASIC front-end workflow for `ilc`, supporting both `-emit-il` compilation and `-run` execution. The helper `compileBasicToIL` loads the source, parses it, folds constants, runs semantic analysis with diagnostics, and lowers the program into IL while tracking source files. Command handling reuses `parseSharedOption`, emits IL text when requested, or verifies and runs the module via the VM. Dependencies include BASIC front-end headers (`Parser`, `ConstFolder`, `SemanticAnalyzer`, `Lowerer`, `DiagnosticEmitter`), the IL expected-based API, serializer, VM runtime headers, and standard `<fstream>`, `<sstream>`, `<iostream>`, and `<string>` facilities.

- **src/tools/il-verify/il-verify.cpp**

  Implements the standalone `il-verify` tool that parses and verifies IL modules from disk. The main routine handles `--version`, checks usage, opens the requested file, and routes diagnostics from the expected-based parse and verify helpers. It prints `OK` on success and returns non-zero when I/O, parsing, or verification fails. Dependencies include `il/api/expected_api.hpp`, `il/core/Module.hpp`, and `<fstream>`, `<iostream>`, `<string>` from the standard library.

- **src/tools/common/module_loader.cpp**

  Implements a shared helper to load IL modules from disk for multiple tools. It wraps the expected-based parse API, prints diagnostics on failure, and returns a pointer or error code to callers so they can reuse consistent file handling.

- **src/tools/ilc/cmd_codegen_x64.cpp**

  Provides the `ilc codegen-x64` subcommand that lowers IL to x86‑64 assembly using the Phase A backend. It parses flags, invokes the backend façade, and writes assembly to stdout or a file, surfacing diagnostics when the pipeline encounters unsupported features.

- **src/tools/common/module_loader.hpp**

  Declares the shared file‑loading helper used across tools to parse IL text into modules with consistent diagnostics and error returns.

- **src/tools/basic/common.hpp**, **src/tools/basic/common.cpp**

  Small helpers shared by BASIC tooling (AST/lex dumps): path loading, SourceManager registration, and utility printing.

- **src/tools/ilc/break_spec.hpp**

  Declares parsing utilities for the `--break` spec accepted by `ilc`, keeping validation logic reusable for tests and subcommands.

- **src/tools/ilc/cli.hpp**

  Declares CLI option parsing helpers and shared options struct used by `ilc` subcommands, including trace/debug toggles and stdin redirection.

- **src/tools/ilc/cmd_codegen_x64.hpp**

  Declares the driver entry for the `codegen-x64` subcommand so the main dispatcher can reference it without pulling implementation details.
