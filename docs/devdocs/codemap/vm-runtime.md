# CODEMAP: Virtual Machine

The Virtual Machine (`src/vm/`) interprets Viper IL with configurable dispatch strategies.

Last updated: 2026-01-15

## Overview

- **Total source files**: 57 (.hpp/.cpp)
- **Subdirectories**: debug/, ops/, ops/common/, ops/generated/

## Core Infrastructure

| File              | Purpose                                                                  |
|-------------------|--------------------------------------------------------------------------|
| `VM.cpp`          | Main interpreter: frame management, instruction dispatch, execution loop |
| `VM.hpp`          | VM class declarations                                                    |
| `VMInit.cpp`      | VM construction, function table setup, per-execution initialization      |
| `VMConfig.hpp`    | Configuration flags and constants (dispatch mode, limits)                |
| `VMConstants.hpp` | VM-wide constants and limits                                             |
| `VMContext.cpp`   | Execution context: function/block maps, global string tables             |
| `VMContext.hpp`   | VMContext declarations                                                   |
| `Runner.cpp`      | Simple facade for running modules with config                            |

## Dispatch Strategies

| File                  | Purpose                                               |
|-----------------------|-------------------------------------------------------|
| `DispatchStrategy.cpp`| Dispatch strategy selection (switch, table, threaded) |
| `DispatchStrategy.hpp`| Dispatch strategy declarations                        |
| `DispatchMacros.hpp`  | Dispatch macro definitions for handler generation     |

## Opcode Handler Infrastructure

| File                      | Purpose                                          |
|---------------------------|--------------------------------------------------|
| `OpHandlers.cpp`          | Opcode handler table construction and accessor   |
| `OpHandlers.hpp`          | Opcode handler declarations                      |
| `OpHandlerUtils.cpp`      | Shared helpers: `storeResult`, slot manipulation |
| `OpHandlerUtils.hpp`      | Handler utility declarations                     |
| `OpHandlerAccess.hpp`     | Restricted access layer for slot read/write      |
| `OpcodeHandlerHelpers.hpp`| Common handler helper declarations               |
| `OpHelpers.cpp`           | Frequently used helper routines                  |

## Opcode Handler Headers by Domain

| File                    | Purpose                           |
|-------------------------|-----------------------------------|
| `OpHandlers_Control.hpp`| Control flow handler declarations |
| `OpHandlers_Float.hpp`  | Floating-point handler declarations|
| `OpHandlers_Int.hpp`    | Integer handler declarations      |
| `OpHandlers_Memory.hpp` | Memory handler declarations       |

## Opcode Handler Implementations

| File                 | Purpose                                     |
|----------------------|---------------------------------------------|
| `control_flow.cpp`   | Branch, call, return, trap handlers         |
| `control_flow.hpp`   | Control flow handler declarations           |
| `fp_ops.cpp`         | Floating-point arithmetic and comparisons   |
| `int_ops_arith.cpp`  | Integer arithmetic and bitwise operations   |
| `int_ops_cmp.cpp`    | Integer comparisons                         |
| `int_ops_convert.cpp`| Type conversions and casts                  |
| `mem_ops.cpp`        | Memory operations: alloca, load, store, gep |
| `IntOpSupport.hpp`   | Overflow/divide-by-zero helpers             |

## Specialized Operations (`ops/`)

| File                   | Purpose                    |
|------------------------|----------------------------|
| `Op_BranchSwitch.cpp`  | Switch instruction handler |
| `Op_CallRet.cpp`       | Call and return handlers   |
| `Op_TrapEh.cpp`        | Trap and exception handling|

## Common Operations (`ops/common/`)

| File           | Purpose                    |
|----------------|----------------------------|
| `Branching.cpp`| Shared branching utilities |
| `Branching.hpp`| Branching declarations     |

## Generated Files (`ops/generated/`)

| File              | Purpose                  |
|-------------------|--------------------------|
| `HandlerTable.hpp`| Generated dispatch table |
| `OpSchema.hpp`    | Generated opcode schema  |

## Runtime Bridge

| File                 | Purpose                                    |
|----------------------|--------------------------------------------|
| `RuntimeBridge.cpp`  | C runtime invocation with trap diagnostics |
| `RuntimeBridge.hpp`  | Runtime bridge declarations                |
| `Marshal.cpp`        | Slot to C ABI type marshalling             |
| `Marshal.hpp`        | Marshal declarations                       |
| `err_bridge.cpp`     | Runtime error to VM diagnostic adapter     |
| `err_bridge.hpp`     | Error bridge declarations                  |

## Trap Handling

| File               | Purpose                             |
|--------------------|-------------------------------------|
| `Trap.cpp`         | Trap kinds and reporting            |
| `Trap.hpp`         | Trap declarations                   |
| `TrapInvariants.hpp`| Trap invariant checking utilities  |

## Tail Call Optimization

| File     | Purpose                        |
|----------|--------------------------------|
| `tco.cpp`| Tail call optimization support |
| `tco.hpp`| TCO declarations               |

## Diagnostics

| File            | Purpose                       |
|-----------------|-------------------------------|
| `DiagFormat.cpp`| Diagnostic message formatting |
| `DiagFormat.hpp`| DiagFormat declarations       |

## String Handling

| File                  | Purpose                                |
|-----------------------|----------------------------------------|
| `ViperStringHandle.hpp`| String handle type for runtime interop|

## Threading

| File               | Purpose                                       |
|--------------------|-----------------------------------------------|
| `ThreadsRuntime.cpp`| Runtime threading support and synchronization|

## Debug Infrastructure (`debug/`)

| File              | Purpose                         |
|-------------------|---------------------------------|
| `Debug.cpp`       | Breakpoint and watch management |
| `VMDebug.cpp`     | Debug hook implementation       |
| `VM_DebugUtils.cpp`| Debug helper utilities         |
| `DebugScript.cpp` | Scripted debugger actions       |
| `Trace.cpp`       | Execution tracing and logging   |
