' heap_demo.bas
PRINT "=== Viper.Collections.Heap Demo ==="
DIM h AS OBJECT
h = NEW Viper.Collections.Heap()
h.Push(30, "low")
h.Push(10, "high")
h.Push(20, "med")
PRINT h.Len
PRINT h.IsEmpty
h.Clear()
PRINT h.IsEmpty
PRINT "done"
END
