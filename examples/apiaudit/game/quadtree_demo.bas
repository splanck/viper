' quadtree_demo.bas - Comprehensive API audit for Viper.Game.Quadtree
' Tests: New, Insert, Remove, Update, QueryRectResult, QueryPointResult,
'        QueryPairs, QueryResult, QuadtreePairResult, ItemCount, Clear

PRINT "=== Quadtree API Audit ==="

' --- New (x, y, width, height) ---
PRINT "--- New ---"
DIM qt AS OBJECT
qt = Viper.Game.Quadtree.New(0, 0, 800, 600)
PRINT qt.ItemCount       ' 0

' --- Insert (id, x, y, w, h) ---
PRINT "--- Insert ---"
DIM ok1 AS INTEGER
ok1 = qt.Insert(1, 10, 10, 20, 20)
PRINT ok1                ' 1
PRINT qt.ItemCount       ' 1

DIM ok2 AS INTEGER
ok2 = qt.Insert(2, 100, 100, 30, 30)
PRINT ok2                ' 1
PRINT qt.ItemCount       ' 2

DIM ok3 AS INTEGER
ok3 = qt.Insert(3, 50, 50, 15, 15)
PRINT ok3                ' 1
PRINT qt.ItemCount       ' 3

' --- QueryRectResult (x, y, w, h) -> QueryResult ---
PRINT "--- QueryRectResult ---"
DIM result AS OBJECT
result = qt.QueryRectResult(0, 0, 200, 200)
PRINT result.Count       ' 3 (all items in range)
PRINT result.Contains(1)
DIM r0 AS INTEGER
r0 = result.GetId(0)
PRINT r0

' --- QueryRectResult (narrow region) ---
PRINT "--- QueryRectResult narrow ---"
DIM found2 AS OBJECT
found2 = qt.QueryRectResult(0, 0, 40, 40)
PRINT found2.Count       ' 1

' --- QueryPointResult (x, y, radius) ---
PRINT "--- QueryPointResult ---"
DIM pFound AS OBJECT
pFound = qt.QueryPointResult(15, 15, 10)
PRINT pFound.Count

' --- Update (id, x, y, w, h) ---
PRINT "--- Update ---"
DIM upd AS INTEGER
upd = qt.Update(1, 400, 400, 20, 20)
PRINT upd                ' 1
PRINT qt.ItemCount       ' 3

' --- QueryRect after move ---
PRINT "--- QueryRect after move ---"
DIM found3Result AS OBJECT
found3Result = qt.QueryRectResult(0, 0, 40, 40)
PRINT found3Result.Count ' 0

DIM found4Result AS OBJECT
found4Result = qt.QueryRectResult(390, 390, 30, 30)
PRINT found4Result.Count ' 1

' --- Remove ---
PRINT "--- Remove ---"
DIM rem1 AS INTEGER
rem1 = qt.Remove(2)
PRINT rem1               ' 1
PRINT qt.ItemCount       ' 2

' --- Remove nonexistent ---
PRINT "--- Remove nonexistent ---"
DIM rem2 AS INTEGER
rem2 = qt.Remove(999)
PRINT rem2               ' 0

' --- QueryPairs / QuadtreePairResult ---
PRINT "--- QueryPairs ---"
qt.Insert(10, 200, 200, 50, 50)
qt.Insert(11, 210, 210, 50, 50)
DIM pairs AS OBJECT
pairs = qt.QueryPairs()
PRINT pairs.Count        ' >= 1

IF pairs.Count > 0 THEN
    DIM pf AS INTEGER
    pf = pairs.First(0)
    DIM ps AS INTEGER
    ps = pairs.Second(0)
    PRINT pf
    PRINT ps
END IF

' --- Clear ---
PRINT "--- Clear ---"
qt.Clear()
PRINT qt.ItemCount       ' 0
DIM found5 AS OBJECT
found5 = qt.QueryRectResult(0, 0, 800, 600)
PRINT found5.Count       ' 0

PRINT "=== Quadtree audit complete ==="
END
