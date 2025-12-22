' Viper.Collections Demo - Collection Classes
' This demo showcases various collection types

DIM n AS INTEGER

' === Seq (Dynamic Array) ===
PRINT "=== Seq (Dynamic Array) ==="
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
seq.Push("apple")
seq.Push("banana")
seq.Push("cherry")
n = seq.Len
PRINT "Seq length after 3 pushes: "; n
PRINT "Has 'banana': "; seq.Has("banana")
PRINT "Find 'cherry': "; seq.Find("cherry")
seq.Remove(1)
PRINT "Length after Remove(1): "; seq.Len
PRINT

' === Map (Dictionary) ===
PRINT "=== Map (Dictionary) ==="
DIM map AS Viper.Collections.Map
map = Viper.Collections.Map.New()
map.Set("name", "Alice")
map.Set("age", "30")
map.Set("city", "Boston")
PRINT "Map length: "; map.Len
PRINT "Has 'age': "; map.Has("age")
PRINT "Has 'email': "; map.Has("email")
map.Remove("age")
PRINT "Length after Remove('age'): "; map.Len
PRINT

' === Stack (LIFO) ===
PRINT "=== Stack (LIFO) ==="
DIM stack AS Viper.Collections.Stack
stack = Viper.Collections.Stack.New()
stack.Push("first")
stack.Push("second")
stack.Push("third")
PRINT "Stack length: "; stack.Len
stack.Pop()
PRINT "Length after Pop: "; stack.Len
stack.Clear()
PRINT "Length after Clear: "; stack.Len
PRINT

' === Queue (FIFO) ===
PRINT "=== Queue (FIFO) ==="
DIM queue AS Viper.Collections.Queue
queue = Viper.Collections.Queue.New()
queue.Add("first")
queue.Add("second")
queue.Add("third")
PRINT "Queue length: "; queue.Len
queue.Take()
PRINT "Length after Take: "; queue.Len
PRINT

' === Heap (Priority Queue) ===
PRINT "=== Heap (Priority Queue) ==="
DIM heap AS Viper.Collections.Heap
heap = Viper.Collections.Heap.New()
heap.Push(3, "medium")
heap.Push(1, "high priority")
heap.Push(5, "low priority")
PRINT "Heap length: "; heap.Len
heap.Pop()
PRINT "Length after Pop (removed highest priority): "; heap.Len
PRINT

' === Bag (String Set) ===
PRINT "=== Bag (String Set) ==="
DIM bag AS Viper.Collections.Bag
bag = Viper.Collections.Bag.New()
bag.Put("red")
bag.Put("green")
bag.Put("blue")
bag.Put("red")  ' Duplicate, won't be added
PRINT "Bag size (red added twice): "; bag.Len
PRINT "Has 'green': "; bag.Has("green")
PRINT "Has 'yellow': "; bag.Has("yellow")
bag.Drop("green")
PRINT "Size after Drop('green'): "; bag.Len
PRINT

' === Ring (Circular Buffer) ===
PRINT "=== Ring (Circular Buffer) ==="
DIM ring AS Viper.Collections.Ring
ring = Viper.Collections.Ring.New(3)
ring.Push("a")
ring.Push("b")
ring.Push("c")
PRINT "Ring capacity: "; ring.Cap
PRINT "Ring length: "; ring.Len
ring.Push("d")  ' Overwrites oldest
PRINT "Length after 4th push (cap=3): "; ring.Len
PRINT

' === Bytes ===
PRINT "=== Bytes ==="
DIM bytes AS Viper.Collections.Bytes
bytes = Viper.Collections.Bytes.New(4)
bytes.Set(0, 72)  ' H
bytes.Set(1, 105) ' i
bytes.Set(2, 33)  ' !
bytes.Set(3, 0)
PRINT "Bytes length: "; bytes.Len
PRINT "Byte at [0]: "; bytes.Get(0)
PRINT "Bytes as string: "; bytes.ToStr()
PRINT "Bytes as hex: "; bytes.ToHex()

END
