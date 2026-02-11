' test_map_bag.bas — Map, Bag, CountMap, OrderedMap
DIM m AS Viper.Collections.Map
m = Viper.Collections.Map.New()
PRINT "map empty: "; m.IsEmpty
m.Set("a", "1")
m.Set("b", "2")
m.Set("c", "3")
PRINT "map len: "; m.Len
PRINT "has a: "; m.Has("a")
PRINT "has z: "; m.Has("z")
m.SetIfMissing("a", "99")
m.Remove("b")
PRINT "map len after remove: "; m.Len
m.Clear()
PRINT "map empty after clear: "; m.IsEmpty

DIM b AS Viper.Collections.Bag
b = Viper.Collections.Bag.New()
PRINT "bag empty: "; b.IsEmpty
b.Put("x")
b.Put("y")
b.Put("z")
b.Put("x")
PRINT "bag len: "; b.Len
PRINT "bag has x: "; b.Has("x")
PRINT "bag has w: "; b.Has("w")
b.Drop("x")
PRINT "bag len after drop: "; b.Len
b.Clear()
PRINT "bag empty after clear: "; b.IsEmpty

DIM cm AS Viper.Collections.CountMap
cm = Viper.Collections.CountMap.New()
cm.Inc("a")
cm.Inc("a")
cm.Inc("b")
cm.IncBy("c", 5)
PRINT "cm get a: "; cm.Get("a")
PRINT "cm get c: "; cm.Get("c")
PRINT "cm total: "; cm.Total
PRINT "cm len: "; cm.Len
cm.Dec("a")
PRINT "cm get a after dec: "; cm.Get("a")
cm.Set("d", 10)
PRINT "cm get d: "; cm.Get("d")
PRINT "cm has d: "; cm.Has("d")
cm.Remove("d")
PRINT "cm has d after remove: "; cm.Has("d")

DIM om AS Viper.Collections.OrderedMap
om = Viper.Collections.OrderedMap.New()
om.Set("first", "1")
om.Set("second", "2")
om.Set("third", "3")
PRINT "om len: "; om.Len
PRINT "om keyat 0: "; om.KeyAt(0)
PRINT "om has first: "; om.Has("first")
om.Remove("second")
PRINT "om len after remove: "; om.Len

PRINT "done"
END
