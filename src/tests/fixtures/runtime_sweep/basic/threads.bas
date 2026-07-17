' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Threads.Thread.Start
' COVER: Zanna.Threads.Thread.Join
' COVER: Zanna.Threads.Thread.TryJoin
' COVER: Zanna.Threads.Thread.JoinFor
' COVER: Zanna.Threads.Thread.Id
' COVER: Zanna.Threads.Thread.IsAlive
' COVER: Zanna.Threads.SafeI64.New
' COVER: Zanna.Threads.SafeI64.Get
' COVER: Zanna.Threads.SafeI64.Set
' COVER: Zanna.Threads.SafeI64.Add

DIM threadRan AS Zanna.Threads.SafeI64
threadRan = NEW Zanna.Threads.SafeI64(0)

SUB WorkerThread()
    Zanna.Threads.Thread.Sleep(50)
    threadRan.Set(1)
END SUB

DIM t AS Zanna.Threads.Thread
t = Zanna.Threads.Thread.Start(ADDRESSOF WorkerThread, NOTHING)

Zanna.Core.Diagnostics.Assert(t.Id > 0, "thread.id")
DIM alive AS INTEGER
alive = t.IsAlive
Zanna.Core.Diagnostics.Assert(alive = 0 OR alive = 1 OR alive = -1, "thread.isalive")

DIM joined AS INTEGER
joined = t.JoinFor(1)
IF joined = 0 THEN
    joined = t.TryJoin()
END IF
IF joined = 0 THEN
    t.Join()
END IF

Zanna.Core.Diagnostics.Assert(threadRan.Get() = 1, "thread.ran")

DIM sharedCounter AS Zanna.Threads.SafeI64
sharedCounter = NEW Zanna.Threads.SafeI64(0)

SUB IncrementWorker()
    DIM i AS INTEGER
    FOR i = 1 TO 100
        sharedCounter.Add(1)
    NEXT i
END SUB

DIM t1 AS Zanna.Threads.Thread
DIM t2 AS Zanna.Threads.Thread
t1 = Zanna.Threads.Thread.Start(ADDRESSOF IncrementWorker, NOTHING)
t2 = Zanna.Threads.Thread.Start(ADDRESSOF IncrementWorker, NOTHING)

t1.Join()
t2.Join()

Zanna.Core.Diagnostics.AssertEq(sharedCounter.Get(), 200, "thread.sharedcounter")

PRINT "RESULT: ok"
END
