# CODEMAP: Support

- **src/support/arena.cpp**

  Defines the bump allocator primitives backing `il::support::Arena`. The constructor initializes an owned byte buffer and `allocate` performs aligned bumps while rejecting invalid power-of-two requests. Callers reset the arena between phases to reclaim memory for short-lived objects without individual frees. Dependencies include `support/arena.hpp` and the standard library facilities used by `std::vector` to manage the buffer.

- **src/support/arena.hpp**

  Declares the `il::support::Arena` class used to service fast, short-lived allocations for parsers and passes. It stores a `std::vector<std::byte>` buffer and a bump pointer so repeated `allocate` calls are O(1) until capacity runs out. The class exposes explicit reset semantics instead of per-allocation frees, making it a good fit for phase-based compilation. Dependencies include `<vector>`, `<cstddef>`, and modules that instantiate the arena such as parsers and VM helpers.

- **src/support/diag_expected.cpp**

  Supplies the plumbing for `Expected<void>` diagnostics used across the toolchain to report recoverable errors. It implements the boolean conversion and error accessor, provides consistent severity-to-string mapping, and centralizes error construction through `makeError`. `printDiag` consults the shared `SourceManager` to prepend file and line information so messages match compiler-style output. Dependencies include `support/diag_expected.hpp`, which defines the diagnostic types and pulls in `<ostream>`, `<string>`, and the `SourceLoc` helpers.

- **src/support/diag_capture.cpp**

  Supplies the out-of-line utilities for `DiagCapture`, converting captured diagnostic buffers into stream output or `Expected<void>` results. The helper forwards to `printDiag` for rendering, and `toDiag` reuses `makeError` so stored text becomes a structured diagnostic tied to an empty source location. `capture_to_expected_impl` bridges legacy boolean-returning APIs into the newer Expected-based flow by either returning success or the captured error. Dependencies include `support/diag_capture.hpp`, which brings in the diagnostic primitives and Expected helpers these adapters rely on.

- **src/support/diagnostics.cpp**

  Implements the diagnostic engine that collects, counts, and prints errors and warnings emitted across the toolchain. It records severity information, formats messages with source locations when a `SourceManager` is provided, and exposes counters so clients can bail out after fatal issues. The printing helper maps enum severities to lowercase strings to keep output consistent between front-end and backend consumers. Dependencies cover the diagnostics interfaces, source management utilities, and standard stream facilities.

- **src/support/diagnostics.hpp**

  Advertises the diagnostics subsystem responsible for collecting, counting, and printing compiler messages. The header enumerates severity levels, the `Diagnostic` record, and the `DiagnosticEngine` API so callers can report events and later flush them to a stream. Counter accessors make it easy for front ends to guard execution on accumulated errors while preserving the order of recorded messages. Dependencies include the shared `source_location.hpp` definitions and standard library facilities such as `<ostream>`, `<string>`, and `<vector>`.

- **src/support/source_manager.cpp**

  Maintains canonical source-file identifiers and paths for diagnostics through the `SourceManager`. New files are normalized with `std::filesystem` so relative paths collapse to stable, platform-independent strings before being assigned incrementing IDs. Consumers such as the lexer, diagnostics engine, and tracing facilities call back into the manager to resolve `SourceLoc` instances into filenames. Dependencies include `source_manager.hpp` and the C++ `<filesystem>` library.

- **src/support/string_interner.cpp**

  Provides the implementation of the `StringInterner`, giving the BASIC front end and VM a shared symbol table. `intern` consults an unordered map before copying new strings into the storage vector, guaranteeing each interned value receives a stable non-zero `Symbol`. The `lookup` helper validates ids and returns the original view or an empty result when the caller passes the reserved sentinel. It relies on `support/string_interner.hpp`, which supplies the container members and the `Symbol` wrapper.

- **src/support/string_interner.hpp**

  Declares the `StringInterner` class and accompanying `Symbol` abstraction used wherever the toolchain needs canonicalized identifiers. The interface exposes `intern` to deduplicate strings and `lookup` to retrieve the original text, making it easy for diagnostics, debuggers, and registries to share keys. Internally the class stores an unordered map from text to `Symbol` alongside a vector of owned strings so views remain valid for the interner's lifetime. Dependencies include `support/symbol.hpp` plus standard `<string>`, `<string_view>`, `<unordered_map>`, and `<vector>` containers.

