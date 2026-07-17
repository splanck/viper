' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Collections.StringSet.New
' COVER: Zanna.Collections.StringSet.IsEmpty
' COVER: Zanna.Collections.StringSet.Count
' COVER: Zanna.Collections.StringSet.Clear
' COVER: Zanna.Collections.StringSet.Intersect
' COVER: Zanna.Collections.StringSet.Difference
' COVER: Zanna.Collections.StringSet.Remove
' COVER: Zanna.Collections.StringSet.Has
' COVER: Zanna.Collections.StringSet.ToSeq
' COVER: Zanna.Collections.StringSet.Union
' COVER: Zanna.Collections.StringSet.Add
' COVER: Zanna.Collections.Bytes.New
' COVER: Zanna.Collections.Bytes.Length
' COVER: Zanna.Collections.Bytes.Clone
' COVER: Zanna.Collections.Bytes.Copy
' COVER: Zanna.Collections.Bytes.Fill
' COVER: Zanna.Collections.Bytes.Find
' COVER: Zanna.Collections.Bytes.Get
' COVER: Zanna.Collections.Bytes.Set
' COVER: Zanna.Collections.Bytes.Slice
' COVER: Zanna.Collections.Bytes.ToBase64
' COVER: Zanna.Collections.Bytes.ToHex
' COVER: Zanna.Collections.Bytes.ToStr
' COVER: Zanna.Collections.Bytes.FromBase64
' COVER: Zanna.Collections.Bytes.FromHex
' COVER: Zanna.Collections.Bytes.FromStr
' COVER: Zanna.Collections.Heap.New
' COVER: Zanna.Collections.Heap.NewMax
' COVER: Zanna.Collections.Heap.IsEmpty
' COVER: Zanna.Collections.Heap.IsMax
' COVER: Zanna.Collections.Heap.Count
' COVER: Zanna.Collections.Heap.Clear
' COVER: Zanna.Collections.Heap.Peek
' COVER: Zanna.Collections.Heap.Pop
' COVER: Zanna.Collections.Heap.Push
' COVER: Zanna.Collections.Heap.ToSeq
' COVER: Zanna.Collections.Heap.TryPeek
' COVER: Zanna.Collections.Heap.TryPop
' COVER: Zanna.Collections.List.New
' COVER: Zanna.Collections.List.get_Count
' COVER: Zanna.Collections.List.Push
' COVER: Zanna.Collections.List.Clear
' COVER: Zanna.Collections.List.Find
' COVER: Zanna.Collections.List.Has
' COVER: Zanna.Collections.List.Insert
' COVER: Zanna.Collections.List.Remove
' COVER: Zanna.Collections.List.RemoveAt
' COVER: Zanna.Collections.List.Get
' COVER: Zanna.Collections.List.Set
' COVER: Zanna.Collections.Map.New
' COVER: Zanna.Collections.Map.IsEmpty
' COVER: Zanna.Collections.Map.Count
' COVER: Zanna.Collections.Map.Clear
' COVER: Zanna.Collections.Map.Get
' COVER: Zanna.Collections.Map.GetOr
' COVER: Zanna.Collections.Map.Has
' COVER: Zanna.Collections.Map.Keys
' COVER: Zanna.Collections.Map.Remove
' COVER: Zanna.Collections.Map.Set
' COVER: Zanna.Collections.Map.SetIfMissing
' COVER: Zanna.Collections.Map.Values
' COVER: Zanna.Collections.Queue.New
' COVER: Zanna.Collections.Queue.IsEmpty
' COVER: Zanna.Collections.Queue.Count
' COVER: Zanna.Collections.Queue.Push
' COVER: Zanna.Collections.Queue.Clear
' COVER: Zanna.Collections.Queue.Peek
' COVER: Zanna.Collections.Queue.Pop
' COVER: Zanna.Collections.Ring.New
' COVER: Zanna.Collections.Ring.get_Capacity
' COVER: Zanna.Collections.Ring.IsEmpty
' COVER: Zanna.Collections.Ring.IsFull
' COVER: Zanna.Collections.Ring.Count
' COVER: Zanna.Collections.Ring.Clear
' COVER: Zanna.Collections.Ring.Get
' COVER: Zanna.Collections.Ring.Peek
' COVER: Zanna.Collections.Ring.Pop
' COVER: Zanna.Collections.Ring.Push
' COVER: Zanna.Collections.Seq.New
' COVER: Zanna.Collections.Seq.WithCapacity
' COVER: Zanna.Collections.Seq.get_Capacity
' COVER: Zanna.Collections.Seq.IsEmpty
' COVER: Zanna.Collections.Seq.get_Count
' COVER: Zanna.Collections.Seq.Clear
' COVER: Zanna.Collections.Seq.Clone
' COVER: Zanna.Collections.Seq.FindOption
' COVER: Zanna.Collections.Seq.First
' COVER: Zanna.Collections.Seq.Get
' COVER: Zanna.Collections.Seq.Has
' COVER: Zanna.Collections.Seq.Insert
' COVER: Zanna.Collections.Seq.Last
' COVER: Zanna.Collections.Seq.Peek
' COVER: Zanna.Collections.Seq.Pop
' COVER: Zanna.Collections.Seq.Push
' COVER: Zanna.Collections.Seq.PushAll
' COVER: Zanna.Collections.Seq.RemoveAt
' COVER: Zanna.Collections.Seq.Reverse
' COVER: Zanna.Collections.Seq.Set
' COVER: Zanna.Collections.Seq.Shuffle
' COVER: Zanna.Collections.Seq.Slice
' COVER: Zanna.Collections.Stack.New
' COVER: Zanna.Collections.Stack.IsEmpty
' COVER: Zanna.Collections.Stack.Count
' COVER: Zanna.Collections.Stack.Clear
' COVER: Zanna.Collections.Stack.Peek
' COVER: Zanna.Collections.Stack.Pop
' COVER: Zanna.Collections.Stack.Push
' COVER: Zanna.Collections.SortedMap.New
' COVER: Zanna.Collections.SortedMap.IsEmpty
' COVER: Zanna.Collections.SortedMap.Count
' COVER: Zanna.Collections.SortedMap.Ceiling
' COVER: Zanna.Collections.SortedMap.Clear
' COVER: Zanna.Collections.SortedMap.Remove
' COVER: Zanna.Collections.SortedMap.First
' COVER: Zanna.Collections.SortedMap.Floor
' COVER: Zanna.Collections.SortedMap.Get
' COVER: Zanna.Collections.SortedMap.Has
' COVER: Zanna.Collections.SortedMap.Keys
' COVER: Zanna.Collections.SortedMap.Last
' COVER: Zanna.Collections.SortedMap.Set
' COVER: Zanna.Collections.SortedMap.Values

