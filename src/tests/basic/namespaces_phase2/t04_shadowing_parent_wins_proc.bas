NAMESPACE A
  SUB F: PRINT "outer": END SUB
  NAMESPACE B
    SUB F: PRINT "inner": END SUB
    USING X
    SUB Main
      F() ' should resolve to A.B.F (inner)
    END SUB
  END NAMESPACE
END NAMESPACE

NAMESPACE X
  SUB F: PRINT "import": END SUB
END NAMESPACE

A.B.Main()
