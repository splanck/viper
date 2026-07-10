' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.get_Id: null thread
' COVER: Viper.Threads.Thread.Id

DIM t AS Viper.Threads.Thread
t = NOTHING

DIM id AS INTEGER
id = t.Id

PRINT "UNREACHABLE"
END
