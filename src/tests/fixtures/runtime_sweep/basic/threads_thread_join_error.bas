' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.Join: null thread
' COVER: Zanna.Threads.Thread.Join

DIM t AS Zanna.Threads.Thread
t = NOTHING

t.Join()

PRINT "UNREACHABLE"
END
