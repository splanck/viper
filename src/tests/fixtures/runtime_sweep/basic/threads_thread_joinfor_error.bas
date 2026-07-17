' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.JoinFor: null thread
' COVER: Zanna.Threads.Thread.JoinFor

DIM t AS Zanna.Threads.Thread
t = NOTHING

t.JoinFor(1)

PRINT "UNREACHABLE"
END
