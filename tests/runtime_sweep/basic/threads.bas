' EXPECT_OUT: RESULT: ok
' COVER: Viper.Threads.Thread.Start
' COVER: Viper.Threads.Thread.Join
' COVER: Viper.Threads.Thread.TryJoin
' COVER: Viper.Threads.Thread.JoinFor
' COVER: Viper.Threads.Thread.Id
' COVER: Viper.Threads.Thread.IsAlive
' COVER: Viper.Threads.SafeI64.New
' COVER: Viper.Threads.SafeI64.Get
' COVER: Viper.Threads.SafeI64.Set
' COVER: Viper.Threads.SafeI64.Add

DIM threadRan AS Viper.Threads.SafeI64
threadRan = NEW Viper.Threads.SafeI64(0)

SUB WorkerThread()
    Viper.Threads.Thread.Sleep(50)
    threadRan.Set(1)
END SUB

DIM t AS Viper.Threads.Thread
t = Viper.Threads.Thread.Start(ADDRESSOF WorkerThread, NOTHING)

Viper.Core.Diagnostics.Assert(t.Id > 0, "thread.id")
DIM alive AS INTEGER
alive = t.IsAlive
Viper.Core.Diagnostics.Assert(alive = 0 OR alive = 1 OR alive = -1, "thread.isalive")

DIM joined AS INTEGER
joined = t.JoinFor(1)
IF joined = 0 THEN
    joined = t.TryJoin()
END IF
IF joined = 0 THEN
    t.Join()
END IF

Viper.Core.Diagnostics.Assert(threadRan.Get() = 1, "thread.ran")

DIM sharedCounter AS Viper.Threads.SafeI64
sharedCounter = NEW Viper.Threads.SafeI64(0)

SUB IncrementWorker()
    DIM i AS INTEGER
    FOR i = 1 TO 100
        sharedCounter.Add(1)
    NEXT i
END SUB

DIM t1 AS Viper.Threads.Thread
DIM t2 AS Viper.Threads.Thread
t1 = Viper.Threads.Thread.Start(ADDRESSOF IncrementWorker, NOTHING)
t2 = Viper.Threads.Thread.Start(ADDRESSOF IncrementWorker, NOTHING)

t1.Join()
t2.Join()

Viper.Core.Diagnostics.AssertEq(sharedCounter.Get(), 200, "thread.sharedcounter")

PRINT "RESULT: ok"
END
