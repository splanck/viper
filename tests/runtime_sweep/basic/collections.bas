' EXPECT_OUT: RESULT: ok
' COVER: Viper.Collections.Bag.New
' COVER: Viper.Collections.Bag.IsEmpty
' COVER: Viper.Collections.Bag.Len
' COVER: Viper.Collections.Bag.Clear
' COVER: Viper.Collections.Bag.Common
' COVER: Viper.Collections.Bag.Diff
' COVER: Viper.Collections.Bag.Drop
' COVER: Viper.Collections.Bag.Has
' COVER: Viper.Collections.Bag.Items
' COVER: Viper.Collections.Bag.Merge
' COVER: Viper.Collections.Bag.Put
' COVER: Viper.Collections.Bytes.New
' COVER: Viper.Collections.Bytes.Len
' COVER: Viper.Collections.Bytes.Clone
' COVER: Viper.Collections.Bytes.Copy
' COVER: Viper.Collections.Bytes.Fill
' COVER: Viper.Collections.Bytes.Find
' COVER: Viper.Collections.Bytes.Get
' COVER: Viper.Collections.Bytes.Set
' COVER: Viper.Collections.Bytes.Slice
' COVER: Viper.Collections.Bytes.ToBase64
' COVER: Viper.Collections.Bytes.ToHex
' COVER: Viper.Collections.Bytes.ToStr
' COVER: Viper.Collections.Bytes.FromBase64
' COVER: Viper.Collections.Bytes.FromHex
' COVER: Viper.Collections.Bytes.FromStr
' COVER: Viper.Collections.Heap.New
' COVER: Viper.Collections.Heap.NewMax
' COVER: Viper.Collections.Heap.IsEmpty
' COVER: Viper.Collections.Heap.IsMax
' COVER: Viper.Collections.Heap.Len
' COVER: Viper.Collections.Heap.Clear
' COVER: Viper.Collections.Heap.Peek
' COVER: Viper.Collections.Heap.Pop
' COVER: Viper.Collections.Heap.Push
' COVER: Viper.Collections.Heap.ToSeq
' COVER: Viper.Collections.Heap.TryPeek
' COVER: Viper.Collections.Heap.TryPop
' COVER: Viper.Collections.List.New
' COVER: Viper.Collections.List.Len
' COVER: Viper.Collections.List.Push
' COVER: Viper.Collections.List.Clear
' COVER: Viper.Collections.List.Find
' COVER: Viper.Collections.List.Has
' COVER: Viper.Collections.List.Insert
' COVER: Viper.Collections.List.Remove
' COVER: Viper.Collections.List.RemoveAt
' COVER: Viper.Collections.List.Get
' COVER: Viper.Collections.List.Set
' COVER: Viper.Collections.Map.New
' COVER: Viper.Collections.Map.IsEmpty
' COVER: Viper.Collections.Map.Len
' COVER: Viper.Collections.Map.Clear
' COVER: Viper.Collections.Map.Get
' COVER: Viper.Collections.Map.GetOr
' COVER: Viper.Collections.Map.Has
' COVER: Viper.Collections.Map.Keys
' COVER: Viper.Collections.Map.Remove
' COVER: Viper.Collections.Map.Set
' COVER: Viper.Collections.Map.SetIfMissing
' COVER: Viper.Collections.Map.Values
' COVER: Viper.Collections.Queue.New
' COVER: Viper.Collections.Queue.IsEmpty
' COVER: Viper.Collections.Queue.Len
' COVER: Viper.Collections.Queue.Push
' COVER: Viper.Collections.Queue.Clear
' COVER: Viper.Collections.Queue.Peek
' COVER: Viper.Collections.Queue.Pop
' COVER: Viper.Collections.Ring.New
' COVER: Viper.Collections.Ring.Cap
' COVER: Viper.Collections.Ring.IsEmpty
' COVER: Viper.Collections.Ring.IsFull
' COVER: Viper.Collections.Ring.Len
' COVER: Viper.Collections.Ring.Clear
' COVER: Viper.Collections.Ring.Get
' COVER: Viper.Collections.Ring.Peek
' COVER: Viper.Collections.Ring.Pop
' COVER: Viper.Collections.Ring.Push
' COVER: Viper.Collections.Seq.New
' COVER: Viper.Collections.Seq.WithCapacity
' COVER: Viper.Collections.Seq.Cap
' COVER: Viper.Collections.Seq.IsEmpty
' COVER: Viper.Collections.Seq.Len
' COVER: Viper.Collections.Seq.Clear
' COVER: Viper.Collections.Seq.Clone
' COVER: Viper.Collections.Seq.Find
' COVER: Viper.Collections.Seq.First
' COVER: Viper.Collections.Seq.Get
' COVER: Viper.Collections.Seq.Has
' COVER: Viper.Collections.Seq.Insert
' COVER: Viper.Collections.Seq.Last
' COVER: Viper.Collections.Seq.Peek
' COVER: Viper.Collections.Seq.Pop
' COVER: Viper.Collections.Seq.Push
' COVER: Viper.Collections.Seq.PushAll
' COVER: Viper.Collections.Seq.Remove
' COVER: Viper.Collections.Seq.Reverse
' COVER: Viper.Collections.Seq.Set
' COVER: Viper.Collections.Seq.Shuffle
' COVER: Viper.Collections.Seq.Slice
' COVER: Viper.Collections.Stack.New
' COVER: Viper.Collections.Stack.IsEmpty
' COVER: Viper.Collections.Stack.Len
' COVER: Viper.Collections.Stack.Clear
' COVER: Viper.Collections.Stack.Peek
' COVER: Viper.Collections.Stack.Pop
' COVER: Viper.Collections.Stack.Push
' COVER: Viper.Collections.TreeMap.New
' COVER: Viper.Collections.TreeMap.IsEmpty
' COVER: Viper.Collections.TreeMap.Len
' COVER: Viper.Collections.TreeMap.Ceil
' COVER: Viper.Collections.TreeMap.Clear
' COVER: Viper.Collections.TreeMap.Remove
' COVER: Viper.Collections.TreeMap.First
' COVER: Viper.Collections.TreeMap.Floor
' COVER: Viper.Collections.TreeMap.Get
' COVER: Viper.Collections.TreeMap.Has
' COVER: Viper.Collections.TreeMap.Keys
' COVER: Viper.Collections.TreeMap.Last
' COVER: Viper.Collections.TreeMap.Set
' COVER: Viper.Collections.TreeMap.Values

