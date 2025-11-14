# CODEMAP: IL Core

- **src/il/core/BasicBlock.cpp**

  Provides the dedicated translation unit for `BasicBlock`, keeping the struct’s definition linked even though all behaviour currently lives inline. Maintaining the `.cpp` file gives debuggers and future helper methods a stable location without forcing rebuilds of unrelated headers. Dependencies are limited to `il/core/BasicBlock.hpp`, which declares the block layout.

- **src/il/core/BasicBlock.hpp**

  Defines the `BasicBlock` aggregate that holds a label, parameter list, ordered instruction vector, and a terminator flag for every IL block. The struct documents invariants around unique labels, parameter arity, and terminator tracking so builders, verifiers, and the VM can reason about control flow consistently. Storing instructions and parameters by value keeps the IR layout contiguous for efficient traversal and serialization. Dependencies include `il/core/Instr.hpp`, `il/core/Param.hpp`, and standard `<string>`/`<vector>` containers.

- **src/il/core/Extern.cpp**

  Serves as the translation unit for IL extern declarations, keeping the `Extern` aggregate in its own object file even though no out-of-line logic is needed today. Maintaining the source file simplifies future expansions such as formatting helpers or explicit template instantiations without forcing widespread rebuilds. The unit depends solely on `il/core/Extern.hpp`.

- **src/il/core/Extern.hpp**

  Defines the `il::core::Extern` struct that models a module's imported functions. It stores the mangled name, declared return type, and ordered parameter list so verifiers and code generators can validate call sites against the runtime ABI. Documentation in the header captures invariants about unique names and signature alignment, making it clear how modules should populate the collection. Dependencies include `il/core/Type.hpp` together with standard `<string>` and `<vector>` containers.

- **src/il/core/Function.cpp**

  Serves as the translation unit for out-of-line helpers tied to `il::core::Function`, keeping the door open for richer logic as the IR grows. Even though functionality currently lives in the header, the dedicated source file guarantees a stable linkage point for debugging utilities or template specializations. Maintaining the file also keeps compile units consistent across build modes. Dependencies include `il/core/Function.hpp`.

- **src/il/core/Function.hpp**

  Models IL function definitions with their signature, parameter list, basic blocks, and SSA value names. Consumers mutate the `blocks` vector as they build or transform functions, while `params` and `retType` expose metadata to verifiers and backends. The struct's simple ownership semantics (value-stored blocks and params) make it easy for the builder, serializer, and VM to traverse the IR without extra indirection. Dependencies include `il/core/BasicBlock.hpp`, `il/core/Param.hpp`, `il/core/Type.hpp`, and standard `<string>`/`<vector>` containers.

- **src/il/core/Global.cpp**

  Provides the standalone compilation unit for IL globals, mirroring the pattern used for other core aggregates. Even though all behaviour currently lives inline, keeping a `.cpp` ensures debuggers and linkers always find a home for potential utility methods. The file only includes `il/core/Global.hpp`.

- **src/il/core/Global.hpp**

  Declares the `il::core::Global` record that describes module-scoped variables and constants. Fields capture the symbol name, IL type, and optional serialized initializer string, allowing the serializer, verifier, and VM to agree on storage layout. Comments document invariants around unique identifiers and initializer type matching so producers stay spec-compliant. It depends on `il/core/Type.hpp` and uses `<string>` for both identifiers and initializers.

- **src/il/core/Instr.cpp**

  Supplies the translation unit for `Instr`, preserving a place for future out-of-line helpers despite the representation being header-only today. The separate object file simplifies linking and debugging when instruction logic evolves. Dependencies consist solely of `il/core/Instr.hpp`.

- **src/il/core/Instr.hpp**

  Declares the `Instr` struct that models IL instructions with optional result ids, explicit type metadata, operand lists, callee names, successor labels, and branch arguments. Keeping results optional lets the same structure represent void terminators and value-producing instructions without extra subclasses. Operands and labels live in `std::vector`s so passes can append or rewrite them while maintaining deterministic ordering, and each instruction retains its `SourceLoc` for diagnostics. Dependencies include `il/core/Opcode.hpp`, `il/core/Type.hpp`, `il/core/Value.hpp`, `support/source_location.hpp`, and standard `<optional>`, `<string>`, and `<vector>` facilities.

- **src/il/core/Module.cpp**

  Provides the translation unit for IL modules, currently relying on inline definitions in the header for the aggregate container. Maintaining a dedicated source file keeps a stable location for future utilities such as explicit template instantiations or logging hooks. The empty namespace ensures build systems still emit an object file, which simplifies linking when libraries expect one. Dependencies include `il/core/Module.hpp`.

