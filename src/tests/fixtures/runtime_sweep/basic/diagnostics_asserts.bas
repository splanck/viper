' EXPECT_OUT: RESULT: ok
' COVER: Viper.Core.Diagnostics.Assert
' COVER: Viper.Core.Diagnostics.AssertEq
' COVER: Viper.Core.Diagnostics.AssertNeq
' COVER: Viper.Core.Diagnostics.AssertEqNum
' COVER: Viper.Core.Diagnostics.AssertEqStr
' COVER: Viper.Core.Diagnostics.AssertNull
' COVER: Viper.Core.Diagnostics.AssertNotNull
' COVER: Viper.Core.Diagnostics.AssertGt
' COVER: Viper.Core.Diagnostics.AssertLt
' COVER: Viper.Core.Diagnostics.AssertGte
' COVER: Viper.Core.Diagnostics.AssertLte

DIM obj AS OBJECT
obj = NEW Viper.Collections.List()

Viper.Core.Diagnostics.Assert(TRUE, "assert")
Viper.Core.Diagnostics.AssertEq(42, 42, "assert.eq")
Viper.Core.Diagnostics.AssertNeq(41, 42, "assert.neq")
Viper.Core.Diagnostics.AssertEqNum(3.5, 3.5, "assert.eqnum")
Viper.Core.Diagnostics.AssertEqStr("ok", "ok", "assert.eqstr")
Viper.Core.Diagnostics.AssertNull(NOTHING, "assert.null")
Viper.Core.Diagnostics.AssertNotNull(obj, "assert.notnull")
Viper.Core.Diagnostics.AssertGt(5, 4, "assert.gt")
Viper.Core.Diagnostics.AssertLt(4, 5, "assert.lt")
Viper.Core.Diagnostics.AssertGte(5, 5, "assert.gte")
Viper.Core.Diagnostics.AssertLte(5, 5, "assert.lte")

PRINT "RESULT: ok"
END
