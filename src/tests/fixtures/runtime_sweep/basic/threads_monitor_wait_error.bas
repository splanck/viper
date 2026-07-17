' EXPECT_EXIT: 1
' EXPECT_ERR: Monitor.Wait: not owner
' COVER: Zanna.Threads.Monitor.Wait

DIM lockObj AS Zanna.Collections.List
lockObj = NEW Zanna.Collections.List()

Zanna.Threads.Monitor.Wait(lockObj)

PRINT "UNREACHABLE"
END
