' objectpool_demo.bas - Comprehensive API audit for Viper.Game.ObjectPool
' Tests: New, Acquire, Release, IsActive, ActiveCount, FreeCount, Capacity,
'        IsFull, IsEmpty, Clear, FirstActive, NextActive, SetData, GetData

PRINT "=== ObjectPool API Audit ==="

' --- New (capacity) ---
PRINT "--- New ---"
DIM pool AS OBJECT
pool = Viper.Game.ObjectPool.New(4)
PRINT pool.Capacity       ' 4
PRINT pool.ActiveCount    ' 0
PRINT pool.FreeCount      ' 4
PRINT pool.IsEmpty        ' 1
PRINT pool.IsFull         ' 0

' --- Acquire ---
PRINT "--- Acquire ---"
DIM id1 AS INTEGER
id1 = pool.Acquire()
PRINT pool.ActiveCount    ' 1
PRINT pool.FreeCount      ' 3
PRINT pool.IsEmpty        ' 0
PRINT pool.IsFull         ' 0

DIM id2 AS INTEGER
id2 = pool.Acquire()
PRINT pool.ActiveCount    ' 2

DIM id3 AS INTEGER
DIM id4 AS INTEGER
id3 = pool.Acquire()
id4 = pool.Acquire()
PRINT pool.ActiveCount    ' 4
PRINT pool.FreeCount      ' 0
PRINT pool.IsFull         ' 1

' --- IsActive ---
PRINT "--- IsActive ---"
PRINT pool.IsActive(id1)   ' 1
PRINT pool.IsActive(id2)   ' 1

' --- Release ---
PRINT "--- Release ---"
DIM released AS INTEGER
released = pool.Release(id2)
PRINT released              ' 1
PRINT pool.IsActive(id2)   ' 0
PRINT pool.ActiveCount     ' 3
PRINT pool.FreeCount       ' 1
PRINT pool.IsFull          ' 0

' --- Release already-released ---
PRINT "--- Release invalid ---"
DIM bad AS INTEGER
bad = pool.Release(id2)
PRINT bad                   ' 0

' --- SetData / GetData ---
PRINT "--- SetData / GetData ---"
DIM setOk AS INTEGER
setOk = pool.SetData(id1, 100)
PRINT setOk                ' 1
PRINT pool.GetData(id1)   ' 100

pool.SetData(id3, 300)
PRINT pool.GetData(id3)   ' 300

' --- FirstActive / NextActive ---
PRINT "--- FirstActive / NextActive ---"
DIM first AS INTEGER
first = pool.FirstActive()
PRINT pool.IsActive(first)   ' 1

DIM nxt AS INTEGER
nxt = pool.NextActive(first)

' --- Re-acquire into released slot ---
PRINT "--- Re-acquire ---"
DIM id5 AS INTEGER
id5 = pool.Acquire()
PRINT pool.ActiveCount     ' 4
PRINT pool.IsActive(id5)  ' 1
PRINT pool.IsFull          ' 1

' --- Clear ---
PRINT "--- Clear ---"
pool.Clear()
PRINT pool.ActiveCount     ' 0
PRINT pool.FreeCount       ' 4
PRINT pool.IsEmpty         ' 1
PRINT pool.IsFull          ' 0

' --- IsActive after clear ---
PRINT "--- IsActive after clear ---"
PRINT pool.IsActive(id1)  ' 0
PRINT pool.IsActive(id3)  ' 0

PRINT "=== ObjectPool audit complete ==="
END