DIM bag AS Zanna.Collections.StringSet
bag = NEW Zanna.Collections.StringSet()
Zanna.Core.Diagnostics.Assert(bag.IsEmpty, "bag.empty")
bag.Add("a")
bag.Add("b")
bag.Add("b")
Zanna.Core.Diagnostics.AssertEq(bag.Count, 2, "bag.len")
Zanna.Core.Diagnostics.Assert(bag.Has("a"), "bag.has")
bag.Remove("a")
Zanna.Core.Diagnostics.AssertEq(bag.Count, 1, "bag.remove")

DIM bag2 AS Zanna.Collections.StringSet
bag2 = NEW Zanna.Collections.StringSet()
bag2.Add("b")
bag2.Add("c")
DIM merged AS Zanna.Collections.StringSet
merged = bag.Union(bag2)
Zanna.Core.Diagnostics.AssertEq(merged.Count, 2, "bag.union")
DIM common AS Zanna.Collections.StringSet
common = bag.Intersect(bag2)
Zanna.Core.Diagnostics.AssertEq(common.Count, 1, "bag.intersect")
DIM diff AS Zanna.Collections.StringSet
diff = bag2.Difference(bag)
Zanna.Core.Diagnostics.AssertEq(diff.Count, 1, "bag.diff")
DIM items AS Zanna.Collections.Seq
items = bag.ToSeq()
Zanna.Core.Diagnostics.AssertEq(items.Count, 1, "bag.items")
bag.Clear()
Zanna.Core.Diagnostics.Assert(bag.IsEmpty, "bag.clear")

