' test_threads.bas — ConcurrentQueue, ConcurrentMap, Gate, Barrier, RwLock, CancelToken, Debouncer, Throttler, Scheduler
DIM cq AS Viper.Threads.ConcurrentQueue
cq = Viper.Threads.ConcurrentQueue.New()
PRINT "cq empty: "; cq.IsEmpty
cq.Enqueue("a")
cq.Enqueue("b")
cq.Enqueue("c")
PRINT "cq len: "; cq.Len
cq.Dequeue()
PRINT "cq len after dequeue: "; cq.Len
cq.Clear()
PRINT "cq empty after clear: "; cq.IsEmpty

DIM cm AS Viper.Threads.ConcurrentMap
cm = Viper.Threads.ConcurrentMap.New()
PRINT "cmap empty: "; cm.IsEmpty
cm.Set("x", "1")
cm.Set("y", "2")
PRINT "cmap len: "; cm.Len
PRINT "cmap has x: "; cm.Has("x")
PRINT "cmap has z: "; cm.Has("z")
cm.Remove("x")
PRINT "cmap len after remove: "; cm.Len
cm.Clear()
PRINT "cmap empty after clear: "; cm.IsEmpty

' NOTE: SafeI64.New() fails with "unsupported on this platform" (BUG-017)
' DIM si AS Viper.Threads.SafeI64
' si = Viper.Threads.SafeI64.New(0)

DIM g AS Viper.Threads.Gate
g = Viper.Threads.Gate.New(3)
PRINT "gate permits: "; g.Permits
PRINT "gate tryenter: "; g.TryEnter()
PRINT "gate permits after enter: "; g.Permits
g.Leave()
PRINT "gate permits after leave: "; g.Permits

DIM b AS Viper.Threads.Barrier
b = Viper.Threads.Barrier.New(2)
PRINT "barrier parties: "; b.Parties
PRINT "barrier waiting: "; b.Waiting

DIM rw AS Viper.Threads.RwLock
rw = Viper.Threads.RwLock.New()
PRINT "rwlock readers: "; rw.Readers
PRINT "rwlock iswritelocked: "; rw.IsWriteLocked
PRINT "rwlock tryreadenter: "; rw.TryReadEnter()
PRINT "rwlock readers after enter: "; rw.Readers
rw.ReadExit()
PRINT "rwlock readers after exit: "; rw.Readers
PRINT "rwlock trywriteenter: "; rw.TryWriteEnter()
rw.WriteExit()

DIM ct AS Viper.Threads.CancelToken
ct = Viper.Threads.CancelToken.New()
PRINT "cancel iscancelled: "; ct.IsCancelled
ct.Cancel()
PRINT "cancel iscancelled after cancel: "; ct.IsCancelled
ct.Reset()
PRINT "cancel iscancelled after reset: "; ct.IsCancelled

DIM db AS Viper.Threads.Debouncer
db = Viper.Threads.Debouncer.New(100)
PRINT "debounce delay: "; db.Delay
PRINT "debounce isready: "; db.IsReady
PRINT "debounce signalcount: "; db.SignalCount
db.Signal()
PRINT "debounce signalcount after signal: "; db.SignalCount

DIM th AS Viper.Threads.Throttler
th = Viper.Threads.Throttler.New(100)
PRINT "throttle interval: "; th.Interval
PRINT "throttle count: "; th.Count
PRINT "throttle try: "; th.Try()
PRINT "throttle count after try: "; th.Count

DIM sched AS Viper.Threads.Scheduler
sched = Viper.Threads.Scheduler.New()
PRINT "sched pending: "; sched.Pending
sched.Schedule("task1", 1000)
PRINT "sched pending after schedule: "; sched.Pending
sched.Cancel("task1")
PRINT "sched pending after cancel: "; sched.Pending
sched.Clear()
PRINT "sched pending after clear: "; sched.Pending

PRINT "done"
END
