' stack_demo.bas
PRINT "=== Viper.Collections.Stack Demo ==="
DIM s AS OBJECT
s = NEW Viper.Collections.Stack()
s.Push("a")
s.Push("b")
s.Push("c")
PRINT s.Len
PRINT s.IsEmpty
s.Clear()
PRINT s.IsEmpty
PRINT "done"
END