DIM bag AS Viper.Collections.Bag
bag = NEW Viper.Collections.Bag()
Viper.Core.Diagnostics.Assert(bag.IsEmpty, "bag.empty")
bag.Put("a")
bag.Put("b")
bag.Put("b")
Viper.Core.Diagnostics.AssertEq(bag.Len, 2, "bag.len")
Viper.Core.Diagnostics.Assert(bag.Has("a"), "bag.has")
bag.Drop("a")
Viper.Core.Diagnostics.AssertEq(bag.Len, 1, "bag.drop")

DIM bag2 AS Viper.Collections.Bag
bag2 = NEW Viper.Collections.Bag()
bag2.Put("b")
bag2.Put("c")
DIM merged AS Viper.Collections.Bag
merged = bag.Union(bag2)
Viper.Core.Diagnostics.AssertEq(merged.Len, 2, "bag.union")
DIM common AS Viper.Collections.Bag
common = bag.Intersect(bag2)
Viper.Core.Diagnostics.AssertEq(common.Len, 1, "bag.intersect")
DIM diff AS Viper.Collections.Bag
diff = bag2.Diff(bag)
Viper.Core.Diagnostics.AssertEq(diff.Len, 1, "bag.diff")
DIM items AS Viper.Collections.Seq
items = bag.Items()
Viper.Core.Diagnostics.AssertEq(items.Len, 1, "bag.items")
bag.Clear()
Viper.Core.Diagnostics.Assert(bag.IsEmpty, "bag.clear")

