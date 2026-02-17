# BASIC Threading Implementation Plan

**Status**: Draft
**Date**: 2025-01-21

---

## Executive Summary

This document outlines a comprehensive plan to add full threading support to the Viper BASIC frontend. The runtime infrastructure is complete; this plan covers the language syntax design, parser changes, semantic analysis, lowering to IL, and testing.

**Estimated Scope**: ~20 files modified/created, 4 implementation phases

---

## 1. Design Decisions

### 1.1 Syntax Choice: Method-Based OOP

BASIC will use the same OOP pattern as other `Viper.*` classes. Threading will be accessed via:

```basic
' Thread creation
DIM t AS Thread
t = Thread.Start(AddressOf WorkerSub)
t = Thread.Start(AddressOf WorkerWithArg, myData)

' Thread operations
t.Join()
t.Join(5000)           ' Timeout in milliseconds
IF t.TryJoin() THEN ...
IF t.IsAlive THEN ...
DIM id AS Long = t.Id

' Static methods
Thread.Sleep(1000)
Thread.Yield()
DIM current AS Long = Thread.CurrentId
```

### 1.2 Function Pointer Syntax: ADDRESSOF

Introduce `ADDRESSOF` keyword to obtain function pointers:

```basic
SUB Worker()
    PRINT "In thread"
END SUB

SUB WorkerWithArg(arg AS Ptr)
    DIM data AS MyClass = CAST(MyClass, arg)
    PRINT data.Name
END SUB

' Usage
DIM funcPtr AS Ptr = AddressOf Worker
DIM t AS Thread = Thread.Start(AddressOf Worker)
```

**Rationale**:
- `ADDRESSOF` is familiar from VB6/VBA
- Consistent with existing BASIC conventions
- Clear semantic intent

### 1.3 Data Passing: Ptr Type with CAST

```basic
CLASS WorkerData
    PUBLIC Value AS Integer
END CLASS

SUB Worker(arg AS Ptr)
    DIM data AS WorkerData = CAST(WorkerData, arg)
    PRINT data.Value
END SUB

DIM data AS NEW WorkerData
data.Value = 42
DIM t AS Thread = Thread.Start(AddressOf Worker, data AS Ptr)
```

### 1.4 Synchronization Primitives

```basic
' Monitor (Mutex)
DIM lock AS NEW Monitor
lock.Enter()
' critical section
lock.Exit()

' With timeout
IF lock.TryEnter(1000) THEN
    ' got lock
    lock.Exit()
END IF

' Wait/Pause (condition variable semantics)
lock.Enter()
WHILE NOT ready
    lock.Wait()
WEND
lock.Exit()

' From another thread
lock.Enter()
ready = TRUE
lock.Pause()   ' Wake one waiter
lock.Exit()
```

### 1.5 Atomic Variables

```basic
' SafeI64 for atomic counters
DIM counter AS SafeI64 = SafeI64.New(0)
counter.Add(1)
DIM val AS Long = counter.Get()
counter.Set(100)

' Compare-and-swap
DIM old AS Long = counter.CompareExchange(expected, newValue)
```

---

## 2. Implementation Phases

### Phase 1: Lexer and Parser Foundation

**Goal**: Parse `ADDRESSOF` expressions and recognize threading classes

**Files to Modify**:

| File | Changes |
|------|---------|
| `src/frontends/basic/TokenKinds.def` | Add `ADDRESSOF` keyword |
| `src/frontends/basic/Lexer.cpp` | Tokenize `ADDRESSOF` |
| `src/frontends/basic/AST.hpp` | Add `AddressOfExpr` node |
| `src/frontends/basic/Parser_Expr.cpp` | Parse `ADDRESSOF identifier` |
| `src/frontends/basic/AstPrinter.cpp` | Dump `AddressOfExpr` for debugging |

**New AST Node**:
```cpp
// In AST.hpp
class AddressOfExpr : public Expr {
public:
    std::string targetName;      // Name of SUB/FUNCTION
    SourceLoc loc;

    AddressOfExpr(std::string name, SourceLoc loc)
        : targetName(std::move(name)), loc(loc) {}
};
```

**Lexer Addition** (TokenKinds.def):
```cpp
KEYWORD(ADDRESSOF, "ADDRESSOF")
```

