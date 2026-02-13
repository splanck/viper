' pathfollower_demo.bas - Comprehensive API audit for Viper.Game.PathFollower
' Tests: New, AddPoint, PointCount, Mode, Speed, Start, Pause, Stop,
'        IsActive, IsFinished, Update, X, Y, Progress, Segment, Angle, Clear

PRINT "=== PathFollower API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM pf AS OBJECT
pf = Viper.Game.PathFollower.New()
PRINT pf.PointCount       ' 0
PRINT pf.IsActive         ' 0
PRINT pf.IsFinished       ' 0

' --- AddPoint ---
PRINT "--- AddPoint ---"
DIM a1 AS INTEGER
a1 = pf.AddPoint(0, 0)
PRINT a1                  ' 1
DIM a2 AS INTEGER
a2 = pf.AddPoint(100, 0)
PRINT a2                  ' 1
DIM a3 AS INTEGER
a3 = pf.AddPoint(100, 100)
PRINT a3                  ' 1
DIM a4 AS INTEGER
a4 = pf.AddPoint(0, 100)
PRINT a4                  ' 1
PRINT pf.PointCount       ' 4

' --- Speed (set/get) ---
PRINT "--- Speed ---"
pf.Speed = 10
PRINT pf.Speed            ' 10

' --- Mode (set/get) ---
PRINT "--- Mode ---"
pf.Mode = 0
PRINT pf.Mode             ' 0

' --- Start ---
PRINT "--- Start ---"
pf.Start()
PRINT pf.IsActive         ' 1
PRINT pf.IsFinished       ' 0
PRINT pf.X                ' 0
PRINT pf.Y                ' 0

' --- Update (deltaMs) ---
PRINT "--- Update ---"
pf.Update(16)
PRINT pf.IsActive         ' 1
PRINT pf.Segment
PRINT pf.Progress

' --- Multiple updates ---
PRINT "--- Multiple updates ---"
pf.Update(16)
pf.Update(16)
pf.Update(16)
PRINT pf.X
PRINT pf.Y

' --- Angle ---
PRINT "--- Angle ---"
PRINT pf.Angle

' --- Pause ---
PRINT "--- Pause ---"
pf.Pause()
PRINT pf.IsActive         ' 0
DIM xBefore AS INTEGER
xBefore = pf.X
pf.Update(100)
PRINT pf.X                ' same as before

' --- Resume ---
PRINT "--- Resume ---"
pf.Start()
PRINT pf.IsActive         ' 1

' --- Progress (set/get) ---
PRINT "--- Progress ---"
pf.Progress = 0
PRINT pf.Progress          ' 0
PRINT pf.X
PRINT pf.Y

' --- Stop ---
PRINT "--- Stop ---"
pf.Stop()
PRINT pf.IsActive         ' 0

' --- Clear ---
PRINT "--- Clear ---"
pf.Clear()
PRINT pf.PointCount       ' 0

' --- Add after clear ---
PRINT "--- Add after clear ---"
pf.AddPoint(0, 0)
pf.AddPoint(50, 50)
PRINT pf.PointCount       ' 2

PRINT "=== PathFollower audit complete ==="
END
