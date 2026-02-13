' quadtree_demo.bas - Comprehensive API audit for Viper.Game.Quadtree
' Tests: New, Insert, Remove, Update, QueryRect, QueryPoint, GetResult,
'        ResultCount, ItemCount, Clear, GetPairs, PairFirst, PairSecond

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

' --- QueryRect (x, y, w, h) -> count ---
PRINT "--- QueryRect ---"
DIM found AS INTEGER
found = qt.QueryRect(0, 0, 200, 200)
PRINT found              ' 3 (all items in range)
PRINT qt.ResultCount     ' 3

' --- GetResult ---
PRINT "--- GetResult ---"
DIM r0 AS INTEGER
r0 = qt.GetResult(0)

' --- QueryRect (narrow region) ---
PRINT "--- QueryRect narrow ---"
DIM found2 AS INTEGER
found2 = qt.QueryRect(0, 0, 40, 40)
PRINT found2             ' 1

' --- QueryPoint (x, y, radius) ---
PRINT "--- QueryPoint ---"
DIM pFound AS INTEGER
pFound = qt.QueryPoint(15, 15, 10)

' --- Update (id, x, y, w, h) ---
PRINT "--- Update ---"
DIM upd AS INTEGER
upd = qt.Update(1, 400, 400, 20, 20)
PRINT upd                ' 1
PRINT qt.ItemCount       ' 3

' --- QueryRect after move ---
PRINT "--- QueryRect after move ---"
DIM found3 AS INTEGER
found3 = qt.QueryRect(0, 0, 40, 40)
PRINT found3             ' 0

DIM found4 AS INTEGER
found4 = qt.QueryRect(390, 390, 30, 30)
PRINT found4             ' 1

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

' --- GetPairs / PairFirst / PairSecond ---
PRINT "--- GetPairs ---"
qt.Insert(10, 200, 200, 50, 50)
qt.Insert(11, 210, 210, 50, 50)
DIM pairCount AS INTEGER
pairCount = qt.GetPairs()
PRINT pairCount          ' >= 1

IF pairCount > 0 THEN
    DIM pf AS INTEGER
    pf = qt.PairFirst(0)
    DIM ps AS INTEGER
    ps = qt.PairSecond(0)
END IF

' --- Clear ---
PRINT "--- Clear ---"
qt.Clear()
PRINT qt.ItemCount       ' 0
DIM found5 AS INTEGER
found5 = qt.QueryRect(0, 0, 800, 600)
PRINT found5             ' 0

PRINT "=== Quadtree audit complete ==="
END
