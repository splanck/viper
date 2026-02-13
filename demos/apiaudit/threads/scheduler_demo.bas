' =============================================================================
' API Audit: Viper.Threads.Scheduler (BASIC)
' =============================================================================
' Tests: New, Schedule, Cancel, IsDue, Poll, Pending, Clear
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Scheduler ==="

' --- New ---
PRINT "--- New ---"
DIM s AS OBJECT = Viper.Threads.Scheduler.New()
PRINT "Created scheduler"

' --- Pending (initial) ---
PRINT "--- Pending (initial) ---"
PRINT "Pending: "; s.Pending

' --- Schedule ---
PRINT "--- Schedule ---"
s.Schedule("task_a", 0)
s.Schedule("task_b", 0)
s.Schedule("task_c", 60000)
PRINT "Scheduled 3 tasks (2 immediate, 1 delayed)"
PRINT "Pending: "; s.Pending

' --- IsDue ---
PRINT "--- IsDue ---"
PRINT "IsDue(task_a): "; s.IsDue("task_a")
PRINT "IsDue(task_c): "; s.IsDue("task_c")
PRINT "IsDue(nonexistent): "; s.IsDue("nonexistent")

' --- Cancel ---
PRINT "--- Cancel ---"
DIM cancelled AS INTEGER = s.Cancel("task_c")
PRINT "Cancel(task_c): "; cancelled
PRINT "Pending after cancel: "; s.Pending
DIM cancelMissing AS INTEGER = s.Cancel("nonexistent")
PRINT "Cancel(nonexistent): "; cancelMissing

' --- Poll ---
PRINT "--- Poll ---"
DIM due AS Viper.Collections.Seq = s.Poll()
PRINT "Poll returned tasks: "; due.Len
PRINT "Pending after poll: "; s.Pending

' --- Clear ---
PRINT "--- Clear ---"
s.Schedule("cleanup1", 0)
s.Schedule("cleanup2", 0)
s.Schedule("cleanup3", 5000)
PRINT "Pending before Clear: "; s.Pending
s.Clear()
PRINT "Pending after Clear: "; s.Pending

' --- Schedule replacement ---
PRINT "--- Schedule replacement ---"
s.Schedule("heartbeat", 1000)
PRINT "Pending: "; s.Pending
s.Schedule("heartbeat", 5000)
PRINT "Pending after re-schedule: "; s.Pending

' --- Poll on empty ---
PRINT "--- Poll on empty ---"
s.Clear()
DIM emptyPoll AS Viper.Collections.Seq = s.Poll()
PRINT "Poll on empty: "; emptyPoll.Len

PRINT "=== Scheduler Audit Complete ==="
END
