' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.TryJoin: null thread
' COVER: Viper.Threads.Thread.TryJoin

DIM t AS Viper.Threads.Thread
t = NOTHING

t.TryJoin()

PRINT "UNREACHABLE"
END
