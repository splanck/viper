REM Test to see how array field access is parsed
CLASS Test
    DIM arr(5) AS STRING

    SUB SetItem(idx AS INTEGER, val AS STRING)
        arr(idx) = val
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.SetItem(0, "Hello")
END
