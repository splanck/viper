---
status: active
audience: developers
last-updated: 2025-12-09
---

# Viper VM — Architecture & Implementation Guide

Comprehensive guide to the Viper Virtual Machine (VM), a stack-based bytecode interpreter that executes Viper IL programs. This document covers the VM's design philosophy, architecture, execution model, and source code organization.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture & Design Philosophy](#architecture--design-philosophy)
3. [Key Components](#key-components)
4. [Execution Model](#execution-model)
5. [Dispatch Strategies](#dispatch-strategies)
6. [Memory Model](#memory-model)
7. [Exception & Trap Handling](#exception--trap-handling)
8. [Runtime Integration](#runtime-integration)
9. [Debug & Tracing](#debug--tracing)
10. [Source Code Guide](#source-code-guide)
11. [Performance Features](#performance-features)

---

## Overview

### What is the Viper VM?

The Viper VM is a **stack-based bytecode interpreter** that executes programs written in Viper's Intermediate Language (IL). It serves as the primary execution engine for the Viper toolchain, providing:

- **Deterministic execution** of IL programs
- **Debugging and tracing** capabilities
- **Exception handling** with structured error recovery
- **Runtime function calls** via the RuntimeBridge
- **Multiple dispatch strategies** optimized for different use cases

### Key Characteristics

| Feature | Description |
|---------|-------------|
| **Architecture** | Stack-based interpreter with SSA register file |
| **Dispatch** | Pluggable (function table, switch, computed goto) |
| **Memory** | Frame-local operand stack with explicit allocation via `alloca` (64KB default) |
| **Error Handling** | Structured exception handling with trap metadata |
| **Debugging** | Built-in breakpoints, stepping, and tracing |
| **Performance** | Tail-call optimization, opcode counting, inline caching |

---

## Architecture & Design Philosophy

### Core Principles

The VM design prioritizes several key principles:

1. **Modularity**: Pluggable dispatch strategies allow optimization without changing the core interpreter
2. **Inspectability**: Comprehensive tracing and debugging support for all execution paths
3. **Correctness**: Deterministic execution with explicit error handling
4. **Performance**: Multiple optimization layers (TCO, inline caching, threaded dispatch)
5. **Simplicity**: Clean separation between interpretation, runtime, and tooling

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      VM (Interpreter)                    │
├─────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Dispatch   │  │   Opcode     │  │    Debug     │ │
│  │   Strategy   │──│   Handlers   │──│   Control    │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
│                           │                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │    Frame     │  │    Trap      │  │    Trace     │ │
│  │   Manager    │  │   Handler    │  │     Sink     │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  IL Module   │    │   Runtime    │    │   External   │
│  (readonly)  │    │    Bridge    │    │   Callbacks  │
└──────────────┘    └──────────────┘    └──────────────┘
```

### Component Relationships

- **VM** owns: Dispatch driver, trace sink, debug controller, function/string maps
- **VM** borrows: IL Module (must outlive VM), optional debug script
- **Frame** owns: Register file, operand stack, exception handlers
- **RuntimeBridge** provides: C runtime function invocation, trap reporting

---

## Key Components

### 1. VM Class (`src/vm/VM.hpp`)

The main interpreter class that orchestrates execution:

```cpp
class VM {
    // Module and configuration
    const il::core::Module& mod;        // IL module (non-owning)
    TraceSink tracer;                   // Trace output
    DebugCtrl debug;                    // Breakpoint controller
    DispatchDriver* dispatchDriver;     // Pluggable dispatch

    // Execution state
    uint64_t instrCount;                // Executed instructions
    uint64_t maxSteps;                  // Step limit (0 = unlimited)

    // Caching and lookup
    std::unordered_map<std::string, const Function*> fnMap;
    std::unordered_map<std::string, rt_string> strMap;
    std::unordered_map<std::string, rt_string> inlineLiteralCache;

    // Exception handling
    TrapState lastTrap;
    TrapToken trapToken;
};
```

**Key responsibilities:**
- Module initialization and function lookup
- Dispatch strategy selection and lifecycle
- String literal caching and lifetime management
- Trap context tracking and formatting
- Debug breakpoint coordination

### 2. Frame (`src/vm/VM.hpp`)

Represents a single function activation record:

```cpp
struct Frame {
    const Function* func;                          // Active function
    std::vector<Slot> regs;                       // SSA register file
    // Operand stack (alloca). Default capacity is 64KB (kDefaultStackSize).
    std::vector<uint8_t> stack;
    size_t sp;                                    // Stack pointer
    std::vector<std::optional<Slot>> params;      // Pending block parameters
    std::vector<HandlerRecord> ehStack;           // Exception handlers
    VmError activeError;                          // Current error payload
    ResumeState resumeState;                      // Resumption metadata
};
```

**Key responsibilities:**
- SSA value storage in register file
- Stack allocation via `alloca` instruction (bump `sp` within `stack`)
- Block parameter passing
- Exception handler stack management
- Resume state for error recovery

### 3. Slot (`src/vm/VM.hpp`)

Tagged union for runtime values:

```cpp
union Slot {
    int64_t i64;      // Integer value
    double f64;       // Floating-point value
    void* ptr;        // Generic pointer
    rt_string str;    // Runtime string handle
};
```

All IL values are represented as `Slot` during execution. Type safety is enforced by the IL verifier and opcode handlers.

### 4. Dispatch Drivers

Pluggable strategies for instruction fetch-decode-execute:

- **FnTableDispatchDriver**: Uses function pointer table lookup
- **SwitchDispatchDriver**: Uses switch statement (inline handlers)
- **ThreadedDispatchDriver**: Uses computed goto (GCC/Clang only)

Selected at VM construction via `DispatchKind` enum.

### 5. Opcode Handlers (`src/vm/OpHandlers*.hpp`)

Category-organized functions that implement IL instructions:

- **Control** (`OpHandlers_Control.hpp`): `br`, `cbr`, `call`, `ret`, `switch`
- **Integer** (`OpHandlers_Int.hpp`): `add`, `mul`, `icmp_*`, `scmp_*`
- **Float** (`OpHandlers_Float.hpp`): `fadd`, `fmul`, `fcmp_*`
- **Memory** (`OpHandlers_Memory.hpp`): `alloca`, `load`, `store`, `gep`

Each handler has signature:
```cpp
ExecResult handler(VM& vm, Frame& fr, const Instr& in,
                  const BlockMap& blocks,
                  const BasicBlock*& bb, size_t& ip);
```

---

## Execution Model

### Execution Flow

```
1. VM::run()
   ├─ Lookup "main" function
   └─ Call execFunction()
      ├─ setupFrame() → Initialize registers, stack, block map
      └─ runFunctionLoop()
         └─ dispatchDriver->run()
            └─ Loop:
               ├─ selectInstruction() → Fetch next instruction
               ├─ executeOpcode() → Dispatch to handler
               ├─ handleDebugBreak() → Check breakpoints
               └─ finalizeDispatch() → Update IP, check for return
```

### Instruction Execution Cycle

For each instruction:

1. **Select**: `selectInstruction()` identifies the next instruction
2. **Trace**: `traceInstruction()` emits trace output if enabled
3. **Execute**: Handler updates frame state and returns `ExecResult`
4. **Finalize**: `finalizeDispatch()` processes jumps/returns

### Control Flow

**Basic blocks:**
- Execution starts at the `entry` block
- Terminators (`ret`, `br`, `cbr`, `switch`) transfer control
- Block parameters are transferred before entering a new block

**Function calls:**
- `call` opcode pushes a new frame onto the execution stack
- Arguments are evaluated and passed as block parameters
- Return value is propagated back via `Slot`

**Tail calls:**
- Detected via `call.tail` attribute
- Reuses current frame instead of allocating new one
- Eliminates stack growth for recursive functions

---

## Dispatch Strategies

### 1. Function Table Dispatch (Default)

Uses a compile-time generated array of function pointers:

```cpp
// Generated in HandlerTable.hpp
static const OpcodeHandlerTable& getOpcodeHandlers() {
    static OpcodeHandlerTable table = {
        &handleAdd,    // Opcode::Add
        &handleSub,    // Opcode::Sub
        // ... one entry per opcode
    };
    return table;
}
```

**Pros:** Simple, portable, easy to debug
**Cons:** Indirect call overhead per instruction

### 2. Switch Dispatch

Expands all handlers inline within a giant switch statement:

```cpp
while (true) {
    Opcode op = fetchOpcode(state);
    switch (op) {
        case Opcode::Add: inline_handle_Add(state); break;
        case Opcode::Sub: inline_handle_Sub(state); break;
        // ... case per opcode
    }
}
```

**Pros:** Better branch prediction, potential for inlining
**Cons:** Large code size, longer compile time

### 3. Threaded Dispatch (GCC/Clang)

Uses computed goto with label addresses:

```cpp
static void* kOpLabels[] = { &&LBL_Add, &&LBL_Sub, /* ... */ };

#define DISPATCH_TO(opcode) goto *kOpLabels[opcode]

for (;;) {
    DISPATCH_TO(fetchOpcode(state));

    LBL_Add: inline_handle_Add(state); DISPATCH_TO(fetchNext());
    LBL_Sub: inline_handle_Sub(state); DISPATCH_TO(fetchNext());
    // ... label per opcode
}
```

**Pros:** Fastest dispatch, direct jump to handlers
**Cons:** Compiler-specific, large code size

### Selecting a Dispatch Strategy

The dispatch strategy is selected at VM construction via environment variable:

```bash
# Use function table dispatch (portable, moderate performance)
VIPER_DISPATCH=table ./ilc -run program.il

# Use switch statement dispatch (good cache locality)
VIPER_DISPATCH=switch ./ilc -run program.il

# Use threaded dispatch (fastest, requires GCC/Clang)
VIPER_DISPATCH=threaded ./ilc -run program.il
```

**Default:** Threaded if supported (`VIPER_THREADING_SUPPORTED=1`), otherwise Switch.

### Shared Dispatch Loop

All strategies share a common dispatch loop (`runSharedDispatchLoop`) that handles:
- State reset per iteration (`beginDispatch`)
- Instruction selection (`selectInstruction`)
- Debug hooks (`VIPER_VM_DISPATCH_BEFORE/AFTER`)
- Trap handling for threaded dispatch
- Finalization and exit conditions (`finalizeDispatch`)

The strategy only implements `executeInstruction()` to map opcodes to handlers.

### Dispatch Loop Performance Optimizations

The shared dispatch loop includes several optimizations:

1. **Cached strategy properties**: `requiresTrapCatch()` and `handlesFinalizationInternally()`
   are cached once at loop entry to avoid virtual call overhead per instruction.

2. **Branch hints**: `[[likely]]` and `[[unlikely]]` attributes guide code layout for hot paths.

3. **Zero-cost hooks**: `VIPER_VM_DISPATCH_BEFORE` and `VIPER_VM_DISPATCH_AFTER` macros
   compile to nothing when disabled. When opcode counting is enabled (`VIPER_VM_OPCOUNTS=1`),
   the counter increment is gated by a runtime flag (`config.enableOpcodeCounts`).

4. **Efficient polling**: `VIPER_VM_DISPATCH_AFTER` only increments the poll counter when
   polling is active (`interruptEveryN > 0`), avoiding wasted cycles in the common case.

### Instrumentation Hooks

The VM provides compile-time configurable hooks for profiling and embedding:

```cpp
// In VMConfig.hpp - define before including VM headers
#define VIPER_VM_DISPATCH_BEFORE(ST, OPCODE) \
    do { myProfiler.onInstruction(ST, OPCODE); } while(0)

#define VIPER_VM_DISPATCH_AFTER(ST, OPCODE) \
    do { myProfiler.afterInstruction(ST, OPCODE); } while(0)
```

**Predefined behavior:**
- `VIPER_VM_DISPATCH_BEFORE`: Increments per-opcode counters when `VIPER_VM_OPCOUNTS=1`
- `VIPER_VM_DISPATCH_AFTER`: Calls poll callback every N instructions if configured

### Per-Opcode Counters

Enable compile-time opcode counting:

```cpp
#define VIPER_VM_OPCOUNTS 1  // Default: enabled
```

Access counters at runtime:
```cpp
vm.resetOpcodeCounts();
vm.run();
auto counts = vm.getOpcodeCounts();  // Returns array<uint64_t, kNumOpcodes>
for (auto [opcode, count] : vm.getNonZeroOpcodeCounts()) {
    std::cout << opcodeMnemonic(opcode) << ": " << count << "\n";
}
```

Disable via environment: `VIPER_ENABLE_OPCOUNTS=0`

### Benchmark Harness

The `ilc bench` command provides a built-in benchmark harness for comparing dispatch strategies:

```sh
# Run all three strategies with 3 iterations each
ilc bench program.il

# Run a specific strategy with 5 iterations
ilc bench program.il -n 5 --table

# Run multiple files with JSON output
ilc bench file1.il file2.il --json

# Limit execution with max-steps
ilc bench program.il --max-steps 1000000
```

**Output format (text):**
```
BENCH <file> <strategy> instr=<N> time_ms=<T> insns_per_sec=<R>
```

**Output format (JSON):**
```json
[
  {
    "file": "program.il",
    "strategy": "table",
    "success": true,
    "instructions": 7000004,
    "time_ms": 3618.33,
    "insns_per_sec": 1934596,
    "return_value": 0
  }
]
```

**Strategy selection flags:**
- `--table`: Run only FnTable dispatch
- `--switch`: Run only Switch dispatch
- `--threaded`: Run only Threaded dispatch
- (default): Run all three strategies

**Example benchmark IL programs** are available in `examples/il/benchmarks/`:
- `arith_stress.il`: Heavy arithmetic workload
- `branch_stress.il`: Branch-heavy control flow
- `call_stress.il`: Function call overhead testing
- `mixed_stress.il`: Combined workload
- `string_stress.il`: String operations

---

## Memory Model

### Register File

Each frame has an SSA register file sized to the function's register count:

```cpp
frame.regs.resize(func->registerCount);
```

Registers are indexed by SSA value ID. Each register is written once and read many times (SSA property).

### Operand Stack

Each frame has an operand stack for `alloca` allocations. The default capacity
is 64KB (`Frame::kDefaultStackSize`):

```cpp
std::vector<uint8_t> stack; // capacity ~= 64KB by default
size_t sp = 0;  // Stack pointer in bytes
```

**Usage:**
- `alloca N` allocates N bytes on the stack
- Returns a `ptr` pointing into `stack` at offset `sp`
- Stack grows upward (`sp` increases)
- No explicit deallocation (frame-scoped)

**Limits:**
- Fixed 1KB size per frame
- Overflow causes trap
- Suitable for small temporaries (strings, small arrays)

### String Handles

Strings are managed by the runtime as opaque handles (`rt_string`):

- **Global strings**: Cached in `strMap`, lifetime = VM lifetime
- **Inline literals**: Cached in `inlineLiteralCache`, supports embedded NULs
- **Runtime strings**: Created by runtime functions, reference-counted

The VM releases all cached handles in its destructor.

---

## Exception & Trap Handling

### Trap Types

Defined in `Trap.hpp`:

```cpp
enum class TrapKind {
    DivideByZero,     // Integer division by zero
    Overflow,         // Arithmetic overflow
    InvalidCast,      // Type conversion failure
    DomainError,      // Semantic violation
    Bounds,           // Array bounds check
    FileNotFound,     // File I/O error
    EOF,              // End of file
    IOError,          // Generic I/O failure
    InvalidOperation, // Invalid state transition
    RuntimeError      // Catch-all
};
```

### Exception Handler Stack

Each frame maintains an exception handler stack:

```cpp
struct HandlerRecord {
    const BasicBlock* handler;  // Handler block
    size_t ipSnapshot;          // IP to restore
};

std::vector<HandlerRecord> ehStack;
```

**IL instructions:**
- `eh.push label handler` — Push handler onto stack
- `eh.pop` — Pop handler from stack
- `eh.entry` — Mark entry point of handler block

### Trap Dispatch

When a trap occurs:

1. **Capture context**: Function, block, instruction, source location
2. **Search for handler**: Walk `ehStack` for active handler
3. **Dispatch or unwind**:
   - **Handler found**: Jump to handler block, set `activeError`
   - **No handler**: Throw `TrapDispatchSignal` to unwind stack
4. **Resume**: Handler uses `resume.same`, `resume.next`, or `resume.label`

### Structured Error Payload

```cpp
struct VmError {
    TrapKind kind;     // Error classification
    int32_t code;      // Secondary code
    uint64_t ip;       // Instruction pointer
    int32_t line;      // Source line (-1 if unknown)
};
```

Accessible via:
- `trap.kind` — Read current trap kind
- `err.get_kind %e` — Extract kind from error value
- `err.get_code %e` — Extract code from error value

---

## Runtime Integration

### RuntimeBridge (`src/vm/RuntimeBridge.hpp`)

Adapter between VM and C runtime library:

```cpp
class RuntimeBridge {
    static Slot call(RuntimeCallContext& ctx,
                    const std::string& name,
                    const std::vector<Slot>& args,
                    ...);

    static void trap(TrapKind kind, const std::string& msg, ...);
    static const RuntimeCallContext* activeContext();
};
```

**Call flow:**
1. IL `call @Viper.Console.PrintI64(args)` instruction (or legacy `@rt_*` alias)
2. Handler evaluates arguments into `Slot` vector
3. `RuntimeBridge::call()` looks up function descriptor by canonical name
4. C function is invoked with marshalled arguments
5. Return value is marshalled back to `Slot`

**Note:** The runtime supports both canonical `@Viper.*` names and legacy `@rt_*` aliases when built with `-DVIPER_RUNTIME_NS_DUAL=ON`.

### Runtime Call Context

Tracks active runtime call for trap diagnostics:

```cpp
struct RuntimeCallContext {
    SourceLoc loc;                      // Call site location
    std::string function;               // Calling IL function
    std::string block;                  // Calling block
    const RuntimeDescriptor* descriptor; // Runtime function
    Slot* argBegin;                     // Argument array
    size_t argCount;                    // Argument count
};
```

Populated before each runtime call, cleared after.

### External Function Registry

Custom functions can be registered:

```cpp
struct ExternDesc {
    std::string name;
    void* ptr;
    // ... signature metadata
};

RuntimeBridge::registerExtern(desc);
```

Enables embedding applications to extend the runtime.

---

## Debug & Tracing

### Trace Sink

Configurable output for instruction tracing:

```cpp
struct TraceConfig {
    bool enabled;           // Enable tracing
    bool ilTrace;           // Trace IL instructions
    bool boolTrace;         // Trace boolean values
    bool srcTrace;          // Trace source locations
};
```

Output format:
```
[func:block:ip] opcode operands → result
```

### Debug Controller

Manages breakpoints and stepping:

```cpp
class DebugCtrl {
    // Breakpoints
    void addBreakLabel(std::string label);
    void addBreakSrcLine(std::string file, int line);
    void clearBreaks();

    // Stepping
    void requestStep(uint64_t count);
    bool shouldBreak(/* context */);
};
```

**Breakpoint types:**
- Block label breakpoints
- Source line breakpoints
- Step count breakpoints

### Debug Scripting

Optional command script for automated debugging:

```cpp
class DebugScript {
    virtual Action onBreakpoint(VM& vm, Frame& fr) = 0;
};
```

Allows programmatic control of execution (continue, step, inspect, etc.).

### Memory Watches

Monitor memory access for debugging:

```cpp
debug.addMemWatch(addr, size, "tag");
auto hits = debug.drainMemWatchEvents();
```

Tracks reads/writes to specific memory ranges.

---

## Source Code Guide

### Directory Structure

```
src/vm/
├── VM.hpp/cpp                  # Main VM class and core interpreter logic
├── VMContext.hpp/cpp           # Execution context helpers
├── VMConfig.hpp                # Build configuration
├── VMConstants.hpp             # VM constants
├── Runner.cpp                  # Public API facade
├── VMInit.cpp                  # VM initialization
│
├── OpHandlers.hpp/cpp          # Handler aggregation and table generation
├── OpHandlerUtils.hpp/cpp      # Handler utility functions
├── OpHandlerAccess.hpp         # Handler access utilities
├── OpHandlers_Control.hpp      # Control flow handlers
├── OpHandlers_Int.hpp          # Integer arithmetic handlers
├── OpHandlers_Float.hpp        # Float arithmetic handlers
├── OpHandlers_Memory.hpp       # Memory operation handlers
├── IntOpSupport.hpp            # Integer operation support
│
├── DispatchStrategy.hpp/cpp    # Pluggable dispatch strategies
│
├── ops/
│   ├── Op_CallRet.cpp          # Call/return implementation
│   ├── Op_BranchSwitch.cpp     # Branch/switch implementation
│   ├── Op_TrapEh.cpp           # Trap/exception handling
│   ├── common/                 # Shared helpers
│   ├── schema/                 # Opcode schema definitions
│   └── generated/              # Generated dispatch tables
│
├── RuntimeBridge.hpp/cpp       # Runtime integration
├── Marshal.hpp/cpp             # Value marshalling
│
├── Trap.hpp/cpp                # Trap definitions and formatting
├── err_bridge.hpp/cpp          # Error bridge helpers
│
├── control_flow.hpp/cpp        # Control flow utilities
├── tco.hpp/cpp                 # Tail-call optimization
│
├── int_ops_arith.cpp           # Integer arithmetic implementations
├── int_ops_cmp.cpp             # Integer comparison implementations
├── int_ops_convert.cpp         # Integer conversion implementations
├── fp_ops.cpp                  # Floating-point implementations
├── mem_ops.cpp                 # Memory operation implementations
│
└── debug/                      # Debug and tracing subsystem
    └── *.cpp                   # Debug controller, trace, scripting
```

### Key Files by Functionality

**Core Interpreter:**
- `VM.hpp`, `VM.cpp` — Main interpreter class
- `VMContext.hpp` — Shared execution helpers
- `Runner.cpp` — Public API facade

**Dispatch:**
- `VM.cpp` — Dispatch driver implementations
- `ops/generated/` — Generated dispatch tables

**Opcode Handlers:**
- `OpHandlers*.hpp` — Handler declarations by category
- `ops/Op_*.cpp` — Complex handler implementations
- `int_ops_*.cpp`, `fp_ops.cpp`, `mem_ops.cpp` — Arithmetic implementations

**Exception Handling:**
- `Trap.hpp`, `Trap.cpp` — Trap types and formatting
- `err_bridge.hpp` — Error bridge integration
- `ops/Op_TrapEh.cpp` — Exception handler opcodes

**Runtime Integration:**
- `RuntimeBridge.hpp`, `RuntimeBridge.cpp` — C runtime adapter
- `Marshal.hpp`, `Marshal.cpp` — Value marshalling

**Debugging:**
- `debug/Debug.cpp` — Breakpoint management
- `debug/Trace.cpp` — Trace output formatting
- `debug/VMDebug.cpp` — Debug integration

---

## Performance Features

### Tail-Call Optimization

Enabled by default (`VIPER_VM_TAILCALL`):

```cpp
// Detect tail call
if (instr.isTailCall()) {
    // Reuse current frame
    return executeTailCall(fr, callee, args);
}
```

Eliminates stack growth for recursive functions.

### Opcode Counting

Compile-time flag (`VIPER_VM_OPCOUNTS`):

```cpp
#if VIPER_VM_OPCOUNTS
std::array<uint64_t, kNumOpcodes> opCounts_;
#endif
```

Tracks execution count per opcode for profiling.

**API:**
```cpp
const auto& counts = vm.opcodeCounts();
auto top = vm.topOpcodes(10);  // Top 10 opcodes
vm.resetOpcodeCounts();
```

### Execution Context Optimization

The VM execution context has been optimized to minimize overhead on the hot path:

**ExecState-based dispatch:** The dispatch macros (`VIPER_VM_DISPATCH_BEFORE`, `VIPER_VM_DISPATCH_AFTER`) use `ExecState` directly instead of `VMContext`, avoiding an extra indirection per instruction:

```cpp
// Hot path uses ExecState directly
VIPER_VM_DISPATCH_BEFORE(state, opcode);  // state is ExecState&

// ExecState.config includes all per-instruction configuration
struct PollConfig {
    uint32_t interruptEveryN;
    std::function<bool(VM&)> pollCallback;
    bool enableOpcodeCounts;  // Direct access for opcode counting
};
```

**VMContext for external APIs:** The `VMContext` wrapper is still used for external APIs (`stepOnce`, `fetchOpcode`, `handleTrapDispatch`) to provide a stable interface, but it's not required on the per-instruction hot path.

### Execution Stack Pre-allocation

The execution stack (`execStack`) tracks active `ExecState` pointers for trap unwinding and debugging:

```cpp
// Pre-allocated to kExecStackInitialCapacity (64) in VM constructor
std::vector<ExecState*> execStack;

// Unified RAII guard for stack management
struct ExecStackGuard {
    VM& vm;
    ExecState* state;
    ExecStackGuard(VM& vmRef, ExecState& stRef) noexcept;
    ~ExecStackGuard() noexcept;
};
```

**Optimizations:**
- Pre-allocated capacity eliminates heap allocation for typical call depths
- Unified `ExecStackGuard` in VM.hpp removes code duplication
- `noexcept` specifiers enable compiler optimizations

### Inline String Literal Cache

Caches runtime handles for string literals:

```cpp
std::unordered_map<std::string_view, ViperStringHandle, ...> inlineLiteralCache;
```

**Optimizations:**
- Pre-populated during VM construction by scanning all ConstStr operands in the module
- Fast path uses `find()` for pre-populated strings (common case)
- Fallback `try_emplace` only for edge cases (dynamically generated strings)
- Eliminates repeated allocation and map insertion for frequently used literals

### Switch Cache

Memoizes switch dispatch data:

```cpp
struct SwitchCache {
    std::unordered_map<int32_t, const BasicBlock*> caseMap;
    const BasicBlock* defaultTarget;
};
```

Amortizes switch table construction across iterations.

### Host Polling

Configurable interrupt callback:

```cpp
vm.setPollConfig(everyN, [](VM& vm) {
    // Host logic (UI events, etc.)
    return true;  // Continue execution
});
```

Allows embedding applications to maintain responsiveness.

---

## Best Practices

### For VM Developers

1. **Opcode Handlers**: Keep handlers simple and delegate to helper functions
2. **Error Handling**: Use `RuntimeBridge::trap()` for runtime errors
3. **Caching**: Consider caching hot lookups (strings, functions, blocks)
4. **Debugging**: Add trace points for complex operations
5. **Testing**: Write unit tests for each handler

### For IL Generators

1. **SSA Form**: Ensure proper SSA (single assignment per register)
2. **Terminators**: Every block must end with a terminator
3. **Type Safety**: Match operand types to instruction signatures
4. **Exception Handlers**: Properly nest `eh.push`/`eh.pop` pairs
5. **Stack Usage**: Keep `alloca` sizes within frame limits (~64KB by default)

### For Embedders

1. **Configuration**: Choose appropriate dispatch strategy
2. **Polling**: Set reasonable interrupt frequency
3. **Externs**: Register custom functions before execution
4. **Tracing**: Enable tracing for debugging, disable for production
5. **Error Handling**: Catch and handle `TrapDispatchSignal` if needed

---

## Further Reading

**Viper Documentation:**
- **[IL Guide](il-guide.md)** — IL specification and semantics
- **[IL Reference](il-reference.md)** — Complete opcode catalog
- **[Getting Started](getting-started.md)** — Build and run Viper

**Developer Documentation** (in `/devdocs`):**
- `runtime-vm.md` — VM and runtime internals (detailed)
- `architecture.md` — Overall system architecture
- `specs/errors.md` — Error handling specification
- `vm-*.md` — VM-specific topics (stepping, profiling, etc.)

**Source Code:**
- `src/vm/` — VM implementation
- `src/runtime/` — C runtime library
- `src/tests/vm/` — VM unit tests
# Viper VM — Performance Tuning and Benchmarking

This guide summarizes runtime tuning knobs for the VM and how to benchmark dispatch performance across modes.

## Dispatch Modes

- Env `VIPER_DISPATCH`:
  - `table`: function-table dispatch via `executeOpcode`
  - `switch`: inline switch dispatch with generated handlers
  - `threaded`: computed goto (if built with `VIPER_VM_THREADED`)

- Env `VIPER_ENABLE_OPCOUNTS` (default on): enable per-opcode execution counters. You can query counts via `Runner::opcodeCounts()` or the `--count` flag in `ilc -run`.

- Env `VIPER_INTERRUPT_EVERY_N`: periodically invoke a host callback every N instructions (see `RunConfig::interruptEveryN`).

## Switch Backend Heuristics

Switch dispatch selects a backend per instruction. Heuristics can be tuned via env:

- `VIPER_SWITCH_DENSE_MAX_RANGE` (default `4096`): maximum value range to consider a dense jump table.
- `VIPER_SWITCH_DENSE_MIN_DENSITY` (default `0.60`): minimum case density for dense backend.
- `VIPER_SWITCH_HASH_MIN_CASES` (default `64`): minimum number of cases before hashing is considered.
- `VIPER_SWITCH_HASH_MAX_DENSITY` (default `0.15`): maximum density to prefer hashed backend.

If `VIPER_SWITCH_MODE` is set to `dense|sorted|hashed|linear|auto`, it overrides the heuristic for all instructions.

## Benchmarking

Use the helper script to compare dispatch performance across modes:

- Script: `scripts/vm_benchmark.sh`
- Output: appends to `bugs/vm_benchmarks.md`

Environment variables:

- `RUNS_PER_CASE` (default `3`): number of runs per (mode, program) pair.
- `IL_GLOB` (default `examples/il/*.il`): glob for IL programs to benchmark (relative to repo root).
- `ILC_BIN`: optional path to `ilc`; otherwise auto-detected under `build/`.

Each invocation writes a timestamped section header and a per-row timestamp, along with averages and min/max timings, the actual dispatch kind, and instruction counts extracted from `--count` and `--time` summaries.

Example:

```
RUNS_PER_CASE=5 IL_GLOB='src/tests/il/e2e/*.il' scripts/vm_benchmark.sh
```

The script sets `VIPER_DEBUG_VM=1` so the VM prints the resolved dispatch kind, and `VIPER_ENABLE_OPCOUNTS=1` to capture counts.
### Concurrency Model

Each `VM` instance is single‑threaded: only one thread may execute within a given
instance at a time. To parallelize, create one VM per thread. The active VM is
tracked via a thread‑local guard (see `ActiveVMGuard` in `src/vm/VMContext.*`), which
binds the VM and its runtime context for the duration of execution. In debug builds,
re‑entering an already active VM on the same thread triggers an assertion.
