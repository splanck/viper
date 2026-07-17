' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Core.Diagnostics.Assert
' COVER: Zanna.Core.Diagnostics.AssertEq
' COVER: Zanna.Core.Diagnostics.AssertNeq
' COVER: Zanna.Core.Diagnostics.AssertEqNum
' COVER: Zanna.Core.Diagnostics.AssertEqStr
' COVER: Zanna.Core.Diagnostics.AssertNull
' COVER: Zanna.Core.Diagnostics.AssertNotNull
' COVER: Zanna.Core.Diagnostics.AssertGt
' COVER: Zanna.Core.Diagnostics.AssertLt
' COVER: Zanna.Core.Diagnostics.AssertGte
' COVER: Zanna.Core.Diagnostics.AssertLte

DIM obj AS OBJECT
obj = NEW Zanna.Collections.List()

Zanna.Core.Diagnostics.Assert(TRUE, "assert")
Zanna.Core.Diagnostics.AssertEq(42, 42, "assert.eq")
Zanna.Core.Diagnostics.AssertNeq(41, 42, "assert.neq")
Zanna.Core.Diagnostics.AssertEqNum(3.5, 3.5, "assert.eqnum")
Zanna.Core.Diagnostics.AssertEqStr("ok", "ok", "assert.eqstr")
Zanna.Core.Diagnostics.AssertNull(NOTHING, "assert.null")
Zanna.Core.Diagnostics.AssertNotNull(obj, "assert.notnull")
Zanna.Core.Diagnostics.AssertGt(5, 4, "assert.gt")
Zanna.Core.Diagnostics.AssertLt(4, 5, "assert.lt")
Zanna.Core.Diagnostics.AssertGte(5, 5, "assert.gte")
Zanna.Core.Diagnostics.AssertLte(5, 5, "assert.lte")

PRINT "RESULT: ok"
END
