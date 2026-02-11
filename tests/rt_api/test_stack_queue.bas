' test_stack_queue.bas — Stack, Queue, Ring, Heap
DIM st AS Viper.Collections.Stack
st = Viper.Collections.Stack.New()
PRINT "stack empty: "; st.IsEmpty
st.Push("a")
st.Push("b")
st.Push("c")
PRINT "stack len: "; st.Len
st.Pop()
st.Pop()
PRINT "stack len after 2 pops: "; st.Len
st.Clear()
PRINT "stack empty after clear: "; st.IsEmpty

DIM q AS Viper.Collections.Queue
q = Viper.Collections.Queue.New()
PRINT "queue empty: "; q.IsEmpty
q.Push("x")
q.Push("y")
q.Push("z")
PRINT "queue len: "; q.Len
q.Pop()
q.Pop()
PRINT "queue len after 2 pops: "; q.Len
q.Clear()
PRINT "queue empty after clear: "; q.IsEmpty

DIM r AS Viper.Collections.Ring
r = Viper.Collections.Ring.New()
PRINT "ring empty: "; r.IsEmpty
r.Push("1")
r.Push("2")
r.Push("3")
PRINT "ring len: "; r.Len
r.Pop()
PRINT "ring len after pop: "; r.Len
r.Clear()
PRINT "ring empty after clear: "; r.IsEmpty

DIM h AS Viper.Collections.Heap
h = Viper.Collections.Heap.New()
PRINT "heap empty: "; h.IsEmpty
h.Push(3, "three")
h.Push(1, "one")
h.Push(2, "two")
PRINT "heap len: "; h.Len
h.Pop()
h.Pop()
PRINT "heap len after 2 pops: "; h.Len
h.Clear()
PRINT "heap empty after clear: "; h.IsEmpty

PRINT "done"
END
