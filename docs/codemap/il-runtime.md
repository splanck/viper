# CODEMAP: IL Runtime

- **src/il/runtime/RuntimeSignatures.cpp**

  Defines the shared registry mapping runtime helper names to IL signatures so frontends, verifiers, and the VM agree on the C ABI. A lazily initialized table enumerates every exported helper and wraps each return/parameter kind in `il::core::Type` objects, exposing lookup helpers for consumers. The data ensures extern declarations carry the right arity and type tags while giving the runtime bridge enough metadata to validate calls. Dependencies include `RuntimeSignatures.hpp`, IL core type definitions, and standard containers such as `<initializer_list>` and `<unordered_map>`.

- **src/il/runtime/RuntimeSignatures.hpp**

  Describes the metadata schema for runtime helper signatures shared across the toolchain. It defines the `RuntimeSignature` struct capturing return and parameter types using IL type objects and documents how parameter order mirrors the C ABI. Accessor functions expose the registry map and an optional lookup helper so consumers can fetch signatures lazily without copying data. Dependencies include `il/core/Type.hpp`, `<string_view>`, `<vector>`, and `<unordered_map>`.