DIM bytes AS Viper.Collections.Bytes
bytes = NEW Viper.Collections.Bytes(4)
bytes.Set(0, 222)
bytes.Set(1, 173)
bytes.Set(2, 190)
bytes.Set(3, 239)
Viper.Core.Diagnostics.AssertEq(bytes.Len, 4, "bytes.len")
Viper.Core.Diagnostics.AssertEq(bytes.Get(0), 222, "bytes.get")
Viper.Core.Diagnostics.AssertEqStr(bytes.ToHex(), "deadbeef", "bytes.hex")
Viper.Core.Diagnostics.AssertEq(bytes.Find(190), 2, "bytes.find")
DIM slice AS Viper.Collections.Bytes
slice = bytes.Slice(1, 3)
Viper.Core.Diagnostics.AssertEq(slice.Len, 2, "bytes.slice")
DIM copy AS Viper.Collections.Bytes
copy = NEW Viper.Collections.Bytes(4)
copy.Fill(0)
copy.Copy(0, bytes, 0, 4)
Viper.Core.Diagnostics.AssertEqStr(copy.ToHex(), "deadbeef", "bytes.copy")
DIM clone AS Viper.Collections.Bytes
clone = bytes.Clone()
clone.Set(0, 0)
Viper.Core.Diagnostics.AssertEq(bytes.Get(0), 222, "bytes.clone")
Viper.Core.Diagnostics.AssertEqStr(bytes.ToBase64(), "3q2+7w==", "bytes.base64")
DIM textBytes AS Viper.Collections.Bytes
textBytes = Viper.Collections.Bytes.FromStr("hello")
Viper.Core.Diagnostics.AssertEqStr(textBytes.ToStr(), "hello", "bytes.tostr")

' Test FromBase64 and FromHex
DIM fromB64 AS Viper.Collections.Bytes
fromB64 = Viper.Collections.Bytes.FromBase64("3q2+7w==")
Viper.Core.Diagnostics.AssertEqStr(fromB64.ToHex(), "deadbeef", "bytes.frombase64")
DIM fromHex AS Viper.Collections.Bytes
fromHex = Viper.Collections.Bytes.FromHex("48656c6c6f")
Viper.Core.Diagnostics.AssertEqStr(fromHex.ToStr(), "Hello", "bytes.fromhex")

DIM heap AS Viper.Collections.Heap
heap = NEW Viper.Collections.Heap()
Viper.Core.Diagnostics.Assert(heap.IsEmpty, "heap.empty")
Viper.Core.Diagnostics.Assert(heap.IsMax = FALSE, "heap.ismax")

' Test NewMax for max-heap
DIM maxHeap AS Viper.Collections.Heap
maxHeap = Viper.Collections.Heap.NewMax(TRUE)
Viper.Core.Diagnostics.Assert(maxHeap.IsMax, "heap.newmax.ismax")
maxHeap.Push(1, "low")
maxHeap.Push(5, "high")
Viper.Core.Diagnostics.AssertEqStr(maxHeap.Peek(), "high", "heap.newmax.peek")
heap.Push(5, "high")
heap.Push(1, "low")
Viper.Core.Diagnostics.AssertEq(heap.Len, 2, "heap.len")
Viper.Core.Diagnostics.AssertEqStr(heap.Peek(), "low", "heap.peek")
    Viper.Core.Diagnostics.AssertEqStr(heap.Pop(), "low", "heap.pop")
    DIM tryPeek AS OBJECT
    tryPeek = heap.TryPeek()
    Viper.Core.Diagnostics.AssertNotNull(tryPeek, "heap.trypeek")
    DIM tryPop AS OBJECT
    tryPop = heap.TryPop()
    Viper.Core.Diagnostics.AssertNotNull(tryPop, "heap.trypop")
DIM heapSeq AS Viper.Collections.Seq
heap.Push(3, "mid")
heapSeq = heap.ToSeq()
Viper.Core.Diagnostics.AssertEq(heapSeq.Len, 1, "heap.toseq")
heap.Clear()
Viper.Core.Diagnostics.Assert(heap.IsEmpty, "heap.clear")

