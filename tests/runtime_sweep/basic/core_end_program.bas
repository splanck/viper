' EXPECT_EXIT: 7
' EXPECT_OUT: BYE
' COVER: Viper.Environment.EndProgram

PRINT "BYE"
Viper.Environment.EndProgram(7)
PRINT "SHOULD_NOT_PRINT"
END
