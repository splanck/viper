' test_special_collections.bas — BitSet, BloomFilter, UnionFind, Bytes, LruCache, MultiMap
DIM bs AS Viper.Collections.BitSet
bs = Viper.Collections.BitSet.New(64)
PRINT "bs empty: "; bs.IsEmpty
PRINT "bs len: "; bs.Len
bs.Set(3)
bs.Set(7)
bs.Set(15)
PRINT "bs count: "; bs.Count
PRINT "bs get 3: "; bs.Get(3)
PRINT "bs get 4: "; bs.Get(4)
bs.Toggle(3)
PRINT "bs get 3 after toggle: "; bs.Get(3)
bs.Clear(7)
PRINT "bs count after clear: "; bs.Count
PRINT "bs tostring: "; bs.ToString()
bs.ClearAll()
PRINT "bs count after clearall: "; bs.Count

DIM bf AS Viper.Collections.BloomFilter
bf = Viper.Collections.BloomFilter.New(1000, 0.01)
bf.Add("hello")
bf.Add("world")
PRINT "bf count: "; bf.Count
PRINT "bf might hello: "; bf.MightContain("hello")
PRINT "bf might xyz: "; bf.MightContain("xyz")

DIM uf AS Viper.Collections.UnionFind
uf = Viper.Collections.UnionFind.New(10)
PRINT "uf count: "; uf.Count
uf.Union(1, 2)
uf.Union(3, 4)
uf.Union(2, 3)
PRINT "uf connected 1,4: "; uf.Connected(1, 4)
PRINT "uf connected 1,5: "; uf.Connected(1, 5)
PRINT "uf setsize 1: "; uf.SetSize(1)

DIM by AS Viper.Collections.Bytes
by = Viper.Collections.Bytes.New(8)
PRINT "by len: "; by.Len
by.Set(0, 65)
by.Set(1, 66)
PRINT "by get 0: "; by.Get(0)
PRINT "by get 1: "; by.Get(1)
by.Fill(0)
PRINT "by get 0 after fill: "; by.Get(0)
PRINT "by tohex: "; by.ToHex()

DIM lru AS Viper.Collections.LruCache
lru = Viper.Collections.LruCache.New(3)
PRINT "lru empty: "; lru.IsEmpty
PRINT "lru cap: "; lru.Cap
lru.Put("a", "1")
lru.Put("b", "2")
lru.Put("c", "3")
PRINT "lru len: "; lru.Len
PRINT "lru has a: "; lru.Has("a")
lru.Put("d", "4")
PRINT "lru len after evict: "; lru.Len
lru.Remove("d")
PRINT "lru len after remove: "; lru.Len

DIM mm AS Viper.Collections.MultiMap
mm = Viper.Collections.MultiMap.New()
PRINT "mm empty: "; mm.IsEmpty
mm.Put("color", "red")
mm.Put("color", "blue")
mm.Put("size", "large")
PRINT "mm len: "; mm.Len
PRINT "mm keycount: "; mm.KeyCount
PRINT "mm countfor color: "; mm.CountFor("color")
PRINT "mm has color: "; mm.Has("color")
mm.RemoveAll("color")
PRINT "mm len after removeall: "; mm.Len

PRINT "done"
END