DIM bytes AS Zanna.Collections.Bytes
bytes = NEW Zanna.Collections.Bytes(4)
bytes.Set(0, 222)
bytes.Set(1, 173)
bytes.Set(2, 190)
bytes.Set(3, 239)
Zanna.Core.Diagnostics.AssertEq(bytes.Length, 4, "bytes.len")
Zanna.Core.Diagnostics.AssertEq(bytes.Get(0), 222, "bytes.get")
Zanna.Core.Diagnostics.AssertEqStr(bytes.ToHex(), "deadbeef", "bytes.hex")
Zanna.Core.Diagnostics.AssertEq(Zanna.Option.UnwrapOrI64(bytes.Find(190), -1), 2, "bytes.find")
DIM slice AS Zanna.Collections.Bytes
slice = bytes.Slice(1, 3)
Zanna.Core.Diagnostics.AssertEq(slice.Length, 2, "bytes.slice")
DIM copy AS Zanna.Collections.Bytes
copy = NEW Zanna.Collections.Bytes(4)
copy.Fill(0)
copy.Copy(0, bytes, 0, 4)
Zanna.Core.Diagnostics.AssertEqStr(copy.ToHex(), "deadbeef", "bytes.copy")
DIM clone AS Zanna.Collections.Bytes
clone = bytes.Clone()
clone.Set(0, 0)
Zanna.Core.Diagnostics.AssertEq(bytes.Get(0), 222, "bytes.clone")
Zanna.Core.Diagnostics.AssertEqStr(bytes.ToBase64(), "3q2+7w==", "bytes.base64")
DIM textBytes AS Zanna.Collections.Bytes
textBytes = Zanna.Collections.Bytes.FromStr("hello")
Zanna.Core.Diagnostics.AssertEqStr(textBytes.ToStr(), "hello", "bytes.tostr")

' Test FromBase64 and FromHex
DIM fromB64 AS Zanna.Collections.Bytes
fromB64 = Zanna.Collections.Bytes.FromBase64("3q2+7w==")
Zanna.Core.Diagnostics.AssertEqStr(fromB64.ToHex(), "deadbeef", "bytes.frombase64")
DIM fromHex AS Zanna.Collections.Bytes
fromHex = Zanna.Collections.Bytes.FromHex("48656c6c6f")
Zanna.Core.Diagnostics.AssertEqStr(fromHex.ToStr(), "Hello", "bytes.fromhex")

DIM heap AS Zanna.Collections.Heap
heap = NEW Zanna.Collections.Heap()
Zanna.Core.Diagnostics.Assert(heap.IsEmpty, "heap.empty")
Zanna.Core.Diagnostics.Assert(heap.IsMax = FALSE, "heap.ismax")

' Test NewMax for max-heap
DIM maxHeap AS Zanna.Collections.Heap
maxHeap = Zanna.Collections.Heap.NewMax(TRUE)
Zanna.Core.Diagnostics.Assert(maxHeap.IsMax, "heap.newmax.ismax")
maxHeap.Push(1, "low")
maxHeap.Push(5, "high")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(maxHeap.Peek()), "high", "heap.newmax.peek")
heap.Push(5, "high")
heap.Push(1, "low")
Zanna.Core.Diagnostics.AssertEq(heap.Count, 2, "heap.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(heap.Peek()), "low", "heap.peek")
    Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(heap.Pop()), "low", "heap.pop")
    DIM tryPeek AS OBJECT
    tryPeek = heap.TryPeek()
    Zanna.Core.Diagnostics.AssertNotNull(tryPeek, "heap.trypeek")
    DIM tryPop AS OBJECT
    tryPop = heap.TryPop()
    Zanna.Core.Diagnostics.AssertNotNull(tryPop, "heap.trypop")
DIM heapSeq AS Zanna.Collections.Seq
heap.Push(3, "mid")
heapSeq = heap.ToSeq()
Zanna.Core.Diagnostics.AssertEq(heapSeq.Count, 1, "heap.toseq")
heap.Clear()
Zanna.Core.Diagnostics.Assert(heap.IsEmpty, "heap.clear")

