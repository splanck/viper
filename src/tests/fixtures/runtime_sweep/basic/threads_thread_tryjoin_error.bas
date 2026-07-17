' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.TryJoin: null thread
' COVER: Zanna.Threads.Thread.TryJoin

DIM t AS Zanna.Threads.Thread
t = NOTHING

t.TryJoin()

PRINT "UNREACHABLE"
END
