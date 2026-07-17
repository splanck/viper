' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.get_IsAlive: null thread
' COVER: Zanna.Threads.Thread.IsAlive

DIM t AS Zanna.Threads.Thread
t = NOTHING

DIM alive AS INTEGER
alive = t.IsAlive

PRINT "UNREACHABLE"
END