DIM list AS Viper.Collections.List
DIM a AS Viper.Collections.List
DIM b AS Viper.Collections.List
DIM c AS Viper.Collections.List
list = NEW Viper.Collections.List()
a = NEW Viper.Collections.List()
b = NEW Viper.Collections.List()
c = NEW Viper.Collections.List()
list.Push(a)
list.Push(c)
Viper.Core.Diagnostics.AssertEq(list.Find(a), 0, "list.find")
Viper.Core.Diagnostics.Assert(list.Has(c), "list.has")
list.Insert(1, b)
Viper.Core.Diagnostics.AssertEq(list.Len, 3, "list.count")
Viper.Core.Diagnostics.Assert(Viper.Core.Object.RefEquals(list.Get(1), b), "list.get")
list.Set(1, a)
Viper.Core.Diagnostics.Assert(Viper.Core.Object.RefEquals(list.Get(1), a), "list.set")
list.Remove(a)
Viper.Core.Diagnostics.AssertEq(list.Len, 2, "list.remove")
list.RemoveAt(0)
Viper.Core.Diagnostics.AssertEq(list.Len, 1, "list.removeat")
list.Clear()
Viper.Core.Diagnostics.AssertEq(list.Len, 0, "list.clear")

DIM m AS Viper.Collections.Map
m = NEW Viper.Collections.Map()
Viper.Core.Diagnostics.Assert(m.IsEmpty, "map.empty")
m.Set("a", a)
m.Set("b", b)
Viper.Core.Diagnostics.AssertEq(m.Len, 2, "map.len")
Viper.Core.Diagnostics.Assert(m.Has("a"), "map.has")
Viper.Core.Diagnostics.Assert(Viper.Core.Object.RefEquals(m.Get("a"), a), "map.get")
Viper.Core.Diagnostics.Assert(Viper.Core.Object.RefEquals(m.GetOr("z", c), c), "map.getor")
Viper.Core.Diagnostics.Assert(m.SetIfMissing("a", c) = 0, "map.setifmissing")
Viper.Core.Diagnostics.Assert(m.Remove("b"), "map.remove")
DIM keys AS Viper.Collections.Seq
keys = m.Keys()
Viper.Core.Diagnostics.AssertEq(keys.Len, 1, "map.keys")
DIM values AS Viper.Collections.Seq
values = m.Values()
Viper.Core.Diagnostics.AssertEq(values.Len, 1, "map.values")
m.Clear()
Viper.Core.Diagnostics.Assert(m.IsEmpty, "map.clear")

DIM q AS Viper.Collections.Queue
q = NEW Viper.Collections.Queue()
Viper.Core.Diagnostics.Assert(q.IsEmpty, "queue.empty")
q.Push("a")
q.Push("b")
Viper.Core.Diagnostics.AssertEq(q.Len, 2, "queue.len")
Viper.Core.Diagnostics.AssertEqStr(q.Peek(), "a", "queue.peek")
Viper.Core.Diagnostics.AssertEqStr(q.Pop(), "a", "queue.take")
q.Clear()
Viper.Core.Diagnostics.Assert(q.IsEmpty, "queue.clear")

DIM ring AS Viper.Collections.Ring
ring = NEW Viper.Collections.Ring(3)
Viper.Core.Diagnostics.AssertEq(ring.Cap, 3, "ring.cap")
Viper.Core.Diagnostics.Assert(ring.IsEmpty, "ring.empty")
ring.Push("a")
ring.Push("b")
Viper.Core.Diagnostics.AssertEq(ring.Len, 2, "ring.len")
Viper.Core.Diagnostics.AssertEqStr(ring.Peek(), "a", "ring.peek")
Viper.Core.Diagnostics.AssertEqStr(ring.Get(1), "b", "ring.get")
Viper.Core.Diagnostics.AssertEqStr(ring.Pop(), "a", "ring.pop")
ring.Push("c")
ring.Push("d")
Viper.Core.Diagnostics.Assert(ring.IsFull, "ring.full")
ring.Clear()
Viper.Core.Diagnostics.Assert(ring.IsEmpty, "ring.clear")

' Test Seq.WithCapacity
DIM seqCap AS Viper.Collections.Seq
seqCap = Viper.Collections.Seq.WithCapacity(100)
Viper.Core.Diagnostics.Assert(seqCap.Cap >= 100, "seq.withcapacity")

DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
Viper.Core.Diagnostics.Assert(seq.IsEmpty, "seq.empty")
seq.Push("a")
seq.Push("b")
seq.Push("c")
Viper.Core.Diagnostics.AssertEq(seq.Len, 3, "seq.len")
Viper.Core.Diagnostics.AssertEqStr(seq.First(), "a", "seq.first")
Viper.Core.Diagnostics.AssertEqStr(seq.Last(), "c", "seq.last")
Viper.Core.Diagnostics.AssertEqStr(seq.Peek(), "c", "seq.peek")
Viper.Core.Diagnostics.AssertEqStr(seq.Get(1), "b", "seq.get")
Viper.Core.Diagnostics.Assert(seq.Has("b"), "seq.has")
Viper.Core.Diagnostics.AssertEq(seq.Find("b"), 1, "seq.find")
seq.Insert(1, "x")
Viper.Core.Diagnostics.AssertEqStr(seq.Get(1), "x", "seq.insert")
seq.Set(1, "b")
seq.Remove(1)
Viper.Core.Diagnostics.AssertEq(seq.Len, 3, "seq.remove")
DIM seq2 AS Viper.Collections.Seq
seq2 = Viper.Collections.Seq.New()
seq2.Push("d")
seq2.Push("e")
seq.PushAll(seq2)
Viper.Core.Diagnostics.AssertEq(seq.Len, 5, "seq.pushall")
seq.Reverse()
seq.Shuffle()
DIM seqClone AS Viper.Collections.Seq
seqClone = seq.Clone()
Viper.Core.Diagnostics.AssertEq(seqClone.Len, seq.Len, "seq.clone")
DIM seqSlice AS Viper.Collections.Seq
seqSlice = seq.Slice(0, 2)
Viper.Core.Diagnostics.AssertEq(seqSlice.Len, 2, "seq.slice")
seq.Clear()
Viper.Core.Diagnostics.Assert(seq.IsEmpty, "seq.clear")

DIM st AS Viper.Collections.Stack
st = NEW Viper.Collections.Stack()
Viper.Core.Diagnostics.Assert(st.IsEmpty, "stack.empty")
st.Push("a")
st.Push("b")
Viper.Core.Diagnostics.AssertEq(st.Len, 2, "stack.len")
Viper.Core.Diagnostics.AssertEqStr(st.Peek(), "b", "stack.peek")
Viper.Core.Diagnostics.AssertEqStr(st.Pop(), "b", "stack.pop")
st.Clear()
Viper.Core.Diagnostics.Assert(st.IsEmpty, "stack.clear")

DIM tm AS Viper.Collections.TreeMap
tm = NEW Viper.Collections.TreeMap()
Viper.Core.Diagnostics.Assert(tm.IsEmpty, "treemap.empty")
    tm.Set("a", "1")
    tm.Set("c", "3")
    tm.Set("e", "5")
Viper.Core.Diagnostics.AssertEq(tm.Len, 3, "treemap.len")
    Viper.Core.Diagnostics.AssertEqStr(tm.First(), "a", "treemap.first")
    Viper.Core.Diagnostics.AssertEqStr(tm.Last(), "e", "treemap.last")
    Viper.Core.Diagnostics.AssertEqStr(tm.Ceil("b"), "c", "treemap.ceil")
    Viper.Core.Diagnostics.AssertEqStr(tm.Floor("d"), "c", "treemap.floor")
    Viper.Core.Diagnostics.AssertEqStr(tm.Get("c"), "3", "treemap.get")
Viper.Core.Diagnostics.Assert(tm.Has("a"), "treemap.has")
tm.Remove("c")
Viper.Core.Diagnostics.Assert(tm.Has("c") = 0, "treemap.remove")
DIM tmKeys AS Viper.Collections.Seq
tmKeys = tm.Keys()
DIM tmVals AS Viper.Collections.Seq
tmVals = tm.Values()
Viper.Core.Diagnostics.AssertEq(tmKeys.Len, 2, "treemap.keys")
Viper.Core.Diagnostics.AssertEq(tmVals.Len, 2, "treemap.values")
tm.Clear()
Viper.Core.Diagnostics.Assert(tm.IsEmpty, "treemap.clear")

PRINT "RESULT: ok"
END
