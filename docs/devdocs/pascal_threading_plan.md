# Pascal Threading Support Implementation Plan

## Overview

This document outlines the implementation plan for adding threading support to Viper Pascal. Unlike BASIC, Pascal already has the `@` (address-of) operator lexed and parsed - it's just rejected at semantic analysis. This makes the implementation simpler.

## Current State

### What Already Works
- **Lexer**: `@` token (`TokenKind::At`) is recognized
- **Parser**: `@expr` is parsed into `AddressOfExpr` node
- **AST**: `AddressOfExpr` and `DereferenceExpr` nodes exist
- **Type System**: `ProcedureTypeNode` and `FunctionTypeNode` are defined
- **Runtime**: Thread, Monitor, SafeI64 classes are already registered

### What's Blocked
The semantic analyzer explicitly rejects address-of operations:
```pascal
// SemanticAnalyzer_Expr.cpp lines 1299-1313
PasType SemanticAnalyzer::typeOfAddressOf(AddressOfExpr &expr)
{
    error(expr, "address-of operator (@) is not supported in Viper Pascal v0.1; use classes instead");
    return PasType::unknown();
}
```

## Proposed Syntax

Pascal will use the standard Pascal `@` operator for obtaining procedure addresses:

```pascal
program ThreadDemo;

procedure WorkerProc;
begin
    WriteLn('Worker running!');
end;

var
    t: Viper.Threads.Thread;
begin
    t := Viper.Threads.Thread.Start(@WorkerProc, nil);
    t.Join();
end.
```

### Syntax Elements
| Element | Description |
|---------|-------------|
| `@ProcName` | Get address of procedure/function |
| `nil` | Null pointer for context argument |
| `Viper.Threads.Thread` | Thread class from runtime |
| `Viper.Threads.SafeI64` | Thread-safe integer |
| `Viper.Threads.Monitor` | Synchronization primitive |

## Implementation Phases

### Phase 1: Enable Address-Of for Procedures

**Goal**: Allow `@ProcedureName` to compile and produce a function pointer.

**Files to Modify**:

1. **`src/frontends/pascal/SemanticAnalyzer_Expr.cpp`**
   - Modify `typeOfAddressOf()` to accept procedure/function identifiers
   - Return `PasType::pointer()` or a procedure-pointer type
   - Validate the target is a valid procedure/function name

2. **`src/frontends/pascal/SemanticAnalyzer.hpp`**
   - Add helper method to look up procedure by name
   - May need `isProcedure(name)` check

**Implementation**:
```cpp
PasType SemanticAnalyzer::typeOfAddressOf(AddressOfExpr &expr)
{
    // Check if operand is a simple identifier (procedure name)
    if (auto *name = dynamic_cast<NameExpr*>(expr.operand.get()))
    {
        // Look up as procedure/function
        if (auto *proc = findProcedure(name->name))
        {
            // Return pointer type - procedures are ptr in IL
            return PasType::pointer();
        }
    }

    // For now, only support procedure addresses
    error(expr, "address-of operator (@) only supports procedure names");
    return PasType::unknown();
}
```

### Phase 2: Enable Nil for Null Pointers

**Goal**: Ensure `nil` works as a null pointer argument.

**Files to Check**:
- `src/frontends/pascal/SemanticAnalyzer_Expr.cpp` - `nil` handling
- `src/frontends/pascal/Lowerer_Expr.cpp` - `nil` lowering

**Note**: Pascal likely already supports `nil` - verify it lowers to `Value::null()`.

### Phase 3: IL Lowering for Address-Of

**Goal**: Generate correct IL for `@ProcedureName`.

**Files to Modify**:

1. **`src/frontends/pascal/Lowerer_Expr.cpp`**
   - Add case for `ExprKind::AddressOf`
   - Emit `Value::global(procName)` for procedure addresses

**Implementation**:
```cpp
LowerResult Lowerer::lowerAddressOf(const AddressOfExpr &expr)
{
    // Get the procedure name from the operand
    if (auto *name = dynamic_cast<const NameExpr*>(expr.operand.get()))
    {
        std::string procName = name->name;
        // Mangle the name if needed
        std::string mangledName = mangleProcedureName(procName);
        // Return global address of the procedure
        return LowerResult{Value::global(mangledName), Type(Type::Kind::Ptr)};
    }

    // Should not reach here after semantic analysis
    error(expr, "invalid address-of operand");
    return LowerResult{Value::null(), Type(Type::Kind::Ptr)};
}
```

### Phase 4: Runtime Class Access Verification

**Goal**: Verify threading classes are accessible from Pascal.

**Test Cases**:
```pascal
program TestThreading;
var
    s: Viper.Threads.SafeI64;
begin
    s := Viper.Threads.SafeI64.New(42);
    WriteLn(s.Get());
    s.Set(100);
    WriteLn(s.Get());
end.
```

**Expected**: Should already work since runtime classes are registered.

### Phase 5: Full Threading Test

**Goal**: Complete threading demonstration.

**Test Program**:
```pascal
program ThreadTest;

var
    counter: Viper.Threads.SafeI64;

procedure IncrementWorker;
var
    i: Integer;
begin
    for i := 1 to 100 do
        counter.Add(1);
end;

var
    t1, t2: Viper.Threads.Thread;
begin
    counter := Viper.Threads.SafeI64.New(0);

    t1 := Viper.Threads.Thread.Start(@IncrementWorker, nil);
    t2 := Viper.Threads.Thread.Start(@IncrementWorker, nil);

    t1.Join();
    t2.Join();

    WriteLn('Final count: ', counter.Get());
    { Should print 200 }
end.
```

