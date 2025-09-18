FUNCTION Touch()
10 PRINT "touch"
20 RETURN 1
END FUNCTION

30 IF FALSE ANDALSO (Touch() = 1) THEN PRINT "andalso true" ELSE PRINT "andalso false"
40 IF TRUE ORELSE (Touch() = 1) THEN PRINT "orelse true" ELSE PRINT "orelse false"
50 END
