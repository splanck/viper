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
' COVER: Viper.Collections.Heap.New
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
' COVER: Viper.Collections.List.Count
' COVER: Viper.Collections.List.Add
' COVER: Viper.Collections.List.Clear
' COVER: Viper.Collections.List.Find
' COVER: Viper.Collections.List.Has
' COVER: Viper.Collections.List.Insert
' COVER: Viper.Collections.List.Remove
' COVER: Viper.Collections.List.RemoveAt
' COVER: Viper.Collections.List.get_Item
' COVER: Viper.Collections.List.set_Item
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
' COVER: Viper.Collections.Queue.Add
' COVER: Viper.Collections.Queue.Clear
' COVER: Viper.Collections.Queue.Peek
' COVER: Viper.Collections.Queue.Take
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
' COVER: Viper.Collections.TreeMap.Drop
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
Viper.Diagnostics.Assert(bag.IsEmpty, "bag.empty")
bag.Put("a")
bag.Put("b")
bag.Put("b")
Viper.Diagnostics.AssertEq(bag.Len, 2, "bag.len")
Viper.Diagnostics.Assert(bag.Has("a"), "bag.has")
bag.Drop("a")
Viper.Diagnostics.AssertEq(bag.Len, 1, "bag.drop")

DIM bag2 AS Viper.Collections.Bag
bag2 = NEW Viper.Collections.Bag()
bag2.Put("b")
bag2.Put("c")
DIM merged AS Viper.Collections.Bag
merged = bag.Merge(bag2)
Viper.Diagnostics.AssertEq(merged.Len, 2, "bag.merge")
DIM common AS Viper.Collections.Bag
common = bag.Common(bag2)
Viper.Diagnostics.AssertEq(common.Len, 1, "bag.common")
DIM diff AS Viper.Collections.Bag
diff = bag2.Diff(bag)
Viper.Diagnostics.AssertEq(diff.Len, 1, "bag.diff")
DIM items AS Viper.Collections.Seq
items = bag.Items()
Viper.Diagnostics.AssertEq(items.Len, 1, "bag.items")
bag.Clear()
Viper.Diagnostics.Assert(bag.IsEmpty, "bag.clear")

DIM bytes AS Viper.Collections.Bytes
bytes = NEW Viper.Collections.Bytes(4)
bytes.Set(0, &HDE)
bytes.Set(1, &HAD)
bytes.Set(2, &HBE)
bytes.Set(3, &HEF)
Viper.Diagnostics.AssertEq(bytes.Len, 4, "bytes.len")
Viper.Diagnostics.AssertEq(bytes.Get(0), &HDE, "bytes.get")
Viper.Diagnostics.AssertEqStr(bytes.ToHex(), "deadbeef", "bytes.hex")
Viper.Diagnostics.AssertEq(bytes.Find(&HBE), 2, "bytes.find")
DIM slice AS Viper.Collections.Bytes
slice = bytes.Slice(1, 3)
Viper.Diagnostics.AssertEq(slice.Len, 2, "bytes.slice")
DIM copy AS Viper.Collections.Bytes
copy = NEW Viper.Collections.Bytes(4)
copy.Fill(0)
copy.Copy(0, bytes, 0, 4)
Viper.Diagnostics.AssertEqStr(copy.ToHex(), "deadbeef", "bytes.copy")
DIM clone AS Viper.Collections.Bytes
clone = bytes.Clone()
clone.Set(0, 0)
Viper.Diagnostics.AssertEq(bytes.Get(0), &HDE, "bytes.clone")
Viper.Diagnostics.AssertEqStr(bytes.ToBase64(), "3q2+7w==", "bytes.base64")
Viper.Diagnostics.AssertEqStr(bytes.ToStr(), Viper.Strings.FromStr("Þ­¾ï"), "bytes.tostr")

