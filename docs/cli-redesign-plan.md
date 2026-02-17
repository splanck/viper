---
status: phase-1-complete
audience: internal
created: 2025-11-15
updated: 2026-01-19
---

# CLI Redesign Implementation Plan

> **Implementation Status (2026-01):** Phase 1 is complete. Both `vbasic` and `ilrun` tools have been
> implemented in `src/tools/vbasic/` and `src/tools/ilrun/`. The tools are functional and follow the
> design outlined below.

## Executive Summary

**Objective**: Simplify Viper CLI from `viper front basic -run script.bas` (5 tokens) to `vbasic script.bas` (2 tokens),
matching industry standards (Python, Node.js, Ruby, Lua).

**Approach**: Create user-friendly wrappers (`vbasic`, `ilrun`) while preserving existing `ilc` functionality for
backwards compatibility.

**Timeline**: 3 phases over 2-3 weeks

- Phase 1: Core wrappers (vbasic, ilrun) - 3-5 days
- Phase 2: Documentation and testing - 2-3 days
- Phase 3: Optional enhancements - 1-2 days

---

## Current State Analysis

### Existing Tool Structure

```
src/
├── tools/
│   ├── viper/                 # Monolithic tool (18MB binary)
│   │   ├── main.cpp           # Main dispatcher
│   │   ├── cmd_run_il.cpp     # IL VM execution
│   │   ├── cmd_front_basic.cpp # BASIC frontend
│   │   ├── cmd_il_opt.cpp     # IL optimizer
│   │   ├── cmd_codegen_x64.cpp # Native codegen
│   │   ├── cli.cpp            # Shared CLI utilities
│   │   └── cli.hpp
│   ├── il-verify/             # IL verifier
│   ├── il-dis/                # IL disassembler
│   ├── basic-ast-dump/        # BASIC AST dumper
│   └── basic-lex-dump/        # BASIC lexer dumper
```

### Current Command Patterns

| Task                | Current Command                     | Token Count |
|---------------------|-------------------------------------|-------------|
| Run BASIC           | `viper front basic -run file.bas`     | 5           |
| Emit BASIC IL       | `viper front basic -emit-il file.bas` | 5           |
| Run IL              | `viper -run file.il`                  | 3           |
| Compile IL → native | `viper codegen x64 -S file.il -o exe` | 7           |
| Optimize IL         | `viper il-opt file.il -o out.il`      | 5           |

### Problems Identified

1. **Cognitive Overhead**: Users must learn `ilc` subcommand structure
2. **Verbose**: Common operations require 5-7 tokens vs 2 for competitors
3. **Non-Intuitive**: `viper front basic -run` is opaque to newcomers
4. **Inconsistent**: Some tools follow simple pattern (`il-verify file.il`), others don't
5. **Discovery**: Hard to find available commands without deep --help reading

---

## Target State

### New User-Facing Commands

| Tool     | Purpose                    | Example                        | Priority |
|----------|----------------------------|--------------------------------|----------|
| `vbasic` | Run/compile BASIC programs | `vbasic game.bas`              | P0       |
| `ilrun`  | Execute IL programs        | `ilrun program.il`             | P0       |
| `viper`  | Compile IL → native        | `viper program.il -o exe`      | P1       |
| `ilopt`  | Optimize IL                | `ilopt program.il -o out.il`   | P2       |

### Developer Tools (Keep As-Is)

- `il-verify` - IL verification
- `il-dis` - IL disassembly
- `basic-ast-dump` - BASIC AST inspection
- `basic-lex-dump` - BASIC token inspection

---

## Phase 1: Core Wrappers (P0)

### 1.1: Create `vbasic` Tool

**Location**: `src/tools/vbasic/`

**Files Created**:

```
src/tools/vbasic/
├── main.cpp          # Thin wrapper
└── cli_compat.cpp    # CLI compatibility and help text
```

**Implementation Strategy**:

**Option A: Thin Wrapper (Recommended)**

- New executable that translates `vbasic` args → `viper front basic` args
- Reuses all existing `cmdFrontBasic` logic
- ~200 lines of code
- Minimal maintenance burden

**Option B: Shared Library**

- Extract `cmdFrontBasic` into shared library
- Both `ilc` and `vbasic` link against it
- More refactoring, same end result
- Deferred to future cleanup

