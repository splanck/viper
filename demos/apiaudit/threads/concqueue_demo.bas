' concqueue_demo.bas
PRINT "=== Viper.Threads.ConcurrentQueue Demo ==="
DIM q AS OBJECT
q = NEW Viper.Threads.ConcurrentQueue()
q.Enqueue("a")
q.Enqueue("b")
PRINT q.Len
PRINT q.IsEmpty
q.Clear()
PRINT q.IsEmpty
PRINT "done"
END
