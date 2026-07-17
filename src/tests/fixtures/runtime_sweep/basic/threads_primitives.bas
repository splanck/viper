' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Threads.Barrier.New
' COVER: Zanna.Threads.Barrier.Parties
' COVER: Zanna.Threads.Barrier.Waiting
' COVER: Zanna.Threads.Barrier.Arrive
' COVER: Zanna.Threads.Barrier.Reset
' COVER: Zanna.Threads.Gate.New
' COVER: Zanna.Threads.Gate.Permits
' COVER: Zanna.Threads.Gate.Enter
' COVER: Zanna.Threads.Gate.Leave
' COVER: Zanna.Threads.Gate.TryEnter
' COVER: Zanna.Threads.Gate.TryEnterFor
' COVER: Zanna.Threads.Monitor.Enter
' COVER: Zanna.Threads.Monitor.Exit
' COVER: Zanna.Threads.Monitor.Notify
' COVER: Zanna.Threads.Monitor.NotifyAll
' COVER: Zanna.Threads.Monitor.TryEnter
' COVER: Zanna.Threads.Monitor.TryEnterFor
' COVER: Zanna.Threads.Monitor.WaitFor
' COVER: Zanna.Threads.RwLock.New
' COVER: Zanna.Threads.RwLock.IsWriteLocked
' COVER: Zanna.Threads.RwLock.Readers
' COVER: Zanna.Threads.RwLock.ReadEnter
' COVER: Zanna.Threads.RwLock.ReadExit
' COVER: Zanna.Threads.RwLock.TryReadEnter
' COVER: Zanna.Threads.RwLock.TryWriteEnter
' COVER: Zanna.Threads.RwLock.WriteEnter
' COVER: Zanna.Threads.RwLock.WriteExit
' COVER: Zanna.Threads.SafeI64.New
' COVER: Zanna.Threads.SafeI64.Add
' COVER: Zanna.Threads.SafeI64.CompareExchange
' COVER: Zanna.Threads.SafeI64.Get
' COVER: Zanna.Threads.SafeI64.Set
' COVER: Zanna.Threads.Thread.Sleep
' COVER: Zanna.Threads.Thread.Yield

DIM gate AS Zanna.Threads.Gate
gate = Zanna.Threads.Gate.New(1)
Zanna.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.permits")

DIM got AS INTEGER
got = gate.TryEnter()
Zanna.Core.Diagnostics.Assert(got <> 0, "gate.tryenter")
Zanna.Core.Diagnostics.AssertEq(gate.Permits, 0, "gate.permits.after")

gate.Leave()
Zanna.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.leave")

Zanna.Core.Diagnostics.Assert(gate.TryEnterFor(1), "gate.tryenterfor")
gate.Leave()

gate.Enter()
Zanna.Core.Diagnostics.AssertEq(gate.Permits, 0, "gate.enter")
gate.Leave(1)
Zanna.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.leavemany")

DIM barrier AS Zanna.Threads.Barrier
barrier = Zanna.Threads.Barrier.New(1)
Zanna.Core.Diagnostics.AssertEq(barrier.Parties, 1, "barrier.parties")
Zanna.Core.Diagnostics.AssertEq(barrier.Waiting, 0, "barrier.waiting")
DIM arriveIndex AS INTEGER
arriveIndex = barrier.Arrive()
Zanna.Core.Diagnostics.Assert(arriveIndex >= 0, "barrier.arrive")
barrier.Reset()

DIM lockObj AS Zanna.Collections.List
lockObj = NEW Zanna.Collections.List()

DIM ok AS INTEGER
ok = Zanna.Threads.Monitor.TryEnter(lockObj)
Zanna.Core.Diagnostics.Assert(ok <> 0, "monitor.tryenter")
Zanna.Threads.Monitor.Exit(lockObj)

ok = Zanna.Threads.Monitor.TryEnterFor(lockObj, 1)
Zanna.Core.Diagnostics.Assert(ok <> 0, "monitor.tryenterfor")
Zanna.Threads.Monitor.Exit(lockObj)

Zanna.Threads.Monitor.Enter(lockObj)
Zanna.Threads.Monitor.Notify(lockObj)
Zanna.Threads.Monitor.NotifyAll(lockObj)
DIM waitOk AS INTEGER
waitOk = Zanna.Threads.Monitor.WaitFor(lockObj, 1)
Zanna.Core.Diagnostics.Assert(waitOk = 0 OR waitOk = 1 OR waitOk = -1, "monitor.waitfor")
Zanna.Threads.Monitor.Exit(lockObj)

DIM rw AS Zanna.Threads.RwLock
rw = Zanna.Threads.RwLock.New()
Zanna.Core.Diagnostics.AssertEq(rw.Readers, 0, "rw.readers")
Zanna.Core.Diagnostics.Assert(rw.IsWriteLocked = FALSE, "rw.writelocked")

rw.ReadEnter()
Zanna.Core.Diagnostics.AssertEq(rw.Readers, 1, "rw.readenter")
rw.ReadExit()

Zanna.Core.Diagnostics.Assert(rw.TryReadEnter(), "rw.tryread")
rw.ReadExit()

Zanna.Core.Diagnostics.Assert(rw.TryWriteEnter(), "rw.trywrite")
rw.WriteExit()

rw.WriteEnter()
Zanna.Core.Diagnostics.Assert(rw.IsWriteLocked, "rw.writeenter")
rw.WriteExit()

DIM cell AS Zanna.Threads.SafeI64
cell = Zanna.Threads.SafeI64.New(10)
Zanna.Core.Diagnostics.AssertEq(cell.Get(), 10, "safe.get")
cell.Set(5)
Zanna.Core.Diagnostics.AssertEq(cell.Get(), 5, "safe.set")
Zanna.Core.Diagnostics.AssertEq(cell.Add(7), 12, "safe.add")
DIM prev AS INTEGER
prev = cell.CompareExchange(12, 1)
Zanna.Core.Diagnostics.AssertEq(prev, 12, "safe.cas")
Zanna.Core.Diagnostics.AssertEq(cell.Get(), 1, "safe.cas.value")

Zanna.Threads.Thread.Sleep(1)
Zanna.Threads.Thread.Yield()

PRINT "RESULT: ok"
END
