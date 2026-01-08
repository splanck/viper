' Test case for Bug #007 fix: Backslash in strings
' After fix, backslash should be a regular character

PRINT "Testing Bug #007 fix:"

' Test single backslash - this was previously corrupting the parser
DIM s1 AS STRING
s1 = "\"
PRINT "Single backslash: '"; s1; "'"

' Test backslash in middle of string
DIM s2 AS STRING
s2 = "path\to\file"
PRINT "Path with backslashes: '"; s2; "'"

' Test double backslash
DIM s3 AS STRING
s3 = "double \\ backslash"
PRINT "Double backslash: '"; s3; "'"

' Test code after backslash strings still parses correctly
DIM x AS INTEGER
x = 42
PRINT "Variable after backslash strings: "; x

' Test class method with backslash (the original bug trigger)
CLASS PathHelper
    PUBLIC FUNCTION GetSeparator() AS STRING
        GetSeparator = "\"
    END FUNCTION

    PUBLIC FUNCTION BuildPath(folder AS STRING, file AS STRING) AS STRING
        BuildPath = folder + "\" + file
    END FUNCTION
END CLASS

DIM helper AS PathHelper
LET helper = NEW PathHelper()
PRINT "Separator: '"; helper.GetSeparator(); "'"
PRINT "Full path: '"; helper.BuildPath("C:\Users", "test.txt"); "'"

PRINT ""
PRINT "Bug #007 fix verified!"
