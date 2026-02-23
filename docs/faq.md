# Viper FAQ

Frequently asked questions about the Viper compiler toolchain.

---

## General

### 1. What is Viper?

Viper is an IL-based compiler toolchain that includes multiple language frontends (Zia and BASIC), an intermediate
language (IL), virtual machine, and native code generator. It's designed as a research and educational platform for
exploring language implementation, compiler design, and runtime systems.

### 2. What makes Viper different?

Viper uses a modern compiler architecture with an intermediate representation (IL) that separates language semantics
from execution. Programs can run in a VM for development/debugging or be compiled to native code for performance. The IL
layer makes it easy to add new language frontends—Zia and BASIC both compile to the same IL and share a common
runtime.

### 3. Is Viper suitable for production use?

No. Viper is an experimental research project at an early stage. While it implements substantial subsets of Zia and
BASIC, it's intended for education, experimentation, and compiler research rather than production use.

---

## Getting Started

### 4. How do I build Viper?

```bash
./scripts/build_viper.sh
```

Requirements: CMake 3.20+, Clang or GCC with C++20 support. See the top-level README for platform-specific details.

### 5. How do I run a program?

```bash
viper run myprogram.zia
viper run myprogram.bas
```

You can also run an entire project directory:

```bash
viper run demos/zia/frogger/
viper run demos/basic/vtris/
```

The standalone tools `vbasic`, `zia`, and `ilrun` are also available:

```bash
vbasic myprogram.bas
zia myprogram.zia
ilrun program.il
```

### 6. Where can I find example programs?

- `/demos/zia/frogger/` - Full Frogger game in Zia demonstrating modules and game architecture
- `/demos/zia/centipede/` - Centipede game in Zia
- `/demos/zia/pacman/` - Pac-Man game in Zia
- `/demos/basic/vtris/` - Full Tetris game demonstrating OOP, graphics, and game loop patterns (BASIC)
- `/demos/basic/chess/` - Chess game in BASIC
- `/examples/basic/` - BASIC example programs
- `/src/tests/golden/zia/` - Zia test programs showing specific language features
- `/src/tests/golden/basic/` - BASIC test programs showing specific language features

### 7. What platforms does Viper support?

Viper builds and runs on:

- **macOS** (Apple Silicon and Intel)
- **Linux** (x86-64)
- **Windows** (x86-64)

Native code generation targets x86-64 (System V and Windows x64 ABIs) and AArch64.

---

## Zia Language

### 8. What is Zia?

Zia is Viper's primary language, designed as a modern, clean systems programming language. It includes:

- Module system for code organization
- Entity types (similar to classes) with methods and fields
- Strong static typing (Integer, Boolean, String, etc.)
- Structured control flow (if/else, while, for, match)
- Functions with type annotations
- Bind system for multi-file projects
- First-class support for the Viper runtime library

### 9. How do I write a simple Zia program?

```rust
module Main;

func start() {
    Viper.Terminal.Say("Hello, world!");
}
```

Run it with:

```bash
./build/src/tools/zia/zia hello.zia
```

### 10. What are entities in Zia?

Entities are Zia's object-oriented construct, similar to classes:

```rust
entity Counter {
    expose Integer value;

    expose func init(start: Integer) {
        value = start;
    }

    expose func increment() {
        value = value + 1;
    }
}
```

Use `expose` to make fields and methods visible outside the entity.

### 11. How do I organize multi-file Zia projects?

Use modules and binds:

```rust
module MyModule;

bind "./OtherModule";

func useOther() {
    OtherModule.someFunction();
}
```

See `/demos/zia/frogger/` for a complete multi-module game example.

---

## BASIC Language

### 12. What BASIC dialect does Viper implement?

Viper BASIC is inspired by classic BASIC (especially QBasic/QuickBASIC) with modern extensions. It includes:

- Structured control flow (If/Then/Else, For/Next, While/Wend, Do/Loop, Select Case)
- Procedures (Sub/Function) with parameters
- Object-oriented programming (Class, Sub New, method calls)
- Arrays (single and multi-dimensional)
- String manipulation
- File I/O
- ANSI terminal graphics (COLOR, LOCATE, CLS)

### 13. Does Viper BASIC support object-oriented programming?

Yes! Viper BASIC includes:

- Class definitions with fields and methods
- Constructors (`Sub New`)
- Object instantiation (`New` keyword)
- Reference-based object semantics
- Method calls and field access

See `/demos/basic/vtris/` for extensive OOP examples.

### 14. Are there any known language limitations or gotchas?

Key limitations to be aware of:

- **Case-insensitive** identifiers (parameter names can collide with field names)
- **No local array copying** from object fields (use class-level temp arrays as workaround)
- **Object assignment creates references**, not copies (use `New` for independent objects)
- **No SET/CALL keywords** (direct assignment only)
- **Type suffixes required** for string functions (use `Str$`, `Chr$`, not `Str`, `Chr`)