**vbasic Command Design**:

```bash
vbasic [options] <file.bas>

Usage Modes:
  vbasic script.bas              # Run program (default)
  vbasic script.bas -c           # Compile to native executable
  vbasic script.bas --emit-il    # Emit IL to stdout
  vbasic script.bas -o out.il    # Emit IL to file

Options:
  -c, --compile                  Compile to native executable
  -o, --output FILE              Output file (executable or IL)
  --emit-il                      Emit IL instead of running
  --trace[=il|src]               Enable execution tracing
  --bounds-checks                Enable array bounds checking
  --stdin-from FILE              Redirect stdin from file
  --max-steps N                  Limit execution steps
  --dump-trap                    Show detailed trap diagnostics
  -h, --help                     Show this help message
  --version                      Show version information

Examples:
  vbasic game.bas                           # Run program
  vbasic game.bas -c -o game                # Compile to executable
  vbasic game.bas --emit-il -o game.il      # Generate IL
  vbasic game.bas --trace --bounds-checks   # Debug mode
  vbasic game.bas --stdin-from input.txt    # Redirect input

Notes:
  - Default action is to run the program
  - Use -c to compile (triggers: BASIC → IL → native pipeline)
  - IL is intermediate; use --emit-il for inspection
  - See 'vbasic --help-detailed' for advanced options
```

**Argument Translation Logic**:

```cpp
// Pseudocode for vbasic → ilc translation
std::vector<std::string> translate_args(int argc, char** argv) {
    std::vector<std::string> ilc_args = {"ilc", "front", "basic"};

    bool has_action = false;
    bool compile = false;
    bool emit_il = false;
    std::string output_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-c" || arg == "--compile") {
            compile = true;
            has_action = true;
        } else if (arg == "--emit-il") {
            emit_il = true;
            has_action = true;
            ilc_args.push_back("-emit-il");
        } else if (arg == "-o" || arg == "--output") {
            output_file = argv[++i];
            if (emit_il) {
                ilc_args.push_back("-o");
                ilc_args.push_back(output_file);
            }
        } else if (arg == "-h" || arg == "--help") {
            show_vbasic_help();
            exit(0);
        } else if (arg.ends_with(".bas")) {
            // Source file
            if (!has_action) {
                ilc_args.push_back("-run");  // Default: run
            }
            ilc_args.push_back(arg);
        } else {
            // Pass through other flags (--trace, --bounds-checks, etc.)
            ilc_args.push_back(arg);
        }
    }

    if (compile) {
        // TODO: Implement full compile pipeline
        // For now, emit IL and call ilc codegen
        std::cerr << "Compilation not yet implemented\n";
        exit(1);
    }

    return ilc_args;
}
```

**CMake Integration**:

```cmake
# Add to src/CMakeLists.txt after ilc definition

add_executable(vbasic
    tools/vbasic/main.cpp
    tools/vbasic/usage.cpp
    tools/viper/cmd_front_basic.cpp  # Reuse existing logic
    tools/viper/cli.cpp)
set_target_properties(vbasic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/vbasic)
target_link_libraries(vbasic
    PRIVATE
    viper_common_opts
    viper::il_full
    il_vm
    il_transform
    fe_basic
    il_api
    il_tools_common)
install(TARGETS vbasic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

**Testing Plan**:

```bash
# Basic execution
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas

# IL emission
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas --emit-il

# Tracing
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas --trace

# Help text
./build/src/tools/vbasic/vbasic --help
```

---

### 1.2: Create `ilrun` Tool

**Location**: `src/tools/ilrun/`

**Files Created**:

```
src/tools/ilrun/
├── main.cpp          # Thin wrapper
└── cli_compat.cpp    # CLI compatibility and help text
```

**ilrun Command Design**:

```bash
ilrun [options] <file.il>

Options:
  --trace[=il|src]               Enable execution tracing
  --stdin-from FILE              Redirect stdin from file
  --max-steps N                  Limit execution steps
  --bounds-checks                Enable runtime bounds checks
  --break LABEL|FILE:LINE        Set breakpoint
  --watch NAME                   Watch variable
  --count                        Show instruction counts
  --time                         Show execution time
  --dump-trap                    Show detailed trap diagnostics
  -h, --help                     Show this help message
  --version                      Show version information