DIM list AS Zanna.Collections.List
DIM a AS Zanna.Collections.List
DIM b AS Zanna.Collections.List
DIM c AS Zanna.Collections.List
list = NEW Zanna.Collections.List()
a = NEW Zanna.Collections.List()
b = NEW Zanna.Collections.List()
c = NEW Zanna.Collections.List()
list.Push(a)
list.Push(c)
Zanna.Core.Diagnostics.AssertEq(Zanna.Option.UnwrapOrI64(list.FindOption(a), -1), 0, "list.find")
Zanna.Core.Diagnostics.Assert(list.Has(c), "list.has")
list.Insert(1, b)
Zanna.Core.Diagnostics.AssertEq(list.Count, 3, "list.count")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.RefEquals(list.Get(1), b), "list.get")
list.Set(1, a)
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.RefEquals(list.Get(1), a), "list.set")
list.Remove(a)
Zanna.Core.Diagnostics.AssertEq(list.Count, 2, "list.remove")
list.RemoveAt(0)
Zanna.Core.Diagnostics.AssertEq(list.Count, 1, "list.removeat")
list.Clear()
Zanna.Core.Diagnostics.AssertEq(list.Count, 0, "list.clear")

DIM m AS Zanna.Collections.Map
m = NEW Zanna.Collections.Map()
Zanna.Core.Diagnostics.Assert(m.IsEmpty, "map.empty")
m.Set("a", a)
m.Set("b", b)
Zanna.Core.Diagnostics.AssertEq(m.Count, 2, "map.len")
Zanna.Core.Diagnostics.Assert(m.Has("a"), "map.has")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.RefEquals(m.Get("a"), a), "map.get")
Zanna.Core.Diagnostics.Assert(Zanna.Core.Object.RefEquals(m.GetOr("z", c), c), "map.getor")
Zanna.Core.Diagnostics.Assert(m.SetIfMissing("a", c) = FALSE, "map.setifmissing")
Zanna.Core.Diagnostics.Assert(m.Remove("b"), "map.remove")
DIM keys AS Zanna.Collections.Seq
keys = m.Keys()
Zanna.Core.Diagnostics.AssertEq(keys.Count, 1, "map.keys")
DIM values AS Zanna.Collections.Seq
values = m.Values()
Zanna.Core.Diagnostics.AssertEq(values.Count, 1, "map.values")
m.Clear()
Zanna.Core.Diagnostics.Assert(m.IsEmpty, "map.clear")