- **src/il/core/Module.hpp**

  Defines the lightweight `il::core::Module` aggregate that owns externs, globals, and function definitions for a compilation unit. The struct exposes a `version` string and vectors so front ends and parsers can build modules incrementally in deterministic order. Downstream passes and the VM inspect these containers to navigate the IR during analysis and execution. Dependencies include `il/core/Extern.hpp`, `il/core/Function.hpp`, `il/core/Global.hpp`, and the standard library `<string>`/`<vector>` containers.

- **src/il/core/OpcodeInfo.cpp**

  Materializes the opcode metadata table by expanding `Opcode.def` into `kOpcodeTable`, describing each opcode’s operand counts, result expectations, side effects, and VM dispatch category. Helper functions expose `getOpcodeInfo`, `isVariadicOperandCount`, and `toString` so verifiers, serializers, and diagnostics can query canonical opcode data. A static assertion ensures the table stays synchronized with the `Opcode` enumeration. Dependencies include `il/core/OpcodeInfo.hpp` and `<string>` for mnemonic conversion.

- **src/il/core/OpcodeInfo.hpp**

  Declares the metadata schema that annotates IL opcodes with result arity, operand type categories, successor counts, and interpreter dispatch hints. Enumerations such as `TypeCategory` and `VMDispatch` let passes reason about legality without parsing `Opcode.def`, while the `OpcodeInfo` struct captures the per-opcode contract in a compact form. The header also exposes the global `kOpcodeTable` and query helpers so callers can map from an `Opcode` to its metadata in constant time. Dependencies include `il/core/Opcode.hpp`, `<array>`, `<cstdint>`, and `<limits>`.

- **src/il/core/Type.cpp**

  Implements the lightweight `Type` wrapper’s constructor and the helpers that translate kind enumerators into canonical lowercase mnemonics. Centralizing these conversions keeps builders, serializers, and diagnostics aligned on the spec’s spelling. Dependencies include `il/core/Type.hpp`.

- **src/il/core/Type.hpp**

  Defines the `Type` value object that wraps primitive IL type kinds and exposes the `Kind` enumeration consumed throughout the compiler. The struct offers a simple constructor and `toString` interface so code can manipulate types without constructing heavyweight descriptors. Because the type is trivially copyable it can be stored by value in IR containers and diagnostic records. Dependencies include `<string>` for string conversions.

- **src/il/core/Value.cpp**

  Provides constructors and formatting helpers for IL SSA values including temporaries, numeric literals, globals, and null pointers. The `toString` routine canonicalizes floating-point output by trimming trailing zeroes and ensuring deterministic formatting for the serializer, while helpers like `constInt` and `global` package values with the right tag. These utilities are widely used when building IR, pretty-printing modules, and interpreting values in the VM. The file depends on `il/core/Value.hpp` and the C++ standard library (`<sstream>`, `<iomanip>`, `<limits>`, `<utility>`) for string conversion.

- **include/viper/il/Module.hpp**

  Public aggregation header exposing IL core types to external clients; forwards the stable IL core definitions without leaking internals.

- **include/viper/il/Verify.hpp**

  Public umbrella header forwarding the IL verifier entry points so tools can depend on a stable include path.

- **src/il/core/Opcode.hpp**

  Declares the central `Opcode` enumeration listing all IL operations understood by the parser, verifier, VM, and codegen. Each mnemonic maps to an index used to look up metadata in `OpcodeInfo`, and the enum ordering is kept stable to preserve table initializers. The header also exposes helpers for casting and range checks. Dependencies are limited to `<cstdint>`.

- **src/il/core/OpcodeNames.cpp**

  Materializes the opcode→mnemonic and mnemonic→opcode conversion helpers separate from the metadata table. Keeping names distinct from `OpcodeInfo` lets tools render mnemonics without pulling in the full metadata definitions. Dependencies include `il/core/Opcode.hpp` and `<string_view>`/`<string>`.

- **src/il/core/Param.hpp**

  Defines the `il::core::Param` POD used to model function and block parameters. Fields capture a type and an optional name, and the struct is stored by value inside `Function` signatures and `BasicBlock` parameter lists. Dependencies include `il/core/Type.hpp` and `<string>`.

- **src/il/core/Value.hpp**

  Declares the tagged-union representation for IL SSA values, covering temporaries, integer/float constants, global references, and null pointers. The header provides constructors, inspectors, and a `toString` helper used by the serializer and diagnostics. Dependencies include `<cstdint>`, `<optional>`, and `<string>`.

- **src/il/core/fwd.hpp**

  Collects forward declarations for IL core aggregates to minimize header coupling. Including this header lets passes refer to `Module`, `Function`, `BasicBlock`, `Instr`, `Type`, and `Value` without pulling in full definitions. Dependencies are limited to standard forward-declaration syntax.
