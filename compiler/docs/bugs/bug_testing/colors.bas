' colors.bas - Color utility module
' Workaround: without CHR(), we can't generate proper ANSI codes
' This module will be included via AddFile

SUB PrintRed(msg AS STRING)
    PRINT "RED: "; msg
END SUB

SUB PrintGreen(msg AS STRING)
    PRINT "GREEN: "; msg
END SUB

SUB PrintYellow(msg AS STRING)
    PRINT "YELLOW: "; msg
END SUB
