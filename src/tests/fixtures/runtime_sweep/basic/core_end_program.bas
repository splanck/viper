' EXPECT_EXIT: 7
' EXPECT_OUT: BYE
' COVER: Viper.System.Environment.EndProgram

PRINT "BYE"
Viper.System.Environment.EndProgram(7)
PRINT "SHOULD_NOT_PRINT"
END
