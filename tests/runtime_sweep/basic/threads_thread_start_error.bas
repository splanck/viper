' EXPECT_EXIT: 1
' EXPECT_ERR: Thread.Start: null entry
' COVER: Viper.Threads.Thread.Start

DIM t AS Viper.Threads.Thread

Viper.Threads.Thread.Start(NOTHING, NOTHING)

PRINT "UNREACHABLE"
END
