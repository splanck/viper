# CODEMAP: VM Runtime

- **src/vm/control_flow.cpp**

  Implements interpreter handlers for branch, call, return, and trap opcodes, centralizing control-flow manipulation inside `OpHandlers`. A shared helper moves execution to successor blocks, seeds block parameters, and flags jumps in the `ExecResult`, while other handlers evaluate call operands and route extern invocations through the `RuntimeBridge`. The logic keeps frame state consistent so the main VM loop can honour returns and traps without manual bookkeeping. Dependencies include `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, `vm/RuntimeBridge.hpp`, and IL core definitions for `BasicBlock`, `Function`, `Instr`, and `Value`.

- **src/vm/debug/Debug.cpp**

  Implements the VM's debugging controller responsible for breakpoints, watches, and source lookup. It normalizes file paths to compare breakpoints across host platforms, interns block labels for quick lookup, and tracks recently triggered locations to avoid duplicate stops. The controller also emits watch output when observed variables change, integrating with the interpreter loop's store callbacks. Dependencies include `vm/Debug.hpp`, IL core instruction/block definitions, the diagnostics source management helpers, and standard containers and streams from `<vector>`, `<string>`, `<unordered_map>`, and `<iostream>`.

- **src/vm/Debug.hpp**

  Exposes the `il::vm::DebugCtrl` interface that the VM uses to register and query breakpoints. The class manages collections of block-level and source-level breakpoints, normalizes user-supplied paths, and remembers the last triggered source location to support coalescing. It also maintains a map of watched variables so the interpreter can emit change notifications with type-aware formatting. Dependencies include `support/string_interner.hpp`, `support/symbol.hpp`, forward declarations of IL core `BasicBlock`/`Instr`, and standard headers `<optional>`, `<string_view>`, `<unordered_map>`, `<unordered_set>`, and `<vector>`.

- **src/vm/debug/DebugScript.cpp**

  Parses debugger automation scripts so the VM can replay predetermined actions during execution. The constructor reads a text file, mapping commands like `continue`, `step`, and `step N` into queued `DebugAction` records while logging unexpected lines to `stderr` as `[DEBUG]` diagnostics. Helper methods let callers append step requests programmatically and retrieve the next action, defaulting to Continue when the queue empties. Dependencies include `vm/DebugScript.hpp` along with standard `<fstream>`, `<iostream>`, and `<sstream>` utilities for file handling.

- **src/vm/DebugScript.hpp**

  Declares the `DebugScript` FIFO wrapper used by the VM debugger to provide scripted actions. It defines the `DebugActionKind` enum, the simple `DebugAction` POD, and member functions for loading scripts, enqueuing steps, polling actions, and checking emptiness. The class stores actions in a `std::queue`, making consumption order explicit for the interpreter's debug loop. Dependencies cover standard headers `<cstdint>`, `<queue>`, `<string>`, with behavior implemented in `DebugScript.cpp`.

- **src/vm/fp_ops.cpp**

  Contains the VM’s floating-point arithmetic and comparison handlers, factoring operand evaluation into templated helpers that write back through `ops::storeResult`. Each opcode relies on host IEEE-754 semantics for doubles, ensuring NaNs and infinities propagate while comparisons return canonical 0/1 truth values. The helpers isolate frame mutation so arithmetic handlers remain side-effect free aside from updating the destination slot. Dependencies include `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, and IL instruction metadata.

- **src/vm/int_ops_arith.cpp**, **src/vm/int_ops_cmp.cpp**, **src/vm/int_ops_convert.cpp**

  Split integer opcode handlers into domain-specific translation units. Arithmetic covers addition, subtraction, multiplication, division, bitwise logic, and index guards, sharing overflow/divide-by-zero helpers via `IntOpSupport.hpp`. Comparison logic resides alongside predicate helpers while conversions handle range-checked casts, boolean normalisation, and integer-to-float operations. All variants reuse `ops::storeResult` and the shared support templates to preserve two’s complement semantics and trap behaviour.

