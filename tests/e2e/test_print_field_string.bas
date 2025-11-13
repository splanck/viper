CLASS T
    DIM msg$ AS STRING

    SUB Show()
        PRINT msg$
    END SUB
END CLASS

DIM t AS T
t = NEW T()
t.msg$ = "Hello"
t.Show()
END