**Parser Logic** (Parser_Expr.cpp):
```cpp
std::unique_ptr<Expr> Parser::parseAddressOfExpr() {
    SourceLoc loc = currentToken().loc;
    consume(Token::ADDRESSOF);

    if (currentToken().kind != Token::IDENTIFIER) {
        error("Expected procedure name after ADDRESSOF");
        return nullptr;
    }

    std::string name = currentToken().text;
    advance();

    return std::make_unique<AddressOfExpr>(std::move(name), loc);
}
```

**Deliverables**:
- [x] `ADDRESSOF Worker` parses to `AddressOfExpr`
- [x] AST dump shows correct structure
- [x] Parser tests pass

---

### Phase 2: Threading Class Registration

**Goal**: Register `Viper.Thread`, `Viper.Monitor`, `Viper.SafeI64` in OopIndex

**Files to Modify/Create**:

| File | Changes |
|------|---------|
| `src/frontends/basic/RuntimeNames.hpp` | New: Threading class name constants |
| `src/frontends/basic/LowerRuntime.cpp` | New: Threading class registration logic |
| `src/frontends/basic/OopIndex.cpp` | Register threading classes |
| `src/frontends/basic/BuiltinRegistry.cpp` | Add threading builtins |
| `src/frontends/basic/builtin_registry.inc` | Threading builtin entries |

**Class Definitions**:

```cpp
// ThreadingClasses.hpp
namespace basic::runtime {

void registerThreadingClasses(OopIndex &index);

// Class metadata
struct ThreadClassInfo {
    static constexpr const char* QualifiedName = "Viper.Thread";

    // Static methods
    static constexpr MethodSig Start_0 = {
        .name = "Start",
        .params = {{"entry", Type::Ptr}},
        .returnType = Type::Ptr,  // Thread handle
        .isStatic = true
    };

    static constexpr MethodSig Start_1 = {
        .name = "Start",
        .params = {{"entry", Type::Ptr}, {"arg", Type::Ptr}},
        .returnType = Type::Ptr,
        .isStatic = true
    };

    static constexpr MethodSig Sleep = {
        .name = "Sleep",
        .params = {{"ms", Type::I64}},
        .returnType = Type::Void,
        .isStatic = true
    };

    static constexpr MethodSig Yield = {
        .name = "Yield",
        .params = {},
        .returnType = Type::Void,
        .isStatic = true
    };

    static constexpr MethodSig CurrentId = {
        .name = "CurrentId",
        .params = {},
        .returnType = Type::I64,
        .isStatic = true
    };

    // Instance methods
    static constexpr MethodSig Join_0 = {
        .name = "Join",
        .params = {},
        .returnType = Type::Void,
        .isStatic = false
    };

    static constexpr MethodSig Join_1 = {
        .name = "Join",
        .params = {{"timeoutMs", Type::I64}},
        .returnType = Type::Bool,
        .isStatic = false
    };

    static constexpr MethodSig TryJoin = {
        .name = "TryJoin",
        .params = {},
        .returnType = Type::Bool,
        .isStatic = false
    };

    static constexpr MethodSig IsAlive = {
        .name = "IsAlive",
        .params = {},
        .returnType = Type::Bool,
        .isStatic = false
    };

    static constexpr MethodSig Id = {
        .name = "Id",
        .params = {},
        .returnType = Type::I64,
        .isStatic = false
    };
};

struct MonitorClassInfo {
    static constexpr const char* QualifiedName = "Viper.Monitor";
    // ... similar method definitions
};

struct SafeI64ClassInfo {
    static constexpr const char* QualifiedName = "Viper.SafeI64";
    // ... similar method definitions
};

} // namespace basic::runtime
```

**Registration Logic**:

