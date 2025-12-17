# CODEMAP: Virtual Machine

The Virtual Machine (`src/vm/`) interprets Viper IL with configurable dispatch strategies.

## Core Infrastructure

| File                | Purpose                                                                  |
|---------------------|--------------------------------------------------------------------------|
| `VM.hpp/cpp`        | Main interpreter: frame management, instruction dispatch, execution loop |
| `VMInit.cpp`        | VM construction, function table setup, per-execution initialization      |
| `VMConfig.hpp`      | Configuration flags and constants (dispatch mode, limits)                |
| `VMConstants.hpp`   | VM-wide constants and limits                                             |
| `VMContext.hpp/cpp` | Execution context: function/block maps, global string tables             |
| `Runner.cpp`        | Simple facade for running modules with config                            |

## Dispatch Strategies

| File                       | Purpose                                               |
|----------------------------|-------------------------------------------------------|
| `DispatchStrategy.hpp/cpp` | Dispatch strategy selection (switch, table, threaded) |
| `OpHandlers.hpp/cpp`       | Opcode handler table construction and accessor        |
| `OpHandlerUtils.hpp/cpp`   | Shared helpers: `storeResult`, slot manipulation      |
| `OpHandlerAccess.hpp`      | Restricted access layer for slot read/write           |
| `OpcodeHandlerHelpers.hpp` | Common handler helper declarations                    |
| `OpHelpers.cpp`            | Frequently used helper routines                       |

## Opcode Handlers by Domain

| File                     | Purpose                                     |
|--------------------------|---------------------------------------------|
| `OpHandlers_Control.hpp` | Control flow handler declarations           |
| `OpHandlers_Float.hpp`   | Floating-point handler declarations         |
| `OpHandlers_Int.hpp`     | Integer handler declarations                |
| `OpHandlers_Memory.hpp`  | Memory handler declarations                 |
| `control_flow.hpp/cpp`   | Branch, call, return, trap handlers         |
| `fp_ops.cpp`             | Floating-point arithmetic and comparisons   |
| `int_ops_arith.cpp`      | Integer arithmetic and bitwise operations   |
| `int_ops_cmp.cpp`        | Integer comparisons                         |
| `int_ops_convert.cpp`    | Type conversions and casts                  |
| `mem_ops.cpp`            | Memory operations: alloca, load, store, gep |
| `IntOpSupport.hpp`       | Overflow/divide-by-zero helpers             |

## Specialized Operations (`ops/`)

| File                             | Purpose                     |
|----------------------------------|-----------------------------|
| `ops/Op_BranchSwitch.cpp`        | Switch instruction handler  |
| `ops/Op_CallRet.cpp`             | Call and return handlers    |
| `ops/Op_TrapEh.cpp`              | Trap and exception handling |
| `ops/common/Branching.hpp/cpp`   | Shared branching utilities  |
| `ops/generated/HandlerTable.hpp` | Generated dispatch table    |
| `ops/generated/OpSchema.hpp`     | Generated opcode schema     |

## Runtime Bridge

| File                    | Purpose                                    |
|-------------------------|--------------------------------------------|
| `RuntimeBridge.hpp/cpp` | C runtime invocation with trap diagnostics |
| `Marshal.hpp/cpp`       | Slot to C ABI type marshalling             |
| `err_bridge.hpp/cpp`    | Runtime error to VM diagnostic adapter     |
| `Trap.hpp/cpp`          | Trap kinds and reporting                   |
| `tco.hpp/cpp`           | Tail call optimization support             |

## Debug Infrastructure (`debug/`)

| File                      | Purpose                         |
|---------------------------|---------------------------------|
| `debug/Debug.cpp`         | Breakpoint and watch management |
| `debug/VMDebug.cpp`       | Debug hook implementation       |
| `debug/VM_DebugUtils.cpp` | Debug helper utilities          |
| `debug/DebugScript.cpp`   | Scripted debugger actions       |
| `debug/Trace.cpp`         | Execution tracing and logging   |

## Public Headers (`include/viper/vm/`)

| File                | Purpose                                           |
|---------------------|---------------------------------------------------|
| `VM.hpp`            | Public VM surface and runner for embedding        |
| `OpcodeNames.hpp`   | Human-readable opcode names for diagnostics/tools |
| `RuntimeBridge.hpp` | Public bridge types used to invoke C runtime      |
