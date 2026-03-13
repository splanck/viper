' test_treemap_trie.bas — TreeMap, Trie, SortedSet, BiMap
DIM tm AS Viper.Collections.TreeMap
tm = Viper.Collections.TreeMap.New()
PRINT "tm empty: "; tm.IsEmpty
tm.Set("banana", "yellow")
tm.Set("apple", "red")
tm.Set("cherry", "dark")
PRINT "tm len: "; tm.Length
PRINT "tm has banana: "; tm.Has("banana")
PRINT "tm first: "; tm.First()
PRINT "tm last: "; tm.Last()
PRINT "tm floor blueberry: "; tm.Floor("blueberry")
PRINT "tm ceil blueberry: "; tm.Ceil("blueberry")
tm.Drop("apple")
PRINT "tm len after drop: "; tm.Length

DIM tr AS Viper.Collections.Trie
tr = Viper.Collections.Trie.New()
PRINT "tr empty: "; tr.IsEmpty
tr.Set("hello", "1")
tr.Set("help", "2")
tr.Set("world", "3")
PRINT "tr len: "; tr.Length
PRINT "tr has help: "; tr.Has("help")
PRINT "tr hasprefix hel: "; tr.HasPrefix("hel")
PRINT "tr hasprefix xyz: "; tr.HasPrefix("xyz")
PRINT "tr longestprefix helping: "; tr.LongestPrefix("helping")
tr.Remove("help")
PRINT "tr len after remove: "; tr.Length

DIM ss AS Viper.Collections.SortedSet
ss = Viper.Collections.SortedSet.New()
PRINT "ss empty: "; ss.IsEmpty
ss.Add("cherry")
ss.Add("apple")
ss.Add("banana")
PRINT "ss len: "; ss.Length
PRINT "ss first: "; ss.First()
PRINT "ss last: "; ss.Last()
PRINT "ss has apple: "; ss.Has("apple")
PRINT "ss at 1: "; ss.At(1)
PRINT "ss indexof banana: "; ss.IndexOf("banana")
PRINT "ss floor blueberry: "; ss.Floor("blueberry")
PRINT "ss ceil blueberry: "; ss.Ceil("blueberry")
ss.Remove("apple")
PRINT "ss len after drop: "; ss.Length

DIM bm AS Viper.Collections.BiMap
bm = Viper.Collections.BiMap.New()
PRINT "bm empty: "; bm.IsEmpty
bm.Put("one", "1")
bm.Put("two", "2")
PRINT "bm len: "; bm.Length
PRINT "bm getbykey one: "; bm.GetByKey("one")
PRINT "bm getbyvalue 2: "; bm.GetByValue("2")
PRINT "bm haskey one: "; bm.HasKey("one")
PRINT "bm hasvalue 2: "; bm.HasValue("2")
bm.RemoveByKey("one")
PRINT "bm len after remove: "; bm.Length

PRINT "done"
END
