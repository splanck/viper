' EXPECT_OUT: RESULT: ok
' COVER: Viper.Diagnostics.Assert
' COVER: Viper.Diagnostics.AssertEq
' COVER: Viper.Diagnostics.AssertNeq
' COVER: Viper.Diagnostics.AssertEqNum
' COVER: Viper.Diagnostics.AssertEqStr
' COVER: Viper.Diagnostics.AssertNull
' COVER: Viper.Diagnostics.AssertNotNull
' COVER: Viper.Diagnostics.AssertGt
' COVER: Viper.Diagnostics.AssertLt
' COVER: Viper.Diagnostics.AssertGte
' COVER: Viper.Diagnostics.AssertLte

DIM obj AS OBJECT
obj = NEW Viper.Collections.List()

Viper.Diagnostics.Assert(1, "assert")
Viper.Diagnostics.AssertEq(42, 42, "assert.eq")
Viper.Diagnostics.AssertNeq(41, 42, "assert.neq")
Viper.Diagnostics.AssertEqNum(3.5, 3.5, "assert.eqnum")
Viper.Diagnostics.AssertEqStr("ok", "ok", "assert.eqstr")
Viper.Diagnostics.AssertNull(NOTHING, "assert.null")
Viper.Diagnostics.AssertNotNull(obj, "assert.notnull")
Viper.Diagnostics.AssertGt(5, 4, "assert.gt")
Viper.Diagnostics.AssertLt(4, 5, "assert.lt")
Viper.Diagnostics.AssertGte(5, 5, "assert.gte")
Viper.Diagnostics.AssertLte(5, 5, "assert.lte")

PRINT "RESULT: ok"
END