```cpp
// ThreadingClasses.cpp
void registerThreadingClasses(OopIndex &index) {
    // Register Thread class
    ClassInfo threadClass;
    threadClass.name = "Thread";
    threadClass.qualifiedName = "Viper.Thread";

    // Add static methods
    threadClass.methods["Start"] = ThreadClassInfo::Start_0;
    threadClass.methods["Start$1"] = ThreadClassInfo::Start_1;  // Overload
    threadClass.methods["Sleep"] = ThreadClassInfo::Sleep;
    threadClass.methods["Yield"] = ThreadClassInfo::Yield;
    threadClass.methods["CurrentId"] = ThreadClassInfo::CurrentId;

    // Add instance methods
    threadClass.methods["Join"] = ThreadClassInfo::Join_0;
    threadClass.methods["Join$1"] = ThreadClassInfo::Join_1;
    threadClass.methods["TryJoin"] = ThreadClassInfo::TryJoin;
    threadClass.methods["IsAlive"] = ThreadClassInfo::IsAlive;
    threadClass.methods["Id"] = ThreadClassInfo::Id;

    index.addClass(std::move(threadClass));

    // Similarly for Monitor and SafeI64...
}
```

**Deliverables**:
- [ ] `Viper.Thread` registered in OopIndex
- [ ] `Viper.Monitor` registered in OopIndex
- [ ] `Viper.SafeI64` registered in OopIndex
- [ ] Method overload resolution works
- [ ] Class lookup tests pass

---

### Phase 3: Semantic Analysis

**Goal**: Validate threading constructs and `ADDRESSOF` targets

**Files to Modify**:

| File | Changes |
|------|---------|
| `src/frontends/basic/SemanticAnalyzer.hpp` | Add `analyzeAddressOfExpr` |
| `src/frontends/basic/SemanticAnalyzer_Expr.cpp` | Validate ADDRESSOF targets |
| `src/frontends/basic/SemanticAnalyzer_Stmts_Runtime.cpp` | Thread call validation |
| `src/frontends/basic/sem/ThreadingSemantics.hpp` | New: Threading validation |
| `src/frontends/basic/sem/ThreadingSemantics.cpp` | New: Entry signature checks |

**ADDRESSOF Validation**:

```cpp
// SemanticAnalyzer_Expr.cpp
void SemanticAnalyzer::analyzeAddressOfExpr(AddressOfExpr &expr) {
    // 1. Find the target SUB/FUNCTION
    const ProcDecl *proc = findProcedure(expr.targetName);
    if (!proc) {
        error(expr.loc, "ADDRESSOF target '" + expr.targetName + "' not found");
        return;
    }

    // 2. Record the target for lowering
    expr.resolvedTarget = proc;

    // 3. Set expression type to Ptr
    expr.type = Type(Type::Kind::Ptr);
}
```

**Thread Entry Validation**:

```cpp
// ThreadingSemantics.cpp
bool validateThreadEntry(const ProcDecl &proc, DiagnosticEmitter &diag) {
    // Valid signatures:
    // 1. SUB name()
    // 2. SUB name(arg AS Ptr)

    if (proc.params.size() == 0) {
        return true;  // No-arg entry is valid
    }

    if (proc.params.size() == 1) {
        if (proc.params[0].type.kind == Type::Kind::Ptr) {
            return true;  // Single Ptr arg is valid
        }
        diag.error(proc.loc, "Thread entry with one parameter must take Ptr type");
        return false;
    }

    diag.error(proc.loc, "Thread entry must have 0 or 1 parameters");
    return false;
}

void validateThreadStart(MethodCallExpr &call, SemanticContext &ctx) {
    // Extract ADDRESSOF target
    if (call.args.size() < 1) {
        ctx.diag.error(call.loc, "Thread.Start requires entry function");
        return;
    }

    auto *addrOf = dynamic_cast<AddressOfExpr*>(call.args[0].get());
    if (!addrOf) {
        ctx.diag.error(call.loc, "Thread.Start first argument must be ADDRESSOF expression");
        return;
    }

    // Validate entry signature
    if (!validateThreadEntry(*addrOf->resolvedTarget, ctx.diag)) {
        return;
    }

    // Validate arg count matches entry signature
    bool entryTakesArg = addrOf->resolvedTarget->params.size() == 1;
    bool callHasArg = call.args.size() == 2;

    if (entryTakesArg && !callHasArg) {
        ctx.diag.error(call.loc, "Thread entry requires argument but none provided");
    }
    if (!entryTakesArg && callHasArg) {
        ctx.diag.error(call.loc, "Thread entry takes no arguments but one provided");
    }
}
```

