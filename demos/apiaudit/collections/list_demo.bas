' list_demo.bas - Comprehensive API audit for Viper.Collections.List
' Tests: New, Push, Pop, Get, Set, Len, IsEmpty, Find, Has, Insert,
'        Remove, RemoveAt, Clear, Flip, First, Last, Slice

PRINT "=== List API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM list AS Viper.Collections.List
list = Viper.Collections.List.New()
PRINT list.Len       ' 0
PRINT list.IsEmpty   ' 1

' --- Push / Len ---
PRINT "--- Push / Len ---"
DIM a AS OBJECT = Viper.Core.Box.Str("apple")
DIM b AS OBJECT = Viper.Core.Box.Str("banana")
DIM c AS OBJECT = Viper.Core.Box.Str("cherry")
list.Push(a)
list.Push(b)
list.Push(c)
PRINT list.Len       ' 3
PRINT list.IsEmpty   ' 0

' --- Get ---
PRINT "--- Get ---"
PRINT Viper.Core.Box.ToStr(list.Get(0))  ' apple
PRINT Viper.Core.Box.ToStr(list.Get(1))  ' banana
PRINT Viper.Core.Box.ToStr(list.Get(2))  ' cherry

' --- Set ---
PRINT "--- Set ---"
DIM d AS OBJECT = Viper.Core.Box.Str("date")
list.Set(1, d)
PRINT Viper.Core.Box.ToStr(list.Get(1))  ' date

' --- First / Last ---
PRINT "--- First / Last ---"
PRINT Viper.Core.Box.ToStr(list.First())  ' apple
PRINT Viper.Core.Box.ToStr(list.Last())   ' cherry

' --- Find / Has ---
PRINT "--- Find / Has ---"
PRINT list.Find(a)    ' 0
PRINT list.Find(b)    ' -1 (was replaced)
PRINT list.Has(a)     ' 1
PRINT list.Has(b)     ' 0

' --- Insert ---
PRINT "--- Insert ---"
DIM e AS OBJECT = Viper.Core.Box.Str("elderberry")
list.Insert(1, e)
PRINT list.Len                            ' 4
PRINT Viper.Core.Box.ToStr(list.Get(1))  ' elderberry
PRINT Viper.Core.Box.ToStr(list.Get(2))  ' date

' --- Remove (by reference) ---
PRINT "--- Remove ---"
PRINT list.Remove(e)   ' 1
PRINT list.Len          ' 3
PRINT list.Remove(e)   ' 0 (already removed)

' --- RemoveAt ---
PRINT "--- RemoveAt ---"
list.RemoveAt(1)
PRINT list.Len                            ' 2
PRINT Viper.Core.Box.ToStr(list.Get(0))  ' apple
PRINT Viper.Core.Box.ToStr(list.Get(1))  ' cherry

' --- Slice ---
PRINT "--- Slice ---"
list.Push(Viper.Core.Box.Str("fig"))
list.Push(Viper.Core.Box.Str("grape"))
DIM sl AS Viper.Collections.List
sl = list.Slice(1, 3)
PRINT sl.Len                              ' 2
PRINT Viper.Core.Box.ToStr(sl.Get(0))    ' cherry
PRINT Viper.Core.Box.ToStr(sl.Get(1))    ' fig

' --- Flip ---
PRINT "--- Flip ---"
list.Flip()
PRINT Viper.Core.Box.ToStr(list.Get(0))  ' grape
PRINT Viper.Core.Box.ToStr(list.Get(3))  ' apple

' --- Pop ---
PRINT "--- Pop ---"
DIM popped AS OBJECT
popped = list.Pop()
PRINT Viper.Core.Box.ToStr(popped)       ' apple
PRINT list.Len                            ' 3

' --- Clear ---
PRINT "--- Clear ---"
list.Clear()
PRINT list.Len       ' 0
PRINT list.IsEmpty   ' 1

PRINT "=== List audit complete ==="
END
