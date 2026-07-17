' EXPECT_EXIT: 7
' EXPECT_OUT: BYE
' COVER: Zanna.System.Environment.Exit

PRINT "BYE"
Zanna.System.Environment.Exit(7)
PRINT "SHOULD_NOT_PRINT"
END
