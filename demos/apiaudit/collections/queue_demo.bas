' queue_demo.bas
PRINT "=== Viper.Collections.Queue Demo ==="
DIM q AS OBJECT
q = NEW Viper.Collections.Queue()
q.Push("a")
q.Push("b")
q.Push("c")
PRINT q.Len
PRINT q.IsEmpty
q.Clear()
PRINT q.IsEmpty
PRINT "done"
END