**Deliverables**:
- [ ] `ADDRESSOF` targets validated at compile time
- [ ] Thread entry signatures enforced
- [ ] Argument count matching validated
- [ ] Clear error messages for all failure cases
- [ ] Semantic analysis tests pass

---

### Phase 4: IL Lowering

**Goal**: Generate correct IL for all threading constructs

**Files to Modify/Create**:

| File | Changes |
|------|---------|
| `src/frontends/basic/Lowerer_Expr.cpp` | Lower `AddressOfExpr` |
| `src/frontends/basic/lower/oop/Lower_OOP_MethodCall.cpp` | Thread method dispatch |
| `src/frontends/basic/builtins/ThreadingBuiltins.hpp` | New: Threading lowering |
| `src/frontends/basic/builtins/ThreadingBuiltins.cpp` | New: Lowering implementations |
| `src/frontends/basic/LowerRuntime.hpp` | Add threading RuntimeFeatures |
| `src/frontends/basic/RuntimeNames.hpp` | Thread runtime function names |

**RuntimeFeature Additions**:

```cpp
// In il/runtime/RuntimeFeatures.def or equivalent
RUNTIME_FEATURE(ThreadStart, "rt_thread_start")
RUNTIME_FEATURE(ThreadStartWithArg, "rt_thread_start_with_arg")
RUNTIME_FEATURE(ThreadJoin, "rt_thread_join")
RUNTIME_FEATURE(ThreadJoinTimeout, "rt_thread_join_timeout")
RUNTIME_FEATURE(ThreadTryJoin, "rt_thread_try_join")
RUNTIME_FEATURE(ThreadIsAlive, "rt_thread_get_is_alive")
RUNTIME_FEATURE(ThreadGetId, "rt_thread_get_id")
RUNTIME_FEATURE(ThreadSleep, "rt_thread_sleep")
RUNTIME_FEATURE(ThreadYield, "rt_thread_yield")
RUNTIME_FEATURE(ThreadCurrentId, "rt_thread_current_id")
RUNTIME_FEATURE(MonitorNew, "rt_monitor_new")
RUNTIME_FEATURE(MonitorEnter, "rt_monitor_enter")
RUNTIME_FEATURE(MonitorExit, "rt_monitor_exit")
RUNTIME_FEATURE(MonitorTryEnter, "rt_monitor_try_enter")
RUNTIME_FEATURE(MonitorTryEnterTimeout, "rt_monitor_try_enter_for")
RUNTIME_FEATURE(MonitorWait, "rt_monitor_wait")
RUNTIME_FEATURE(MonitorPause, "rt_monitor_pause")
RUNTIME_FEATURE(SafeI64New, "rt_safe_i64_new")
RUNTIME_FEATURE(SafeI64Get, "rt_safe_i64_get")
RUNTIME_FEATURE(SafeI64Set, "rt_safe_i64_set")
RUNTIME_FEATURE(SafeI64Add, "rt_safe_i64_add")
RUNTIME_FEATURE(SafeI64CompareExchange, "rt_safe_i64_compare_exchange")
```

**ADDRESSOF Lowering**:

```cpp
// Lowerer_Expr.cpp
RVal Lowerer::lowerAddressOfExpr(const AddressOfExpr &expr) {
    // Get the IL function name for the target
    std::string funcName = mangleProcName(expr.resolvedTarget);

    // Emit addr_of instruction
    Value result = builder_.addAddrOf(funcName);

    return {result, Type(Type::Kind::Ptr)};
}
```

**Thread.Start Lowering**:

