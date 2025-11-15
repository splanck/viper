' game_utils.bas - Utility functions for the adventure game

' Print a divider line
SUB PrintDivider()
    PRINT "=========================================="
END SUB

' Print a header
SUB PrintHeader(title AS STRING)
    PRINT ""
    PrintDivider()
    PRINT "  "; title
    PrintDivider()
    PRINT ""
END SUB

' Convert boolean integer to YES/NO string
FUNCTION BoolToYesNo(value AS INTEGER) AS STRING
    DIM result AS STRING
    result = "NO"
    ' Workaround: can't use IF in FUNCTION, so use math
    ' This won't work, need a different approach
    RETURN result
END FUNCTION

' Pad a string to a certain length
FUNCTION PadString(s AS STRING, length AS INTEGER) AS STRING
    DIM result AS STRING
    DIM current AS INTEGER
    result = s
    current = LEN(s)

    ' Fill with spaces - using FOR loop instead of IF
    DIM i AS INTEGER
    FOR i = current TO length - 1
        result = result + " "
    NEXT i

    RETURN result
END FUNCTION