See `/docs/bugs/viper_bugs.md` for documented issues and workarounds.

### 15. How do I use the AddFile keyword for modular programs?

Use `AddFile` to include other BASIC source files:

```basic
AddFile "utilities.bas"
AddFile "../../lib/graphics.bas"

' Now you can use classes/functions from included files
Dim helper As HelperClass
helper = New HelperClass()
```

Paths are relative to the file containing the `AddFile` statement.

---

## IL (Intermediate Language)

### 19. What is the Viper IL?

The Viper Intermediate Language is a low-level, typed, control-flow graph representation that sits between frontends (
Zia, BASIC) and backends (VM or native code). It's similar to LLVM IR or .NET CIL but designed specifically for this
project's needs.

See `/docs/il-guide.md` for the complete IL specification.

### 20. Can I write IL code directly?

Yes! The IL has a textual assembly syntax. You can write `.il` files and run them:

```bash
./build/src/tools/viper/viper run myprogram.il
```

Tools available:

- `il-dis` - Disassembler
- `il-verify` - IL verifier
- `ilrun` - IL interpreter

### 21. How does the compilation pipeline work?

```
Source (Zia/BASIC) → Parser → Semantic Analysis → IL Generation → IL Transforms →
  ├─→ VM Interpreter (for development/debugging)
  └─→ Native Codegen (for performance)
```

The IL layer provides optimization passes, verification, and serialization. Different backends can consume the same IL.

---

## VM and Runtime

### 22. What's the difference between VM and native execution?

- **VM (Interpreter)**: Executes IL directly. Slower but includes debugging support (breakpoints, stepping, watches).
  Default execution mode.
- **Native**: Compiles IL to machine code. Much faster but fewer debugging features.

For development, use VM mode. For performance testing, use native compilation.

### 23. How do I debug programs?

The VM supports source-level debugging:

```bash
# Set breakpoint and run
./build/src/tools/viper/viper front basic -run program.bas -break program.bas:42

# Use debugger commands
# (watch variables, step through code, inspect state)
```

See VM debugging tests in `/src/tests/vm/` for examples.

### 24. What runtime functions are available?

Both frontends (Zia, BASIC) share the same runtime library. Built-in functions include:

- **Math**: `Sin`, `Cos`, `Tan`, `Sqrt`, `Abs`, `Round`, `Trunc`
- **String**: `Length`/`Len`, `Copy`/`Mid$`, `Concat`, `Trim`
- **I/O**: `Print`/`Write`, `Input`/`Read`
- **Graphics**: `Color`, `Locate`, `Cls`
- **Conversion**: `ToString_Int`/`Str$`, `ToInt`/`Val`

See the respective builtin registries in `/src/frontends/zia/` and `/src/frontends/basic/builtins/`
for language-specific function lists.

---

## Development and Contributing

### 25. How do I add a new built-in function?

1. Add the function signature to the builtin registry for the frontend
2. Implement the lowering logic in the frontend
3. Add the runtime implementation in `/src/runtime/`
4. Add tests

See `/docs/frontend-howto.md` for detailed guidance.

### 26. How do I report bugs or request features?

- **Bugs**: Add to `/docs/bugs/viper_bugs.md` or open an issue
- **Features**: Open a discussion or create an issue describing the use case
- **Contributing**: Follow the Conventional Commits format for commit messages

### 27. Where can I find more documentation?

Key documentation files:

- `/docs/il-guide.md` - Complete IL specification (normative reference)
- `/docs/architecture.md` - System architecture overview
- `/docs/codemap.md` - Source code organization and navigation
- `/docs/frontend-howto.md` - Guide to frontend development
- `/docs/zia-getting-started.md` - Zia language tutorial
- `/docs/zia-reference.md` - Zia language reference
- `/docs/basic-language.md` - BASIC language tutorial
- `/docs/basic-reference.md` - BASIC language reference
- `/CLAUDE.md` - Development workflow and contribution guidelines

For code-level documentation, see header comments in source files.

---

## Quick Reference

**Build and run a Zia program:**

```bash
cmake --build build -j
./build/src/tools/zia/zia program.zia
```

**Build and run a BASIC program:**

```bash
./build/src/tools/vbasic/vbasic program.bas
```

**Run with debugging:**

```bash
./build/src/tools/ilrun/ilrun program.il --break main:10
```

**View generated IL:**

```bash
./build/src/tools/zia/zia program.zia --emit-il
./build/src/tools/vbasic/vbasic program.bas --emit-il
```

**Run tests:**

```bash
ctest --test-dir build --output-on-failure
```

**Format code:**

```bash
clang-format -i <files>
```

---

*Last updated: February 2026*
