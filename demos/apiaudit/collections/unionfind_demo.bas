' unionfind_demo.bas - Comprehensive API audit for Viper.Collections.UnionFind
' Tests: New, Find, Union, Connected, Count, SetSize, Reset

PRINT "=== UnionFind API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM uf AS OBJECT
uf = Viper.Collections.UnionFind.New(6)
PRINT uf.Count       ' 6

' --- Find ---
PRINT "--- Find ---"
PRINT uf.Find(0)     ' 0
PRINT uf.Find(3)     ' 3

' --- Union ---
PRINT "--- Union ---"
PRINT uf.Union(0, 1)  ' 1 (merged)
PRINT uf.Count         ' 5
PRINT uf.Union(2, 3)  ' 1 (merged)
PRINT uf.Count         ' 4
PRINT uf.Union(0, 1)  ' 0 (already same set)
PRINT uf.Count         ' 4

' --- Connected ---
PRINT "--- Connected ---"
PRINT uf.Connected(0, 1)  ' 1
PRINT uf.Connected(2, 3)  ' 1
PRINT uf.Connected(0, 2)  ' 0
PRINT uf.Connected(4, 5)  ' 0

' --- Union across groups ---
PRINT "--- Union across groups ---"
PRINT uf.Union(1, 3)      ' 1
PRINT uf.Count             ' 3
PRINT uf.Connected(0, 2)  ' 1
PRINT uf.Connected(0, 3)  ' 1

' --- SetSize ---
PRINT "--- SetSize ---"
PRINT uf.SetSize(0)       ' 4
PRINT uf.SetSize(4)       ' 1
PRINT uf.SetSize(5)       ' 1

' --- Union remaining ---
PRINT "--- Union remaining ---"
uf.Union(4, 5)
PRINT uf.Count             ' 2
PRINT uf.SetSize(4)        ' 2

uf.Union(0, 4)
PRINT uf.Count             ' 1
PRINT uf.SetSize(0)        ' 6

' --- Reset ---
PRINT "--- Reset ---"
uf.Reset()
PRINT uf.Count             ' 6
PRINT uf.Connected(0, 1)  ' 0
PRINT uf.SetSize(0)        ' 1

PRINT "=== UnionFind audit complete ==="
END