```cpp
// ThreadingBuiltins.cpp
Value lowerThreadStart(LowerCtx &ctx, const MethodCallExpr &call) {
    auto *addrOf = dynamic_cast<AddressOfExpr*>(call.args[0].get());
    std::string entryName = mangleProcName(addrOf->resolvedTarget);

    if (call.args.size() == 1) {
        // Thread.Start(AddressOf Entry)
        ctx.requestHelper(RuntimeFeature::ThreadStart);

        Value entryPtr = ctx.lowerExpr(*call.args[0]);
        return ctx.emitCallRet(
            Type(Type::Kind::Ptr),
            "rt_thread_start",
            {entryPtr},
            call.loc
        );
    } else {
        // Thread.Start(AddressOf Entry, arg)
        ctx.requestHelper(RuntimeFeature::ThreadStartWithArg);

        Value entryPtr = ctx.lowerExpr(*call.args[0]);
        Value argPtr = ctx.lowerExpr(*call.args[1]);

        // Coerce arg to Ptr if needed
        if (call.args[1]->type.kind != Type::Kind::Ptr) {
            argPtr = ctx.emitCast(argPtr, Type(Type::Kind::Ptr));
        }

        return ctx.emitCallRet(
            Type(Type::Kind::Ptr),
            "rt_thread_start_with_arg",
            {entryPtr, argPtr},
            call.loc
        );
    }
}

Value lowerThreadJoin(LowerCtx &ctx, Value threadHandle,
                      const MethodCallExpr &call) {
    if (call.args.empty()) {
        // t.Join() - blocking
        ctx.requestHelper(RuntimeFeature::ThreadJoin);
        ctx.emitCall("rt_thread_join", {threadHandle}, call.loc);
        return Value::Void();
    } else {
        // t.Join(timeout) - returns bool
        ctx.requestHelper(RuntimeFeature::ThreadJoinTimeout);
        Value timeout = ctx.lowerExpr(*call.args[0]);
        return ctx.emitCallRet(
            Type(Type::Kind::Bool),
            "rt_thread_join_for",
            {threadHandle, timeout},
            call.loc
        );
    }
}
```

**Monitor Lowering**:

```cpp
Value lowerMonitorEnter(LowerCtx &ctx, Value monitorHandle) {
    ctx.requestHelper(RuntimeFeature::MonitorEnter);
    ctx.emitCall("rt_monitor_enter", {monitorHandle}, ctx.loc());
    return Value::Void();
}

Value lowerMonitorExit(LowerCtx &ctx, Value monitorHandle) {
    ctx.requestHelper(RuntimeFeature::MonitorExit);
    ctx.emitCall("rt_monitor_exit", {monitorHandle}, ctx.loc());
    return Value::Void();
}

Value lowerMonitorTryEnter(LowerCtx &ctx, Value monitorHandle,
                           const MethodCallExpr &call) {
    if (call.args.empty()) {
        ctx.requestHelper(RuntimeFeature::MonitorTryEnter);
        return ctx.emitCallRet(
            Type(Type::Kind::Bool),
            "rt_monitor_try_enter",
            {monitorHandle},
            call.loc
        );
    } else {
        ctx.requestHelper(RuntimeFeature::MonitorTryEnterTimeout);
        Value timeout = ctx.lowerExpr(*call.args[0]);
        return ctx.emitCallRet(
            Type(Type::Kind::Bool),
            "rt_monitor_try_enter_for",
            {monitorHandle, timeout},
            call.loc
        );
    }
}
```

**SafeI64 Lowering**:

```cpp
Value lowerSafeI64New(LowerCtx &ctx, const MethodCallExpr &call) {
    ctx.requestHelper(RuntimeFeature::SafeI64New);
    Value initial = call.args.empty()
        ? ctx.emitConstI64(0)
        : ctx.lowerExpr(*call.args[0]);
    return ctx.emitCallRet(
        Type(Type::Kind::Ptr),
        "rt_safe_i64_new",
        {initial},
        call.loc
    );
}

Value lowerSafeI64Add(LowerCtx &ctx, Value handle, Value delta) {
    ctx.requestHelper(RuntimeFeature::SafeI64Add);
    return ctx.emitCallRet(
        Type(Type::Kind::I64),
        "rt_safe_i64_add",
        {handle, delta},
        ctx.loc()
    );
}
```

**Deliverables**:
- [ ] `ADDRESSOF` lowers to `addr_of` IL instruction
- [ ] `Thread.Start` emits correct runtime calls
- [ ] `Thread.Join` with/without timeout works
- [ ] `Monitor` methods lower correctly
- [ ] `SafeI64` methods lower correctly
- [ ] All runtime features declared in IL output
- [ ] Lowering tests pass

---

## 3. Testing Strategy

### 3.1 Unit Tests

**Location**: `src/tests/frontends/basic/`

| Test File | Coverage |
|-----------|----------|
| `LexerThreadingTests.cpp` | ADDRESSOF tokenization |
| `ParserThreadingTests.cpp` | ADDRESSOF expression parsing |
| `SemanticThreadingTests.cpp` | Entry validation, type checking |
| `LowerThreadingTests.cpp` | IL generation verification |