DIM heap AS Viper.Collections.Heap
heap = NEW Viper.Collections.Heap()
Viper.Diagnostics.Assert(heap.IsEmpty, "heap.empty")
Viper.Diagnostics.Assert(heap.IsMax = 0, "heap.ismax")
heap.Push(5, "high")
heap.Push(1, "low")
Viper.Diagnostics.AssertEq(heap.Len, 2, "heap.len")
Viper.Diagnostics.AssertEqStr(heap.Peek(), "low", "heap.peek")
DIM hval AS STRING
hval = heap.Pop()
Viper.Diagnostics.AssertEqStr(hval, "low", "heap.pop")
DIM tryPeek AS STRING
tryPeek = heap.TryPeek()
Viper.Diagnostics.Assert(tryPeek <> "", "heap.trypeek")
DIM tryPop AS STRING
tryPop = heap.TryPop()
Viper.Diagnostics.Assert(tryPop <> "", "heap.trypop")
DIM heapSeq AS Viper.Collections.Seq
heap.Push(3, "mid")
heapSeq = heap.ToSeq()
Viper.Diagnostics.AssertEq(heapSeq.Len, 1, "heap.toseq")
heap.Clear()
Viper.Diagnostics.Assert(heap.IsEmpty, "heap.clear")

DIM list AS Viper.Collections.List
DIM a AS Viper.Collections.List
DIM b AS Viper.Collections.List
DIM c AS Viper.Collections.List
list = NEW Viper.Collections.List()
a = NEW Viper.Collections.List()
b = NEW Viper.Collections.List()
c = NEW Viper.Collections.List()
list.Add(a)
list.Add(c)
Viper.Diagnostics.AssertEq(list.Find(a), 0, "list.find")
Viper.Diagnostics.Assert(list.Has(c), "list.has")
list.Insert(1, b)
Viper.Diagnostics.AssertEq(list.Count, 3, "list.count")
Viper.Diagnostics.AssertEq(list.get_Item(1), b, "list.get")
list.set_Item(1, a)
Viper.Diagnostics.AssertEq(list.get_Item(1), a, "list.set")
list.Remove(a)
Viper.Diagnostics.AssertEq(list.Count, 2, "list.remove")
list.RemoveAt(0)
Viper.Diagnostics.AssertEq(list.Count, 1, "list.removeat")
list.Clear()
Viper.Diagnostics.AssertEq(list.Count, 0, "list.clear")

DIM m AS Viper.Collections.Map
m = NEW Viper.Collections.Map()
Viper.Diagnostics.Assert(m.IsEmpty, "map.empty")
m.Set("a", a)
m.Set("b", b)
Viper.Diagnostics.AssertEq(m.Len, 2, "map.len")
Viper.Diagnostics.Assert(m.Has("a"), "map.has")
Viper.Diagnostics.AssertEq(m.Get("a"), a, "map.get")
Viper.Diagnostics.AssertEq(m.GetOr("z", c), c, "map.getor")
Viper.Diagnostics.Assert(m.SetIfMissing("a", c) = 0, "map.setifmissing")
Viper.Diagnostics.Assert(m.Remove("b"), "map.remove")
DIM keys AS Viper.Collections.Seq
keys = m.Keys()
Viper.Diagnostics.AssertEq(keys.Len, 1, "map.keys")
DIM values AS Viper.Collections.Seq
values = m.Values()
Viper.Diagnostics.AssertEq(values.Len, 1, "map.values")
m.Clear()
Viper.Diagnostics.Assert(m.IsEmpty, "map.clear")

DIM q AS Viper.Collections.Queue
q = NEW Viper.Collections.Queue()
Viper.Diagnostics.Assert(q.IsEmpty, "queue.empty")
q.Add("a")
q.Add("b")
Viper.Diagnostics.AssertEq(q.Len, 2, "queue.len")
Viper.Diagnostics.AssertEqStr(q.Peek(), "a", "queue.peek")
Viper.Diagnostics.AssertEqStr(q.Take(), "a", "queue.take")
q.Clear()
Viper.Diagnostics.Assert(q.IsEmpty, "queue.clear")

DIM ring AS Viper.Collections.Ring
ring = NEW Viper.Collections.Ring(3)
Viper.Diagnostics.AssertEq(ring.Cap, 3, "ring.cap")
Viper.Diagnostics.Assert(ring.IsEmpty, "ring.empty")
ring.Push("a")
ring.Push("b")
Viper.Diagnostics.AssertEq(ring.Len, 2, "ring.len")
Viper.Diagnostics.AssertEqStr(ring.Peek(), "a", "ring.peek")
Viper.Diagnostics.AssertEqStr(ring.Get(1), "b", "ring.get")
Viper.Diagnostics.AssertEqStr(ring.Pop(), "a", "ring.pop")
ring.Push("c")
ring.Push("d")
Viper.Diagnostics.Assert(ring.IsFull, "ring.full")
ring.Clear()
Viper.Diagnostics.Assert(ring.IsEmpty, "ring.clear")

DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
Viper.Diagnostics.Assert(seq.IsEmpty, "seq.empty")
seq.Push("a")
seq.Push("b")
seq.Push("c")
Viper.Diagnostics.AssertEq(seq.Len, 3, "seq.len")
Viper.Diagnostics.AssertEqStr(seq.First(), "a", "seq.first")
Viper.Diagnostics.AssertEqStr(seq.Last(), "c", "seq.last")
Viper.Diagnostics.AssertEqStr(seq.Peek(), "c", "seq.peek")
Viper.Diagnostics.AssertEqStr(seq.Get(1), "b", "seq.get")
Viper.Diagnostics.Assert(seq.Has("b"), "seq.has")
Viper.Diagnostics.AssertEq(seq.Find("b"), 1, "seq.find")
seq.Insert(1, "x")
Viper.Diagnostics.AssertEqStr(seq.Get(1), "x", "seq.insert")
seq.Set(1, "b")
seq.Remove(1)
Viper.Diagnostics.AssertEq(seq.Len, 3, "seq.remove")
DIM seq2 AS Viper.Collections.Seq
seq2 = Viper.Collections.Seq.New()
seq2.Push("d")
seq2.Push("e")
seq.PushAll(seq2)
Viper.Diagnostics.AssertEq(seq.Len, 5, "seq.pushall")
seq.Reverse()
seq.Shuffle()
DIM seqClone AS Viper.Collections.Seq
seqClone = seq.Clone()
Viper.Diagnostics.AssertEq(seqClone.Len, seq.Len, "seq.clone")
DIM seqSlice AS Viper.Collections.Seq
seqSlice = seq.Slice(0, 2)
Viper.Diagnostics.AssertEq(seqSlice.Len, 2, "seq.slice")
seq.Clear()
Viper.Diagnostics.Assert(seq.IsEmpty, "seq.clear")

DIM st AS Viper.Collections.Stack
st = NEW Viper.Collections.Stack()
Viper.Diagnostics.Assert(st.IsEmpty, "stack.empty")
st.Push("a")
st.Push("b")
Viper.Diagnostics.AssertEq(st.Len, 2, "stack.len")
Viper.Diagnostics.AssertEqStr(st.Peek(), "b", "stack.peek")
Viper.Diagnostics.AssertEqStr(st.Pop(), "b", "stack.pop")
st.Clear()
Viper.Diagnostics.Assert(st.IsEmpty, "stack.clear")

DIM tm AS Viper.Collections.TreeMap
tm = NEW Viper.Collections.TreeMap()
Viper.Diagnostics.Assert(tm.IsEmpty, "treemap.empty")
tm.Set("a", "1")
tm.Set("c", "3")
tm.Set("e", "5")
Viper.Diagnostics.AssertEq(tm.Len, 3, "treemap.len")
Viper.Diagnostics.AssertEqStr(tm.First(), "a", "treemap.first")
Viper.Diagnostics.AssertEqStr(tm.Last(), "e", "treemap.last")
Viper.Diagnostics.AssertEqStr(tm.Ceil("b"), "c", "treemap.ceil")
Viper.Diagnostics.AssertEqStr(tm.Floor("d"), "c", "treemap.floor")
Viper.Diagnostics.AssertEqStr(tm.Get("c"), "3", "treemap.get")
Viper.Diagnostics.Assert(tm.Has("a"), "treemap.has")
tm.Drop("c")
Viper.Diagnostics.Assert(tm.Has("c") = 0, "treemap.drop")
DIM tmKeys AS Viper.Collections.Seq
tmKeys = tm.Keys()
DIM tmVals AS Viper.Collections.Seq
tmVals = tm.Values()
Viper.Diagnostics.AssertEq(tmKeys.Len, 2, "treemap.keys")
Viper.Diagnostics.AssertEq(tmVals.Len, 2, "treemap.values")
tm.Clear()
Viper.Diagnostics.Assert(tm.IsEmpty, "treemap.clear")

PRINT "RESULT: ok"
END
