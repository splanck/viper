' BUG-005/020 regression: String operations in VM mode
' The original crash was in native codegen with string args.
' This test verifies string handling works correctly.
DIM s AS STRING = "hello"
DIM s2 AS STRING = " world"
PRINT s; s2
PRINT LEN(s)
PRINT "ok"