## File Change Summary

| File | Changes |
|------|---------|
| `SemanticAnalyzer_Expr.cpp` | Enable `@` for procedure names |
| `Lowerer_Expr.cpp` | Add `lowerAddressOf()` implementation |
| `tests/runtime_sweep/pascal/threads.pas` | New test file |

## Differences from BASIC Implementation

| Aspect | BASIC | Pascal |
|--------|-------|--------|
| **Syntax** | `ADDRESSOF ProcName` | `@ProcName` |
| **Keyword** | New keyword added | Existing `@` operator |
| **Null pointer** | `NOTHING` | `nil` |
| **Lexer changes** | Yes (new keyword) | No |
| **Parser changes** | Yes (new expression) | No |
| **AST changes** | Yes (new node) | No |
| **Semantic changes** | Yes | Yes (remove rejection) |
| **Lowering changes** | Yes | Yes |

## Risk Assessment

### Low Risk
- Runtime classes already registered and working
- `@` operator already lexed and parsed
- AST nodes already exist
- Type system supports procedure types

### Medium Risk
- Procedure name mangling must match IL function names
- May need to handle unit-qualified procedure names

### Mitigation
- Start with simple non-unit procedures
- Add unit qualification support incrementally
- Follow existing Pascal name mangling patterns

## Success Criteria

1. `@ProcedureName` compiles without errors
2. Thread.Start accepts procedure pointer
3. Multi-threaded program executes correctly
4. All existing Pascal tests continue to pass

## Estimated Complexity

| Phase | Effort | Files |
|-------|--------|-------|
| Phase 1 | Low | 1-2 |
| Phase 2 | Minimal | 0-1 |
| Phase 3 | Low | 1 |
| Phase 4 | Verification | 0 |
| Phase 5 | Test creation | 1 |

**Total**: ~3-4 files to modify, significantly simpler than BASIC implementation.

## API Reference

### Viper.Threads.Thread

| Method | Signature | Description |
|--------|-----------|-------------|
| `Start` | `(ptr, ptr) -> obj` | Start thread with entry point and context |
| `Join` | `() -> void` | Wait for thread to complete |
| `TryJoin` | `() -> boolean` | Non-blocking join attempt |
| `JoinFor` | `(i64) -> boolean` | Join with timeout (ms) |

| Property | Type | Description |
|----------|------|-------------|
| `Id` | `i64` | Unique thread identifier |
| `IsAlive` | `boolean` | Whether thread is running |

### Viper.Threads.SafeI64

| Method | Signature | Description |
|--------|-----------|-------------|
| `New` | `(i64) -> obj` | Create with initial value |
| `Get` | `() -> i64` | Read current value |
| `Set` | `(i64) -> void` | Write new value |
| `Add` | `(i64) -> i64` | Atomic add, return new value |
| `CompareExchange` | `(i64, i64) -> i64` | CAS operation |

### Viper.Threads.Monitor

| Method | Signature | Description |
|--------|-----------|-------------|
| `Enter` | `(obj) -> void` | Acquire lock on object |
| `TryEnter` | `(obj) -> boolean` | Try to acquire lock |
| `Exit` | `(obj) -> void` | Release lock |
| `Wait` | `(i64) -> boolean` | Wait with timeout |
| `Notify` | `() -> void` | Wake one waiter |
| `NotifyAll` | `() -> void` | Wake all waiters |

## Example Programs

### Basic Thread Creation
```pascal
program BasicThread;

procedure Worker;
begin
    WriteLn('Hello from thread!');
end;

var
    t: Viper.Threads.Thread;
begin
    t := Viper.Threads.Thread.Start(@Worker, nil);
    t.Join();
    WriteLn('Thread finished');
end.
```

### Thread-Safe Counter
```pascal
program SafeCounter;

var
    counter: Viper.Threads.SafeI64;

procedure Increment;
begin
    counter.Add(1);
end;

var
    t1, t2: Viper.Threads.Thread;
begin
    counter := Viper.Threads.SafeI64.New(0);

    t1 := Viper.Threads.Thread.Start(@Increment, nil);
    t2 := Viper.Threads.Thread.Start(@Increment, nil);

    t1.Join();
    t2.Join();

    WriteLn('Count: ', counter.Get()); { Prints 2 }
end.
```

### Producer-Consumer (Advanced)
```pascal
program ProducerConsumer;

var
    buffer: Viper.Threads.SafeI64;
    done: Viper.Threads.SafeI64;

procedure Producer;
var
    i: Integer;
begin
    for i := 1 to 10 do
    begin
        buffer.Set(i);
        Viper.Threads.Thread.Sleep(10);
    end;
    done.Set(1);
end;

procedure Consumer;
begin
    while done.Get() = 0 do
    begin
        WriteLn('Value: ', buffer.Get());
        Viper.Threads.Thread.Sleep(15);
    end;
end;

var
    prod, cons: Viper.Threads.Thread;
begin
    buffer := Viper.Threads.SafeI64.New(0);
    done := Viper.Threads.SafeI64.New(0);

    prod := Viper.Threads.Thread.Start(@Producer, nil);
    cons := Viper.Threads.Thread.Start(@Consumer, nil);

    prod.Join();
    cons.Join();
end.
```
