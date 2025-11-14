# CODEMAP: IL Runtime

- **src/il/runtime/RuntimeSignatures.cpp**

  Defines the shared registry mapping runtime helper names to IL signatures so frontends, verifiers, and the VM agree on the C ABI. A lazily initialized table enumerates every exported helper and wraps each return/parameter kind in `il::core::Type` objects, exposing lookup helpers for consumers. The data ensures extern declarations carry the right arity and type tags while giving the runtime bridge enough metadata to validate calls. Dependencies include `RuntimeSignatures.hpp`, IL core type definitions, and standard containers such as `<initializer_list>` and `<unordered_map>`.

- **src/il/runtime/RuntimeSignatures.hpp**

  Describes the metadata schema for runtime helper signatures shared across the toolchain. It defines the `RuntimeSignature` struct capturing return and parameter types using IL type objects and documents how parameter order mirrors the C ABI. Accessor functions expose the registry map and an optional lookup helper so consumers can fetch signatures lazily without copying data. Dependencies include `il/core/Type.hpp`, `<string_view>`, `<vector>`, and `<unordered_map>`.

- **src/il/runtime/RuntimeSignatureParser.hpp**, **src/il/runtime/RuntimeSignatureParser.cpp**

  Parses compact textual ABI signature spellings (e.g., `i64(str,i64)`) into structured `RuntimeSignature` records. The header declares the parser API; the implementation tokenizes, validates arity and kinds, and surfaces errors with helpful messages, allowing table authors to define signatures tersely without sacrificing validation. Dependencies include IL core type helpers and `<string_view>`/`<optional>`.

- **src/il/runtime/signatures/Registry.cpp**

  Aggregates category-specific signature sets (arrays, math, strings, file I/O) into the global runtime registry. Each category translation unit registers its helpers by name, and the registry stitches them together so verifiers and the VM bridge consult a single catalog.

- **src/il/runtime/RuntimeSignaturesData.hpp**

  Declares the data bundles used to materialise the signature catalog, separating static data from parser and registry code.

- **src/il/runtime/RuntimeSigs.def**

  Macro list of runtime signatures consumed by generator code to reduce duplication across tables. Included by implementation files; not edited directly.

- **src/il/runtime/signatures/Registry.hpp**

  Declares the registration entry points used by category files to install their signatures into the global table.

- **src/il/runtime/signatures/Signatures_Arrays.cpp**, **src/il/runtime/signatures/Signatures_FileIO.cpp**, **src/il/runtime/signatures/Signatures_Math.cpp**, **src/il/runtime/signatures/Signatures_Strings.cpp**

  Category‑specific signature sets registering array helpers, file I/O routines, math functions, and string operations into the registry.

- **src/il/runtime/HelperEffects.hpp**

  Declares effect tags and attributes for runtime helpers, indicating side‑effects and aliasing behaviour used by verifiers/optimisers.
