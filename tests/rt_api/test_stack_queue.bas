' test_stack_queue.bas — Stack, Queue, Ring, Heap
DIM st AS Viper.Collections.Stack
st = Viper.Collections.Stack.New()
PRINT "stack empty: "; st.IsEmpty
st.Push("a")
st.Push("b")
st.Push("c")
PRINT "stack len: "; st.Length
st.Pop()
st.Pop()
PRINT "stack len after 2 pops: "; st.Length
st.Clear()
PRINT "stack empty after clear: "; st.IsEmpty

DIM q AS Viper.Collections.Queue
q = Viper.Collections.Queue.New()
PRINT "queue empty: "; q.IsEmpty
q.Push("x")
q.Push("y")
q.Push("z")
PRINT "queue len: "; q.Length
q.Pop()
q.Pop()
PRINT "queue len after 2 pops: "; q.Length
q.Clear()
PRINT "queue empty after clear: "; q.IsEmpty

DIM r AS Viper.Collections.Ring
r = Viper.Collections.Ring.New()
PRINT "ring empty: "; r.IsEmpty
r.Push("1")
r.Push("2")
r.Push("3")
PRINT "ring len: "; r.Length
r.Pop()
PRINT "ring len after pop: "; r.Length
r.Clear()
PRINT "ring empty after clear: "; r.IsEmpty

DIM h AS Viper.Collections.Heap
h = Viper.Collections.Heap.New()
PRINT "heap empty: "; h.IsEmpty
h.Push(3, "three")
h.Push(1, "one")
h.Push(2, "two")
PRINT "heap len: "; h.Length
h.Pop()
h.Pop()
PRINT "heap len after 2 pops: "; h.Length
h.Clear()
PRINT "heap empty after clear: "; h.IsEmpty

PRINT "done"
END
