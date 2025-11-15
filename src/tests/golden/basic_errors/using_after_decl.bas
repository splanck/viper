REM Test E_NS_005: USING after declaration

NAMESPACE Data
  CLASS Record
    DIM id AS I64
  END CLASS
END NAMESPACE

NAMESPACE Collections
  CLASS List
    DIM size AS I64
  END CLASS
END NAMESPACE

REM This USING comes after NAMESPACE declarations - illegal
USING Collections

END
