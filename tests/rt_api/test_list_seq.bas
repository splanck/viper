' test_list_seq.bas — Viper.Collections.List + Seq
DIM l AS Viper.Collections.List
l = Viper.Collections.List.New()
l.Push("a")
l.Push("b")
l.Push("c")
PRINT "len: "; l.Len
PRINT "find b: "; l.Find("b")
PRINT "has b: "; l.Has("b")
l.Set(1, "B")
l.Insert(0, "z")
PRINT "len after insert: "; l.Len
l.RemoveAt(0)
PRINT "len after removeat: "; l.Len
l.Remove("a")
PRINT "len after remove: "; l.Len
l.Reverse()
l.Clear()
PRINT "len after clear: "; l.Len

DIM s AS Viper.Collections.Seq
s = Viper.Collections.Seq.New()
s.Push("x")
s.Push("y")
s.Push("z")
PRINT "seq len: "; s.Len
PRINT "seq find y: "; s.Find("y")
PRINT "seq has y: "; s.Has("y")
PRINT "seq isempty: "; s.IsEmpty
s.Reverse()
DIM s2 AS Viper.Collections.Seq
s2 = s.Clone()
PRINT "clone len: "; s2.Len
DIM s3 AS Viper.Collections.Seq
s3 = s.Slice(0, 2)
PRINT "slice len: "; s3.Len
DIM s4 AS Viper.Collections.Seq
s4 = s.Take(2)
PRINT "take len: "; s4.Len
DIM s5 AS Viper.Collections.Seq
s5 = s.Drop(1)
PRINT "drop len: "; s5.Len
s.Sort()
s.SortDesc()
s.Clear()
PRINT "seq len after clear: "; s.Len
PRINT "seq isempty: "; s.IsEmpty
PRINT "done"
END
