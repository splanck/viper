' EXPECT_OUT: RESULT: ok
' COVER: Viper.Threads.Barrier.New
' COVER: Viper.Threads.Barrier.Parties
' COVER: Viper.Threads.Barrier.Waiting
' COVER: Viper.Threads.Barrier.Arrive
' COVER: Viper.Threads.Barrier.Reset
' COVER: Viper.Threads.Gate.New
' COVER: Viper.Threads.Gate.Permits
' COVER: Viper.Threads.Gate.Enter
' COVER: Viper.Threads.Gate.Leave
' COVER: Viper.Threads.Gate.TryEnter
' COVER: Viper.Threads.Gate.TryEnterFor
' COVER: Viper.Threads.Monitor.Enter
' COVER: Viper.Threads.Monitor.Exit
' COVER: Viper.Threads.Monitor.Pause
' COVER: Viper.Threads.Monitor.PauseAll
' COVER: Viper.Threads.Monitor.TryEnter
' COVER: Viper.Threads.Monitor.TryEnterFor
' COVER: Viper.Threads.Monitor.WaitFor
' COVER: Viper.Threads.RwLock.New
' COVER: Viper.Threads.RwLock.IsWriteLocked
' COVER: Viper.Threads.RwLock.Readers
' COVER: Viper.Threads.RwLock.ReadEnter
' COVER: Viper.Threads.RwLock.ReadExit
' COVER: Viper.Threads.RwLock.TryReadEnter
' COVER: Viper.Threads.RwLock.TryWriteEnter
' COVER: Viper.Threads.RwLock.WriteEnter
' COVER: Viper.Threads.RwLock.WriteExit
' COVER: Viper.Threads.SafeI64.New
' COVER: Viper.Threads.SafeI64.Add
' COVER: Viper.Threads.SafeI64.CompareExchange
' COVER: Viper.Threads.SafeI64.Get
' COVER: Viper.Threads.SafeI64.Set
' COVER: Viper.Threads.Thread.Sleep
' COVER: Viper.Threads.Thread.Yield

DIM gate AS Viper.Threads.Gate
gate = Viper.Threads.Gate.New(1)
Viper.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.permits")

DIM got AS INTEGER
got = gate.TryEnter()
Viper.Core.Diagnostics.Assert(got <> 0, "gate.tryenter")
Viper.Core.Diagnostics.AssertEq(gate.Permits, 0, "gate.permits.after")

gate.Leave()
Viper.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.leave")

Viper.Core.Diagnostics.Assert(gate.TryEnterFor(1) <> 0, "gate.tryenterfor")
gate.Leave()

gate.Enter()
Viper.Core.Diagnostics.AssertEq(gate.Permits, 0, "gate.enter")
gate.Leave(1)
Viper.Core.Diagnostics.AssertEq(gate.Permits, 1, "gate.leavemany")

DIM barrier AS Viper.Threads.Barrier
barrier = Viper.Threads.Barrier.New(1)
Viper.Core.Diagnostics.AssertEq(barrier.Parties, 1, "barrier.parties")
Viper.Core.Diagnostics.AssertEq(barrier.Waiting, 0, "barrier.waiting")
DIM arriveIndex AS INTEGER
arriveIndex = barrier.Arrive()
Viper.Core.Diagnostics.Assert(arriveIndex >= 0, "barrier.arrive")
barrier.Reset()

DIM lockObj AS Viper.Collections.List
lockObj = NEW Viper.Collections.List()

DIM ok AS INTEGER
ok = Viper.Threads.Monitor.TryEnter(lockObj)
Viper.Core.Diagnostics.Assert(ok <> 0, "monitor.tryenter")
Viper.Threads.Monitor.Exit(lockObj)

ok = Viper.Threads.Monitor.TryEnterFor(lockObj, 1)
Viper.Core.Diagnostics.Assert(ok <> 0, "monitor.tryenterfor")
Viper.Threads.Monitor.Exit(lockObj)

Viper.Threads.Monitor.Enter(lockObj)
Viper.Threads.Monitor.Pause(lockObj)
Viper.Threads.Monitor.PauseAll(lockObj)
DIM waitOk AS INTEGER
waitOk = Viper.Threads.Monitor.WaitFor(lockObj, 1)
Viper.Core.Diagnostics.Assert(waitOk = 0 OR waitOk = 1 OR waitOk = -1, "monitor.waitfor")
Viper.Threads.Monitor.Exit(lockObj)

DIM rw AS Viper.Threads.RwLock
rw = Viper.Threads.RwLock.New()
Viper.Core.Diagnostics.AssertEq(rw.Readers, 0, "rw.readers")
Viper.Core.Diagnostics.Assert(rw.IsWriteLocked = FALSE, "rw.writelocked")

rw.ReadEnter()
Viper.Core.Diagnostics.AssertEq(rw.Readers, 1, "rw.readenter")
rw.ReadExit()

Viper.Core.Diagnostics.Assert(rw.TryReadEnter() <> 0, "rw.tryread")
rw.ReadExit()

Viper.Core.Diagnostics.Assert(rw.TryWriteEnter() <> 0, "rw.trywrite")
rw.WriteExit()

rw.WriteEnter()
Viper.Core.Diagnostics.Assert(rw.IsWriteLocked, "rw.writeenter")
rw.WriteExit()

DIM cell AS Viper.Threads.SafeI64
cell = Viper.Threads.SafeI64.New(10)
Viper.Core.Diagnostics.AssertEq(cell.Get(), 10, "safe.get")
cell.Set(5)
Viper.Core.Diagnostics.AssertEq(cell.Get(), 5, "safe.set")
Viper.Core.Diagnostics.AssertEq(cell.Add(7), 12, "safe.add")
DIM prev AS INTEGER
prev = cell.CompareExchange(12, 1)
Viper.Core.Diagnostics.AssertEq(prev, 12, "safe.cas")
Viper.Core.Diagnostics.AssertEq(cell.Get(), 1, "safe.cas.value")

Viper.Threads.Thread.Sleep(1)
Viper.Threads.Thread.Yield()

PRINT "RESULT: ok"
END
