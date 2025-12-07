# Viper FAQ

Frequently asked questions about the Viper compiler toolchain.

---

## General

### 1. What is Viper?

Viper is an IL-based compiler toolchain that includes multiple language frontends (BASIC and Pascal), an intermediate language (IL), virtual machine, and native code generator. It's designed as a research and educational platform for exploring language implementation, compiler design, and runtime systems.

### 2. What makes Viper different?

Viper uses a modern compiler architecture with an intermediate representation (IL) that separates language semantics from execution. Programs can run in a VM for development/debugging or be compiled to native code for performance. The IL layer makes it easy to add new language frontends—both BASIC and Pascal compile to the same IL and share a common runtime.

### 3. Is Viper suitable for production use?

No. Viper is an experimental research project at an early stage. While it implements substantial subsets of BASIC and Pascal, it's intended for education, experimentation, and compiler research rather than production use.

---

## Getting Started

### 4. How do I build Viper?

```bash
cmake -S . -B build
cmake --build build -j
```

Requirements: CMake 3.20+, Clang or GCC with C++20 support. See the top-level README for platform-specific details.

### 5. How do I run a program?

**BASIC:**
```bash
./build/src/tools/vbasic/vbasic myprogram.bas
```

**Pascal:**
```bash
./build/src/tools/vpascal/vpascal myprogram.pas
```

For additional options:
```bash
./build/src/tools/vbasic/vbasic --help
./build/src/tools/vpascal/vpascal --help
```

The advanced `ilc` command is also available:
```bash
./build/src/tools/ilc/ilc front basic -run myprogram.bas
./build/src/tools/ilc/ilc front pascal -run myprogram.pas
```

### 6. Where can I find example programs?

- `/demos/vTris/` - Full Tetris game demonstrating OOP, graphics, and game loop patterns (BASIC)
- `/examples/basic/` - BASIC example programs
- `/examples/pascal/` - Pascal example programs (hello.pas, factorial.pas, fibonacci.pas)
- `/tests/basic/` - BASIC test programs showing specific language features
- `/src/tests/data/pascal/` - Pascal test programs showing language features

### 7. What platforms does Viper support?

Viper builds and runs on:
- **macOS** (Apple Silicon and Intel)
- **Linux** (x86-64)

Native code generation targets x86-64 (SysV ABI) and AArch64.

---

## BASIC Language

### 8. What BASIC dialect does Viper implement?

Viper BASIC is inspired by classic BASIC (especially QBasic/QuickBASIC) with modern extensions. It includes:
- Structured control flow (If/Then/Else, For/Next, While/Wend, Do/Loop, Select Case)
- Procedures (Sub/Function) with parameters
- Object-oriented programming (Class, Sub New, method calls)
- Arrays (single and multi-dimensional)
- String manipulation
- File I/O
- ANSI terminal graphics (COLOR, LOCATE, CLS)

### 9. Does Viper BASIC support object-oriented programming?

Yes! Viper BASIC includes:
- Class definitions with fields and methods
- Constructors (`Sub New`)
- Object instantiation (`New` keyword)
- Reference-based object semantics
- Method calls and field access

See `/demos/vTris/` for extensive OOP examples.

### 10. Are there any known language limitations or gotchas?

Key limitations to be aware of:
- **Case-insensitive** identifiers (parameter names can collide with field names)
- **No local array copying** from object fields (use class-level temp arrays as workaround)
- **Object assignment creates references**, not copies (use `New` for independent objects)
- **No SET/CALL keywords** (direct assignment and calls only)
- **Type suffixes required** for string functions (use `Str$`, `Chr$`, not `Str`, `Chr`)

See `/bugs/basic_bugs.md` for documented issues and workarounds.

### 11. How do I use the AddFile keyword for modular programs?

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

## Pascal Language

### 12. What Pascal dialect does Viper implement?

Viper Pascal is inspired by standard Pascal with modern extensions. It includes:
- Structured control flow (If/Then/Else, For/To/Downto, While/Do, Repeat/Until, Case)
- Procedures and functions with parameters
- Units for modular code organization
- Exception handling (Try/Except/Finally, Raise)
- Arrays and records
- Pointer types
- Strong typing with type declarations

### 13. Does Viper Pascal support units?

Yes! Viper Pascal supports units with separate interface and implementation sections:

```pascal
unit MyUnit;

interface
  function Square(x: Integer): Integer;

implementation

function Square(x: Integer): Integer;
begin
  Square := x * x
end;

end.
```

Use `uses` to import units into your program.

### 14. What built-in functions are available in Pascal?

