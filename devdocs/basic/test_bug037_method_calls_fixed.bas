REM BUG-037 Fix Validation: SUB methods on class instances can now be called
REM Previously, multi-letter variable names caused method calls to be misparsed
REM as qualified procedure calls, resulting in "unknown procedure" errors.

REM Test 1: Multi-letter variable with SUB methods
CLASS Database
  recordCount AS INTEGER

  SUB AddRecord()
    Me.recordCount = Me.recordCount + 1
  END SUB

  SUB PrintCount()
    PRINT "Records: "; Me.recordCount
  END SUB
END CLASS

DIM db AS Database
db = NEW Database()
db.recordCount = 0
db.AddRecord()
db.AddRecord()
db.PrintCount()

REM Test 2: Single-letter variable (always worked)
CLASS Test
  SUB TestSub()
    PRINT "Single-letter var works"
  END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.TestSub()

REM Test 3: FUNCTION methods work inline (BUG-039 not yet fixed for assignment)
CLASS Calculator
  FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a + b
  END FUNCTION

  SUB Display(result AS INTEGER)
    PRINT "Result: "; result
  END SUB
END CLASS

DIM calc AS Calculator
calc = NEW Calculator()
calc.Display(calc.Add(10, 20))

PRINT "All BUG-037 tests passed!"
END
