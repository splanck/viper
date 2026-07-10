' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.Join: null thread
' COVER: Viper.Threads.Thread.Join

DIM t AS Viper.Threads.Thread
t = NOTHING

t.Join()

PRINT "UNREACHABLE"
END