- **src/support/symbol.cpp**

  Defines comparison and utility operators for the interned `Symbol` identifier type. Equality and inequality simply compare the stored integral id, while the boolean conversion treats zero as the reserved invalid sentinel. A `std::hash` specialization reuses the id so symbols integrate directly with unordered containers. Dependencies are limited to `support/symbol.hpp`, which declares the wrapper and exposes the underlying field.

- **src/support/source_location.cpp**

  Implements the helper that reports whether a `SourceLoc` points at a registered file. The method checks for a nonzero file identifier so diagnostics and tools can ignore default-constructed locations. Dependencies include only `support/source_location.hpp`, which defines the lightweight value type.

- **src/common/RunProcess.hpp**, **src/common/RunProcess.cpp**

  Cross‑platform subprocess launcher used by developer tools and tests. `run_process` builds a shell command string from argv fragments, launches it via the host `popen` facility, captures stdout/stderr, and returns a `RunResult` with the exit status and output. The header defines the result record; the implementation normalises quoting across platforms and supports optional working directory and environment overrides. Dependencies are standard C/C++ headers and, on POSIX, `<sys/wait.h>`/`<unistd.h>`.

- **src/common/IntegerHelpers.hpp**

  Header‑only utilities for bit‑width‑aware integer math: widening/narrowing with selectable overflow policy (wrap, trap, saturate), common‑width promotion for binary ops, and min/max/mask helpers. The functions are used across the BASIC front end and VM to preserve two’s‑complement semantics and explicit trap behaviour for overflows. Dependencies are standard `<limits>`, `<bit>`/`<cstdint>`, and `<stdexcept>` where trapping is enabled.

- **src/support/source_location.hpp**

  Declares the `SourceLoc` value type (file id, line, column) and helpers to test for registration and default construction. Used across diagnostics, parser, and VM for precise error messages.

- **src/support/source_manager.cpp**, **src/support/source_manager.hpp**

  Owns loaded source buffers and assigns stable file ids. Provides APIs to register in‑memory text or files, query line/column mappings, and format ranges for diagnostics.

- **src/support/diag_expected.cpp**, **src/support/diag_expected.hpp**

  Expected/diagnostic wrapper bridging result‑style error handling with structured diagnostics. Lets layers return `Expected<T>` and stream diagnostics consistently.

- **src/support/diagnostics.cpp**, **src/support/diagnostics.hpp**

  Common diagnostics primitives and emitters (severity, codes, message formatting) used by front end, verifier, and tools.

- **src/support/diag_capture.cpp**, **src/support/diag_capture.hpp**

  Small utility to capture streamed diagnostics into a buffer for tests and tooling without changing production emitters.

- **src/support/arena.cpp**, **src/support/arena.hpp**

  Simple bump‑pointer arena used for short‑lived allocations in passes where allocation patterns are append‑only and reset en masse.

- **src/support/result.hpp**

  Minimal `Result<T>` helper used in limited places as a non‑exception transport for success/failure distinct from `Expected<T>`.

- **src/support/options.hpp**

  Central place for small global compile‑time options and feature toggles consumed by subsystems.

- **src/support/string_interner.hpp**

  Declares the `StringInterner` API; pairs with the implementation already documented to expose `intern`/`lookup` and the `Symbol` type.

- **src/support/symbol.hpp**

  Declares the `Symbol` wrapper type and its hash; used as stable ids for interned strings.

- **include/viper/parse/Cursor.h**

  Public reusable text cursor for IL/BASIC parsers operating over `std::string_view` without allocations.

- **include/viper/pass/PassManager.hpp**

  Public façade for pass registration/execution used by tools; complements internal pass managers.

- **include/viper/runtime/rt.h**

  Public umbrella header aggregating the runtime C headers for consumers of the C ABI from C++ code.

- **src/parse/Cursor.cpp**

  Implements small utilities for the public parsing cursor; pairs with `include/viper/parse/Cursor.h`.

- **src/pass/PassManager.cpp**

  Implements the public pass manager façade; complements `include/viper/pass/PassManager.hpp` for tools.
