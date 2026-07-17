' Thread safety and concurrency edge case testing

DIM result AS INTEGER
DIM i AS INTEGER

PRINT "=== Thread Basic Tests ==="

' Test Thread.Sleep with edge values
PRINT "Sleep(0)..."
Zanna.Threads.Thread.Sleep(0)
PRINT "Sleep(0) completed"

PRINT "Sleep(1)..."
Zanna.Threads.Thread.Sleep(1)
PRINT "Sleep(1) completed"

' Test Sleep with negative (should clamp to 0)
PRINT "Sleep(-1) [should clamp to 0]..."
Zanna.Threads.Thread.Sleep(-1)
PRINT "Sleep(-1) completed"
PRINT ""

' === SafeI64 edge cases ===
PRINT "=== SafeI64 Edge Cases ==="
DIM counter AS Zanna.Threads.SafeI64

counter = Zanna.Threads.SafeI64.New(0)
PRINT "SafeI64.New(0): "; counter.Get()

counter.Set(100)
PRINT "After Set(100): "; counter.Get()

result = counter.Add(50)
PRINT "Add(50) returned: "; result; ", Get: "; counter.Get()

result = counter.Add(-200)
PRINT "Add(-200) returned: "; result; ", Get: "; counter.Get()

' Test overflow behavior
DIM bigCounter AS Zanna.Threads.SafeI64
bigCounter = Zanna.Threads.SafeI64.New(9223372036854775807)
PRINT "SafeI64 with max int64: "; bigCounter.Get()

' CompareExchange
counter.Set(42)
result = counter.CompareExchange(42, 100)
PRINT "CompareExchange(42, 100) [expected=42]: returned "; result; ", Get: "; counter.Get()

result = counter.CompareExchange(42, 200)
PRINT "CompareExchange(42, 200) [expected=100]: returned "; result; ", Get: "; counter.Get()
PRINT ""

' === Gate (semaphore) edge cases ===
PRINT "=== Gate Edge Cases ==="
DIM gate AS Zanna.Threads.Gate

gate = Zanna.Threads.Gate.New(1)
PRINT "Gate.New(1) Permits: "; gate.Permits

gate.Enter()
PRINT "After Enter: Permits: "; gate.Permits

DIM acquired AS INTEGER
acquired = gate.TryEnter()
PRINT "TryEnter [should fail]: "; acquired

gate.Leave()
PRINT "After Leave: Permits: "; gate.Permits

' Zero permits
DIM zeroGate AS Zanna.Threads.Gate
zeroGate = Zanna.Threads.Gate.New(0)
PRINT "Gate.New(0) Permits: "; zeroGate.Permits

acquired = zeroGate.TryEnter()
PRINT "TryEnter on 0-permit gate: "; acquired

' Leave with count (LeaveMany overload)
zeroGate.Leave(5)
PRINT "After Leave(5): Permits: "; zeroGate.Permits
PRINT ""

' === Barrier edge cases ===
PRINT "=== Barrier Edge Cases ==="
DIM barrier AS Zanna.Threads.Barrier

barrier = Zanna.Threads.Barrier.New(1)
PRINT "Barrier.New(1) Parties: "; barrier.Parties
PRINT "Waiting: "; barrier.Waiting

result = barrier.Arrive()
PRINT "Arrive() returned: "; result
PRINT "After Arrive, Waiting: "; barrier.Waiting

barrier.Reset()
PRINT "After Reset, Waiting: "; barrier.Waiting
PRINT ""

' === RwLock edge cases ===
PRINT "=== RwLock Edge Cases ==="
DIM rwlock AS Zanna.Threads.RwLock

rwlock = Zanna.Threads.RwLock.New()
PRINT "RwLock.New() Readers: "; rwlock.Readers
PRINT "IsWriteLocked: "; rwlock.IsWriteLocked

rwlock.ReadEnter()
PRINT "After ReadEnter: Readers: "; rwlock.Readers

rwlock.ReadExit()
PRINT "After ReadExit: Readers: "; rwlock.Readers

rwlock.WriteEnter()
PRINT "After WriteEnter: IsWriteLocked: "; rwlock.IsWriteLocked

rwlock.WriteExit()
PRINT "After WriteExit: IsWriteLocked: "; rwlock.IsWriteLocked

' Test TryRead and TryWrite
acquired = rwlock.TryReadEnter()
PRINT "TryReadEnter: "; acquired
IF acquired THEN rwlock.ReadExit()

acquired = rwlock.TryWriteEnter()
PRINT "TryWriteEnter: "; acquired
IF acquired THEN rwlock.WriteExit()
PRINT ""

' === Monitor on object ===
PRINT "=== Monitor Edge Cases ==="
DIM obj AS Zanna.Collections.Seq
obj = Zanna.Collections.Seq.New()

Zanna.Threads.Monitor.Enter(obj)
PRINT "Monitor.Enter on Seq: success"

Zanna.Threads.Monitor.Exit(obj)
PRINT "Monitor.Exit: success"

acquired = Zanna.Threads.Monitor.TryEnter(obj)
PRINT "TryEnter: "; acquired

IF acquired THEN Zanna.Threads.Monitor.Exit(obj)

' TryEnterFor with 0 timeout
acquired = Zanna.Threads.Monitor.TryEnterFor(obj, 0)
PRINT "TryEnterFor(0ms): "; acquired
IF acquired THEN Zanna.Threads.Monitor.Exit(obj)

acquired = Zanna.Threads.Monitor.TryEnterFor(obj, 1)
PRINT "TryEnterFor(1ms): "; acquired
IF acquired THEN Zanna.Threads.Monitor.Exit(obj)
PRINT ""

PRINT "=== Thread Edge Case Tests Complete ==="
END