Examples:
  ilrun program.il                      # Run IL program
  ilrun program.il --trace              # Run with tracing
  ilrun program.il --break main:10      # Debug with breakpoint
  ilrun program.il --count --time       # Performance profiling

Notes:
  - IL files must define func @main()
  - Use --trace=src for source-level tracing (requires debug info)
  - See 'ilrun --help-detailed' for debugging options
```

**Implementation**:

```cpp
// src/tools/ilrun/main.cpp
#include "tools/viper/cli.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    // Translate: ilrun <file.il> [opts] → ilc -run <file.il> [opts]
    std::vector<const char*> ilc_args = {"ilc", "-run"};
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            usage();
            return 0;
        }
        ilc_args.push_back(argv[i]);
    }

    return cmdRunIL(argc, argv);
}
```

**CMake Integration**:

```cmake
add_executable(ilrun
    tools/ilrun/main.cpp
    tools/ilrun/usage.cpp
    tools/viper/cmd_run_il.cpp  # Reuse existing logic
    tools/viper/cli.cpp
    tools/viper/break_spec.cpp)
set_target_properties(ilrun PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilrun)
target_link_libraries(ilrun
    PRIVATE
    viper_common_opts
    viper::il_full
    il_vm
    il_api
    il_tools_common)
install(TARGETS ilrun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

---

### 1.3: Refactor Existing `ilc` Tool (Optional, P1)

**Current State**: `ilc` does 5 things
**Desired State**: `ilc` focuses on IL → native compilation

**Options**:

**A. Keep `ilc` As-Is (Recommended for Phase 1)**

- Maintain full backwards compatibility
- No breaking changes
- Users gradually migrate to `vbasic`/`ilrun`

**B. Add Deprecation Warnings (Future)**

```cpp
if (cmd == "front") {
    std::cerr << "Warning: 'ilc front basic' is deprecated. Use 'vbasic' instead.\n";
}
```

**C. Simplify `ilc` (Far Future)**

- Remove `front` and `-run` subcommands
- Focus on IL compilation only
- Requires major version bump

---

## Phase 2: Documentation and Testing

### 2.1: Update Documentation

**Files to Modify**:

- `docs/getting-started.md` - Replace `viper front basic` examples with `vbasic`
- `docs/basic-language.md` - Update all command examples
- `docs/basic-reference.md` - Update code examples
- `README.md` - Update quickstart examples
- `CLAUDE.md` - Update build commands section

**New Documentation**:

- `docs/tools/vbasic.md` - Complete vbasic reference
- `docs/tools/ilrun.md` - Complete ilrun reference
- `docs/migration-guide.md` - ilc → vbasic migration

**Example Update**:

```markdown
<!-- BEFORE -->
./build/src/tools/viper/viper front basic -run examples/basic/ex1_hello_cond.bas

<!-- AFTER -->
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas

<!-- Or with shorthand if installed -->
vbasic examples/basic/ex1_hello_cond.bas
```

### 2.2: Test Coverage

**Unit Tests**:

- Argument parsing for `vbasic`
- Argument translation vbasic → ilc
- Help text formatting
- Error handling

**Integration Tests**:

```bash
# Test matrix
for test in examples/basic/*.bas; do
    echo "Testing: $test"

    # Old way (must still work)
    ./build/src/tools/viper/viper front basic -run "$test" > old_out.txt

    # New way
    ./build/src/tools/vbasic/vbasic "$test" > new_out.txt

    # Outputs must match
    diff old_out.txt new_out.txt || exit 1
done
```

**Golden Tests**:

```cmake
# Add to tests/CMakeLists.txt
add_test(NAME vbasic_hello
    COMMAND vbasic ${CMAKE_SOURCE_DIR}/examples/basic/ex1_hello_cond.bas)
set_tests_properties(vbasic_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello")
```

### 2.3: Install and PATH Setup

**CMake Install Rules** (already in place, just verify):

```cmake
install(TARGETS vbasic ilrun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

**User Setup Instructions**:

```bash
# After cmake --install build --prefix ~/.local
export PATH="$HOME/.local/bin:$PATH"

# Now available globally
vbasic script.bas
ilrun program.il
```

---

## Phase 3: Enhanced Features (P2)

### 3.1: Smart Compilation Pipeline (`vbasic -c`)

Currently, compiling BASIC → native requires 2 commands:

```bash
ilc front basic -emit-il game.bas > game.il
ilc codegen x64 -S game.il -o game
```

**Goal**: Single command

```bash
vbasic game.bas -c -o game
```

**Implementation**:

```cpp
// In vbasic main.cpp
if (compile_mode) {
    // Step 1: BASIC → IL (temp file)
    std::string temp_il = std::tmpnam(nullptr) + ".il";
    int result = compile_basic_to_il(source_path, temp_il, config);
    if (result != 0) return result;

    // Step 2: IL → native
    result = compile_il_to_native(temp_il, output_file, config);

    // Step 3: Cleanup
    std::filesystem::remove(temp_il);

    return result;
}
```

**Dependencies**:

- Link against `il_codegen_x86_64` library
- Reuse `cmd_codegen_x64` logic
- Handle platform detection (x64 only for now)

### 3.2: File Extension Auto-Detection (`ilc` Smart Mode)

**Goal**: `viper file.bas` → auto-run BASIC, `viper file.il` → auto-run IL

**Implementation**:

```cpp
// In ilc main.cpp, before subcommand dispatch
if (argc == 2) {
    std::string arg = argv[1];

    if (arg.ends_with(".bas")) {
        // Auto-dispatch to BASIC frontend
        const char* new_argv[] = {"ilc", "front", "basic", "-run", argv[1]};
        return cmdFrontBasic(1, const_cast<char**>(new_argv + 4));
    }

    if (arg.ends_with(".il")) {
        // Auto-dispatch to IL runner
        return cmdRunIL(1, argv + 1);
    }
}
```

**Pros**: Convenience for interactive use
**Cons**: Less explicit, harder to script reliably

**Decision**: Optional enhancement, document clearly

### 3.3: `ilopt` Tool

**Goal**: Rename `viper il-opt` → `ilopt`

**Implementation**:

```cpp
// src/tools/ilopt/main.cpp
#include "tools/viper/cli.hpp"

int main(int argc, char** argv) {
    return cmdILOpt(argc - 1, argv + 1);
}
```

**CMake**:

```cmake
add_executable(ilopt
    tools/ilopt/main.cpp
    tools/viper/cmd_il_opt.cpp
    tools/viper/cli.cpp)
target_link_libraries(ilopt PRIVATE ... il_transform ...)
install(TARGETS ilopt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

---

## Migration Strategy

### Backwards Compatibility

**Phase 1-2 (2-4 weeks)**:

- All old commands continue working
- Documentation shows new commands first, old commands second
- No deprecation warnings

**Phase 3-6 months**:

- Add soft deprecation notices
- Update all examples to new commands
- Community feedback period

**Phase 6-12 months**:

- Add `--deprecated` warning flag
- Plan for `ilc` simplification in major version

### Communication

**Changelog Entry**:

```markdown
## [Version X.Y.0] - 2025-MM-DD

### Added
- **New CLI tools for better user experience**
  - `vbasic` - Run and compile BASIC programs (`vbasic script.bas`)
  - `ilrun` - Execute IL programs (`ilrun program.il`)
  - Simpler, more intuitive commands matching industry standards (Python, Node.js, Ruby)

### Deprecated (Soft)
- `viper front basic -run` - Use `vbasic` instead
- `viper -run` - Use `ilrun` instead
- Old commands still work, but new commands are recommended

### Documentation
- Updated getting-started guide with new commands
- Added tool-specific documentation (docs/tools/)
- Created migration guide for existing users
```

---

## File Structure After Implementation

```
src/tools/
├── vbasic/                     # User-friendly BASIC tool (implemented)
│   ├── main.cpp
│   └── cli_compat.cpp
├── ilrun/                      # User-friendly IL runner (implemented)
│   ├── main.cpp
│   └── cli_compat.cpp
├── ilopt/                      # IL optimizer (P2: not yet created)
│   └── main.cpp
├── viper/                      # Main CLI tool
│   ├── main.cpp                # Main dispatcher
│   ├── cmd_run_il.cpp
│   ├── cmd_front_basic.cpp
│   ├── cmd_il_opt.cpp
│   ├── cmd_codegen_x64.cpp
│   └── cli.cpp
├── zia/                        # Zia language tool (added in v0.2.0)
│   ├── main.cpp
│   └── usage.cpp
├── il-verify/                  # Keep as-is (good naming)
├── il-dis/                     # Keep as-is (good naming)
├── basic-ast-dump/             # Keep as-is (dev tool)
└── basic-lex-dump/             # Keep as-is (dev tool)
```

---

## Implementation Checklist

### Phase 1: Core Wrappers (Week 1)

- [x] Create `src/tools/vbasic/` directory
- [x] Implement `vbasic/main.cpp` with argument translation
- [x] Implement `vbasic/usage.cpp` with help text
- [x] Add CMake target for `vbasic`
- [x] Build and test `vbasic` with existing examples
- [x] Create `src/tools/ilrun/` directory
- [x] Implement `ilrun/main.cpp`
- [x] Implement `ilrun/usage.cpp`
- [x] Add CMake target for `ilrun`
- [x] Build and test `ilrun` with IL programs
- [x] Verify install targets work correctly

### Phase 2: Documentation & Testing (Week 2)

- [ ] Update `docs/getting-started.md`
- [ ] Update `docs/basic-language.md`
- [ ] Update `README.md` quickstart
- [ ] Create `docs/tools/vbasic.md`
- [ ] Create `docs/tools/ilrun.md`
- [ ] Create `docs/migration-guide.md`
- [ ] Add integration tests for `vbasic`
- [ ] Add integration tests for `ilrun`
- [ ] Add golden tests to test suite
- [ ] Verify all existing tests still pass
- [ ] Update CLAUDE.md with new commands

### Phase 3: Enhanced Features (Week 3 - Optional)

- [ ] Implement `vbasic -c` compilation pipeline
- [ ] Add file extension auto-detection to `ilc`
- [ ] Create `ilopt` tool
- [ ] Add unit tests for new features
- [ ] Document advanced features
- [ ] Performance testing
- [ ] Error message improvements

---

## Risk Assessment

### Low Risk

- Creating new wrapper tools (no changes to existing code)
- Documentation updates
- Adding deprecation notices (non-breaking)

### Medium Risk

- Compilation pipeline (`vbasic -c`) - requires careful integration
- Auto-detection in `ilc` - could cause unexpected behavior

### High Risk

- Removing old `ilc` subcommands - breaking change, defer to major version

### Mitigation

- Extensive testing with existing examples
- Keep all old commands working
- Gradual migration with long deprecation period
- Feature flags for experimental features

---

## Success Metrics

### Usability

- [ ] New user can run BASIC program without reading documentation
- [ ] Command length reduced from 5 tokens to 2 tokens
- [ ] Help text clearly explains most common use cases

### Compatibility

- [ ] All existing `ilc` commands continue working
- [ ] All tests pass with new tools
- [ ] CI/CD builds successfully

### Adoption

- [ ] Documentation uses new commands throughout
- [ ] Examples updated to new commands
- [ ] Community feedback positive

---

## Open Questions

1. **Should `vbasic` default to run or compile?**
    - Recommendation: Run (matches Python, Node, Ruby)
    - Explicit `-c` flag for compilation

2. **Should `ilc` remain as monolithic tool?**
    - Recommendation: Yes for now, simplify in future major version
    - Add deprecation warnings first

3. **Should we create `ilc` symlink for backwards compat?**
    - Recommendation: Keep actual `ilc` binary, no symlinks needed

4. **What about Windows?**
    - Same approach works on Windows
    - CMake handles .exe extension automatically
    - PATH setup similar to Unix

---

## Next Steps

1. **Get approval** for overall approach
2. **Start Phase 1** with `vbasic` implementation
3. **Test with existing examples** to verify no regressions
4. **Iterate** based on feedback
5. **Document** as we go
6. **Release** incrementally (Phase 1 → Phase 2 → Phase 3)

---

## References

- Current viper implementation: `src/tools/viper/`
- CMake build config: `src/CMakeLists.txt` (vbasic around line 380, ilrun around line 428)
- Existing CLI helpers: `src/tools/viper/cli.hpp`, `cli.cpp`
- BASIC frontend: `src/tools/viper/cmd_front_basic.cpp`
- IL runner: `src/tools/viper/cmd_run_il.cpp`
- Zia tool: `src/tools/zia/`
