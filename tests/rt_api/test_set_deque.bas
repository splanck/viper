' test_set_deque.bas — Set, Deque, DefaultMap, SparseArray, WeakMap
DIM s AS Viper.Collections.Set
s = Viper.Collections.Set.New()
PRINT "set empty: "; s.IsEmpty
s.Add("apple")
s.Add("banana")
s.Add("cherry")
PRINT "set len: "; s.Len
PRINT "set has apple: "; s.Has("apple")
PRINT "set has mango: "; s.Has("mango")
s.Remove("banana")
PRINT "set len after remove: "; s.Len

DIM dq AS Viper.Collections.Deque
dq = Viper.Collections.Deque.New()
PRINT "deque empty: "; dq.IsEmpty
dq.PushBack("a")
dq.PushBack("b")
dq.PushFront("z")
PRINT "deque len: "; dq.Len
dq.PopFront()
PRINT "deque len after popfront: "; dq.Len
dq.PopBack()
PRINT "deque len after popback: "; dq.Len
dq.PushBack("x")
dq.PushBack("y")
dq.Reverse()
PRINT "deque len after reverse: "; dq.Len
dq.Clear()
PRINT "deque empty after clear: "; dq.IsEmpty

DIM sa AS Viper.Collections.SparseArray
sa = Viper.Collections.SparseArray.New()
sa.Set(0, "zero")
sa.Set(100, "hundred")
sa.Set(999, "big")
PRINT "sparse len: "; sa.Len
PRINT "sparse has 100: "; sa.Has(100)
PRINT "sparse has 50: "; sa.Has(50)
sa.Remove(100)
PRINT "sparse len after remove: "; sa.Len

DIM wm AS Viper.Collections.WeakMap
wm = Viper.Collections.WeakMap.New()
PRINT "weakmap empty: "; wm.IsEmpty
wm.Set("key1", "val1")
wm.Set("key2", "val2")
PRINT "weakmap len: "; wm.Len
PRINT "weakmap has key1: "; wm.Has("key1")
wm.Remove("key1")
PRINT "weakmap len after remove: "; wm.Len
wm.Clear()
PRINT "weakmap empty after clear: "; wm.IsEmpty

PRINT "done"
END