Built-in functions include:
- **Math**: `Abs`, `Sqr`, `Sqrt`, `Sin`, `Cos`, `Tan`, `Exp`, `Ln`, `Round`, `Trunc`
- **String**: `Length`, `Copy`, `Pos`, `Concat`, `UpperCase`, `LowerCase`, `Trim`
- **Conversion**: `IntToStr`, `FloatToStr`, `StrToInt`, `StrToFloat`, `Chr`, `Ord`
- **I/O**: `Write`, `WriteLn`, `Read`, `ReadLn`
- **Ordinal**: `Succ`, `Pred`, `Inc`, `Dec`

See `/src/frontends/pascal/BuiltinRegistry.cpp` for the complete list.

---

## IL (Intermediate Language)

### 15. What is the Viper IL?

The Viper Intermediate Language is a low-level, typed, control-flow graph representation that sits between frontends (BASIC, Pascal) and backends (VM or native code). It's similar to LLVM IR or .NET CIL but designed specifically for this project's needs.

See `/docs/il-guide.md` for the complete IL specification.

### 16. Can I write IL code directly?

Yes! The IL has a textual assembly syntax. You can write `.il` files and run them:

```bash
./build/src/tools/ilc/ilc run myprogram.il
```

Tools available:
- `il-dis` - Disassembler
- `il-verify` - IL verifier
- `ilrun` - IL interpreter

### 17. How does the compilation pipeline work?

```
Source (BASIC/Pascal) → Parser → Semantic Analysis → IL Generation → IL Transforms →
  ├─→ VM Interpreter (for development/debugging)
  └─→ Native Codegen (for performance)
```

The IL layer provides optimization passes, verification, and serialization. Different backends can consume the same IL.

---

## VM and Runtime

### 18. What's the difference between VM and native execution?

- **VM (Interpreter)**: Executes IL directly. Slower but includes debugging support (breakpoints, stepping, watches). Default execution mode.
- **Native**: Compiles IL to machine code. Much faster but fewer debugging features.

For development, use VM mode. For performance testing, use native compilation.

### 19. How do I debug programs?

The VM supports source-level debugging:

```bash
# Set breakpoint and run
./build/src/tools/ilc/ilc front basic -run program.bas -break program.bas:42

# Use debugger commands
# (watch variables, step through code, inspect state)
```

See VM debugging tests in `/tests/vm/` for examples.

### 20. What runtime functions are available?

Both BASIC and Pascal share the same runtime library. Built-in functions include:
- **Math**: `Sin`, `Cos`, `Tan`, `Sqrt`, `Abs`, `Round`, `Trunc`
- **String**: `Length`/`Len`, `Copy`/`Mid$`, `Concat`, `Trim`
- **I/O**: `Print`/`Write`, `Input`/`Read`
- **Graphics**: `Color`, `Locate`, `Cls`
- **Conversion**: `IntToStr`/`Str$`, `StrToInt`/`Val`

See the respective builtin registries in `/src/frontends/basic/` and `/src/frontends/pascal/` for language-specific function lists.

---

## Development and Contributing

### 21. How do I add a new built-in function?

1. Add the function signature to the builtin registry for the frontend
2. Implement the lowering logic in the frontend
3. Add the runtime implementation in `/src/runtime/`
4. Add tests

See `/docs/frontend-howto.md` for detailed guidance.

### 22. How do I report bugs or request features?

- **Bugs**: Add to `/bugs/basic_bugs.md` (BASIC) or open an issue (Pascal)
- **Features**: Open a discussion or create an issue describing the use case
- **Contributing**: Follow the Conventional Commits format for commit messages

### 23. Where can I find more documentation?

Key documentation files:
- `/docs/il-guide.md` - Complete IL specification (normative reference)
- `/docs/architecture.md` - System architecture overview
- `/docs/codemap.md` - Source code organization and navigation
- `/docs/frontend-howto.md` - Guide to frontend development
- `/docs/basic-language.md` - BASIC language tutorial
- `/docs/basic-reference.md` - BASIC language reference
- `/docs/pascal-language.md` - Pascal language tutorial
- `/docs/pascal-reference.md` - Pascal language reference
- `/CLAUDE.md` - Development workflow and contribution guidelines

For code-level documentation, see header comments in source files.

---

## Quick Reference

**Build and run a BASIC program:**
```bash
cmake --build build -j
./build/src/tools/vbasic/vbasic program.bas
```

**Build and run a Pascal program:**
```bash
./build/src/tools/vpascal/vpascal program.pas
```

**Run with debugging:**
```bash
./build/src/tools/ilrun/ilrun program.il --break main:10
```

**View generated IL:**
```bash
./build/src/tools/vbasic/vbasic program.bas --emit-il
./build/src/tools/vpascal/vpascal program.pas --emit-il
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

*Last updated: December 2025*
