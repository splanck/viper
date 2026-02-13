' deque_demo.bas
PRINT "=== Viper.Collections.Deque Demo ==="
DIM d AS OBJECT
d = NEW Viper.Collections.Deque()
d.PushBack("a")
d.PushBack("b")
d.PushFront("z")
PRINT d.Len
PRINT d.IsEmpty
d.Clear()
PRINT d.IsEmpty
PRINT "done"
END