- **src/vm/mem_ops.cpp**

  Implements the VM opcode handlers for memory-centric IL instructions such as `alloca`, `load`, `store`, `gep`, and constant-string helpers. Each routine evaluates operands through the VM, enforces stack bounds or null checks, and mirrors type-directed load/store semantics so runtime behaviour matches the IL specification. Store handlers also funnel writes through the debug controller, and pointer math reuses shared slot helpers to keep execution deterministic. Dependencies cover `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, `vm/RuntimeBridge.hpp`, IL core instruction/type definitions, and standard `<cassert>` and `<cstring>` facilities.

- **src/vm/OpHandlerUtils.cpp**

- **src/vm/debug/Trace.cpp**

  Implements the tracing sink and utilities that render per‑instruction execution logs based on `TraceConfig`, used by drivers and tests to inspect VM behaviour.

- **src/vm/debug/VM_DebugUtils.cpp**, **src/vm/debug/VMDebug.cpp**

  Implementation files providing debug helpers, breakpoint resolution glue, and integration points backing `DebugCtrl` and VM debug actions.

- **src/vm/control_flow.hpp**

  Declares shared helpers used by branch/call/return/trap handlers to perform block transitions and seed parameters.

- **src/vm/OpHandlerAccess.hpp**

  Exposes a restricted internal access layer to VM slots and operand evaluation shared by opcode handlers.

- **src/vm/Marshal.cpp**

  Implements marshalling of VM slot values to C ABI types for runtime calls; pairs with the header already mapped.

- **src/vm/Trap.cpp**

  Implements trap construction and reporting helpers declared in `Trap.hpp`.

- **src/vm/OpHandlers_Control.hpp**, **src/vm/OpHandlers_Float.hpp**, **src/vm/OpHandlers_Int.hpp**, **src/vm/OpHandlers_Memory.hpp**

  Declarations for per‑domain opcode handler groupings used by the VM dispatch.

- **src/vm/ops/common/Branching.cpp**, **src/vm/ops/common/Branching.hpp**

  Shared branching helpers for generated and hand‑written handlers.

- **src/vm/ops/Op_BranchSwitch.cpp**, **src/vm/ops/Op_CallRet.cpp**, **src/vm/ops/Op_TrapEh.cpp**

  Hand‑written opcode handlers complementing the generated table for complex control‑flow and trap/exception behaviour.

- **src/vm/ops/generated/OpSchema.hpp**

  Generated schema header describing opcode operand/result shapes consumed by op generators and validators.
  Provides shared helper routines used by opcode implementations to manipulate VM state. The `storeResult` utility grows the register vector as needed before writing an instruction's destination slot, ensuring the interpreter never reads uninitialized registers. Keeping the logic centralized prevents handlers from duplicating resize and assignment code and maintains invariant checks in one location. Dependencies include `vm/OpHandlerUtils.hpp` and IL instruction definitions from `il/core/Instr.hpp`.

- **src/vm/OpHandlerUtils.hpp**

  Declares reusable helpers available to opcode handlers, currently exposing `storeResult` within the `il::vm::detail::ops` namespace. The header wires in `vm/VM.hpp` so helpers can operate on frames and slot unions without additional includes. Housing the declarations separately lets opcode sources include lightweight utilities without dragging in the entire handler table. Dependencies include `vm/VM.hpp` and IL instruction forward declarations, with implementations in `OpHandlerUtils.cpp`.

- **src/vm/OpHandlers.cpp**

  Builds the opcode-dispatch table by translating metadata emitted from `il/core/Opcode.def` into concrete handler function pointers. Each opcode’s declared VM dispatch kind maps to a corresponding method on `OpHandlers`, allowing the VM to remain declarative and auto-updated when new opcodes are added. The table is materialized lazily and cached for reuse across VM instances to avoid recomputation. It depends on `vm/OpHandlers.hpp`, opcode metadata headers (`Opcode.hpp`, `OpcodeInfo.hpp`), and the generated definitions in `Opcode.def`.

- **src/vm/OpHandlers.hpp**

  Advertises the `il::vm::detail::OpHandlers` struct whose static methods implement each IL opcode the interpreter supports. Every handler receives the active `VM`, frame, decoded instruction, and block map so it can evaluate operands, mutate registers, branch, or trigger runtime calls, with shared plumbing factored into `OpHandlerUtils`. The header also exposes `getOpcodeHandlers`, the accessor that returns the lazily built dispatch table consumed by the main interpreter loop. It depends on `vm/VM.hpp` for `Frame`, `ExecResult`, and block metadata, which in turn pull in IL instruction types and the runtime bridge contracts.

- **src/vm/RuntimeBridge.cpp**

  Provides the dynamic bridge that allows the VM to invoke C runtime helpers while preserving precise trap diagnostics. On first use it materializes a dispatch table mapping runtime symbol names to thin adapters that unpack `Slot` arguments, call the underlying C functions, and marshal results back into VM slots. Before each call it records the current `SourceLoc`, function, and block names so the exported `vm_trap` hook can report accurate context if the callee aborts. The implementation relies on `RuntimeBridge.hpp`, the VM's `Slot` type, generated runtime headers such as `rt_math.h` and `rt_random.h`, and standard library utilities like `<unordered_map>` and `<sstream>`.

- **src/vm/RuntimeBridge.hpp**

  Declares the static `RuntimeBridge` adapters that let the VM invoke the C runtime and surface traps. The `call` helper marshals evaluated slot arguments into the runtime ABI while threading source locations, function names, and block labels for diagnostics. A companion `trap` entry centralizes error reporting so runtime failures share consistent messaging paths. Dependencies include `rt.hpp`, `support/source_location.hpp`, the forward-declared VM `Slot` union, and standard `<string>` and `<vector>` containers.

- **src/vm/Trace.cpp**

  Implements deterministic tracing facilities for the VM, emitting IL-level or source-level logs depending on the configured mode. Each step event walks the owning function's blocks to locate the instruction pointer, formats operands with locale-stable helpers, and optionally loads source snippets using the `SourceManager`. It supports Windows console quirks, writes to `stderr` with flush control, and ensures floating-point text matches serializer output. Dependencies include `Trace.hpp`, IL core instruction/value types, `support/source_manager.hpp`, `<locale>`, and `<filesystem>/<fstream>`.

- **src/vm/Trace.hpp**

  Declares the tracing configuration and sink used by the VM to emit execution logs. `TraceConfig` models the available modes and can carry a `SourceManager` pointer to support source-aware traces. `TraceSink` exposes an `onStep` callback that the interpreter invokes with each instruction and active frame so the implementation can render deterministic text. Dependencies include IL instruction forward declarations, `il::support::SourceManager`, the VM `Frame` type, and the standard headers consumed by `Trace.cpp`.

- **src/vm/VM.cpp**

  Drives execution of IL modules by locating `main`, preparing frames, and stepping through instructions with debug and tracing hooks. The interpreter evaluates SSA values into runtime slots, routes opcodes through the handler table, and manages control-flow transitions within the execution loop. It coordinates with the runtime bridge for traps/globals and with debug controls to pause or resume execution as needed. Dependencies include `vm/VM.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Value`, `Opcode`), the opcode handler table, tracing/debug infrastructure, and the `RuntimeBridge` C ABI.

- **src/vm/VM.hpp**

  Defines the VM's public interface, including the slot union, execution frame container, and the interpreter class itself. The header documents how `VM` wires together tracing, debugging, and opcode dispatch, exposing the `ExecResult` structure and handler table typedefs that drive the interpreter loop. It also details constructor knobs like step limits and debug scripts so embedding tools understand lifecycle expectations. Nested data members describe ownership semantics for modules, runtime strings, and per-function lookup tables. Dependencies include the VM debug and trace headers, IL opcode/type forward declarations, the runtime `rt.hpp` bridge, and standard containers such as `<vector>`, `<array>`, and `<unordered_map>`.

- **src/vm/VMDebug.cpp**

  Implements the debugging hooks that sit inside `VM::handleDebugBreak` and `VM::processDebugControl`. The code coordinates label and source line breakpoints through `DebugCtrl`, honours scripted stepping via `DebugScript`, and enforces global step limits before handing control back to the interpreter loop. When a break fires it logs contextual information, syncs pending block parameters into registers, and returns sentinel slots that pause execution. Dependencies include `vm/VM.hpp`, IL core block/function/instruction headers, the shared `support::SourceManager`, `vm/DebugScript.hpp`, and the standard `<filesystem>`, `<iostream>`, and `<string>` libraries.

- **src/vm/VMInit.cpp**

  Handles VM construction and per-function execution state initialization. The constructor captures module references, seeds lookup tables for functions and global strings, and wires tracing plus debugging facilities provided through `TraceConfig` and `DebugCtrl`. Supporting routines `setupFrame` and `prepareExecution` allocate register files, map block labels, and stage entry parameters so the main interpreter loop can run without additional setup. It depends on `VM.hpp`, IL core structures (`Module`, `Function`, `BasicBlock`, `Global`), runtime helpers like `rt_const_cstr`, and standard containers along with `<cassert>`.

- **src/vm/control_flow.hpp**

  Declares interfaces and small helpers used by the control‑flow handlers to perform block transitions and seed block parameters.

- **src/vm/IntOpSupport.hpp**

  Declares overflow and divide‑by‑zero helpers plus typed operand decoding utilities shared by integer opcode handlers.

- **src/vm/OpHandlerAccess.hpp**

  Exposes a restricted internal access layer that lets handlers read/write VM slot values and evaluate operands uniformly.

- **src/vm/OpHelpers.cpp**

  Implements small, frequently used VM helper routines not tied to a specific opcode domain (trap emission, slot utilities, etc.).

- **src/vm/Marshal.hpp**, **src/vm/Marshal.cpp**

  Declares and implements marshalling between IL `Value`/slot representations and C runtime ABI types used by the runtime bridge.

- **src/vm/err_bridge.hpp**, **src/vm/err_bridge.cpp**

  Small adapter layer that funnels runtime `vm_trap` calls into the VM's diagnostic system with consistent formatting/context.

- **src/vm/Trap.hpp**, **src/vm/Trap.cpp**

  Declares and implements trap kinds and reporting helpers used across the VM to signal error conditions to the embedding tools.

- **src/vm/VMConfig.hpp**

  Declares configuration flags and constants that control VM build/runtime behaviour (e.g., threaded dispatch availability).

- **src/vm/VMContext.hpp**, **src/vm/VMContext.cpp**

  Declares and implements the VM’s execution context bookkeeping (function/block maps, global string tables) used during init and execution.

- **src/vm/Runner.cpp**

  Implements a simple façade to run a module with provided config and query instruction counts and last trap; pairs with the public `include/viper/vm/VM.hpp`.

- **include/viper/vm/VM.hpp**

  Public header exposing a lightweight `Runner` façade and run helpers around the VM, suitable for tools embedding the interpreter without internals.

- **include/viper/vm/debug/Debug.hpp**

  Public header exposing debugger configuration and tracing types used to configure VM instances from tools.

- **include/viper/vm/internal/OpHelpers.hpp**

  Public inline helpers that provide typed operand load/store shims over VM slots for consumers that need to implement custom handlers or utilities.

- **src/vm/ops/schema/ops.yaml**

  Declarative opcode schema consumed by generator scripts to produce handler tables and inline dispatch macros; documents opcode→handler mapping.

- Generated VM opcode tables (src/vm/ops/generated/*) are maintained as committed includes; no build-time generator required.

  Code generation script that reads the opcode schema and emits handler tables and inline switch/threaded dispatch includes under `generated/`.

- **src/vm/ops/generated/HandlerTable.hpp**

  Generated header defining the compiled opcode→handler dispatch table consulted by the VM at runtime.

- **src/vm/TargetX64.cpp**

  (If present) target‑specific VM glue; otherwise omitted.
