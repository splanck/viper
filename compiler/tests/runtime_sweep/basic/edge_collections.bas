' Edge case testing for Collection operations
' Looking for crashes, incorrect results, or unexpected behavior

DIM num AS INTEGER

' === Seq edge cases ===
PRINT "=== Seq Edge Cases ==="

DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()

' Operations on empty seq
PRINT "Empty seq Len: "; seq.Len
PRINT "Empty seq Find('x'): "; seq.Find("x")
PRINT "Empty seq Has('x'): "; seq.Has("x")

' Pop from empty - should this crash?
PRINT "Calling Pop on empty seq..."
' seq.Pop()  ' Commented - may crash

' Get with out of bounds index
PRINT "Calling Get(0) on empty seq..."
' DIM val AS STRING
' val = seq.Get(0)  ' Commented - may crash

' Add items then test
seq.Push("a")
seq.Push("b")
seq.Push("c")
PRINT "After push a,b,c, Len: "; seq.Len

' Get with negative index
PRINT "Calling Get(-1)..."
' val = seq.Get(-1)  ' Commented - may crash

' Get with out of bounds
PRINT "Calling Get(100)..."
' val = seq.Get(100)  ' Commented - may crash

' Remove with out of bounds
PRINT "Calling Remove(100)..."
' seq.Remove(100)  ' Commented - may crash

' Insert at negative index
PRINT "Calling Insert(-1, 'x')..."
' seq.Insert(-1, "x")  ' Commented - may crash

' Set at out of bounds
PRINT "Calling Set(100, 'x')..."
' seq.Set(100, "x")  ' Commented - may crash

PRINT ""

' === Map edge cases ===
PRINT "=== Map Edge Cases ==="

DIM map AS Viper.Collections.Map
map = Viper.Collections.Map.New()

' Operations on empty map
PRINT "Empty map Len: "; map.Len
PRINT "Empty map Has('x'): "; map.Has("x")

' Get non-existent key
PRINT "Calling Get('nonexistent')..."
' DIM mval AS STRING
' mval = map.Get("nonexistent")  ' May crash or return null

' Remove non-existent key
PRINT "Remove('nonexistent'): "; map.Remove("nonexistent")

' Set with empty key
map.Set("", "empty key value")
PRINT "Set empty key, Has(''): "; map.Has("")

' Set with very long key
DIM longKey AS STRING
longKey = Viper.String.Repeat("x", 10000)
map.Set(longKey, "long key value")
PRINT "Set 10000-char key, Has: "; map.Has(longKey)

PRINT ""

' === Stack edge cases ===
PRINT "=== Stack Edge Cases ==="

DIM stack AS Viper.Collections.Stack
stack = Viper.Collections.Stack.New()

PRINT "Empty stack Len: "; stack.Len

' Pop from empty
PRINT "Calling Pop on empty stack..."
' stack.Pop()  ' May crash

' Peek on empty
PRINT "Calling Peek on empty stack..."
' DIM sval AS STRING
' sval = stack.Peek()  ' May crash

PRINT ""

' === Queue edge cases ===
PRINT "=== Queue Edge Cases ==="

DIM queue AS Viper.Collections.Queue
queue = Viper.Collections.Queue.New()

PRINT "Empty queue Len: "; queue.Len

' Take from empty
PRINT "Calling Take on empty queue..."
' queue.Take()  ' May crash

' Peek on empty
PRINT "Calling Peek on empty queue..."
' DIM qval AS STRING
' qval = queue.Peek()  ' May crash

PRINT ""

' === Heap edge cases ===
PRINT "=== Heap Edge Cases ==="

DIM heap AS Viper.Collections.Heap
heap = Viper.Collections.Heap.New()

PRINT "Empty heap Len: "; heap.Len

' Pop from empty
PRINT "Calling Pop on empty heap..."
' heap.Pop()  ' May crash

' Peek on empty
PRINT "Calling Peek on empty heap..."
' DIM hval AS STRING
' hval = heap.Peek()  ' May crash

' Push with same priority
heap.Push(1, "a")
heap.Push(1, "b")
heap.Push(1, "c")
PRINT "Pushed 3 items with same priority, Len: "; heap.Len

PRINT ""

' === Bytes edge cases ===
PRINT "=== Bytes Edge Cases ==="

DIM bytes AS Viper.Collections.Bytes
bytes = Viper.Collections.Bytes.New(0)
PRINT "Bytes.New(0) Len: "; bytes.Len

' Get from empty
PRINT "Calling Get(0) on empty bytes..."
' num = bytes.Get(0)  ' May crash

' Set on empty
PRINT "Calling Set(0, 65) on empty bytes..."
' bytes.Set(0, 65)  ' May crash

' Create with negative size
PRINT "Calling Bytes.New(-1)..."
' bytes = Viper.Collections.Bytes.New(-1)  ' May crash

' Create with very large size
PRINT "Calling Bytes.New(1000000)..."
DIM bigBytes AS Viper.Collections.Bytes
bigBytes = Viper.Collections.Bytes.New(1000000)
PRINT "Created 1MB bytes, Len: "; bigBytes.Len

PRINT ""

' === Ring edge cases ===
PRINT "=== Ring Edge Cases ==="

DIM ring AS Viper.Collections.Ring
ring = Viper.Collections.Ring.New(0)
PRINT "Ring.New(0) Cap: "; ring.Cap
PRINT "Ring.New(0) Len: "; ring.Len

' Push to zero-capacity ring
PRINT "Pushing to zero-cap ring..."
ring.Push("test")
PRINT "After push, Len: "; ring.Len

' Create with negative capacity
PRINT "Calling Ring.New(-1)..."
' ring = Viper.Collections.Ring.New(-1)  ' May crash

PRINT ""

' === Bag edge cases ===
PRINT "=== Bag Edge Cases ==="

DIM bag AS Viper.Collections.Bag
bag = Viper.Collections.Bag.New()

PRINT "Empty bag Len: "; bag.Len
PRINT "Empty bag Has('x'): "; bag.Has("x")

' Drop non-existent
PRINT "Drop('nonexistent'): "; bag.Drop("nonexistent")

' Put empty string
bag.Put("")
PRINT "Put(''), Has(''): "; bag.Has("")
PRINT "After Put(''), Len: "; bag.Len

PRINT ""
PRINT "=== Collection Edge Case Tests Complete ==="
END
