REM Test BUG-010: STATIC keyword
SUB Counter()
    STATIC count
    count = count + 1
    PRINT count
END SUB

Counter()
Counter()
Counter()
END
