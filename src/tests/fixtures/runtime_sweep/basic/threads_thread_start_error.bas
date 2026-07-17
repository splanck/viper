' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.Start: null entry
' COVER: Zanna.Threads.Thread.Start

DIM t AS Zanna.Threads.Thread

Zanna.Threads.Thread.Start(NOTHING, NOTHING)

PRINT "UNREACHABLE"
END
