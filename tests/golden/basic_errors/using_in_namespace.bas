REM Test E_NS_008: USING inside a namespace block (not file scope)

NAMESPACE Collections
  CLASS List
    DIM size AS I64
  END CLASS
END NAMESPACE

NAMESPACE Application
  REM This USING is illegal - must be at file scope
  USING Collections

  CLASS MyApp
    DIM data AS I64
  END CLASS
END NAMESPACE

END