### 3.2 Integration Tests

**Location**: `src/tests/integration/basic/`

```basic
' test_thread_basic.bas
SUB Worker()
    PRINT "In thread"
END SUB

DIM t AS Thread = Thread.Start(AddressOf Worker)
t.Join()
PRINT "Done"
' Expected output:
' In thread
' Done
```

```basic
' test_thread_with_data.bas
CLASS Counter
    PUBLIC Value AS Integer
END CLASS

SUB Increment(arg AS Ptr)
    DIM c AS Counter = CAST(Counter, arg)
    c.Value = c.Value + 1
END SUB

DIM c AS NEW Counter
c.Value = 0

DIM t1 AS Thread = Thread.Start(AddressOf Increment, c AS Ptr)
DIM t2 AS Thread = Thread.Start(AddressOf Increment, c AS Ptr)
t1.Join()
t2.Join()

PRINT c.Value  ' May be 1 or 2 due to race - demonstrates need for sync
```

```basic
' test_monitor.bas
CLASS SharedCounter
    PRIVATE lock AS NEW Monitor
    PRIVATE value AS Integer

    SUB New()
        value = 0
    END SUB

    SUB Increment()
        lock.Enter()
        value = value + 1
        lock.Exit()
    END SUB

    FUNCTION GetValue() AS Integer
        lock.Enter()
        GetValue = value
        lock.Exit()
    END FUNCTION
END CLASS

SUB Worker(arg AS Ptr)
    DIM c AS SharedCounter = CAST(SharedCounter, arg)
    FOR i = 1 TO 1000
        c.Increment()
    NEXT i
END SUB

DIM counter AS NEW SharedCounter
DIM t1 AS Thread = Thread.Start(AddressOf Worker, counter AS Ptr)
DIM t2 AS Thread = Thread.Start(AddressOf Worker, counter AS Ptr)
t1.Join()
t2.Join()

PRINT counter.GetValue()  ' Should be 2000
```

```basic
' test_safe_i64.bas
DIM counter AS SafeI64 = SafeI64.New(0)

SUB Worker(arg AS Ptr)
    DIM c AS SafeI64 = CAST(SafeI64, arg)
    FOR i = 1 TO 1000
        c.Add(1)
    NEXT i
END SUB

DIM t1 AS Thread = Thread.Start(AddressOf Worker, counter AS Ptr)
DIM t2 AS Thread = Thread.Start(AddressOf Worker, counter AS Ptr)
t1.Join()
t2.Join()

PRINT counter.Get()  ' Should be 2000
```

### 3.3 Golden Tests

**Location**: `src/tests/golden/basic/threading/`

Verify IL output matches expected patterns:

```
# test_thread_start.golden
INPUT: Thread.Start(AddressOf Worker)
EXPECTED IL:
  %0 = addr_of @Worker
  %1 = call @rt_thread_start(%0)
```

### 3.4 VM vs Native Equivalence

Run all threading tests in both VM and native mode, verify identical output.

---

## 4. Documentation Updates

| Document | Updates Needed |
|----------|----------------|
| `docs/basic-reference.md` | Add threading section |
| `docs/basic-language.md` | Threading tutorial |
| `docs/viperlib/README.md` | BASIC examples for Thread/Monitor/SafeI64 |
| `README.md` | Update BASIC status table |

---

## 5. Implementation Schedule

| Phase | Description | Dependencies | Estimated Files |
|-------|-------------|--------------|-----------------|
| **Phase 1** | Lexer/Parser for ADDRESSOF | None | 5 files |
| **Phase 2** | Class registration | Phase 1 | 5 files |
| **Phase 3** | Semantic analysis | Phase 1, 2 | 5 files |
| **Phase 4** | IL lowering | Phase 1, 2, 3 | 6 files |
| **Testing** | Full test coverage | All phases | 8+ files |
| **Docs** | Documentation updates | All phases | 4 files |

---

## 6. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Entry validation edge cases | Medium | Medium | Comprehensive test suite |
| Overload resolution conflicts | Low | Medium | Follow existing OOP patterns |
| Thread-safety in lowerer | Low | High | Lowerer is single-threaded |
| Native codegen gaps | Medium | Medium | Test VM and native equally |
| Performance overhead | Low | Low | Runtime already optimized |

