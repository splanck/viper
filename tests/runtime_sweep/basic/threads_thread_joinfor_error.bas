' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.JoinFor: null thread
' COVER: Viper.Threads.Thread.JoinFor

DIM t AS Viper.Threads.Thread
t = NOTHING

t.JoinFor(1)

PRINT "UNREACHABLE"
END
