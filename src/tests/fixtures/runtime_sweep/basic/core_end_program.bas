' EXPECT_EXIT: 7
' EXPECT_OUT: BYE
' COVER: Viper.System.Environment.Exit

PRINT "BYE"
Viper.System.Environment.Exit(7)
PRINT "SHOULD_NOT_PRINT"
END
