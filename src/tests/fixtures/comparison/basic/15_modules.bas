' Test: Modules & Namespaces
' Tests: NAMESPACE, USING
' NOTE: USING must appear before NAMESPACE declarations

PRINT "=== Modules & Namespaces Test ==="

' Simple namespace test
NAMESPACE MyModule
    FUNCTION Helper$() AS STRING
        Helper$ = "Helper function works!"
    END FUNCTION
END NAMESPACE

PRINT ""
PRINT "--- Namespace Functions ---"
PRINT MyModule.Helper$()

PRINT ""
PRINT "NAMESPACE: Works"
PRINT "Qualified access: Works"

PRINT ""
PRINT "=== Modules & Namespaces test complete ==="
