' seq_demo.bas - Comprehensive API audit for Viper.Collections.Seq
' Tests: New, WithCapacity, Push, Pop, Get, Set, Len, Cap, IsEmpty, Find, Has,
'        Insert, Remove, Clear, Clone, First, Last, Peek, Slice, Reverse,
'        Sort, SortDesc, Shuffle, Take, Drop, PushAll
' Note: Inline Viper.Core.Box.ToI64(s.Get(N)) can corrupt heap state for
'       subsequent calls. Store Get results in a variable first.

PRINT "=== Seq API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM s AS Viper.Collections.Seq
s = Viper.Collections.Seq.New()
PRINT s.Len       ' 0
PRINT s.IsEmpty   ' 1

' --- WithCapacity ---
PRINT "--- WithCapacity ---"
DIM sc AS Viper.Collections.Seq
sc = Viper.Collections.Seq.WithCapacity(10)
PRINT sc.Len      ' 0
PRINT sc.Cap      ' 10

' --- Push / Len / Cap ---
PRINT "--- Push / Len / Cap ---"
s.Push(Viper.Core.Box.I64(10))
s.Push(Viper.Core.Box.I64(20))
s.Push(Viper.Core.Box.I64(30))
PRINT s.Len       ' 3
PRINT s.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
DIM v0 AS OBJECT
v0 = s.Get(0)
PRINT Viper.Core.Box.ToI64(v0)  ' 10
DIM v1 AS OBJECT
v1 = s.Get(1)
PRINT Viper.Core.Box.ToI64(v1)  ' 20
DIM v2 AS OBJECT
v2 = s.Get(2)
PRINT Viper.Core.Box.ToI64(v2)  ' 30

' --- Set ---
PRINT "--- Set ---"
s.Set(1, Viper.Core.Box.I64(25))
DIM sv AS OBJECT
sv = s.Get(1)
PRINT Viper.Core.Box.ToI64(sv)  ' 25

' --- First / Last / Peek ---
PRINT "--- First / Last / Peek ---"
DIM fv AS OBJECT
fv = s.First()
PRINT Viper.Core.Box.ToI64(fv)  ' 10
DIM lv AS OBJECT
lv = s.Last()
PRINT Viper.Core.Box.ToI64(lv)  ' 30
DIM pk AS OBJECT
pk = s.Peek()
PRINT Viper.Core.Box.ToI64(pk)  ' 30

' --- Find / Has ---
PRINT "--- Find / Has ---"
PRINT s.Find(Viper.Core.Box.I64(10))   ' 0
PRINT s.Find(Viper.Core.Box.I64(99))   ' -1
PRINT s.Has(Viper.Core.Box.I64(30))    ' 1
PRINT s.Has(Viper.Core.Box.I64(99))    ' 0

' --- Insert ---
PRINT "--- Insert ---"
s.Insert(1, Viper.Core.Box.I64(15))
PRINT s.Len                             ' 4
DIM iv1 AS OBJECT
iv1 = s.Get(1)
PRINT Viper.Core.Box.ToI64(iv1)        ' 15
DIM iv2 AS OBJECT
iv2 = s.Get(2)
PRINT Viper.Core.Box.ToI64(iv2)        ' 25

' --- Remove ---
PRINT "--- Remove ---"
DIM removed AS OBJECT
removed = s.Remove(1)
PRINT Viper.Core.Box.ToI64(removed)    ' 15
PRINT s.Len                             ' 3

' --- Pop ---
PRINT "--- Pop ---"
DIM popped AS OBJECT
popped = s.Pop()
PRINT Viper.Core.Box.ToI64(popped)     ' 30
PRINT s.Len                             ' 2

' --- Clone ---
PRINT "--- Clone ---"
DIM c AS Viper.Collections.Seq
c = s.Clone()
PRINT c.Len                             ' 2
DIM cv0 AS OBJECT
cv0 = c.Get(0)
PRINT Viper.Core.Box.ToI64(cv0)        ' 10
DIM cv1 AS OBJECT
cv1 = c.Get(1)
PRINT Viper.Core.Box.ToI64(cv1)        ' 25

' --- Slice ---
PRINT "--- Slice ---"
s.Push(Viper.Core.Box.I64(30))
s.Push(Viper.Core.Box.I64(40))
DIM sl AS Viper.Collections.Seq
sl = s.Slice(1, 3)
PRINT sl.Len                            ' 2
DIM slv0 AS OBJECT
slv0 = sl.Get(0)
PRINT Viper.Core.Box.ToI64(slv0)       ' 25
DIM slv1 AS OBJECT
slv1 = sl.Get(1)
PRINT Viper.Core.Box.ToI64(slv1)       ' 30

' --- Reverse ---
PRINT "--- Reverse ---"
s.Reverse()
DIM rv0 AS OBJECT
rv0 = s.Get(0)
PRINT Viper.Core.Box.ToI64(rv0)        ' 40
DIM rv3 AS OBJECT
rv3 = s.Get(3)
PRINT Viper.Core.Box.ToI64(rv3)        ' 10

' --- Sort ---
PRINT "--- Sort ---"
s.Sort()
DIM so0 AS OBJECT
so0 = s.Get(0)
PRINT Viper.Core.Box.ToI64(so0)        ' 10
DIM so3 AS OBJECT
so3 = s.Get(3)
PRINT Viper.Core.Box.ToI64(so3)        ' 40

' --- SortDesc ---
PRINT "--- SortDesc ---"
s.SortDesc()
DIM sd0 AS OBJECT
sd0 = s.Get(0)
PRINT Viper.Core.Box.ToI64(sd0)        ' 40
DIM sd3 AS OBJECT
sd3 = s.Get(3)
PRINT Viper.Core.Box.ToI64(sd3)        ' 10

' --- Shuffle ---
PRINT "--- Shuffle ---"
s.Shuffle()
PRINT s.Len                             ' 4

' --- Take ---
PRINT "--- Take ---"
s.Sort()
DIM t AS Viper.Collections.Seq
t = s.Take(2)
PRINT t.Len                             ' 2
DIM tv0 AS OBJECT
tv0 = t.Get(0)
PRINT Viper.Core.Box.ToI64(tv0)        ' 10

' --- Drop ---
PRINT "--- Drop ---"
DIM d AS Viper.Collections.Seq
d = s.Drop(2)
PRINT d.Len                             ' 2
DIM dv0 AS OBJECT
dv0 = d.Get(0)
PRINT Viper.Core.Box.ToI64(dv0)        ' 30

' --- PushAll ---
PRINT "--- PushAll ---"
DIM a AS Viper.Collections.Seq
a = Viper.Collections.Seq.New()
a.Push(Viper.Core.Box.I64(1))
a.Push(Viper.Core.Box.I64(2))
DIM b AS Viper.Collections.Seq
b = Viper.Collections.Seq.New()
b.Push(Viper.Core.Box.I64(3))
b.Push(Viper.Core.Box.I64(4))
a.PushAll(b)
PRINT a.Len                             ' 4
DIM av2 AS OBJECT
av2 = a.Get(2)
PRINT Viper.Core.Box.ToI64(av2)        ' 3
DIM av3 AS OBJECT
av3 = a.Get(3)
PRINT Viper.Core.Box.ToI64(av3)        ' 4

' --- Clear ---
PRINT "--- Clear ---"
a.Clear()
PRINT a.Len                             ' 0
PRINT a.IsEmpty                         ' 1

PRINT "=== Seq audit complete ==="
END