---

## 7. Success Criteria

1. All threading integration tests pass
2. VM and native produce identical output
3. No memory leaks in thread lifecycle
4. Clear error messages for all misuse scenarios
5. Documentation complete and accurate
6. Existing BASIC tests unaffected (no regressions)

---

## 8. Future Enhancements (Out of Scope)

- `Gate`, `Barrier`, `RwLock` exposure (advanced primitives)
- Async/await syntax (deferred to v0.2)
- Thread pool built-in class
- Parallel FOR loops
- Channel-based communication

---

## Appendix A: Complete API Reference

### Thread Class

```basic
CLASS Viper.Thread
    ' Static Methods
    SHARED FUNCTION Start(entry AS Ptr) AS Thread
    SHARED FUNCTION Start(entry AS Ptr, arg AS Ptr) AS Thread
    SHARED SUB Sleep(milliseconds AS Long)
    SHARED SUB Yield()
    SHARED FUNCTION CurrentId() AS Long

    ' Instance Methods
    SUB Join()
    FUNCTION Join(timeoutMs AS Long) AS Boolean
    FUNCTION TryJoin() AS Boolean
    FUNCTION IsAlive() AS Boolean
    FUNCTION Id() AS Long
END CLASS
```

### Monitor Class

```basic
CLASS Viper.Monitor
    ' Constructor
    SUB New()

    ' Instance Methods
    SUB Enter()
    SUB Exit()
    FUNCTION TryEnter() AS Boolean
    FUNCTION TryEnter(timeoutMs AS Long) AS Boolean
    SUB Wait()
    FUNCTION Wait(timeoutMs AS Long) AS Boolean
    SUB Pause()      ' Wake one waiter
    SUB PauseAll()   ' Wake all waiters
END CLASS
```

### SafeI64 Class

```basic
CLASS Viper.SafeI64
    ' Static Constructor
    SHARED FUNCTION New(initial AS Long) AS SafeI64

    ' Instance Methods
    FUNCTION Get() AS Long
    SUB Set(value AS Long)
    FUNCTION Add(delta AS Long) AS Long
    FUNCTION CompareExchange(expected AS Long, desired AS Long) AS Long
END CLASS
```

---

## Appendix B: Example Program

```basic
' producer_consumer.bas
' Demonstrates threading with Monitor synchronization

CLASS Buffer
    PRIVATE lock AS NEW Monitor
    PRIVATE items(10) AS Integer
    PRIVATE count AS Integer
    PRIVATE head AS Integer
    PRIVATE tail AS Integer

    SUB New()
        count = 0
        head = 0
        tail = 0
    END SUB

    SUB Put(item AS Integer)
        lock.Enter()
        WHILE count = 10
            lock.Wait()  ' Buffer full, wait
        WEND
        items(tail) = item
        tail = (tail + 1) MOD 10
        count = count + 1
        lock.Pause()  ' Wake consumer
        lock.Exit()
    END SUB

    FUNCTION Take() AS Integer
        lock.Enter()
        WHILE count = 0
            lock.Wait()  ' Buffer empty, wait
        WEND
        Take = items(head)
        head = (head + 1) MOD 10
        count = count - 1
        lock.Pause()  ' Wake producer
        lock.Exit()
    END FUNCTION
END CLASS

DIM buffer AS NEW Buffer
DIM done AS Boolean = FALSE

SUB Producer(arg AS Ptr)
    DIM buf AS Buffer = CAST(Buffer, arg)
    FOR i = 1 TO 100
        buf.Put(i)
        PRINT "Produced: "; i
    NEXT i
END SUB

SUB Consumer(arg AS Ptr)
    DIM buf AS Buffer = CAST(Buffer, arg)
    FOR i = 1 TO 100
        DIM item AS Integer = buf.Take()
        PRINT "Consumed: "; item
    NEXT i
END SUB

DIM producer AS Thread = Thread.Start(AddressOf Producer, buffer AS Ptr)
DIM consumer AS Thread = Thread.Start(AddressOf Consumer, buffer AS Ptr)

producer.Join()
consumer.Join()

PRINT "Done!"
```
