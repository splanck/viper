' unionfind_demo.bas
PRINT "=== Viper.Collections.UnionFind Demo ==="
DIM uf AS OBJECT
uf = NEW Viper.Collections.UnionFind(10)
uf.Union(0, 1)
uf.Union(2, 3)
uf.Union(1, 3)
PRINT uf.Connected(0, 3)
PRINT uf.Connected(0, 5)
PRINT uf.Count
PRINT uf.Find(3)
PRINT uf.SetSize(0)
PRINT "done"
END