DIM q AS Zanna.Collections.Queue
q = NEW Zanna.Collections.Queue()
Zanna.Core.Diagnostics.Assert(q.IsEmpty, "queue.empty")
q.Push("a")
q.Push("b")
Zanna.Core.Diagnostics.AssertEq(q.Count, 2, "queue.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(q.Peek()), "a", "queue.peek")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(q.Pop()), "a", "queue.take")
q.Clear()
Zanna.Core.Diagnostics.Assert(q.IsEmpty, "queue.clear")

DIM ring AS Zanna.Collections.Ring
ring = NEW Zanna.Collections.Ring(3)
Zanna.Core.Diagnostics.AssertEq(ring.Capacity, 3, "ring.cap")
Zanna.Core.Diagnostics.Assert(ring.IsEmpty, "ring.empty")
ring.Push("a")
ring.Push("b")
Zanna.Core.Diagnostics.AssertEq(ring.Count, 2, "ring.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(ring.Peek()), "a", "ring.peek")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(ring.Get(1)), "b", "ring.get")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(ring.Pop()), "a", "ring.pop")
ring.Push("c")
ring.Push("d")
Zanna.Core.Diagnostics.Assert(ring.IsFull, "ring.full")
ring.Clear()
Zanna.Core.Diagnostics.Assert(ring.IsEmpty, "ring.clear")

' Test Seq.WithCapacity
DIM seqCap AS Zanna.Collections.Seq
seqCap = Zanna.Collections.Seq.WithCapacity(100)
Zanna.Core.Diagnostics.Assert(seqCap.Capacity >= 100, "seq.withcapacity")

DIM seq AS Zanna.Collections.Seq
seq = Zanna.Collections.Seq.New()
Zanna.Core.Diagnostics.Assert(seq.IsEmpty, "seq.empty")
seq.Push("a")
seq.Push("b")
seq.Push("c")
Zanna.Core.Diagnostics.AssertEq(seq.Count, 3, "seq.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.First()), "a", "seq.first")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.Last()), "c", "seq.last")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.Peek()), "c", "seq.peek")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.Get(1)), "b", "seq.get")
Zanna.Core.Diagnostics.Assert(seq.Has("b"), "seq.has")
Zanna.Core.Diagnostics.AssertEq(Zanna.Option.UnwrapI64(seq.FindOption("b")), 1, "seq.find")
seq.Insert(1, "x")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(seq.Get(1)), "x", "seq.insert")
seq.Set(1, "b")
seq.RemoveAt(1)
Zanna.Core.Diagnostics.AssertEq(seq.Count, 3, "seq.remove")
DIM seq2 AS Zanna.Collections.Seq
seq2 = Zanna.Collections.Seq.New()
seq2.Push("d")
seq2.Push("e")
seq.PushAll(seq2)
Zanna.Core.Diagnostics.AssertEq(seq.Count, 5, "seq.pushall")
seq.Reverse()
seq.Shuffle()
DIM seqClone AS Zanna.Collections.Seq
seqClone = seq.Clone()
Zanna.Core.Diagnostics.AssertEq(seqClone.Count, seq.Count, "seq.clone")
DIM seqSlice AS Zanna.Collections.Seq
seqSlice = seq.Slice(0, 2)
Zanna.Core.Diagnostics.AssertEq(seqSlice.Count, 2, "seq.slice")
seq.Clear()
Zanna.Core.Diagnostics.Assert(seq.IsEmpty, "seq.clear")

DIM st AS Zanna.Collections.Stack
st = NEW Zanna.Collections.Stack()
Zanna.Core.Diagnostics.Assert(st.IsEmpty, "stack.empty")
st.Push("a")
st.Push("b")
Zanna.Core.Diagnostics.AssertEq(st.Count, 2, "stack.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(st.Peek()), "b", "stack.peek")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(st.Pop()), "b", "stack.pop")
st.Clear()
Zanna.Core.Diagnostics.Assert(st.IsEmpty, "stack.clear")

DIM tm AS Zanna.Collections.SortedMap
tm = NEW Zanna.Collections.SortedMap()
Zanna.Core.Diagnostics.Assert(tm.IsEmpty, "treemap.empty")
    tm.Set("a", "1")
    tm.Set("c", "3")
    tm.Set("e", "5")
Zanna.Core.Diagnostics.AssertEq(tm.Count, 3, "treemap.len")
    Zanna.Core.Diagnostics.AssertEqStr(tm.First(), "a", "treemap.first")
    Zanna.Core.Diagnostics.AssertEqStr(tm.Last(), "e", "treemap.last")
    Zanna.Core.Diagnostics.AssertEqStr(tm.Ceiling("b"), "c", "treemap.ceil")
    Zanna.Core.Diagnostics.AssertEqStr(tm.Floor("d"), "c", "treemap.floor")
    Zanna.Core.Diagnostics.AssertEqStr(Zanna.Core.Box.ToStr(tm.Get("c")), "3", "treemap.get")
Zanna.Core.Diagnostics.Assert(tm.Has("a"), "treemap.has")
tm.Remove("c")
Zanna.Core.Diagnostics.Assert(tm.Has("c") = FALSE, "treemap.remove")
DIM tmKeys AS Zanna.Collections.Seq
tmKeys = tm.Keys()
DIM tmVals AS Zanna.Collections.Seq
tmVals = tm.Values()
Zanna.Core.Diagnostics.AssertEq(tmKeys.Count, 2, "treemap.keys")
Zanna.Core.Diagnostics.AssertEq(tmVals.Count, 2, "treemap.values")
tm.Clear()
Zanna.Core.Diagnostics.Assert(tm.IsEmpty, "treemap.clear")

PRINT "RESULT: ok"
END
