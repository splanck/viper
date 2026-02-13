' scheduler_demo.bas
PRINT "=== Viper.Threads.Scheduler Demo ==="
DIM s AS OBJECT
s = NEW Viper.Threads.Scheduler()
PRINT s.Pending
s.Schedule("task1", 0)
PRINT s.Pending
PRINT s.IsDue("task1")
PRINT s.Cancel("task1")
PRINT s.Pending
s.Clear()
PRINT "done"
END
