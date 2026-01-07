REM Test: Duplicate procedure detection is case-insensitive across namespaces

NAMESPACE A.B
  SUB F()
  END SUB
END NAMESPACE

NAMESPACE a.b
  SUB f()
  END SUB
END NAMESPACE

END

