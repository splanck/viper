' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.get_IsAlive: null thread
' COVER: Viper.Threads.Thread.IsAlive

DIM t AS Viper.Threads.Thread
t = NOTHING

DIM alive AS INTEGER
alive = t.IsAlive

PRINT "UNREACHABLE"
END
