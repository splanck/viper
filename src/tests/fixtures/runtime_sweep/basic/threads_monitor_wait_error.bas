' EXPECT_EXIT: 1
' EXPECT_ERR: Monitor.Wait: not owner
' COVER: Viper.Threads.Monitor.Wait

DIM lockObj AS Viper.Collections.List
lockObj = NEW Viper.Collections.List()

Viper.Threads.Monitor.Wait(lockObj)

PRINT "UNREACHABLE"
END
