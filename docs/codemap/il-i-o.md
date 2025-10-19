# CODEMAP: IL I/O

- **src/il/io/FunctionParser.cpp**

  Implements the low-level text parser for IL function bodies, decoding headers, block labels, and instruction streams into the mutable `ParserState`. Header parsing splits out parameter lists, return types, and seeds temporary IDs, while block parsing validates parameter arity and reconciles pending branch argument counts. The main `parseFunction` loop walks the textual body line by line, honoring `.loc` directives, delegating instruction syntax to `parseInstruction`, and emitting structured diagnostics via the capture helpers. Dependencies include `il/io/FunctionParser.hpp`, `il/core` definitions for modules, functions, blocks, and params, the sibling `InstrParser`, `ParserUtil`, `TypeParser`, and diagnostic support from `support/diag_expected.hpp`.

- **src/il/io/InstrParser.cpp**

  Parses individual IL instruction lines into `il::core::Instr` instances while updating the parser state that tracks temporaries, blocks, and diagnostics. Utility routines decode operands into values or types, apply opcode metadata to validate operand counts, result arity, and successor lists, and enqueue branch targets for later block resolution. Specialized parsers handle calls, branch target lists, and SSA assignment prefixes so the textual form produced by the serializer round-trips cleanly. Branch destinations now reject any trailing tokens that follow the closing parenthesis, surfacing a `malformed <mnemonic>` diagnostic tied to the offending instruction. Dependencies include `il/io/InstrParser.hpp`, IL core opcode/type/value headers, helper utilities from `ParserUtil` and `TypeParser`, and diagnostic plumbing in `support/diag_expected.hpp`.

- **src/il/io/ModuleParser.cpp**

  Implements the module-level IL parser responsible for directives like `il`, `extern`, `global`, and `func`. Helpers normalize tokens, parse type lists via `parseType`, capture diagnostics from the function parser, and repackage failures as `Expected` errors tied to the current line. Extern and global directives mutate the active `ParserState` in place, while function headers dispatch into the dedicated function parser and version directives update module metadata. Dependencies include `il/io/ModuleParser.hpp`, IL core containers, subordinate parsers (`FunctionParser`, `ParserUtil`, `TypeParser`), `support/diag_expected.hpp`, and standard `<sstream>`, `<string_view>`, `<utility>`, and `<vector>` utilities.

- **src/il/io/ModuleParser.hpp**

  Declares the `parseModuleHeader` helper that advances the IL reader through top-level directives. Callers provide an input stream, the current line buffer, and the shared `ParserState` so externs, globals, and functions are appended directly to the module. Errors are streamed to an `std::ostream`, mirroring the rest of the parsing layer while keeping this interface minimal. Dependencies are limited to `il/io/ParserState.hpp` alongside `<istream>`, `<ostream>`, and `<string>`.

- **src/il/io/Parser.cpp**

  Implements the façade for parsing textual IL modules from an input stream. It seeds a `ParserState`, normalizes each line while skipping comments or blanks, and then hands structural decisions to the detail `parseModuleHeader_E` helper. Errors from that helper propagate unchanged so callers receive consistent diagnostics with precise line numbers. Dependencies include `Parser.hpp`, `ModuleParser.hpp`, `ParserUtil.hpp`, the parser-state helpers, IL core `Module` definitions, and the diagnostics `Expected` wrapper.

- **src/il/io/ParserUtil.cpp**

  Collects small lexical helpers shared by the IL text parser, including trimming whitespace, reading comma-delimited tokens, and parsing integer or floating literal spellings. Each function wraps the corresponding standard-library conversion while enforcing full-token consumption so upstream parsers can surface precise errors. They are intentionally stateless and operate on caller-provided buffers to keep instruction parsing allocation-free. Dependencies include `il/io/ParserUtil.hpp` together with `<cctype>` and `<exception>` from the standard library.

- **src/il/io/Parser.hpp**

  Declares the IL parser entry point that orchestrates module, function, and instruction sub-parsers. The class exposes a single static `parse` routine, signaling that parsing is a stateless operation layered over a supplied module instance. Its includes reveal the composition of specialized parsers and parser-state bookkeeping while documenting the diagnostic channel used for reporting errors. Dependencies include IL core forward declarations, the function/instruction/module parser headers, `ParserState.hpp`, and the `il::support::Expected` utility.

- **src/il/io/ParserState.cpp**

  Implements the lightweight constructor for the shared IL parser state, wiring the mutable context to the module being populated. Having the definition in a `.cpp` avoids inlining across translation units that include the header. The only dependency is `il/io/ParserState.hpp`.

- **src/il/io/ParserState.hpp**

  Declares `il::io::detail::ParserState`, the mutable context threaded through module, function, and instruction parsers. It keeps references to the current module, function, and basic block along with SSA bookkeeping structures like `tempIds`, `nextTemp`, and unresolved branch metadata. The nested `PendingBr` struct and `blockParamCount` map let parsers defer validation until all labels are seen while `curLoc` tracks active `.loc` directives for diagnostics. Dependencies cover `il/core/fwd.hpp`, `support/source_location.hpp`, and standard `<string>`, `<unordered_map>`, and `<vector>` utilities.

- **src/il/io/TypeParser.cpp**

  Translates textual IL type mnemonics like `i64`, `ptr`, or `str` into `il::core::Type` objects used by the parser, returning a default type when the spelling is unknown. Callers can optionally receive a success flag via the `ok` pointer, allowing higher-level parsers to differentiate between absent and malformed type annotations. The mapping mirrors the primitive set documented in `docs/il-guide.md#reference`, ensuring serializer and parser stay aligned on accepted spellings. Dependencies include `il/io/TypeParser.hpp`, which exposes the interface backed by `il::core::Type` definitions.

- **src/il/io/Serializer.cpp**

  Emits IL modules into textual form by traversing externs, globals, and function bodies with deterministic ordering when canonical mode is requested. It prints `.loc` metadata, rewrites operands using `Value::toString`, and honors opcode-specific formatting rules for calls, branches, loads/stores, and returns. Extern declarations can be sorted lexicographically to support diff-friendly output, and functions render their block parameters alongside instructions. Dependencies include the serializer interface, IL core containers (`Extern`, `Global`, `Function`, `BasicBlock`, `Instr`, `Module`, `Opcode`, `Value`), and the standard `<algorithm>`/`<sstream>` utilities.
